#pragma once

#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/FileSystem/PackFormat.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    class PackFile;

    // A .trpak, read-only, with its table held in memory and its files read from disk as they are asked for. A stored file is read in ranges straight from the pack, and a compressed one is decompressed whole as it opens
    class TRINITY_API PackSource final : public FileSource
    {
    public:
        // Reads and checks the header and table, so a pack that was cut short, is corrupted or is from a newer Trinity is refused before it mounts
        [[nodiscard]] static Expected<Scope<PackSource>, std::string> Load(const std::filesystem::path& nativePath);

        ~PackSource() override;

        PackSource(const PackSource&) = delete;
        PackSource& operator=(const PackSource&) = delete;

        [[nodiscard]] Expected<FileBuffer, FileError> Read(std::string_view relativePath) const override;
        [[nodiscard]] Expected<Ref<FileReadHandle>, FileError> Open(std::string_view relativePath) const override;
        [[nodiscard]] Expected<FileType, FileError> Stat(std::string_view relativePath) const override;
        [[nodiscard]] Expected<std::vector<DirectoryEntry>, FileError> List(std::string_view relativeDirectory) const override;

        [[nodiscard]] std::string Describe() const override;

    private:
        struct Entry
        {
            // Into m_Table
            std::string_view Path;
            PackEntry File;
        };

        PackSource() = default;

        [[nodiscard]] const Entry* Find(std::string_view relativePath) const;
        [[nodiscard]] bool IsDirectory(std::string_view relativePath) const;
        [[nodiscard]] FileError GetMissingError(std::string_view relativePath) const;
        // The whole file, decompressed and checked against its hash
        [[nodiscard]] Expected<FileBuffer, FileError> ReadWhole(const Entry& entry) const;

        std::filesystem::path m_NativePath;
        // Shared with the read handles opened on stored files, so they read on after the pack is unmounted
        Ref<PackFile> m_File;
        FileBuffer m_Table;
        std::vector<Entry, TaggedAllocator<Entry, MemoryTag::FileSystem>> m_Entries;
    };
}