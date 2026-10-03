#pragma once

#include "Trinity/FileSystem/FileSystem.hpp"

#include <filesystem>

namespace Trinity
{
    class TRINITY_API DirectorySource final : public FileSource
    {
    public:
        explicit DirectorySource(std::filesystem::path root);

        [[nodiscard]] Expected<FileBuffer, FileError> Read(std::string_view relativePath) const override;
        [[nodiscard]] Expected<FileType, FileError> Stat(std::string_view relativePath) const override;
        [[nodiscard]] Expected<std::vector<DirectoryEntry>, FileError> List(std::string_view relativeDirectory) const override;

        [[nodiscard]] Expected<void, FileError> Write(std::string_view relativePath, std::span<const std::byte> data) override;
        [[nodiscard]] Expected<void, FileError> Remove(std::string_view relativePath) override;

        [[nodiscard]] std::string Describe() const override;

    private:
        [[nodiscard]] Expected<std::filesystem::path, FileError> Resolve(std::string_view relativePath, bool allowMissing = false) const;

        std::filesystem::path m_Root;
    };
}