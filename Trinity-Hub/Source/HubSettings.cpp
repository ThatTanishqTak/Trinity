#include "HubSettings.hpp"

#include "HubText.hpp"

#include <Trinity.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>

namespace
{
    std::string ReadScalar(const YAML::Node& map, const char* key)
    {
        const YAML::Node l_Value = map[key];

        return l_Value && l_Value.IsScalar() ? l_Value.Scalar() : std::string();
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
        TR_WARN("Hub: {} was written in format {} by a newer Hub, and this one reads format {} and older, so it starts with its own settings", c_Path, l_Format, c_FormatVersion);

        return l_Settings;
    }

    const std::string l_Theme = ReadScalar(l_Root, "Theme");
    if (std::ranges::find(c_Themes, std::string_view(l_Theme), &Theme::Key) != c_Themes.end())
    {
        l_Settings.ThemeKey = l_Theme;
    }
    else if (!l_Theme.empty())
    {
        TR_WARN("Hub: {} names a theme {}, which is none of dark, light and classic, so Hub is drawn {}", c_Path, l_Theme, l_Settings.ThemeKey);
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
            TR_WARN("Hub: {} names a scale {}, which is not a number from {} to {}, so Hub is drawn at {}", c_Path, l_ScaleText, Trinity::ImGuiLayer::c_MinimumUserScale, Trinity::ImGuiLayer::c_MaximumUserScale, l_Settings.Scale);
        }
    }

    l_Settings.InstallsDirectory = ReadScalar(l_Root, "InstallsDirectory");

    return l_Settings;
}

std::filesystem::path HubSettings::GetInstallsDirectory(const std::filesystem::path& defaultDirectory) const
{
    if (InstallsDirectory.empty())
    {
        return defaultDirectory;
    }

    std::filesystem::path l_Directory = HubText::FromUtf8(InstallsDirectory);
    l_Directory.make_preferred();

    return l_Directory;
}

void HubSettings::Save() const
{
    YAML::Emitter l_Emitter;
    l_Emitter << YAML::BeginMap;
    l_Emitter << YAML::Key << "Format" << YAML::Value << c_FormatVersion;
    l_Emitter << YAML::Key << "Theme" << YAML::Value << ThemeKey;
    l_Emitter << YAML::Key << "Scale" << YAML::Value << Scale;
    l_Emitter << YAML::Key << "InstallsDirectory" << YAML::Value << YAML::DoubleQuoted << InstallsDirectory;
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