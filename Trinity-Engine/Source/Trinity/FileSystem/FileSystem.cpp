#include "Trinity/FileSystem/FileSystem.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/DirectorySource.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <system_error>
#include <utility>

namespace Trinity
{
    namespace
    {
        struct MountEntry
        {
            std::string Point;
            Scope<FileSource> Source;
            MountAccess Access = MountAccess::ReadOnly;
        };

        struct State
        {
            std::shared_mutex Mutex;
            std::vector<MountEntry, TaggedAllocator<MountEntry, MemoryTag::FileSystem>> Mounts;
        };

        State* s_State = nullptr;

        bool IsForbiddenCharacter(char character)
        {
            constexpr std::string_view c_Forbidden = "\\:*?\"<>|";

            return static_cast<unsigned char>(character) < 0x20 || c_Forbidden.find(character) != std::string_view::npos;
        }

        bool IsValidComponent(std::string_view component)
        {
            return component != "." && component != ".." && component.back() != '.' && component.back() != ' ' && std::ranges::none_of(component, IsForbiddenCharacter);
        }

        std::optional<std::string_view> RelativeTo(std::string_view path, std::string_view mountPoint)
        {
            if (mountPoint == "/")
            {
                return path.substr(1);
            }

            if (path == mountPoint)
            {
                return std::string_view{};
            }

            if (path.starts_with(mountPoint) && path.size() > mountPoint.size() && path[mountPoint.size()] == '/')
            {
                return path.substr(mountPoint.size() + 1);
            }

            return std::nullopt;
        }
    }

    std::string_view ToString(FileError error)
    {
        switch (error)
        {
            case FileError::InvalidPath:
            {
                return "invalid path";
            }
            case FileError::NotMounted:
            {
                return "nothing is mounted there";
            }
            case FileError::NotFound:
            {
                return "not found";
            }
            case FileError::CaseMismatch:
            {
                return "case differs from the name on disk";
            }
            case FileError::NotAFile:
            {
                return "not a file";
            }
            case FileError::TooLarge:
            {
                return "too large";
            }
            case FileError::ReadFailed:
            {
                return "read failed";
            }
            case FileError::ReadOnly:
            {
                return "read-only";
            }
            case FileError::WriteFailed:
            {
                return "write failed";
            }
        }

        return "unknown error";
    }

    namespace FileSystem
    {
        void Initialize()
        {
            TR_CORE_ASSERT(s_State == nullptr, "The file system is already initialized.");

            s_State = Memory::New<State>(MemoryTag::FileSystem);
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The file system is not initialized.");

            Memory::Delete(s_State);
            s_State = nullptr;
        }

        bool Mount(std::string_view mountPoint, Scope<FileSource> source, MountAccess access)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The file system is not initialized.");

            std::optional<std::string> l_Point = NormalizePath(mountPoint);
            if (!l_Point || source == nullptr)
            {
                TR_CORE_ERROR("Cannot mount at '{}': {}", mountPoint, l_Point ? "no source" : "invalid mount point");

                return false;
            }

            TR_CORE_INFO("Mounted {} at {}{}", source->Describe(), *l_Point, access == MountAccess::ReadWrite ? " (read-write)" : "");

            std::unique_lock l_Lock(s_State->Mutex);
            s_State->Mounts.push_back({ std::move(*l_Point), std::move(source), access });

            return true;
        }

        bool MountDirectory(std::string_view mountPoint, const std::filesystem::path& nativeDirectory, MountAccess access)
        {
            std::error_code l_Error;
            const std::filesystem::path l_Root = std::filesystem::absolute(nativeDirectory, l_Error);
            if (l_Error || !std::filesystem::is_directory(l_Root, l_Error))
            {
                TR_CORE_ERROR("Cannot mount '{}' at '{}': it is not a directory", nativeDirectory.string(), mountPoint);

                return false;
            }

            return Mount(mountPoint, CreateScope<DirectorySource>(l_Root.lexically_normal()), access);
        }

        std::size_t Unmount(std::string_view mountPoint)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The file system is not initialized.");

            const std::optional<std::string> l_Point = NormalizePath(mountPoint);
            if (!l_Point)
            {
                return 0;
            }

            std::unique_lock l_Lock(s_State->Mutex);

            return std::erase_if(s_State->Mounts, [&l_Point](const MountEntry& entry) { return entry.Point == *l_Point; });
        }

        Expected<FileBuffer, FileError> ReadFile(std::string_view path)
        {
            TR_PROFILE_SCOPE("FileSystem::ReadFile");
            TR_CORE_ASSERT(s_State != nullptr, "The file system is not initialized.");

            const std::optional<std::string> l_Path = NormalizePath(path);
            if (!l_Path)
            {
                return Unexpected{ FileError::InvalidPath };
            }

            std::shared_lock l_Lock(s_State->Mutex);

            FileError l_Error = FileError::NotMounted;
            for (auto it_Mount = s_State->Mounts.rbegin(); it_Mount != s_State->Mounts.rend(); ++it_Mount)
            {
                const std::optional<std::string_view> l_Relative = RelativeTo(*l_Path, it_Mount->Point);
                if (!l_Relative)
                {
                    continue;
                }

                Expected<FileBuffer, FileError> l_Result = it_Mount->Source->Read(*l_Relative);
                if (l_Result || l_Result.GetError() != FileError::NotFound)
                {
                    return l_Result;
                }

                l_Error = FileError::NotFound;
            }

            return Unexpected{ l_Error };
        }

        Expected<std::string, FileError> ReadText(std::string_view path)
        {
            Expected<FileBuffer, FileError> l_Buffer = ReadFile(path);
            if (!l_Buffer)
            {
                return Unexpected{ l_Buffer.GetError() };
            }

            return std::string(reinterpret_cast<const char*>(l_Buffer->data()), l_Buffer->size());
        }

        bool Exists(std::string_view path)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The file system is not initialized.");

            const std::optional<std::string> l_Path = NormalizePath(path);
            if (!l_Path)
            {
                return false;
            }

            std::shared_lock l_Lock(s_State->Mutex);
            for (auto it_Mount = s_State->Mounts.rbegin(); it_Mount != s_State->Mounts.rend(); ++it_Mount)
            {
                if (RelativeTo(it_Mount->Point, *l_Path).has_value())
                {
                    return true;
                }

                const std::optional<std::string_view> l_Relative = RelativeTo(*l_Path, it_Mount->Point);
                if (!l_Relative)
                {
                    continue;
                }

                const Expected<FileType, FileError> l_Type = it_Mount->Source->Stat(*l_Relative);
                if (l_Type)
                {
                    return true;
                }

                if (l_Type.GetError() == FileError::CaseMismatch)
                {
                    TR_CORE_WARN("'{}' does not exist: {}", *l_Path, ToString(l_Type.GetError()));

                    return false;
                }
            }

            return false;
        }

        Expected<std::vector<DirectoryEntry>, FileError> List(std::string_view directory)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The file system is not initialized.");

            const std::optional<std::string> l_Directory = NormalizePath(directory);
            if (!l_Directory)
            {
                return Unexpected{ FileError::InvalidPath };
            }

            std::map<std::string, FileType> l_Merged;
            bool l_Found = false;

            std::shared_lock l_Lock(s_State->Mutex);
            for (const MountEntry& it_Mount : s_State->Mounts)
            {
                if (const std::optional<std::string_view> l_Below = RelativeTo(it_Mount.Point, *l_Directory); l_Below && !l_Below->empty())
                {
                    l_Merged[std::string(l_Below->substr(0, l_Below->find('/')))] = FileType::Directory;
                    l_Found = true;

                    continue;
                }

                const std::optional<std::string_view> l_Relative = RelativeTo(*l_Directory, it_Mount.Point);
                if (!l_Relative)
                {
                    continue;
                }

                const Expected<std::vector<DirectoryEntry>, FileError> l_Entries = it_Mount.Source->List(*l_Relative);
                if (!l_Entries)
                {
                    if (l_Entries.GetError() != FileError::NotFound)
                    {
                        return Unexpected{ l_Entries.GetError() };
                    }

                    continue;
                }

                for (const DirectoryEntry& it_Entry : *l_Entries)
                {
                    l_Merged[it_Entry.Name] = it_Entry.Type;
                }
                l_Found = true;
            }

            if (!l_Found)
            {
                return Unexpected{ FileError::NotFound };
            }

            std::vector<DirectoryEntry> l_Result;
            l_Result.reserve(l_Merged.size());
            for (auto& [a_Name, a_Type] : l_Merged)
            {
                l_Result.push_back({ a_Name, a_Type });
            }

            return l_Result;
        }

        Expected<void, FileError> WriteFile(std::string_view path, std::span<const std::byte> data)
        {
            TR_PROFILE_SCOPE("FileSystem::WriteFile");
            TR_CORE_ASSERT(s_State != nullptr, "The file system is not initialized.");

            const std::optional<std::string> l_Path = NormalizePath(path);
            if (!l_Path)
            {
                return Unexpected{ FileError::InvalidPath };
            }

            std::shared_lock l_Lock(s_State->Mutex);

            bool l_Mounted = false;
            for (auto it_Mount = s_State->Mounts.rbegin(); it_Mount != s_State->Mounts.rend(); ++it_Mount)
            {
                const std::optional<std::string_view> l_Relative = RelativeTo(*l_Path, it_Mount->Point);
                if (!l_Relative)
                {
                    continue;
                }

                l_Mounted = true;
                if (it_Mount->Access == MountAccess::ReadWrite)
                {
                    return it_Mount->Source->Write(*l_Relative, data);
                }
            }

            return Unexpected{ l_Mounted ? FileError::ReadOnly : FileError::NotMounted };
        }

        Expected<void, FileError> WriteText(std::string_view path, std::string_view text)
        {
            return WriteFile(path, std::as_bytes(std::span(text.data(), text.size())));
        }

        Expected<void, FileError> RemoveFile(std::string_view path)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The file system is not initialized.");

            const std::optional<std::string> l_Path = NormalizePath(path);
            if (!l_Path)
            {
                return Unexpected{ FileError::InvalidPath };
            }

            std::shared_lock l_Lock(s_State->Mutex);

            bool l_Mounted = false;
            for (auto it_Mount = s_State->Mounts.rbegin(); it_Mount != s_State->Mounts.rend(); ++it_Mount)
            {
                const std::optional<std::string_view> l_Relative = RelativeTo(*l_Path, it_Mount->Point);
                if (!l_Relative)
                {
                    continue;
                }

                l_Mounted = true;
                if (it_Mount->Access == MountAccess::ReadWrite)
                {
                    return it_Mount->Source->Remove(*l_Relative);
                }
            }

            return Unexpected{ l_Mounted ? FileError::ReadOnly : FileError::NotMounted };
        }

        std::optional<std::string> NormalizePath(std::string_view path)
        {
            if (path.empty() || path.front() != '/')
            {
                return std::nullopt;
            }

            std::string l_Result;
            l_Result.reserve(path.size());

            std::size_t l_Position = 0;
            while (l_Position < path.size())
            {
                while (l_Position < path.size() && path[l_Position] == '/')
                {
                    ++l_Position;
                }

                if (l_Position == path.size())
                {
                    break;
                }

                const std::size_t l_End = std::min(path.find('/', l_Position), path.size());
                const std::string_view l_Component = path.substr(l_Position, l_End - l_Position);
                if (!IsValidComponent(l_Component))
                {
                    return std::nullopt;
                }

                l_Result += '/';
                l_Result += l_Component;
                l_Position = l_End;
            }

            if (l_Result.empty())
            {
                l_Result = "/";
            }

            return l_Result;
        }
    }
}