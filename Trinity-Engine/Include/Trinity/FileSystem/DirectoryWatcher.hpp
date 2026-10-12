#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Trinity
{
    // What changed below a watched folder since it was last asked: each path relative to the folder, once, in the order first told. Overflowed when more changed than could be told, so everything below it should be looked at again
    struct DirectoryChanges
    {
        std::vector<std::filesystem::path> Paths;
        bool Overflowed = false;

        [[nodiscard]] bool IsEmpty() const { return Paths.empty() && !Overflowed; }
    };

    // A folder and everything below it, watched for files and folders added, changed, removed or renamed: by the system's notifications on Windows, and elsewhere by comparing each file's size and time with those of the last look, at most twice a second. Poll it from one thread as often as wanted, since it never waits
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