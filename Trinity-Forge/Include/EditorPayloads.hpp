#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

constexpr const char* c_EntityPayload = "TRINITY_ENTITY";
constexpr const char* c_AssetPayload = "TRINITY_ASSET";
constexpr const char* c_FolderPayload = "TRINITY_FOLDER";
constexpr const char* c_ScriptPayload = "TRINITY_SCRIPT";
// An action dragged from the Input Actions panel: its asset's UUID in 8 bytes, then its path, Map/Action
constexpr const char* c_InputActionPayload = "TRINITY_INPUT_ACTION";

[[nodiscard]] inline std::string MakeInputActionPayload(std::uint64_t asset, std::string_view path)
{
    std::string l_Data(sizeof(asset), '\0');
    std::memcpy(l_Data.data(), &asset, sizeof(asset));
    l_Data += path;

    return l_Data;
}

// The asset and the path, valid while the payload is, or nothing for data too short to be one
[[nodiscard]] inline std::optional<std::pair<std::uint64_t, std::string_view>> ReadInputActionPayload(const void* data, int size)
{
    if (data == nullptr || size < static_cast<int>(sizeof(std::uint64_t)))
    {
        return std::nullopt;
    }

    std::uint64_t l_Asset = 0;
    std::memcpy(&l_Asset, data, sizeof(l_Asset));

    return std::pair(l_Asset, std::string_view(static_cast<const char*>(data) + sizeof(l_Asset), static_cast<std::size_t>(size) - sizeof(l_Asset)));
}