#pragma once

#include "Trinity/Core/Export.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace Trinity
{
    // Over the scene's game view, as a screen overlay, or in the scene itself on a quad of its own, as Unity's world-space panels are
    enum class UIRenderMode : std::uint8_t
    {
        Screen,
        World
    };

    // Which of the scene's passes a world document's quad is drawn in: with the blended meshes, from the farthest to the nearest, behind the opaque meshes in front of it, or with the sprites, by sorting layer, then order, then the hierarchy's order
    enum class UIWorldSorting : std::uint8_t
    {
        Depth,
        Sprites
    };

    // Whether a world document's texture is the size given, or the size its body lays itself out at, as Unity 6.2's world-space UIDocument's Size Mode
    enum class UIWorldSizeMode : std::uint8_t
    {
        Fixed,
        Dynamic
    };

    // Where a world document is drawn and how large. It is drawn, unlit, into a texture of its own size in pixels, which a quad of that size over Pixels Per Unit shows, placed by its pivot on its entity: from (0, 0) at its bottom left to (1, 1) at its top right, as Unity's pivots are
    struct UIWorldSettings
    {
        static constexpr std::uint32_t c_MaxSize = 4096;
        static constexpr float c_MinPixelsPerUnit = 0.001f;
        static constexpr float c_MaxPixelsPerUnit = 100000.0f;

        UIWorldSorting Sorting = UIWorldSorting::Depth;
        // Sprites': the sorting layer it is drawn in, as a sprite's is. Its UIDocument's sort order is its order in that layer
        std::int32_t SortingLayer = 0;
        UIWorldSizeMode SizeMode = UIWorldSizeMode::Fixed;
        // Fixed's, in pixels, each within 1 and c_MaxSize
        glm::uvec2 Size{ 400, 300 };
        float PixelsPerUnit = 100.0f;
        glm::vec2 Pivot{ 0.5f, 0.5f };
        // Turned to face the camera that draws it, keeping its entity's position and scale, as a health bar does. Otherwise it faces its entity's +Z, as a sprite does
        bool FaceCamera = false;

        [[nodiscard]] bool operator==(const UIWorldSettings&) const = default;
    };

    // What the unit quad sprites are drawn from, centred on its origin with +Y up, is turned into for a world document whose texture is this size: the document's size in units, placed by its pivot on the entity, through the entity's world transform. Facing the camera, the entity's rotation gives way to the camera's, read from the rows of the view-projection it is drawn through, which any projection without skew allows
    [[nodiscard]] TRINITY_API glm::mat4 GetWorldUIMatrix(const UIWorldSettings& settings, glm::uvec2 pixels, const glm::mat4& world, const glm::mat4& viewProjection);

    [[nodiscard]] TRINITY_API std::string_view ToString(UIRenderMode mode);
    [[nodiscard]] TRINITY_API std::optional<UIRenderMode> FindUIRenderMode(std::string_view name);
    [[nodiscard]] TRINITY_API std::string_view ToString(UIWorldSorting sorting);
    [[nodiscard]] TRINITY_API std::optional<UIWorldSorting> FindUIWorldSorting(std::string_view name);
    [[nodiscard]] TRINITY_API std::string_view ToString(UIWorldSizeMode mode);
    [[nodiscard]] TRINITY_API std::optional<UIWorldSizeMode> FindUIWorldSizeMode(std::string_view name);
}