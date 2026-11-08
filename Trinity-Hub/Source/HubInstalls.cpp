#include "HubInstalls.hpp"

#include "HubText.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

namespace
{
    std::string ReadScalar(const YAML::Node& map, const char* key)
    {
        const YAML::Node l_Value = map[key];

        return l_Value && l_Value.IsScalar() ? l_Value.Scalar() : std::string();
    }

    std::vector<std::string> ReadNames(const YAML::Node& map, const char* key)
    {
        std::vector<std::string> l_Names;
        const YAML::Node l_Sequence = map[key];
        if (!l_Sequence || !l_Sequence.IsSequence())
        {
            return l_Names;
        }

        for (const YAML::Node& it_Item : l_Sequence)
        {
            if (it_Item.IsScalar() && !it_Item.Scalar().empty())
            {
                l_Names.push_back(it_Item.Scalar());
            }
        }

        return l_Names;
    }

    void WriteNames(YAML::Emitter& emitter, const char* key, const std::vector<std::string>& names)
    {
        emitter << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (const std::string& it_Name : names)
        {
            emitter << it_Name;
        }

        emitter << YAML::EndSeq;
    }

    const char* ToString(HubInstall::Origin origin)
    {
        return origin == HubInstall::Origin::Installed ? "Installed" : "Located";
    }

    std::filesystem::path Normalize(const std::filesystem::path& path)
    {
        std::error_code l_Error;
        std::filesystem::path l_Path = std::filesystem::absolute(path, l_Error).lexically_normal();
        if (l_Error)
        {
            l_Path = path.lexically_normal();
        }

        if (!l_Path.has_filename() && l_Path.has_parent_path() && l_Path != l_Path.root_path())
        {
            l_Path = l_Path.parent_path();
        }

        return l_Path;
    }
}

// A size still being counted is let finish its folder and dropped
HubInstalls::~HubInstalls()
{
    if (m_Count)
    {
        m_Count->Stop.store(true, std::memory_order_relaxed);
        Trinity::JobSystem::Wait(m_Jobs);
    }
}

void HubInstalls::Load()
{
    m_Installs.clear();
    m_Writable = true;

    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(c_Path);
    if (!l_Text)
    {
        CountSizes();

        return;
    }

    YAML::Node l_Root;
    try
    {
        l_Root = YAML::Load(*l_Text);
    }
    catch (const YAML::Exception& exception)
    {
        TR_WARN("Hub: {} is not valid YAML, so Hub lists no versions, and writes it again once one is located or installed: {}", c_Path, exception.what());

        return;
    }

    std::uint32_t l_Format = 0;
    const std::string l_FormatText = l_Root.IsMap() ? ReadScalar(l_Root, "Format") : std::string();
    const std::from_chars_result l_Parsed = std::from_chars(l_FormatText.data(), l_FormatText.data() + l_FormatText.size(), l_Format);
    if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_FormatText.data() + l_FormatText.size() || l_Format == 0)
    {
        TR_WARN("Hub: {} has no valid Format version, so Hub lists no versions, and writes it again once one is located or installed", c_Path);

        return;
    }

    if (l_Format > c_FormatVersion)
    {
        TR_WARN("Hub: {} was written in format {} by a newer Hub, and this one reads format {} and older, so it lists no versions and leaves the file as it is", c_Path, l_Format, c_FormatVersion);
        m_Writable = false;

        return;
    }

    const YAML::Node l_List = l_Root["Installs"];
    if (l_List && l_List.IsSequence())
    {
        std::size_t l_Index = 0;
        for (const YAML::Node& it_Item : l_List)
        {
            ++l_Index;
            const std::string l_Directory = it_Item.IsMap() ? ReadScalar(it_Item, "Directory") : std::string();
            const std::string l_Origin = it_Item.IsMap() ? ReadScalar(it_Item, "Origin") : std::string();
            if (l_Directory.empty() || (l_Origin != "Installed" && l_Origin != "Located"))
            {
                TR_WARN("Hub: version {} in {} names no folder, or neither Installed nor Located, so it is left out", l_Index, c_Path);

                continue;
            }

            HubInstall l_Install;
            std::filesystem::path l_Path = HubText::FromUtf8(l_Directory);
            l_Install.Directory = Normalize(l_Path.make_preferred());
            l_Install.Source = l_Origin == "Installed" ? HubInstall::Origin::Installed : HubInstall::Origin::Located;
            if (Find(l_Install.Directory) != nullptr)
            {
                continue;
            }

            Trinity::EngineManifest& l_Manifest = l_Install.Manifest;
            l_Manifest.Version = ReadScalar(it_Item, "Version");
            l_Manifest.Commit = ReadScalar(it_Item, "Commit");
            l_Manifest.Modified = ReadScalar(it_Item, "Modified") == "true";
            l_Manifest.Date = ReadScalar(it_Item, "Date");
            l_Manifest.Platform = ReadScalar(it_Item, "Platform");
            l_Manifest.System = ReadScalar(it_Item, "System");
            l_Manifest.Architecture = ReadScalar(it_Item, "Architecture");
            l_Manifest.Configurations = ReadNames(it_Item, "Configurations");
            l_Manifest.Exports = ReadNames(it_Item, "Exports");
            m_Installs.push_back(std::move(l_Install));
        }
    }

    Refresh();
}

// What a version's manifest says now goes into Installs.yaml, for the next time its folder is gone
void HubInstalls::Refresh()
{
    bool l_Changed = false;
    for (HubInstall& it_Install : m_Installs)
    {
        const Trinity::EngineManifest l_Before = it_Install.Manifest;
        Read(it_Install);
        l_Changed = l_Changed || it_Install.Manifest != l_Before;
    }

    Sort();
    if (l_Changed)
    {
        Save();
    }

    CountSizes();
}

void HubInstalls::Update()
{
    if (!m_Count || !m_Count->Done.load(std::memory_order_acquire))
    {
        return;
    }

    Trinity::JobSystem::Wait(m_Jobs);
    for (std::size_t it_Index = 0; it_Index < m_Count->Directories.size(); ++it_Index)
    {
        for (HubInstall& it_Install : m_Installs)
        {
            if (it_Install.Directory == m_Count->Directories[it_Index])
            {
                it_Install.Size = m_Count->Sizes[it_Index];
            }
        }
    }

    m_Count.reset();
    if (std::exchange(m_CountAgain, false))
    {
        CountSizes();
    }
}

const HubInstall* HubInstalls::Find(const std::filesystem::path& directory) const
{
    const auto a_Install = std::ranges::find_if(m_Installs, [&directory](const HubInstall& install) { return IsSameFolder(install.Directory, directory); });

    return a_Install != m_Installs.end() ? &*a_Install : nullptr;
}

const HubInstall* HubInstalls::FindInstalled(std::string_view name) const
{
    const auto a_Install = std::ranges::find_if(m_Installs, [name](const HubInstall& install) { return install.Source == HubInstall::Origin::Installed && GetInstallName(install.Manifest) == name; });

    return a_Install != m_Installs.end() ? &*a_Install : nullptr;
}

// Dates are all written alike, so their text sorts as they do
const HubInstall* HubInstalls::FindUsable(std::string_view version) const
{
    const HubInstall* l_Chosen = nullptr;
    for (const HubInstall& it_Install : m_Installs)
    {
        if (!IsUsable(it_Install) || it_Install.Manifest.Version != version)
        {
            continue;
        }

        if (l_Chosen == nullptr || it_Install.Manifest.Date > l_Chosen->Manifest.Date || (it_Install.Manifest.Date == l_Chosen->Manifest.Date && it_Install.Source == HubInstall::Origin::Installed && l_Chosen->Source == HubInstall::Origin::Located))
        {
            l_Chosen = &it_Install;
        }
    }

    return l_Chosen;
}

// The list is kept newest first, so the first usable one is of the newest version
const HubInstall* HubInstalls::FindNewestUsable() const
{
    const auto a_Install = std::ranges::find_if(m_Installs, &HubInstalls::IsUsable);

    return a_Install != m_Installs.end() ? FindUsable(a_Install->Manifest.Version) : nullptr;
}

void HubInstalls::Add(const std::filesystem::path& directory, HubInstall::Origin origin, Trinity::EngineManifest manifest)
{
    auto a_Install = std::ranges::find_if(m_Installs, [&directory](const HubInstall& install) { return IsSameFolder(install.Directory, directory); });
    if (a_Install == m_Installs.end())
    {
        m_Installs.push_back({});
        a_Install = std::prev(m_Installs.end());
        a_Install->Directory = Normalize(directory);
    }

    a_Install->Source = origin;
    a_Install->Manifest = std::move(manifest);
    a_Install->Missing = false;
    a_Install->Problem.clear();
    a_Install->Size.reset();

    Sort();
    Save();
    CountSizes();
}

void HubInstalls::Remove(const std::filesystem::path& directory)
{
    std::erase_if(m_Installs, [&directory](const HubInstall& install) { return IsSameFolder(install.Directory, directory); });
    Save();
}

std::string HubInstalls::GetInstallName(const Trinity::EngineManifest& manifest)
{
    return std::format("Trinity-{}-{}-{}", manifest.Version, manifest.Platform, manifest.Architecture);
}

std::string HubInstalls::Describe(const Trinity::EngineManifest& manifest)
{
    if (manifest.Commit.empty())
    {
        return std::format("Trinity {} (no commit)", manifest.Version);
    }

    return std::format("Trinity {} ({}{})", manifest.Version, std::string_view(manifest.Commit).substr(0, Trinity::c_ShortCommitLength), manifest.Modified ? ", modified" : "");
}

bool HubInstalls::IsSameFolder(const std::filesystem::path& left, const std::filesystem::path& right)
{
    std::error_code l_Error;
    if (std::filesystem::exists(left, l_Error) && std::filesystem::exists(right, l_Error))
    {
        const bool l_Same = std::filesystem::equivalent(left, right, l_Error);

        return l_Same && !l_Error;
    }

    return Normalize(left) == Normalize(right);
}

bool HubInstalls::IsUsable(const HubInstall& install)
{
    return !install.Missing && install.Problem.empty() && install.Manifest.Platform == Trinity::GetBuildPlatform() && install.Manifest.Architecture == Trinity::GetBuildArchitecture();
}

void HubInstalls::Save() const
{
    if (!m_Writable)
    {
        TR_WARN("Hub: {} is of a newer format, which this Hub never writes over, so this change to the list lasts until Hub closes", c_Path);

        return;
    }

    YAML::Emitter l_Emitter;
    l_Emitter << YAML::BeginMap;
    l_Emitter << YAML::Key << "Format" << YAML::Value << c_FormatVersion;
    l_Emitter << YAML::Key << "Installs" << YAML::Value << YAML::BeginSeq;
    for (const HubInstall& it_Install : m_Installs)
    {
        const Trinity::EngineManifest& l_Manifest = it_Install.Manifest;
        l_Emitter << YAML::BeginMap;
        l_Emitter << YAML::Key << "Directory" << YAML::Value << YAML::DoubleQuoted << HubText::ToUtf8(it_Install.Directory.generic_u8string());
        l_Emitter << YAML::Key << "Origin" << YAML::Value << ToString(it_Install.Source);
        l_Emitter << YAML::Key << "Version" << YAML::Value << YAML::DoubleQuoted << l_Manifest.Version;
        l_Emitter << YAML::Key << "Commit" << YAML::Value << YAML::DoubleQuoted << l_Manifest.Commit;
        l_Emitter << YAML::Key << "Modified" << YAML::Value << l_Manifest.Modified;
        l_Emitter << YAML::Key << "Date" << YAML::Value << YAML::DoubleQuoted << l_Manifest.Date;
        l_Emitter << YAML::Key << "Platform" << YAML::Value << l_Manifest.Platform;
        l_Emitter << YAML::Key << "System" << YAML::Value << l_Manifest.System;
        l_Emitter << YAML::Key << "Architecture" << YAML::Value << l_Manifest.Architecture;
        WriteNames(l_Emitter, "Configurations", l_Manifest.Configurations);
        WriteNames(l_Emitter, "Exports", l_Manifest.Exports);
        l_Emitter << YAML::EndMap;
    }

    l_Emitter << YAML::EndSeq;
    l_Emitter << YAML::EndMap;

    const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(c_Path, std::string(l_Emitter.c_str()) + "\n");
    if (!l_Written)
    {
        TR_WARN("Hub: {} could not be written, so this list of versions lasts until Hub closes: {}", c_Path, Trinity::ToString(l_Written.GetError()));
    }
}

// A version whose manifest cannot be read keeps what it last said, with the reason shown beside it
void HubInstalls::Read(HubInstall& install) const
{
    std::error_code l_Error;
    if (!std::filesystem::is_directory(install.Directory, l_Error))
    {
        install.Missing = true;
        install.Problem.clear();
        install.Size.reset();

        return;
    }

    install.Missing = false;
    Trinity::Expected<Trinity::EngineManifest, std::string> l_Manifest = Trinity::ReadEngineManifest(install.Directory);
    if (!l_Manifest)
    {
        install.Problem = l_Manifest.GetError();

        return;
    }

    install.Problem.clear();
    install.Manifest = std::move(*l_Manifest);
}

// Newest first, and for one version the latest commit first
void HubInstalls::Sort()
{
    std::ranges::stable_sort(m_Installs, [](const HubInstall& left, const HubInstall& right)
    {
        if (const std::strong_ordering l_Order = Trinity::CompareVersions(left.Manifest.Version, right.Manifest.Version); l_Order != 0)
        {
            return l_Order > 0;
        }

        if (left.Manifest.Date != right.Manifest.Date)
        {
            return left.Manifest.Date > right.Manifest.Date;
        }

        return left.Directory < right.Directory;
    });
}

void HubInstalls::CountSizes()
{
    if (m_Count)
    {
        m_CountAgain = true;

        return;
    }

    m_Count = std::make_unique<SizeCount>();
    for (const HubInstall& it_Install : m_Installs)
    {
        if (!it_Install.Missing)
        {
            m_Count->Directories.push_back(it_Install.Directory);
        }
    }

    m_Count->Sizes.resize(m_Count->Directories.size());
    SizeCount* l_Count = m_Count.get();
    Trinity::JobSystem::Submit([l_Count] { Count(*l_Count); }, &m_Jobs);
}

// Every file under each folder, not following links, so a link to somewhere else is not counted as the version's
void HubInstalls::Count(SizeCount& count)
{
    for (std::size_t it_Index = 0; it_Index < count.Directories.size(); ++it_Index)
    {
        std::uint64_t l_Total = 0;
        std::error_code l_Error;
        std::filesystem::recursive_directory_iterator it_Entry(count.Directories[it_Index], std::filesystem::directory_options::skip_permission_denied, l_Error);
        for (; !l_Error && it_Entry != std::filesystem::recursive_directory_iterator(); it_Entry.increment(l_Error))
        {
            if (count.Stop.load(std::memory_order_relaxed))
            {
                break;
            }

            std::error_code l_EntryError;
            if (it_Entry->is_symlink(l_EntryError))
            {
                it_Entry.disable_recursion_pending();

                continue;
            }

            if (it_Entry->is_regular_file(l_EntryError))
            {
                const std::uintmax_t l_Size = it_Entry->file_size(l_EntryError);
                if (!l_EntryError)
                {
                    l_Total += l_Size;
                }
            }
        }

        if (count.Stop.load(std::memory_order_relaxed))
        {
            break;
        }

        count.Sizes[it_Index] = l_Total;
    }

    count.Done.store(true, std::memory_order_release);
}