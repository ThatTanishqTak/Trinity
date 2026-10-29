#include "Trinity/Project/SettingsYaml.hpp"

#include "Trinity/Core/Log.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <system_error>
#include <type_traits>

namespace Trinity::SettingsYaml
{
    namespace
    {
        // A whole number below the layer count, or nothing
        std::optional<std::uint32_t> ReadLayer(const YAML::Node& node)
        {
            std::uint32_t l_Layer = 0;
            const std::string l_Text = node && node.IsScalar() ? node.Scalar() : std::string();
            const std::from_chars_result l_Parsed = std::from_chars(l_Text.data(), l_Text.data() + l_Text.size(), l_Layer);
            if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_Text.data() + l_Text.size() || l_Layer >= c_MaxCollisionLayers)
            {
                return std::nullopt;
            }

            return l_Layer;
        }

        // Each number of the sequence, as many as there are, or nothing when it is not that long a sequence of numbers
        template<std::size_t Count>
        std::optional<std::array<float, Count>> ReadFloats(const YAML::Node& node)
        {
            if (!node || !node.IsSequence() || node.size() != Count)
            {
                return std::nullopt;
            }

            std::array<float, Count> l_Values{};
            for (std::size_t it_Index = 0; it_Index < Count; ++it_Index)
            {
                const std::string l_Text = node[it_Index].IsScalar() ? node[it_Index].Scalar() : std::string();
                const std::from_chars_result l_Parsed = std::from_chars(l_Text.data(), l_Text.data() + l_Text.size(), l_Values[it_Index]);
                if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_Text.data() + l_Text.size() || !std::isfinite(l_Values[it_Index]))
                {
                    return std::nullopt;
                }
            }

            return l_Values;
        }

        // A number the whole text is, and finite, or nothing
        template<typename T>
        std::optional<T> ReadNumber(const YAML::Node& node)
        {
            T l_Value{};
            const std::string l_Text = node && node.IsScalar() ? node.Scalar() : std::string();
            const std::from_chars_result l_Parsed = std::from_chars(l_Text.data(), l_Text.data() + l_Text.size(), l_Value);
            if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_Text.data() + l_Text.size())
            {
                return std::nullopt;
            }

            if constexpr (std::is_floating_point_v<T>)
            {
                if (!std::isfinite(l_Value))
                {
                    return std::nullopt;
                }
            }

            return l_Value;
        }

        // Each number as short as reads back the same, as the scene files write them
        std::string FormatNumber(float value)
        {
            return std::format("{}", value);
        }
    }

    std::string ReadString(const YAML::Node& map, const char* key, std::string_view fallback)
    {
        const YAML::Node l_Value = map[key];

        return l_Value && l_Value.IsScalar() ? l_Value.Scalar() : std::string(fallback);
    }

    CollisionLayers ReadCollisionLayers(const YAML::Node& physics, std::string_view owner)
    {
        CollisionLayers l_Layers;
        if (!physics || !physics.IsMap())
        {
            return l_Layers;
        }

        bool l_Unread = false;
        if (const YAML::Node l_Names = physics["LayerNames"]; l_Names && l_Names.IsMap())
        {
            l_Layers.Names.fill(std::string());
            for (const auto& it_Name : l_Names)
            {
                const std::optional<std::uint32_t> l_Layer = ReadLayer(it_Name.first);
                if (!l_Layer || !it_Name.second.IsScalar())
                {
                    l_Unread = true;

                    continue;
                }

                l_Layers.Names[*l_Layer] = it_Name.second.Scalar();
            }
        }

        if (const YAML::Node l_Pairs = physics["IgnoredPairs"]; l_Pairs && l_Pairs.IsSequence())
        {
            for (const YAML::Node& it_Pair : l_Pairs)
            {
                const std::optional<std::uint32_t> l_First = it_Pair.IsSequence() && it_Pair.size() == 2 ? ReadLayer(it_Pair[0]) : std::nullopt;
                const std::optional<std::uint32_t> l_Second = it_Pair.IsSequence() && it_Pair.size() == 2 ? ReadLayer(it_Pair[1]) : std::nullopt;
                if (!l_First || !l_Second)
                {
                    l_Unread = true;

                    continue;
                }

                l_Layers.SetCollides(*l_First, *l_Second, false);
            }
        }

        if (l_Unread)
        {
            TR_CORE_WARN("{} names a collision layer past the last, or one that is not a number, which is left out", owner);
        }

        return l_Layers;
    }

    void ReadWorldSettings(const YAML::Node& physics, std::string_view owner, PhysicsSettings& settings)
    {
        if (!physics || !physics.IsMap())
        {
            return;
        }

        bool l_Unread = false;
        if (const YAML::Node l_Node = physics["Gravity3D"])
        {
            const std::optional<std::array<float, 3>> l_Gravity = ReadFloats<3>(l_Node);
            l_Unread |= !l_Gravity;
            settings.Gravity3D = l_Gravity ? glm::vec3((*l_Gravity)[0], (*l_Gravity)[1], (*l_Gravity)[2]) : settings.Gravity3D;
        }

        if (const YAML::Node l_Node = physics["Gravity2D"])
        {
            const std::optional<std::array<float, 2>> l_Gravity = ReadFloats<2>(l_Node);
            l_Unread |= !l_Gravity;
            settings.Gravity2D = l_Gravity ? glm::vec2((*l_Gravity)[0], (*l_Gravity)[1]) : settings.Gravity2D;
        }

        if (const YAML::Node l_Node = physics["StepRate"])
        {
            std::uint32_t l_Rate = 0;
            const std::string l_Text = l_Node.IsScalar() ? l_Node.Scalar() : std::string();
            const std::from_chars_result l_Parsed = std::from_chars(l_Text.data(), l_Text.data() + l_Text.size(), l_Rate);
            const bool l_Read = l_Parsed.ec == std::errc() && l_Parsed.ptr == l_Text.data() + l_Text.size() && l_Rate > 0;
            l_Unread |= !l_Read;
            settings.StepRate = l_Read ? l_Rate : settings.StepRate;
        }

        if (l_Unread)
        {
            TR_CORE_WARN("{} has a gravity or step rate that cannot be read, which keeps its default", owner);
        }
    }

    void ReadAudioSettings(const YAML::Node& audio, std::string_view owner, AudioSettings& settings)
    {
        if (!audio || !audio.IsMap())
        {
            return;
        }

        bool l_Unread = false;
        const auto a_Read = [&]<typename T>(const char* key, T & value)
        {
            if (const YAML::Node l_Node = audio[key])
            {
                const std::optional<T> l_Value = ReadNumber<T>(l_Node);
                l_Unread |= !l_Value;
                value = l_Value ? *l_Value : value;
            }
        };

        const std::string l_Mixer = ReadString(audio, "Mixer", "");
        if (const std::optional<UUID> l_Asset = UUID::Parse(l_Mixer))
        {
            settings.Mixer = *l_Asset;
        }
        else
        {
            l_Unread |= !l_Mixer.empty();
        }

        a_Read("RealVoices", settings.RealVoices);
        a_Read("VirtualVoices", settings.VirtualVoices);
        a_Read("DopplerFactor", settings.DopplerFactor);
        a_Read("OcclusionRays", settings.OcclusionRays);
        a_Read("OcclusionTime", settings.OcclusionTime);
        if (const YAML::Node l_Layers = audio["OcclusionLayers"])
        {
            std::uint32_t l_Mask = 0;
            bool l_Read = l_Layers.IsSequence();
            for (std::size_t it_Layer = 0; l_Read && it_Layer < l_Layers.size(); ++it_Layer)
            {
                const std::optional<std::uint32_t> l_Layer = ReadLayer(l_Layers[it_Layer]);
                l_Read = l_Layer.has_value();
                l_Mask |= l_Read ? 1u << *l_Layer : 0u;
            }

            l_Unread |= !l_Read;
            settings.OcclusionLayers = l_Read ? l_Mask : settings.OcclusionLayers;
        }

        if (l_Unread)
        {
            TR_CORE_WARN("{} has an audio setting that cannot be read, which keeps its default", owner);
        }
    }

    void ReadWindowSettings(const YAML::Node& window, std::string_view owner, WindowSettings& settings)
    {
        if (!window || !window.IsMap())
        {
            return;
        }

        bool l_Unread = false;
        if (const YAML::Node l_Mode = window["Mode"])
        {
            const std::optional<FullScreenMode> l_Read = l_Mode.IsScalar() ? ParseFullScreenMode(l_Mode.Scalar()) : std::nullopt;
            l_Unread |= !l_Read;
            settings.Mode = l_Read.value_or(settings.Mode);
        }

        const auto a_ReadSize = [&](const char* key, std::uint32_t& value)
        {
            if (const YAML::Node l_Node = window[key])
            {
                const std::optional<std::uint32_t> l_Size = ReadNumber<std::uint32_t>(l_Node);
                const bool l_Read = l_Size && *l_Size > 0 && *l_Size <= WindowSettings::c_MaxSize;
                l_Unread |= !l_Read;
                value = l_Read ? *l_Size : value;
            }
        };

        a_ReadSize("Width", settings.Width);
        a_ReadSize("Height", settings.Height);
        if (const YAML::Node l_VSync = window["VSync"])
        {
            const bool l_Read = l_VSync.IsScalar() && (l_VSync.Scalar() == "true" || l_VSync.Scalar() == "false");
            l_Unread |= !l_Read;
            settings.VSync = l_Read ? l_VSync.Scalar() == "true" : settings.VSync;
        }

        if (l_Unread)
        {
            TR_CORE_WARN("{} has a window setting that cannot be read, which keeps its default", owner);
        }
    }

    void WritePhysicsSettings(YAML::Emitter& emitter, const PhysicsSettings& settings)
    {
        const CollisionLayers& l_Layers = settings.Layers;
        emitter << YAML::Key << "Physics" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "Gravity3D" << YAML::Value << YAML::Flow << YAML::BeginSeq << FormatNumber(settings.Gravity3D.x) << FormatNumber(settings.Gravity3D.y) << FormatNumber(settings.Gravity3D.z) << YAML::EndSeq;
        emitter << YAML::Key << "Gravity2D" << YAML::Value << YAML::Flow << YAML::BeginSeq << FormatNumber(settings.Gravity2D.x) << FormatNumber(settings.Gravity2D.y) << YAML::EndSeq;
        emitter << YAML::Key << "StepRate" << YAML::Value << settings.StepRate;
        emitter << YAML::Key << "LayerNames" << YAML::Value << YAML::BeginMap;
        for (std::uint32_t it_Layer = 0; it_Layer < c_MaxCollisionLayers; ++it_Layer)
        {
            if (!l_Layers.Names[it_Layer].empty())
            {
                emitter << YAML::Key << it_Layer << YAML::Value << YAML::DoubleQuoted << l_Layers.Names[it_Layer];
            }
        }

        emitter << YAML::EndMap;
        emitter << YAML::Key << "IgnoredPairs" << YAML::Value << YAML::BeginSeq;
        for (std::uint32_t it_First = 0; it_First < c_MaxCollisionLayers; ++it_First)
        {
            for (std::uint32_t it_Second = it_First; it_Second < c_MaxCollisionLayers; ++it_Second)
            {
                if (!l_Layers.Collides(it_First, it_Second))
                {
                    emitter << YAML::Flow << YAML::BeginSeq << it_First << it_Second << YAML::EndSeq;
                }
            }
        }

        emitter << YAML::EndSeq;
        emitter << YAML::EndMap;
    }

    void WriteAudioSettings(YAML::Emitter& emitter, const AudioSettings& settings)
    {
        emitter << YAML::Key << "Audio" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "Mixer" << YAML::Value << YAML::DoubleQuoted << (settings.Mixer ? settings.Mixer.ToString() : std::string());
        emitter << YAML::Key << "RealVoices" << YAML::Value << settings.RealVoices;
        emitter << YAML::Key << "VirtualVoices" << YAML::Value << settings.VirtualVoices;
        emitter << YAML::Key << "DopplerFactor" << YAML::Value << FormatNumber(settings.DopplerFactor);
        emitter << YAML::Key << "OcclusionLayers" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (std::uint32_t it_Layer = 0; it_Layer < c_MaxCollisionLayers; ++it_Layer)
        {
            if ((settings.OcclusionLayers & (1u << it_Layer)) != 0)
            {
                emitter << it_Layer;
            }
        }

        emitter << YAML::EndSeq;
        emitter << YAML::Key << "OcclusionRays" << YAML::Value << settings.OcclusionRays;
        emitter << YAML::Key << "OcclusionTime" << YAML::Value << FormatNumber(settings.OcclusionTime);
        emitter << YAML::EndMap;
    }

    void WriteWindowSettings(YAML::Emitter& emitter, const WindowSettings& settings)
    {
        emitter << YAML::Key << "Window" << YAML::Value << YAML::BeginMap;
        emitter << YAML::Key << "Mode" << YAML::Value << std::string(ToString(settings.Mode));
        emitter << YAML::Key << "Width" << YAML::Value << settings.Width;
        emitter << YAML::Key << "Height" << YAML::Value << settings.Height;
        emitter << YAML::Key << "VSync" << YAML::Value << settings.VSync;
        emitter << YAML::EndMap;
    }
}