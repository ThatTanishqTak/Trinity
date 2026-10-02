#include "Trinity/FileSystem/DirectorySource.hpp"

#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Profiler.hpp"

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

        std::filesystem::path FromUtf8(std::string_view text)
        {
            return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string l_Text = path.u8string();

            return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
        }

        bool EqualsIgnoreAsciiCase(std::string_view left, std::string_view right)
        {
            const auto a_Lower = [](char character) { return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character; };

            return std::ranges::equal(left, right, [&a_Lower](char a, char b) { return a_Lower(a) == a_Lower(b); });
        }
    }

    DirectorySource::DirectorySource(std::filesystem::path root) : m_Root(std::move(root))
    {

    }

    Expected<std::filesystem::path, FileError> DirectorySource::Resolve(std::string_view relativePath) const
    {
        std::filesystem::path l_Path = m_Root;
        const bool l_CheckCase = s_CheckCase.Get();

        std::size_t l_Position = 0;
        while (l_Position < relativePath.size())
        {
            const std::size_t l_End = std::min(relativePath.find('/', l_Position), relativePath.size());
            const std::string_view l_Component = relativePath.substr(l_Position, l_End - l_Position);
            const std::filesystem::path l_Native = FromUtf8(l_Component);
            l_Position = l_End + 1;

            if (l_CheckCase)
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

                    l_OtherCase = l_OtherCase || EqualsIgnoreAsciiCase(ToUtf8(l_Name), l_Component);
                }

                if (!l_Exact)
                {
                    return Unexpected{ l_OtherCase ? FileError::CaseMismatch : FileError::NotFound };
                }
            }

            l_Path /= l_Native;
        }

        std::error_code l_Error;
        if (!l_CheckCase && !std::filesystem::exists(l_Path, l_Error))
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

        const std::uintmax_t l_Size = std::filesystem::file_size(*l_Path, l_Error);
        if (l_Error)
        {
            return Unexpected{ FileError::ReadFailed };
        }

        if (l_Size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        {
            return Unexpected{ FileError::TooLarge };
        }

        std::ifstream l_File(*l_Path, std::ios::binary);
        if (!l_File)
        {
            return Unexpected{ FileError::ReadFailed };
        }

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

    std::string DirectorySource::Describe() const
    {
        return std::format("directory '{}'", ToUtf8(m_Root));
    }
}