#pragma once

#include "Trinity/Core/Export.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace Trinity
{
    enum class Tonemapper : std::uint8_t
    {
        None,
        PBRNeutral
    };

    // The EV100 under which a linear value reaches the display unscaled, since the exposure is 1 / (1.2 * 2^EV100)
    constexpr float c_NeutralEV100 = -0.263034406f;

    // How a camera's linear scene becomes the display's image: scaled by the exposure, mapped into [0, 1] by the curve, where None only clamps, then encoded as sRGB
    struct ToneMapping
    {
        float ExposureEV100 = c_NeutralEV100;
        Tonemapper Curve = Tonemapper::PBRNeutral;
    };

    [[nodiscard]] TRINITY_API float GetExposureScale(float ev100);

    [[nodiscard]] TRINITY_API float SrgbToLinear(float value);
    [[nodiscard]] TRINITY_API float LinearToSrgb(float value);

    // Khronos PBR Neutral, as glTF Sample Viewer applies it, to a linear colour
    [[nodiscard]] TRINITY_API glm::vec3 ApplyPBRNeutral(glm::vec3 color);

    // What the tonemap pass writes for a linear scene colour before it is stored in 8 bits, so tests can compare against it
    [[nodiscard]] TRINITY_API glm::vec3 ApplyToneMapping(glm::vec3 color, const ToneMapping& toneMapping);

    [[nodiscard]] TRINITY_API std::string_view ToString(Tonemapper tonemapper);
    [[nodiscard]] TRINITY_API std::optional<Tonemapper> ParseTonemapper(std::string_view text);
}