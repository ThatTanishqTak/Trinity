#include "Trinity/Project/GameSettings.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Project/Project.hpp"
#include "Trinity/Project/SettingsYaml.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <system_error>

namespace Trinity
{
    namespace
    {
        constexpr std::string_view c_Owner = "Game: Game.trgame";
        constexpr std::array<GraphicsAPI, 3> c_GraphicsAPIs{ GraphicsAPI::D3D12, GraphicsAPI::Vulkan, GraphicsAPI::Metal };

        std::optional<GraphicsAPI> ParseGraphicsAPI(std::string_view name)
        {
            const auto a_Found = std::ranges::find_if(c_GraphicsAPIs, [name](GraphicsAPI api) { return ToString(api) == name; });

            return a_Found != c_GraphicsAPIs.end() ? std::optional<GraphicsAPI>(*a_Found) : std::nullopt;
        }
    }

    std::string WriteGameSettings(const GameSettings& settings)
    {
        YAML::Emitter l_Emitter;
        l_Emitter << YAML::BeginMap;
        l_Emitter << YAML::Key << "Format" << YAML::Value << GameSettings::c_FormatVersion;
        l_Emitter << YAML::Key << "Product" << YAML::Value << YAML::DoubleQuoted << settings.Product;
        l_Emitter << YAML::Key << "Company" << YAML::Value << YAML::DoubleQuoted << settings.Company;
        l_Emitter << YAML::Key << "Version" << YAML::Value << YAML::DoubleQuoted << settings.Version;
        l_Emitter << YAML::Key << "Scenes" << YAML::Value << YAML::BeginSeq;
        for (const UUID it_Scene : settings.Scenes)
        {
            l_Emitter << YAML::DoubleQuoted << it_Scene.ToString();
        }

        l_Emitter << YAML::EndSeq;
        l_Emitter << YAML::Key << "Module" << YAML::Value << YAML::DoubleQuoted << settings.Module;
        l_Emitter << YAML::Key << "GraphicsAPIs" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (const GraphicsAPI it_API : settings.GraphicsAPIs)
        {
            l_Emitter << std::string(ToString(it_API));
        }

        l_Emitter << YAML::EndSeq;
        SettingsYaml::WritePhysicsSettings(l_Emitter, settings.Physics);
        SettingsYaml::WriteAudioSettings(l_Emitter, settings.Audio);
        l_Emitter << YAML::Key << "Input" << YAML::Value << YAML::BeginMap;
        l_Emitter << YAML::Key << "Actions" << YAML::Value << YAML::DoubleQuoted << (settings.InputActions ? settings.InputActions.ToString() : std::string());
        l_Emitter << YAML::EndMap;
        l_Emitter << YAML::EndMap;

        return std::string(l_Emitter.c_str()) + "\n";
    }

    Expected<GameSettings, std::string> ParseGameSettings(std::string_view text)
    {
        YAML::Node l_Root;
        try
        {
            l_Root = YAML::Load(std::string(text));
        }
        catch (const YAML::Exception& exception)
        {
            return Unexpected{ std::format("{} is not valid YAML: {}", GameSettings::c_FileName, exception.what()) };
        }

        if (!l_Root.IsMap())
        {
            return Unexpected{ std::format("{} holds no settings", GameSettings::c_FileName) };
        }

        std::uint32_t l_Format = 0;
        const std::string l_FormatText = SettingsYaml::ReadString(l_Root, "Format", "");
        const std::from_chars_result l_Parsed = std::from_chars(l_FormatText.data(), l_FormatText.data() + l_FormatText.size(), l_Format);
        if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_FormatText.data() + l_FormatText.size() || l_Format == 0)
        {
            return Unexpected{ std::format("{} has no valid Format version", GameSettings::c_FileName) };
        }

        if (l_Format > GameSettings::c_FormatVersion)
        {
            return Unexpected{ std::format("{} was written in format {} by a newer Trinity, and this player reads format {} and older", GameSettings::c_FileName, l_Format, GameSettings::c_FormatVersion) };
        }

        GameSettings l_Settings;
        l_Settings.Product = SettingsYaml::ReadString(l_Root, "Product", "");
        l_Settings.Company = SettingsYaml::ReadString(l_Root, "Company", "");
        l_Settings.Version = SettingsYaml::ReadString(l_Root, "Version", "");
        if (l_Settings.Product.empty() || l_Settings.Company.empty())
        {
            return Unexpected{ std::format("{} names no product or no company, which a game's saves and log go under", GameSettings::c_FileName) };
        }

        bool l_Unread = false;
        if (const YAML::Node l_Scenes = l_Root["Scenes"]; l_Scenes && l_Scenes.IsSequence())
        {
            for (const YAML::Node& it_Scene : l_Scenes)
            {
                const std::optional<UUID> l_Scene = it_Scene.IsScalar() ? UUID::Parse(it_Scene.Scalar()) : std::nullopt;
                l_Unread |= !l_Scene;
                if (l_Scene && std::ranges::find(l_Settings.Scenes, *l_Scene) == l_Settings.Scenes.end())
                {
                    l_Settings.Scenes.push_back(*l_Scene);
                }
            }
        }

        if (l_Settings.Scenes.empty())
        {
            return Unexpected{ std::format("{} has no scene to start with", GameSettings::c_FileName) };
        }

        const std::string l_Module = SettingsYaml::ReadString(l_Root, "Module", "");
        if (Project::IsValidModuleName(l_Module))
        {
            l_Settings.Module = l_Module;
        }
        else
        {
            l_Unread |= !l_Module.empty();
        }

        if (const YAML::Node l_APIs = l_Root["GraphicsAPIs"]; l_APIs && l_APIs.IsSequence())
        {
            for (const YAML::Node& it_API : l_APIs)
            {
                const std::optional<GraphicsAPI> l_API = it_API.IsScalar() ? ParseGraphicsAPI(it_API.Scalar()) : std::nullopt;
                l_Unread |= !l_API;
                if (l_API && std::ranges::find(l_Settings.GraphicsAPIs, *l_API) == l_Settings.GraphicsAPIs.end())
                {
                    l_Settings.GraphicsAPIs.push_back(*l_API);
                }
            }
        }

        l_Settings.Physics.Layers = SettingsYaml::ReadCollisionLayers(l_Root["Physics"], c_Owner);
        SettingsYaml::ReadWorldSettings(l_Root["Physics"], c_Owner, l_Settings.Physics);
        SettingsYaml::ReadAudioSettings(l_Root["Audio"], c_Owner, l_Settings.Audio);

        const YAML::Node l_Input = l_Root["Input"];
        const std::string l_Actions = l_Input && l_Input.IsMap() ? SettingsYaml::ReadString(l_Input, "Actions", "") : std::string();
        if (const std::optional<UUID> l_Asset = UUID::Parse(l_Actions))
        {
            l_Settings.InputActions = *l_Asset;
        }
        else
        {
            l_Unread |= !l_Actions.empty();
        }

        if (l_Unread)
        {
            TR_CORE_WARN("{} names a scene, module, graphics API or input actions that cannot be one, which is left out", c_Owner);
        }

        return l_Settings;
    }

    std::string GetGameSettingsPath()
    {
        return std::format("{}/{}", Project::c_CacheMount, GameSettings::c_FileName);
    }
}