#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Trinity
{
    // Paths are relative to the folder. Overflowed means everything below it should be looked at again
    struct DirectoryChanges
    {
        std::vector<std::filesystem::path> Paths;
        bool Overflowed = false;

        [[nodiscard]] bool IsEmpty() const { return Paths.empty() && !Overflowed; }
    };

    // Notifications on Windows, a scan twice a second elsewhere. Poll from one thread; it never waits
    class TRINITY_API DirectoryWatcher
    {
    public:
        DirectoryWatcher() = default;
        ~DirectoryWatcher();

        DirectoryWatcher(DirectoryWatcher&& other) noexcept;
        DirectoryWatcher& operator=(DirectoryWatcher&& other) noexcept;

        DirectoryWatcher(const DirectoryWatcher&) = delete;
        DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

        [[nodiscard]] static Expected<DirectoryWatcher, std::string> Watch(const std::filesystem::path& directory);

        void Stop();

        [[nodiscard]] DirectoryChanges Poll();

        [[nodiscard]] bool IsWatching() const { return m_Handle != nullptr; }
        [[nodiscard]] const std::filesystem::path& GetDirectory() const { return m_Directory; }

    private:
        DirectoryWatcher(void* handle, std::filesystem::path directory);

        void* m_Handle = nullptr;
        std::filesystem::path m_Directory;
    };
}