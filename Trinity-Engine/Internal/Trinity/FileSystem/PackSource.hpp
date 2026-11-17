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

    // The table is held in memory. A stored file is read in ranges, a compressed one whole
    class TRINITY_API PackSource final : public FileSource
    {
    public:
        // Refuses a pack that is cut short, corrupted or from a newer Trinity
        [[nodiscard]] static Expected<Scope<PackSource>, std::string> Load(const std::filesystem::path& nativePath);

        ~PackSource() override;

        PackSource(const PackSource&) = delete;
        PackSource& operator=(const PackSource&) = delete;

        [[nodiscard]] Expected<FileBuffer, FileError> Read(std::string_view relativePath) const override;
        [[nodiscard]] Expected<Ref<FileReadHandle>, FileError> Open(std::string_view relativePath) const override;
        [[nodiscard]] Expected<FileType, FileError> Stat(std::string_view relativePath) const override;
        [[nodiscard]] Expected<std::vector<DirectoryEntry>, FileError> List(std::string_view relativeDirectory) const override;

        [[nodiscard]] std::string Describe() const override;

#if !defined(TR_DISTRIBUTION)
        // Returns how many files failed their hash, which a ranged read never checks. Not in Distribution
        [[nodiscard]] std::size_t CheckFiles() const;
#endif

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
        // Decompressed and checked against its hash
        [[nodiscard]] Expected<FileBuffer, FileError> ReadWhole(const Entry& entry) const;

        std::filesystem::path m_NativePath;
        // Shared with read handles on stored files, so they read on after the pack is unmounted
        Ref<PackFile> m_File;
        FileBuffer m_Table;
        std::vector<Entry, TaggedAllocator<Entry, MemoryTag::FileSystem>> m_Entries;
    };
}