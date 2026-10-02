#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Memory.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    enum class FileError : std::uint8_t
    {
        InvalidPath,
        NotMounted,
        NotFound,
        CaseMismatch,
        NotAFile,
        TooLarge,
        ReadFailed
    };

    [[nodiscard]] std::string_view ToString(FileError error);

    enum class FileType : std::uint8_t
    {
        File,
        Directory
    };

    using FileBuffer = std::vector<std::byte, TaggedAllocator<std::byte, MemoryTag::FileSystem>>;

    struct DirectoryEntry
    {
        std::string Name;
        FileType Type = FileType::File;
    };

    class FileSource
    {
    public:
        virtual ~FileSource() = default;

        [[nodiscard]] virtual Expected<FileBuffer, FileError> Read(std::string_view relativePath) const = 0;
        [[nodiscard]] virtual Expected<FileType, FileError> Stat(std::string_view relativePath) const = 0;
        [[nodiscard]] virtual Expected<std::vector<DirectoryEntry>, FileError> List(std::string_view relativeDirectory) const = 0;

        [[nodiscard]] virtual std::string Describe() const = 0;
    };

    namespace FileSystem
    {
        void Initialize();
        void Shutdown();

        bool Mount(std::string_view mountPoint, Scope<FileSource> source);
        bool MountDirectory(std::string_view mountPoint, const std::filesystem::path& nativeDirectory);
        std::size_t Unmount(std::string_view mountPoint);

        [[nodiscard]] Expected<FileBuffer, FileError> ReadFile(std::string_view path);
        [[nodiscard]] Expected<std::string, FileError> ReadText(std::string_view path);
        [[nodiscard]] bool Exists(std::string_view path);
        [[nodiscard]] Expected<std::vector<DirectoryEntry>, FileError> List(std::string_view directory);
        [[nodiscard]] std::optional<std::string> NormalizePath(std::string_view path);
    }
}