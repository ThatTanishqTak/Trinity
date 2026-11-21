#pragma once

#include "Trinity/Core/Export.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace Trinity
{
    // Over the scene's game view, or in the scene on a quad of its own, as Unity's world-space panels
    enum class UIRenderMode : std::uint8_t
    {
        Screen,
        World
    };

    // With the blended meshes, far to near, or with the sprites, by sorting layer, order and hierarchy
    enum class UIWorldSorting : std::uint8_t
    {
        Depth,
        Sprites
    };

    // The size given, or the size its body lays itself out at, as Unity 6.2's Size Mode
    enum class UIWorldSizeMode : std::uint8_t
    {
        Fixed,
        Dynamic
    };

    // Drawn unlit into a texture, shown on a quad of that size over Pixels Per Unit, set by its pivot
    struct UIWorldSettings
    {
        static constexpr std::uint32_t c_MaxSize = 4096;
        static constexpr float c_MinPixelsPerUnit = 0.001f;
        static constexpr float c_MaxPixelsPerUnit = 100000.0f;

        UIWorldSorting Sorting = UIWorldSorting::Depth;
        // Sprites': the sorting layer it is drawn in. Its UIDocument's sort order is its order in the layer
        std::int32_t SortingLayer = 0;
        UIWorldSizeMode SizeMode = UIWorldSizeMode::Fixed;
        // Fixed's, in pixels, each within 1 and c_MaxSize
        glm::uvec2 Size{ 400, 300 };
        float PixelsPerUnit = 100.0f;
        glm::vec2 Pivot{ 0.5f, 0.5f };
        // Faces the camera that draws it, as a health bar does. Otherwise it faces its entity's +Z
        bool FaceCamera = false;

        [[nodiscard]] bool operator==(const UIWorldSettings&) const = default;
    };

    // The sprites' unit quad made the document's. Facing the camera needs a projection without skew
    [[nodiscard]] TRINITY_API glm::mat4 GetWorldUIMatrix(const UIWorldSettings& settings, glm::uvec2 pixels, const glm::mat4& world, const glm::mat4& viewProjection);

    [[nodiscard]] TRINITY_API std::string_view ToString(UIRenderMode mode);
    [[nodiscard]] TRINITY_API std::optional<UIRenderMode> FindUIRenderMode(std::string_view name);
    [[nodiscard]] TRINITY_API std::string_view ToString(UIWorldSorting sorting);
    [[nodiscard]] TRINITY_API std::optional<UIWorldSorting> FindUIWorldSorting(std::string_view name);
    [[nodiscard]] TRINITY_API std::string_view ToString(UIWorldSizeMode mode);
    [[nodiscard]] TRINITY_API std::optional<UIWorldSizeMode> FindUIWorldSizeMode(std::string_view name);
}