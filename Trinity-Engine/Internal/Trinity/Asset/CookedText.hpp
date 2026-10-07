#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/UUID.hpp"

#include <yaml-cpp/yaml.h>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

// Reading the YAML that importers cook. A key that is absent leaves its value as it is, and one that holds anything other than what the writer writes leaves it too and is named in malformed, so the caller can refuse the file
namespace Trinity::CookedText
{
    template<typename T>
    [[nodiscard]] std::optional<T> ParseNumber(const YAML::Node& node)
    {
        if (!node || !node.IsScalar())
        {
            return std::nullopt;
        }

        const std::string& l_Text = node.Scalar();
        T l_Value{};
        const std::from_chars_result l_Parsed = std::from_chars(l_Text.data(), l_Text.data() + l_Text.size(), l_Value);
        if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_Text.data() + l_Text.size())
        {
            return std::nullopt;
        }

        return l_Value;
    }

    template<typename T>
    void ReadNumber(const YAML::Node& map, std::string_view key, T& value, std::string& malformed)
    {
        const YAML::Node l_Node = map[std::string(key)];
        if (!l_Node)
        {
            return;
        }

        const std::optional<T> l_Value = ParseNumber<T>(l_Node);
        if (!l_Value)
        {
            malformed = key;

            return;
        }

        value = *l_Value;
    }

    inline void ReadFloats(const YAML::Node& map, std::string_view key, std::span<float> values, std::string& malformed)
    {
        const YAML::Node l_Node = map[std::string(key)];
        if (!l_Node)
        {
            return;
        }

        if (!l_Node.IsSequence() || l_Node.size() != values.size())
        {
            malformed = key;

            return;
        }

        for (std::size_t it_Index = 0; it_Index < values.size(); ++it_Index)
        {
            const std::optional<float> l_Value = ParseNumber<float>(l_Node[it_Index]);
            if (!l_Value)
            {
                malformed = key;

                return;
            }

            values[it_Index] = *l_Value;
        }
    }

    inline void ReadBool(const YAML::Node& map, std::string_view key, bool& value, std::string& malformed)
    {
        const YAML::Node l_Node = map[std::string(key)];
        if (!l_Node)
        {
            return;
        }

        if (!l_Node.IsScalar() || (l_Node.Scalar() != "true" && l_Node.Scalar() != "false"))
        {
            malformed = key;

            return;
        }

        value = l_Node.Scalar() == "true";
    }

    inline void ReadString(const YAML::Node& map, std::string_view key, std::string& value, std::string& malformed)
    {
        const YAML::Node l_Node = map[std::string(key)];
        if (!l_Node)
        {
            return;
        }

        if (!l_Node.IsScalar())
        {
            malformed = key;

            return;
        }

        value = l_Node.Scalar();
    }

    [[nodiscard]] inline std::optional<UUID> ParseUUID(const YAML::Node& node)
    {
        return node && node.IsScalar() ? UUID::Parse(node.Scalar()) : std::nullopt;
    }

    inline void ReadUUID(const YAML::Node& map, std::string_view key, UUID& value, std::string& malformed)
    {
        const YAML::Node l_Node = map[std::string(key)];
        if (!l_Node)
        {
            return;
        }

        const std::optional<UUID> l_Value = ParseUUID(l_Node);
        if (!l_Value)
        {
            malformed = key;

            return;
        }

        value = *l_Value;
    }

    // The root of a cooked file, refused without a Format, and with one newer than the reader's
    [[nodiscard]] inline Expected<YAML::Node, std::string> LoadRoot(std::string_view text, std::string_view kind, std::uint32_t formatVersion)
    {
        YAML::Node l_Root;
        try
        {
            l_Root = YAML::Load(std::string(text));
        }
        catch (const YAML::Exception& l_Exception)
        {
            return Unexpected{ std::format("not YAML: {}", l_Exception.what()) };
        }

        const YAML::Node& l_Map = l_Root;
        const std::optional<std::uint32_t> l_Format = l_Map.IsMap() ? ParseNumber<std::uint32_t>(l_Map["Format"]) : std::nullopt;
        if (!l_Format || *l_Format == 0)
        {
            return Unexpected{ std::format("not {}, since it has no Format", kind) };
        }

        if (*l_Format > formatVersion)
        {
            return Unexpected{ std::format("format {} was written by a newer Trinity, which reads up to {}", *l_Format, formatVersion) };
        }

        return l_Root;
    }
}