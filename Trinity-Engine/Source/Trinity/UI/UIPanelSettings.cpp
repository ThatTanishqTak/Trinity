#include "Trinity/UI/UIPanelSettings.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Trinity
{
    namespace
    {
        constexpr std::array<std::string_view, 3> c_ScaleModeNames{ "ConstantPixelSize", "ScaleWithScreenSize", "ConstantPhysicalSize" };
        constexpr std::array<std::string_view, 3> c_ScreenMatchModeNames{ "MatchWidthOrHeight", "Expand", "Shrink" };
        constexpr float c_BaseDpi = 96.0f;
    }

    // As UI Toolkit's Panel Settings work it out, so Match goes evenly from the width's ratio to the height's
    float UIPanelSettings::GetScale(std::uint32_t width, std::uint32_t height, float dpiScale) const
    {
        float l_Scale = 1.0f;
        switch (ScaleMode)
        {
            case UIScaleMode::ConstantPixelSize:
            {
                l_Scale = Scale;

                break;
            }
            case UIScaleMode::ScaleWithScreenSize:
            {
                if (width == 0 || height == 0 || ReferenceResolution.x == 0 || ReferenceResolution.y == 0)
                {
                    break;
                }

                const float l_Width = static_cast<float>(width) / static_cast<float>(ReferenceResolution.x);
                const float l_Height = static_cast<float>(height) / static_cast<float>(ReferenceResolution.y);
                switch (ScreenMatchMode)
                {
                    case UIScreenMatchMode::Expand:
                    {
                        l_Scale = std::min(l_Width, l_Height);

                        break;
                    }
                    case UIScreenMatchMode::Shrink:
                    {
                        l_Scale = std::max(l_Width, l_Height);

                        break;
                    }
                    case UIScreenMatchMode::MatchWidthOrHeight:
                    {
                        l_Scale = std::lerp(l_Width, l_Height, std::clamp(Match, 0.0f, 1.0f));

                        break;
                    }
                }

                break;
            }
            case UIScaleMode::ConstantPhysicalSize:
            {
                l_Scale = ReferenceDpi > 0.0f ? dpiScale * c_BaseDpi / ReferenceDpi : 1.0f;

                break;
            }
        }

        return std::isfinite(l_Scale) ? std::clamp(l_Scale, c_MinScale, c_MaxScale) : 1.0f;
    }

    std::string_view ToString(UIScaleMode mode)
    {
        const std::size_t l_Index = static_cast<std::size_t>(mode);

        return l_Index < c_ScaleModeNames.size() ? c_ScaleModeNames[l_Index] : "Invalid";
    }

    std::optional<UIScaleMode> FindUIScaleMode(std::string_view name)
    {
        const auto a_Found = std::ranges::find(c_ScaleModeNames, name);

        return a_Found != c_ScaleModeNames.end() ? std::optional(static_cast<UIScaleMode>(a_Found - c_ScaleModeNames.begin())) : std::nullopt;
    }

    std::string_view ToString(UIScreenMatchMode mode)
    {
        const std::size_t l_Index = static_cast<std::size_t>(mode);

        return l_Index < c_ScreenMatchModeNames.size() ? c_ScreenMatchModeNames[l_Index] : "Invalid";
    }

    std::optional<UIScreenMatchMode> FindUIScreenMatchMode(std::string_view name)
    {
        const auto a_Found = std::ranges::find(c_ScreenMatchModeNames, name);

        return a_Found != c_ScreenMatchModeNames.end() ? std::optional(static_cast<UIScreenMatchMode>(a_Found - c_ScreenMatchModeNames.begin())) : std::nullopt;
    }
}