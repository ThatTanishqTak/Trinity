#include "Trinity/Core/FileLock.hpp"

#include "Trinity/Core/Platform.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"

#include <charconv>
#include <chrono>
#include <format>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

namespace Trinity
{
    namespace
    {
        // The holder writes its ID just after it takes the lock, so a file read in between is empty for a moment
        std::uint32_t ReadHolder(const std::filesystem::path& path)
        {
            for (int it_Try = 0; it_Try < 5; ++it_Try)
            {
                if (const std::optional<std::string> l_Text = Platform::ReadLockedFile(path))
                {
                    std::uint32_t l_ID = 0;
                    const std::from_chars_result l_Parsed = std::from_chars(l_Text->data(), l_Text->data() + l_Text->size(), l_ID);
                    if (l_Parsed.ec == std::errc() && l_ID != 0)
                    {
                        return l_ID;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            return 0;
        }
    }

    FileLock::FileLock(void* handle, std::filesystem::path path) : m_Handle(handle), m_Path(std::move(path))
    {

    }

    FileLock::~FileLock()
    {
        Release();
    }

    FileLock::FileLock(FileLock&& other) noexcept : m_Handle(std::exchange(other.m_Handle, nullptr)), m_Path(std::move(other.m_Path))
    {

    }

    FileLock& FileLock::operator=(FileLock&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            m_Handle = std::exchange(other.m_Handle, nullptr);
            m_Path = std::move(other.m_Path);
        }

        return *this;
    }

    Expected<FileLock, FileLockRefusal> FileLock::TryAcquire(const std::filesystem::path& path)
    {
        std::error_code l_Error;
        std::filesystem::create_directories(path.parent_path(), l_Error);

        std::string l_Reason;
        void* l_Handle = Platform::TryLockFile(path, std::format("{}\n", Platform::GetProcessID()), l_Reason);
        if (l_Handle != nullptr)
        {
            return FileLock(l_Handle, path);
        }

        if (!l_Reason.empty())
        {
            return Unexpected{ FileLockRefusal{ false, 0, std::format("{} could not be locked: {}", FileSystemUtilities::ToUtf8(path), l_Reason) } };
        }

        const std::uint32_t l_Holder = ReadHolder(path);

        return Unexpected{ FileLockRefusal{ true, l_Holder, l_Holder != 0 ? std::format("process {} holds {}", l_Holder, FileSystemUtilities::ToUtf8(path)) : std::format("another process holds {}", FileSystemUtilities::ToUtf8(path)) } };
    }

    void FileLock::Release()
    {
        if (m_Handle != nullptr)
        {
            Platform::ReleaseFileLock(m_Handle);
            m_Handle = nullptr;
        }
    }
}