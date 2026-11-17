#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Trinity
{
    // HolderID is 0 when the holder's ID could not be read
    struct FileLockRefusal
    {
        bool Held = false;
        std::uint32_t HolderID = 0;
        std::string Reason;
    };

    // The system releases it when its holder ends, killed or not. The file holds the holder's ID
    class TRINITY_API FileLock
    {
    public:
        FileLock() = default;
        ~FileLock();

        FileLock(FileLock&& other) noexcept;
        FileLock& operator=(FileLock&& other) noexcept;

        FileLock(const FileLock&) = delete;
        FileLock& operator=(const FileLock&) = delete;

        // Makes the file and its folder when missing. Never waits
        [[nodiscard]] static Expected<FileLock, FileLockRefusal> TryAcquire(const std::filesystem::path& path);

        void Release();

        [[nodiscard]] bool IsHeld() const { return m_Handle != nullptr; }
        [[nodiscard]] const std::filesystem::path& GetPath() const { return m_Path; }

    private:
        FileLock(void* handle, std::filesystem::path path);

        void* m_Handle = nullptr;
        std::filesystem::path m_Path;
    };
}