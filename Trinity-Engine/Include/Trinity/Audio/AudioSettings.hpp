#pragma once

#include <cstdint>

namespace Trinity
{
    // How a running scene's sounds are mixed, as Unity's Audio settings have it, which the project will name with its mixer
    struct AudioSettings
    {
        // Voices mixed at once, the most important first, then the loudest
        std::uint32_t RealVoices = 32;
        // Voices playing at once, mixed or not. Beyond it the least important stop
        std::uint32_t VirtualVoices = 512;
        // Scales every source's Doppler Level
        float DopplerFactor = 1.0f;

        [[nodiscard]] bool operator==(const AudioSettings&) const = default;
    };
}