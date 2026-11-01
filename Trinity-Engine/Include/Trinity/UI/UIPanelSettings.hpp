#pragma once

#include "Trinity/Core/Export.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace Trinity
{
    // How a screen document's size follows its view's, as Unity's Panel Settings Scale Mode: a fixed scale, the view's size against a reference resolution, or the monitor's DPI against a reference DPI
    enum class UIScaleMode : std::uint8_t
    {
        ConstantPixelSize,
        ScaleWithScreenSize,
        ConstantPhysicalSize
    };

    // How Scale With Screen Size weighs the view's width and height against the reference's: by Match between the two, by the smaller, so the reference always fits, or by the larger, so it always fills
    enum class UIScreenMatchMode : std::uint8_t
    {
        MatchWidthOrHeight,
        Expand,
        Shrink
    };

    // A screen document's scaling, as Unity's Panel Settings has it. The scale is how many of the view's pixels one dp of the document is, so lengths and font sizes written in dp follow it, and text is drawn sharp at every size, while px stays the view's own pixels
    struct UIPanelSettings
    {
        static constexpr float c_MinScale = 0.01f;
        static constexpr float c_MaxScale = 100.0f;
        static constexpr std::uint32_t c_MaxReferenceSize = 16384;

        UIScaleMode ScaleMode = UIScaleMode::ScaleWithScreenSize;
        // Constant Pixel Size's
        float Scale = 1.0f;
        // Scale With Screen Size's, in pixels
        glm::uvec2 ReferenceResolution{ 1920, 1080 };
        UIScreenMatchMode ScreenMatchMode = UIScreenMatchMode::MatchWidthOrHeight;
        // From 0, the width alone, to 1, the height alone
        float Match = 0.0f;
        // Constant Physical Size's: the DPI the document is written for, on which one dp is one pixel
        float ReferenceDpi = 96.0f;

        // For a view of this size, on a monitor of this DPI scale against 96 DPI, between c_MinScale and c_MaxScale. A zero size or reference reads as 1
        [[nodiscard]] TRINITY_API float GetScale(std::uint32_t width, std::uint32_t height, float dpiScale) const;

        [[nodiscard]] bool operator==(const UIPanelSettings&) const = default;
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(UIScaleMode mode);
    [[nodiscard]] TRINITY_API std::optional<UIScaleMode> FindUIScaleMode(std::string_view name);
    [[nodiscard]] TRINITY_API std::string_view ToString(UIScreenMatchMode mode);
    [[nodiscard]] TRINITY_API std::optional<UIScreenMatchMode> FindUIScreenMatchMode(std::string_view name);
}