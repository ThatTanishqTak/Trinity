#pragma once

#include "Trinity/Core/Export.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace Trinity
{
    // As Unity's Panel Settings Scale Mode: a fixed scale, a reference resolution, or a reference DPI
    enum class UIScaleMode : std::uint8_t
    {
        ConstantPixelSize,
        ScaleWithScreenSize,
        ConstantPhysicalSize
    };

    // By Match between width and height, by the smaller so it always fits, or the larger so it fills
    enum class UIScreenMatchMode : std::uint8_t
    {
        MatchWidthOrHeight,
        Expand,
        Shrink
    };

    // As Unity's Panel Settings. The scale is the view's pixels to one dp. px stays the view's pixels
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

        // Between c_MinScale and c_MaxScale. dpiScale is against 96 DPI. A zero size or reference gives 1
        [[nodiscard]] TRINITY_API float GetScale(std::uint32_t width, std::uint32_t height, float dpiScale) const;
        // A world document's: Scale alone, whatever the scale mode, between c_MinScale and c_MaxScale
        [[nodiscard]] TRINITY_API float GetWorldScale() const;

        [[nodiscard]] bool operator==(const UIPanelSettings&) const = default;
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(UIScaleMode mode);
    [[nodiscard]] TRINITY_API std::optional<UIScaleMode> FindUIScaleMode(std::string_view name);
    [[nodiscard]] TRINITY_API std::string_view ToString(UIScreenMatchMode mode);
    [[nodiscard]] TRINITY_API std::optional<UIScreenMatchMode> FindUIScreenMatchMode(std::string_view name);
}