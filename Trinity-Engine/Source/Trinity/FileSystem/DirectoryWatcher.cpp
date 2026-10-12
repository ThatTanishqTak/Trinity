#include "Trinity/FileSystem/DirectoryWatcher.hpp"

#include "Trinity/Core/Platform.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace Trinity
{
    DirectoryWatcher::DirectoryWatcher(void* handle, std::filesystem::path directory) : m_Handle(handle), m_Directory(std::move(directory))
    {

    }

    DirectoryWatcher::~DirectoryWatcher()
    {
        Stop();
    }

    DirectoryWatcher::DirectoryWatcher(DirectoryWatcher&& other) noexcept : m_Handle(std::exchange(other.m_Handle, nullptr)), m_Directory(std::move(other.m_Directory))
    {

    }

    DirectoryWatcher& DirectoryWatcher::operator=(DirectoryWatcher&& other) noexcept
    {
        if (this != &other)
        {
            Stop();
            m_Handle = std::exchange(other.m_Handle, nullptr);
            m_Directory = std::move(other.m_Directory);
        }

        return *this;
    }

    Expected<DirectoryWatcher, std::string> DirectoryWatcher::Watch(const std::filesystem::path& directory)
    {
        std::error_code l_Error;
        const std::filesystem::path l_Directory = std::filesystem::absolute(directory, l_Error).lexically_normal();

        std::string l_Reason;
        void* l_Handle = Platform::OpenDirectoryWatch(l_Directory, l_Reason);
        if (l_Handle == nullptr)
        {
            return Unexpected{ std::format("{} cannot be watched: {}", FileSystemUtilities::ToUtf8(l_Directory), l_Reason) };
        }

        return DirectoryWatcher(l_Handle, l_Directory);
    }

    void DirectoryWatcher::Stop()
    {
        if (m_Handle != nullptr)
        {
            Platform::CloseDirectoryWatch(m_Handle);
            m_Handle = nullptr;
        }
    }

    // Windows tells of a file saved once several times over, so each path is kept the first time only
    DirectoryChanges DirectoryWatcher::Poll()
    {
        DirectoryChanges l_Changes;
        if (m_Handle == nullptr)
        {
            return l_Changes;
        }

        std::vector<std::filesystem::path> l_Paths;
        l_Changes.Overflowed = Platform::ReadDirectoryWatch(m_Handle, l_Paths);
        for (std::filesystem::path& it_Path : l_Paths)
        {
            if (std::ranges::find(l_Changes.Paths, it_Path) == l_Changes.Paths.end())
            {
                l_Changes.Paths.push_back(std::move(it_Path));
            }
        }

        return l_Changes;
    }
}