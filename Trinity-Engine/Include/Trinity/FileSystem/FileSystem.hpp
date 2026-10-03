#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Memory.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
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
        ReadFailed,
        ReadOnly,
        WriteFailed
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(FileError error);

    enum class FileType : std::uint8_t
    {
        File,
        Directory
    };

    enum class MountAccess : std::uint8_t
    {
        ReadOnly,
        ReadWrite
    };

    using FileBuffer = std::vector<std::byte, TaggedAllocator<std::byte, MemoryTag::FileSystem>>;

    struct DirectoryEntry
    {
        std::string Name;
        FileType Type = FileType::File;
    };

    struct FileRequestState;

    class TRINITY_API FileRequest
    {
    public:
        FileRequest() = default;
        explicit FileRequest(std::shared_ptr<FileRequestState> state);
        ~FileRequest();

        FileRequest(FileRequest&& other) noexcept = default;
        FileRequest& operator=(FileRequest&& other) noexcept;

        FileRequest(const FileRequest&) = delete;
        FileRequest& operator=(const FileRequest&) = delete;

        void Cancel();

        [[nodiscard]] bool IsPending() const;

    private:
        std::shared_ptr<FileRequestState> m_State;
    };

    using ReadCallback = std::move_only_function<void(Expected<FileBuffer, FileError> result)>;

    class TRINITY_API FileSource
    {
    public:
        virtual ~FileSource() = default;

        [[nodiscard]] virtual Expected<FileBuffer, FileError> Read(std::string_view relativePath) const = 0;
        [[nodiscard]] virtual Expected<FileType, FileError> Stat(std::string_view relativePath) const = 0;
        [[nodiscard]] virtual Expected<std::vector<DirectoryEntry>, FileError> List(std::string_view relativeDirectory) const = 0;

        // Only called for read-write mounts
        [[nodiscard]] virtual Expected<void, FileError> Write([[maybe_unused]] std::string_view relativePath, [[maybe_unused]] std::span<const std::byte> data)
        {
            return Unexpected{ FileError::ReadOnly };
        }

        [[nodiscard]] virtual Expected<void, FileError> Remove([[maybe_unused]] std::string_view relativePath)
        {
            return Unexpected{ FileError::ReadOnly };
        }

        [[nodiscard]] virtual std::string Describe() const = 0;
    };

    namespace FileSystem
    {
        TRINITY_API void Initialize();
        TRINITY_API void Shutdown();

        TRINITY_API bool Mount(std::string_view mountPoint, Scope<FileSource> source, MountAccess access = MountAccess::ReadOnly);
        TRINITY_API bool MountDirectory(std::string_view mountPoint, const std::filesystem::path& nativeDirectory, MountAccess access = MountAccess::ReadOnly);
        TRINITY_API std::size_t Unmount(std::string_view mountPoint);

        [[nodiscard]] TRINITY_API Expected<FileBuffer, FileError> ReadFile(std::string_view path);
        [[nodiscard]] TRINITY_API Expected<std::string, FileError> ReadText(std::string_view path);
        [[nodiscard]] TRINITY_API bool Exists(std::string_view path);
        [[nodiscard]] TRINITY_API Expected<std::vector<DirectoryEntry>, FileError> List(std::string_view directory);
        [[nodiscard]] TRINITY_API FileRequest ReadFileAsync(std::string_view path, ReadCallback callback);
        [[nodiscard]] TRINITY_API Expected<void, FileError> WriteFile(std::string_view path, std::span<const std::byte> data);
        [[nodiscard]] TRINITY_API Expected<void, FileError> WriteText(std::string_view path, std::string_view text);
        [[nodiscard]] TRINITY_API Expected<void, FileError> RemoveFile(std::string_view path);
        [[nodiscard]] TRINITY_API std::optional<std::string> NormalizePath(std::string_view path);
    }
}