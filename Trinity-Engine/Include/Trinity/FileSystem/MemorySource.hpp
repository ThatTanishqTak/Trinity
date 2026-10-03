#pragma once

#include "Trinity/FileSystem/FileSystem.hpp"

#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace Trinity
{
    class TRINITY_API MemorySource final : public FileSource
    {
    public:
        explicit MemorySource(std::string description);

        bool AddFile(std::string_view relativePath, std::span<const std::byte> data);
        bool AddText(std::string_view relativePath, std::string_view text);

        [[nodiscard]] Expected<FileBuffer, FileError> Read(std::string_view relativePath) const override;
        [[nodiscard]] Expected<FileType, FileError> Stat(std::string_view relativePath) const override;
        [[nodiscard]] Expected<std::vector<DirectoryEntry>, FileError> List(std::string_view relativeDirectory) const override;

        [[nodiscard]] std::string Describe() const override;

    private:
        [[nodiscard]] bool IsDirectory(std::string_view relativePath) const;
        [[nodiscard]] FileError GetMissingError(std::string_view relativePath) const;

        std::map<std::string, FileBuffer, std::less<>> m_Files;
        std::string m_Description;
    };
}