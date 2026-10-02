#include "Trinity/FileSystem/DirectorySource.hpp"

#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <system_error>
#include <utility>

namespace Trinity
{
    namespace
    {
        ConsoleVariable<bool> s_CheckCase("filesystem.check_case", true, "Fail directory lookups whose case differs from the names on disk, on every platform");

        using FileSystemUtilities::FromUtf8;
        using FileSystemUtilities::ToUtf8;
    }

    DirectorySource::DirectorySource(std::filesystem::path root) : m_Root(std::move(root))
    {

    }

    Expected<std::filesystem::path, FileError> DirectorySource::Resolve(std::string_view relativePath, bool allowMissing) const
    {
        std::filesystem::path l_Path = m_Root;
        const bool l_CheckCase = s_CheckCase.Get();
        bool l_Missing = false;

        std::size_t l_Position = 0;
        while (l_Position < relativePath.size())
        {
            const std::size_t l_End = std::min(relativePath.find('/', l_Position), relativePath.size());
            const std::string_view l_Component = relativePath.substr(l_Position, l_End - l_Position);
            const std::filesystem::path l_Native = FromUtf8(l_Component);
            l_Position = l_End + 1;

            if (l_CheckCase && !l_Missing)
            {
                bool l_Exact = false;
                bool l_OtherCase = false;

                std::error_code l_Error;
                std::filesystem::directory_iterator l_Iterator(l_Path, l_Error);
                for (; !l_Error && l_Iterator != std::filesystem::directory_iterator(); l_Iterator.increment(l_Error))
                {
                    const std::filesystem::path l_Name = l_Iterator->path().filename();
                    if (l_Name == l_Native)
                    {
                        l_Exact = true;

                        break;
                    }

                    l_OtherCase = l_OtherCase || FileSystemUtilities::EqualsIgnoreAsciiCase(ToUtf8(l_Name), l_Component);
                }

                if (!l_Exact)
                {
                    if (l_OtherCase || !allowMissing)
                    {
                        return Unexpected{ l_OtherCase ? FileError::CaseMismatch : FileError::NotFound };
                    }

                    l_Missing = true;
                }
            }

            l_Path /= l_Native;
        }

        std::error_code l_Error;
        if (!l_CheckCase && !allowMissing && !std::filesystem::exists(l_Path, l_Error))
        {
            return Unexpected{ FileError::NotFound };
        }

        return l_Path;
    }

    Expected<FileBuffer, FileError> DirectorySource::Read(std::string_view relativePath) const
    {
        TR_PROFILE_SCOPE("DirectorySource::Read");

        const Expected<std::filesystem::path, FileError> l_Path = Resolve(relativePath);
        if (!l_Path)
        {
            return Unexpected{ l_Path.GetError() };
        }

        std::error_code l_Error;
        if (!std::filesystem::is_regular_file(*l_Path, l_Error))
        {
            return Unexpected{ std::filesystem::is_directory(*l_Path, l_Error) ? FileError::NotAFile : FileError::NotFound };
        }

        std::ifstream l_File(*l_Path, std::ios::binary | std::ios::ate);
        if (!l_File)
        {
            return Unexpected{ FileError::ReadFailed };
        }

        const std::streamoff l_Size = l_File.tellg();
        if (l_Size < 0)
        {
            return Unexpected{ FileError::ReadFailed };
        }

        if (static_cast<std::uintmax_t>(l_Size) > std::numeric_limits<std::size_t>::max())
        {
            return Unexpected{ FileError::TooLarge };
        }

        l_File.seekg(0);

        FileBuffer l_Buffer(static_cast<std::size_t>(l_Size));
        l_File.read(reinterpret_cast<char*>(l_Buffer.data()), static_cast<std::streamsize>(l_Size));
        if (l_File.gcount() != static_cast<std::streamsize>(l_Size))
        {
            return Unexpected{ FileError::ReadFailed };
        }

        return l_Buffer;
    }

    Expected<FileType, FileError> DirectorySource::Stat(std::string_view relativePath) const
    {
        const Expected<std::filesystem::path, FileError> l_Path = Resolve(relativePath);
        if (!l_Path)
        {
            return Unexpected{ l_Path.GetError() };
        }

        std::error_code l_Error;
        if (std::filesystem::is_directory(*l_Path, l_Error))
        {
            return FileType::Directory;
        }

        if (std::filesystem::is_regular_file(*l_Path, l_Error))
        {
            return FileType::File;
        }

        return Unexpected{ FileError::NotFound };
    }

    Expected<std::vector<DirectoryEntry>, FileError> DirectorySource::List(std::string_view relativeDirectory) const
    {
        const Expected<std::filesystem::path, FileError> l_Path = Resolve(relativeDirectory);
        if (!l_Path)
        {
            return Unexpected{ l_Path.GetError() };
        }

        std::error_code l_Error;
        if (!std::filesystem::is_directory(*l_Path, l_Error))
        {
            return Unexpected{ FileError::NotFound };
        }

        std::vector<DirectoryEntry> l_Entries;
        std::filesystem::directory_iterator l_Iterator(*l_Path, l_Error);
        for (; !l_Error && l_Iterator != std::filesystem::directory_iterator(); l_Iterator.increment(l_Error))
        {
            std::error_code l_TypeError;
            l_Entries.push_back({ ToUtf8(l_Iterator->path().filename()), l_Iterator->is_directory(l_TypeError) ? FileType::Directory : FileType::File });
        }

        if (l_Error)
        {
            return Unexpected{ FileError::ReadFailed };
        }

        return l_Entries;
    }

    Expected<void, FileError> DirectorySource::Write(std::string_view relativePath, std::span<const std::byte> data)
    {
        TR_PROFILE_SCOPE("DirectorySource::Write");

        if (relativePath.empty())
        {
            return Unexpected{ FileError::NotAFile };
        }

        const Expected<std::filesystem::path, FileError> l_Path = Resolve(relativePath, true);
        if (!l_Path)
        {
            return Unexpected{ l_Path.GetError() };
        }

        std::error_code l_Error;
        if (std::filesystem::is_directory(*l_Path, l_Error))
        {
            return Unexpected{ FileError::NotAFile };
        }

        std::filesystem::create_directories(l_Path->parent_path(), l_Error);
        if (l_Error)
        {
            return Unexpected{ FileError::WriteFailed };
        }

        // Written beside the target and renamed over it, so readers and crashes never see a partial file.
        std::filesystem::path l_Temporary = *l_Path;
        l_Temporary += FromUtf8(std::format(".{}.tmp", UUID::Generate()));
        {
            std::ofstream l_File(l_Temporary, std::ios::binary | std::ios::trunc);
            l_File.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            l_File.flush();
            if (!l_File)
            {
                l_File.close();
                std::filesystem::remove(l_Temporary, l_Error);

                return Unexpected{ FileError::WriteFailed };
            }
        }

        std::filesystem::rename(l_Temporary, *l_Path, l_Error);
        if (l_Error)
        {
            std::error_code l_Ignored;
            std::filesystem::remove(l_Temporary, l_Ignored);

            return Unexpected{ FileError::WriteFailed };
        }

        return {};
    }

    Expected<void, FileError> DirectorySource::Remove(std::string_view relativePath)
    {
        const Expected<std::filesystem::path, FileError> l_Path = Resolve(relativePath);
        if (!l_Path)
        {
            return Unexpected{ l_Path.GetError() };
        }

        std::error_code l_Error;
        if (!std::filesystem::is_regular_file(*l_Path, l_Error))
        {
            return Unexpected{ std::filesystem::is_directory(*l_Path, l_Error) ? FileError::NotAFile : FileError::NotFound };
        }

        if (!std::filesystem::remove(*l_Path, l_Error) || l_Error)
        {
            return Unexpected{ FileError::WriteFailed };
        }

        return {};
    }

    std::string DirectorySource::Describe() const
    {
        return std::format("directory '{}'", ToUtf8(m_Root));
    }
}