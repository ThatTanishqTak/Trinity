#include "Trinity/FileSystem/MemorySource.hpp"

#include "Trinity/FileSystem/FileSystemUtilities.hpp"

#include <format>
#include <optional>
#include <utility>

namespace Trinity
{
    MemorySource::MemorySource(std::string description) : m_Description(std::move(description))
    {

    }

    bool MemorySource::AddFile(std::string_view relativePath, std::span<const std::byte> data)
    {
        const std::optional<std::string> l_Normalized = FileSystem::NormalizePath(std::string("/").append(relativePath));
        if (!l_Normalized || *l_Normalized == "/")
        {
            return false;
        }

        std::string l_Key = l_Normalized->substr(1);
        const std::string l_AsDirectory = l_Key + "/";
        for (const auto& [a_Existing, a_Data] : m_Files)
        {
            if (a_Existing.starts_with(l_AsDirectory) || l_Key.starts_with(a_Existing + "/"))
            {
                return false;
            }
        }

        m_Files.insert_or_assign(std::move(l_Key), FileBuffer(data.begin(), data.end()));

        return true;
    }

    bool MemorySource::AddText(std::string_view relativePath, std::string_view text)
    {
        return AddFile(relativePath, std::as_bytes(std::span(text.data(), text.size())));
    }

    bool MemorySource::IsDirectory(std::string_view relativePath) const
    {
        if (relativePath.empty())
        {
            return true;
        }

        const std::string l_Prefix = std::string(relativePath) + "/";
        const auto a_Iterator = m_Files.lower_bound(l_Prefix);

        return a_Iterator != m_Files.end() && a_Iterator->first.starts_with(l_Prefix);
    }

    FileError MemorySource::GetMissingError(std::string_view relativePath) const
    {
        const std::string l_AsDirectory = std::string(relativePath) + "/";
        for (const auto& [a_Key, a_Data] : m_Files)
        {
            if (FileSystemUtilities::EqualsIgnoreAsciiCase(a_Key, relativePath) || FileSystemUtilities::StartsWithIgnoreAsciiCase(a_Key, l_AsDirectory))
            {
                return FileError::CaseMismatch;
            }
        }

        return FileError::NotFound;
    }

    Expected<FileBuffer, FileError> MemorySource::Read(std::string_view relativePath) const
    {
        if (const auto a_Iterator = m_Files.find(relativePath); a_Iterator != m_Files.end())
        {
            return a_Iterator->second;
        }

        return Unexpected{ IsDirectory(relativePath) ? FileError::NotAFile : GetMissingError(relativePath) };
    }

    Expected<FileType, FileError> MemorySource::Stat(std::string_view relativePath) const
    {
        if (m_Files.contains(relativePath))
        {
            return FileType::File;
        }

        if (IsDirectory(relativePath))
        {
            return FileType::Directory;
        }

        return Unexpected{ GetMissingError(relativePath) };
    }

    Expected<std::vector<DirectoryEntry>, FileError> MemorySource::List(std::string_view relativeDirectory) const
    {
        if (!IsDirectory(relativeDirectory))
        {
            return Unexpected{ m_Files.contains(relativeDirectory) ? FileError::NotFound : GetMissingError(relativeDirectory) };
        }

        const std::string l_Prefix = relativeDirectory.empty() ? std::string() : std::string(relativeDirectory) + "/";

        std::vector<DirectoryEntry> l_Entries;
        for (auto it_File = m_Files.lower_bound(l_Prefix); it_File != m_Files.end() && it_File->first.starts_with(l_Prefix); ++it_File)
        {
            const std::string_view l_Rest = std::string_view(it_File->first).substr(l_Prefix.size());
            const std::size_t l_Slash = l_Rest.find('/');
            const std::string_view l_Name = l_Rest.substr(0, l_Slash);

            if (l_Entries.empty() || l_Entries.back().Name != l_Name)
            {
                l_Entries.push_back({ std::string(l_Name), l_Slash == std::string_view::npos ? FileType::File : FileType::Directory });
            }
        }

        return l_Entries;
    }

    std::string MemorySource::Describe() const
    {
        return std::format("memory '{}' ({} file(s))", m_Description, m_Files.size());
    }
}