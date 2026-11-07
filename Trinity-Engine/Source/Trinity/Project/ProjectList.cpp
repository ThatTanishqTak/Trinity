#include "Trinity/Project/ProjectList.hpp"

#include "Trinity/Core/Platform.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"
#include "Trinity/Project/SettingsYaml.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <format>
#include <fstream>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

namespace Trinity
{
    namespace
    {
        using FileSystemUtilities::FromUtf8;
        using FileSystemUtilities::ToUtf8;
        using SettingsYaml::ReadString;

        // A writer holds the lock for as long as one read and one write take, so a wait this long is for one that is stuck, or a lock that cannot be had
        constexpr std::chrono::seconds c_LockWait{ 10 };
        constexpr std::chrono::milliseconds c_LongestPause{ 4 };

        // Held while the list is read, merged and written, and let go by the system too when its holder ends, killed or not
        class ListLock
        {
        public:
            ListLock(ListLock&& other) noexcept : m_Handle(std::exchange(other.m_Handle, nullptr))
            {

            }

            ListLock& operator=(ListLock&&) = delete;
            ListLock(const ListLock&) = delete;
            ListLock& operator=(const ListLock&) = delete;

            ~ListLock()
            {
                if (m_Handle != nullptr)
                {
                    Platform::ReleaseFileLock(m_Handle);
                }
            }

            // Waited for while another reader or writer holds it, a pause longer each try
            static Expected<ListLock, std::string> Take(const std::filesystem::path& file)
            {
                std::error_code l_Error;
                std::filesystem::create_directories(file.parent_path(), l_Error);
                std::filesystem::path l_Path = file;
                l_Path += ".lock";

                const auto l_Deadline = std::chrono::steady_clock::now() + c_LockWait;
                std::chrono::milliseconds l_Pause{ 1 };
                while (true)
                {
                    std::string l_Reason;
                    if (void* l_Handle = Platform::TryLockFile(l_Path, std::format("{}\n", Platform::GetProcessID()), l_Reason))
                    {
                        return ListLock(l_Handle);
                    }

                    if (!l_Reason.empty())
                    {
                        return Unexpected{ std::format("{} could not be locked: {}", ToUtf8(l_Path), l_Reason) };
                    }

                    if (std::chrono::steady_clock::now() >= l_Deadline)
                    {
                        return Unexpected{ std::format("{} stayed locked by another program for {} seconds", ToUtf8(l_Path), c_LockWait.count()) };
                    }

                    std::this_thread::sleep_for(l_Pause);
                    l_Pause = std::min(l_Pause * 2, c_LongestPause);
                }
            }

        private:
            explicit ListLock(void* handle) : m_Handle(handle)
            {

            }

            void* m_Handle = nullptr;
        };

        std::filesystem::path Normalize(const std::filesystem::path& path)
        {
            std::error_code l_Error;
            const std::filesystem::path l_Path = std::filesystem::absolute(path, l_Error);

            return (l_Error ? path : l_Path).lexically_normal();
        }

        std::string GetNow()
        {
            return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        }

        // The opened ones newest first, then those never opened, each kept in its order otherwise
        void Sort(std::vector<ProjectListEntry>& entries)
        {
            std::ranges::stable_sort(entries, [](const ProjectListEntry& left, const ProjectListEntry& right)
            {
                if (left.LastOpened.empty() != right.LastOpened.empty())
                {
                    return right.LastOpened.empty();
                }

                return left.LastOpened > right.LastOpened;
            });
        }

        // Read while the lock is held. A list not there yet is empty, and one that cannot be read is refused, so nothing writes over it
        Expected<std::vector<ProjectListEntry>, std::string> ReadList(const std::filesystem::path& file)
        {
            std::error_code l_Error;
            if (!std::filesystem::exists(file, l_Error))
            {
                return std::vector<ProjectListEntry>();
            }

            std::ifstream l_Stream(file, std::ios::binary);
            const std::string l_Text((std::istreambuf_iterator<char>(l_Stream)), std::istreambuf_iterator<char>());
            if (l_Stream.bad())
            {
                return Unexpected{ std::format("{} could not be read", ToUtf8(file)) };
            }

            YAML::Node l_Root;
            try
            {
                l_Root = YAML::Load(l_Text);
            }
            catch (const YAML::Exception& exception)
            {
                return Unexpected{ std::format("{} is not valid YAML, so it is left as it is until it is mended or deleted: {}", ToUtf8(file), exception.what()) };
            }

            if (l_Root.IsNull())
            {
                return std::vector<ProjectListEntry>();
            }

            std::uint32_t l_Format = 0;
            const std::string l_FormatText = l_Root.IsMap() ? ReadString(l_Root, "Format", "") : std::string();
            const std::from_chars_result l_Parsed = std::from_chars(l_FormatText.data(), l_FormatText.data() + l_FormatText.size(), l_Format);
            if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_FormatText.data() + l_FormatText.size() || l_Format == 0)
            {
                return Unexpected{ std::format("{} has no valid Format version, so it is left as it is until it is mended or deleted", ToUtf8(file)) };
            }

            if (l_Format > ProjectList::c_FormatVersion)
            {
                return Unexpected{ std::format("{} was written in format {} by a newer Hub, and this one reads format {} and older, so it is left as it is", ToUtf8(file), l_Format, ProjectList::c_FormatVersion) };
            }

            std::vector<ProjectListEntry> l_Entries;
            const YAML::Node l_List = l_Root["Projects"];
            if (!l_List || !l_List.IsSequence())
            {
                return l_Entries;
            }

            for (const YAML::Node& it_Item : l_List)
            {
                const std::string l_File = it_Item.IsMap() ? ReadString(it_Item, "File", "") : std::string();
                if (l_File.empty())
                {
                    continue;
                }

                ProjectListEntry l_Entry;
                l_Entry.File = FromUtf8(l_File);
                l_Entry.File.make_preferred();
                l_Entry.Name = ReadString(it_Item, "Name", ToUtf8(l_Entry.File.stem()));
                l_Entry.EngineVersion = ReadString(it_Item, "EngineVersion", "");
                l_Entry.LastOpened = ReadString(it_Item, "LastOpened", "");
                l_Entries.push_back(std::move(l_Entry));
            }

            return l_Entries;
        }

        // Into a file beside it first, which then takes its place, so a reader never meets a list half written
        Expected<void, std::string> WriteList(const std::filesystem::path& file, const std::vector<ProjectListEntry>& entries)
        {
            YAML::Emitter l_Emitter;
            l_Emitter << YAML::BeginMap;
            l_Emitter << YAML::Key << "Format" << YAML::Value << ProjectList::c_FormatVersion;
            l_Emitter << YAML::Key << "Projects" << YAML::Value << YAML::BeginSeq;
            for (const ProjectListEntry& it_Entry : entries)
            {
                l_Emitter << YAML::BeginMap;
                l_Emitter << YAML::Key << "File" << YAML::Value << YAML::DoubleQuoted << ToUtf8(it_Entry.File.generic_u8string());
                l_Emitter << YAML::Key << "Name" << YAML::Value << YAML::DoubleQuoted << it_Entry.Name;
                l_Emitter << YAML::Key << "EngineVersion" << YAML::Value << YAML::DoubleQuoted << it_Entry.EngineVersion;
                l_Emitter << YAML::Key << "LastOpened" << YAML::Value << YAML::DoubleQuoted << it_Entry.LastOpened;
                l_Emitter << YAML::EndMap;
            }

            l_Emitter << YAML::EndSeq;
            l_Emitter << YAML::EndMap;

            std::filesystem::path l_Written = file;
            l_Written += ".tmp";
            {
                std::ofstream l_Stream(l_Written, std::ios::binary | std::ios::trunc);
                l_Stream << l_Emitter.c_str() << '\n';
                l_Stream.close();
                if (!l_Stream)
                {
                    return Unexpected{ std::format("{} could not be written", ToUtf8(l_Written)) };
                }
            }

            std::error_code l_Error;
            std::filesystem::rename(l_Written, file, l_Error);
            if (l_Error)
            {
                std::filesystem::remove(l_Written, l_Error);

                return Unexpected{ std::format("{} could not be replaced: {}", ToUtf8(file), l_Error.message()) };
            }

            return {};
        }

        // The same path is the same project at once, and the disk is asked only of one by the same name, so a long list is searched without a call to the disk for each entry
        bool IsListed(const ProjectListEntry& entry, const std::filesystem::path& project)
        {
            if (Normalize(entry.File) == project)
            {
                return true;
            }

            return FileSystemUtilities::EqualsIgnoreAsciiCase(ToUtf8(entry.File.filename()), ToUtf8(project.filename())) && ProjectList::IsSameFile(entry.File, project);
        }
    }

    namespace ProjectList
    {
        std::filesystem::path GetHubDirectory()
        {
            const std::filesystem::path l_Base = Platform::GetUserDataDirectory();

            return (l_Base.empty() ? std::filesystem::path("UserData") : l_Base) / "Trinity" / "Hub";
        }

        Expected<std::vector<ProjectListEntry>, std::string> Read(const std::filesystem::path& file)
        {
            Expected<ListLock, std::string> l_Lock = ListLock::Take(file);
            if (!l_Lock)
            {
                return Unexpected{ l_Lock.GetError() };
            }

            Expected<std::vector<ProjectListEntry>, std::string> l_Entries = ReadList(file);
            if (l_Entries)
            {
                Sort(*l_Entries);
            }

            return l_Entries;
        }

        Expected<void, std::string> Change(const std::filesystem::path& file, const std::function<void(std::vector<ProjectListEntry>&)>& change)
        {
            Expected<ListLock, std::string> l_Lock = ListLock::Take(file);
            if (!l_Lock)
            {
                return Unexpected{ l_Lock.GetError() };
            }

            Expected<std::vector<ProjectListEntry>, std::string> l_Entries = ReadList(file);
            if (!l_Entries)
            {
                return Unexpected{ l_Entries.GetError() };
            }

            change(*l_Entries);
            Sort(*l_Entries);

            return WriteList(file, *l_Entries);
        }

        Expected<void, std::string> RecordOpened(const std::filesystem::path& file, ProjectListEntry entry)
        {
            entry.File = Normalize(entry.File);
            entry.LastOpened = GetNow();

            return Change(file, [&entry](std::vector<ProjectListEntry>& entries)
            {
                const auto a_Entry = std::ranges::find_if(entries, [&entry](const ProjectListEntry& listed) { return IsListed(listed, entry.File); });
                if (a_Entry == entries.end())
                {
                    entries.push_back(entry);

                    return;
                }

                a_Entry->File = entry.File;
                a_Entry->Name = entry.Name;
                a_Entry->EngineVersion = entry.EngineVersion;
                a_Entry->LastOpened = entry.LastOpened;
            });
        }

        Expected<bool, std::string> Add(const std::filesystem::path& file, ProjectListEntry entry)
        {
            entry.File = Normalize(entry.File);
            bool l_Added = false;
            const Expected<void, std::string> l_Changed = Change(file, [&entry, &l_Added](std::vector<ProjectListEntry>& entries)
            {
                if (std::ranges::none_of(entries, [&entry](const ProjectListEntry& listed) { return IsListed(listed, entry.File); }))
                {
                    entries.push_back(entry);
                    l_Added = true;
                }
            });

            if (!l_Changed)
            {
                return Unexpected{ l_Changed.GetError() };
            }

            return l_Added;
        }

        Expected<void, std::string> Remove(const std::filesystem::path& file, const std::filesystem::path& project)
        {
            const std::filesystem::path l_Project = Normalize(project);

            return Change(file, [&l_Project](std::vector<ProjectListEntry>& entries)
            {
                std::erase_if(entries, [&l_Project](const ProjectListEntry& entry) { return IsListed(entry, l_Project); });
            });
        }

        bool IsSameFile(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            std::error_code l_Error;
            if (std::filesystem::exists(left, l_Error) && std::filesystem::exists(right, l_Error))
            {
                const bool l_Same = std::filesystem::equivalent(left, right, l_Error);

                return l_Same && !l_Error;
            }

            return Normalize(left) == Normalize(right);
        }
    }
}