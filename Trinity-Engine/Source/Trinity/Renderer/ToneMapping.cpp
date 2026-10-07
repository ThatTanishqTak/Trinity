#include "Trinity/Renderer/ToneMapping.hpp"

#include <algorithm>
#include <cmath>

namespace Trinity
{
    float GetExposureScale(float ev100)
    {
        return 1.0f / (1.2f * std::exp2(ev100));
    }

    float SrgbToLinear(float value)
    {
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    float LinearToSrgb(float value)
    {
        return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    }

    // Base colours lit by white light of intensity 1 come out as they are, which is why up to 0.04 is taken off first, and highlights from 0.76 roll off towards white with a little desaturation
    glm::vec3 ApplyPBRNeutral(glm::vec3 color)
    {
        constexpr float c_StartCompression = 0.8f - 0.04f;
        constexpr float c_Desaturation = 0.15f;

        const float l_Min = std::min({ color.r, color.g, color.b });
        const float l_Offset = l_Min < 0.08f ? l_Min - 6.25f * l_Min * l_Min : 0.04f;
        color -= l_Offset;

        const float l_Peak = std::max({ color.r, color.g, color.b });
        if (l_Peak < c_StartCompression)
        {
            return color;
        }

        constexpr float c_Range = 1.0f - c_StartCompression;
        const float l_NewPeak = 1.0f - c_Range * c_Range / (l_Peak + c_Range - c_StartCompression);
        color *= l_NewPeak / l_Peak;

        const float l_Blend = 1.0f - 1.0f / (c_Desaturation * (l_Peak - l_NewPeak) + 1.0f);

        return glm::mix(color, glm::vec3(l_NewPeak), l_Blend);
    }

    glm::vec3 ApplyToneMapping(glm::vec3 color, const ToneMapping& toneMapping)
    {
        color = glm::max(color, glm::vec3(0.0f)) * GetExposureScale(toneMapping.ExposureEV100);
        if (toneMapping.Curve == Tonemapper::PBRNeutral)
        {
            color = ApplyPBRNeutral(color);
        }

        color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));

        return { LinearToSrgb(color.r), LinearToSrgb(color.g), LinearToSrgb(color.b) };
    }

    std::string_view ToString(Tonemapper tonemapper)
    {
        switch (tonemapper)
        {
            case Tonemapper::None:
            {
                return "None";
            }
            case Tonemapper::PBRNeutral:
            {
                return "PBRNeutral";
            }
        }

        return "Unknown";
    }

    std::optional<Tonemapper> ParseTonemapper(std::string_view text)
    {
        for (const Tonemapper it_Tonemapper : { Tonemapper::None, Tonemapper::PBRNeutral })
        {
            if (text == ToString(it_Tonemapper))
            {
                return it_Tonemapper;
            }
        }

        return std::nullopt;
    }
}