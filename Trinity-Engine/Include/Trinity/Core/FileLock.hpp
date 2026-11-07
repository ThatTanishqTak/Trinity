#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Trinity
{
    // Why a FileLock was not had: another process holds it, named by the ID it wrote, 0 when that could not be read, or the file could not be locked, for the reason given
    struct FileLockRefusal
    {
        bool Held = false;
        std::uint32_t HolderID = 0;
        std::string Reason;
    };

    // A file one process at a time holds, as one Hub runs at a time, until it is released or its holder ends, killed or not, when the system lets go of it, so the next to ask takes over a lock left behind. The holder writes its process ID into the file, by which another names it and brings it forward
    class TRINITY_API FileLock
    {
    public:
        FileLock() = default;
        ~FileLock();

        FileLock(FileLock&& other) noexcept;
        FileLock& operator=(FileLock&& other) noexcept;

        FileLock(const FileLock&) = delete;
        FileLock& operator=(const FileLock&) = delete;

        // Takes the lock, making the file and its folder when they are missing, without waiting
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