#include "HubSettings.hpp"

#include "HubText.hpp"

#include <Trinity.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

namespace
{
    constexpr const char* c_ListView = "List";
    constexpr const char* c_GridView = "Grid";

    std::string ReadScalar(const YAML::Node& map, const char* key)
    {
        const YAML::Node l_Value = map[key];

        return l_Value && l_Value.IsScalar() ? l_Value.Scalar() : std::string();
    }

    // A folder a setting names, in the platform's own separators, or the default where it names none
    std::filesystem::path GetDirectory(const std::string& setting, const std::filesystem::path& defaultDirectory)
    {
        if (setting.empty())
        {
            return defaultDirectory;
        }

        std::filesystem::path l_Directory = HubText::FromUtf8(setting);
        l_Directory.make_preferred();

        return l_Directory;
    }
}

HubSettings HubSettings::Load()
{
    HubSettings l_Settings;
    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(c_Path);
    if (!l_Text)
    {
        return l_Settings;
    }

    YAML::Node l_Root;
    try
    {
        l_Root = YAML::Load(*l_Text);
    }
    catch (const YAML::Exception& exception)
    {
        TR_WARN("Hub: {} is not valid YAML, so Hub starts with its own settings: {}", c_Path, exception.what());

        return l_Settings;
    }

    std::uint32_t l_Format = 0;
    const std::string l_FormatText = l_Root.IsMap() ? ReadScalar(l_Root, "Format") : std::string();
    const std::from_chars_result l_Parsed = std::from_chars(l_FormatText.data(), l_FormatText.data() + l_FormatText.size(), l_Format);
    if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_FormatText.data() + l_FormatText.size() || l_Format == 0)
    {
        TR_WARN("Hub: {} has no valid Format version, so Hub starts with its own settings", c_Path);

        return l_Settings;
    }

    if (l_Format > c_FormatVersion)
    {
        TR_WARN("Hub: {} is format {} from a newer Hub. It reads {} and older: defaults used", c_Path, l_Format, c_FormatVersion);

        return l_Settings;
    }

    const std::string l_Theme = ReadScalar(l_Root, "Theme");
    if (Trinity::Theme::IsKnown(l_Theme))
    {
        l_Settings.ThemeKey = l_Theme;
    }
    else if (!l_Theme.empty())
    {
        TR_WARN("Hub: {} names a theme {}, not dark or light, so Hub is drawn {}", c_Path, l_Theme, l_Settings.ThemeKey);
    }

    const std::string l_ScaleText = ReadScalar(l_Root, "Scale");
    if (!l_ScaleText.empty())
    {
        float l_Scale = 0.0f;
        const std::from_chars_result l_ScaleParsed = std::from_chars(l_ScaleText.data(), l_ScaleText.data() + l_ScaleText.size(), l_Scale);
        if (l_ScaleParsed.ec == std::errc() && l_ScaleParsed.ptr == l_ScaleText.data() + l_ScaleText.size() && l_Scale >= Trinity::ImGuiLayer::c_MinimumUserScale && l_Scale <= Trinity::ImGuiLayer::c_MaximumUserScale)
        {
            l_Settings.Scale = l_Scale;
        }
        else
        {
            TR_WARN("Hub: {} names a scale {}, not a number from {} to {}, so Hub is drawn at {}", c_Path, l_ScaleText, Trinity::ImGuiLayer::c_MinimumUserScale, Trinity::ImGuiLayer::c_MaximumUserScale, l_Settings.Scale);
        }
    }

    l_Settings.InstallsDirectory = ReadScalar(l_Root, "InstallsDirectory");
    l_Settings.ProjectsDirectory = ReadScalar(l_Root, "ProjectsDirectory");
    l_Settings.ProjectsGrid = ReadScalar(l_Root, "ProjectsView") == c_GridView;
    // A file from before the setting says nothing of it, and keeps Hub's own
    if (const std::string l_TitleBar = ReadScalar(l_Root, "CustomTitleBar"); !l_TitleBar.empty())
    {
        l_Settings.CustomTitleBar = l_TitleBar != "false";
    }

    if (const YAML::Node l_Favorites = l_Root["Favorites"]; l_Favorites && l_Favorites.IsSequence())
    {
        for (const YAML::Node& it_Favorite : l_Favorites)
        {
            if (it_Favorite.IsScalar() && !it_Favorite.Scalar().empty() && std::ranges::find(l_Settings.Favorites, it_Favorite.Scalar()) == l_Settings.Favorites.end())
            {
                l_Settings.Favorites.push_back(it_Favorite.Scalar());
            }
        }
    }

    return l_Settings;
}

std::filesystem::path HubSettings::GetInstallsDirectory(const std::filesystem::path& defaultDirectory) const
{
    return GetDirectory(InstallsDirectory, defaultDirectory);
}

std::filesystem::path HubSettings::GetProjectsDirectory(const std::filesystem::path& defaultDirectory) const
{
    return GetDirectory(ProjectsDirectory, defaultDirectory);
}

bool HubSettings::IsFavorite(const std::filesystem::path& file) const
{
    return std::ranges::find(Favorites, HubText::ToUtf8(file)) != Favorites.end();
}

void HubSettings::SetFavorite(const std::filesystem::path& file, bool favorite)
{
    std::string l_File = HubText::ToUtf8(file);
    const auto a_Favorite = std::ranges::find(Favorites, l_File);
    if (favorite && a_Favorite == Favorites.end())
    {
        Favorites.push_back(std::move(l_File));
    }
    else if (!favorite && a_Favorite != Favorites.end())
    {
        Favorites.erase(a_Favorite);
    }
}

void HubSettings::Save() const
{
    YAML::Emitter l_Emitter;
    l_Emitter << YAML::BeginMap;
    l_Emitter << YAML::Key << "Format" << YAML::Value << c_FormatVersion;
    l_Emitter << YAML::Key << "Theme" << YAML::Value << ThemeKey;
    l_Emitter << YAML::Key << "Scale" << YAML::Value << Scale;
    l_Emitter << YAML::Key << "InstallsDirectory" << YAML::Value << YAML::DoubleQuoted << InstallsDirectory;
    l_Emitter << YAML::Key << "ProjectsDirectory" << YAML::Value << YAML::DoubleQuoted << ProjectsDirectory;
    l_Emitter << YAML::Key << "ProjectsView" << YAML::Value << (ProjectsGrid ? c_GridView : c_ListView);
    l_Emitter << YAML::Key << "CustomTitleBar" << YAML::Value << CustomTitleBar;
    l_Emitter << YAML::Key << "Favorites" << YAML::Value << YAML::BeginSeq;
    for (const std::string& it_Favorite : Favorites)
    {
        l_Emitter << YAML::DoubleQuoted << it_Favorite;
    }

    l_Emitter << YAML::EndSeq;
    l_Emitter << YAML::EndMap;

    const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(c_Path, std::string(l_Emitter.c_str()) + "\n");
    if (!l_Written)
    {
        TR_WARN("Hub: {} could not be written, so this setting lasts until Hub closes: {}", c_Path, Trinity::ToString(l_Written.GetError()));
    }
}

void HubSettings::Apply() const
{
    static_cast<void>(Trinity::ConsoleVariables::Set("ui.theme", ThemeKey));
    static_cast<void>(Trinity::ConsoleVariables::Set("ui.scale", std::format("{}", Scale)));
}