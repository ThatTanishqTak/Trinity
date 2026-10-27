#pragma once

#include "Trinity/Core/UUID.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Trinity
{
    // How a spatialized source's volume falls with its distance d from the listener, clamped between its minimum and maximum distances: Logarithmic as min / d, the inverse distance law Unity's Logarithmic follows, Linear from 1 at the minimum to 0 at the maximum, and Exponential as (min / d) squared, twice as steep in decibels as Logarithmic
    enum class AudioRolloff : std::uint8_t
    {
        Logarithmic,
        Linear,
        Exponential
    };

    // Unity's names, as scenes and sound events save them, in the enum's order
    inline constexpr std::array<std::string_view, 3> c_AudioRolloffKeys{ "Logarithmic", "Linear", "Exponential" };

    [[nodiscard]] constexpr std::string_view GetAudioRolloffKey(AudioRolloff rolloff)
    {
        return static_cast<std::size_t>(rolloff) < c_AudioRolloffKeys.size() ? c_AudioRolloffKeys[static_cast<std::size_t>(rolloff)] : c_AudioRolloffKeys.front();
    }

    [[nodiscard]] constexpr std::optional<AudioRolloff> FindAudioRolloff(std::string_view key)
    {
        for (std::size_t it_Rolloff = 0; it_Rolloff < c_AudioRolloffKeys.size(); ++it_Rolloff)
        {
            if (c_AudioRolloffKeys[it_Rolloff] == key)
            {
                return static_cast<AudioRolloff>(it_Rolloff);
            }
        }

        return std::nullopt;
    }

    // How a running scene's sounds are mixed, as Unity's Audio settings have it, which the project saves with its mixer
    struct AudioSettings
    {
        // The project's Audio Mixer asset, or none for Master alone
        UUID Mixer;
        // Voices mixed at once, the most important first, then the loudest
        std::uint32_t RealVoices = 32;
        // Voices playing at once, mixed or not. Beyond it the least important stop
        std::uint32_t VirtualVoices = 512;
        // Scales every source's Doppler Level
        float DopplerFactor = 1.0f;
        // The collision layers whose colliders muffle a source with Occlusion on
        std::uint32_t OcclusionLayers = c_AllCollisionLayers;
        // Rays cast a frame at most, from the listener to the occluded sources in turn, the longest unchecked first
        std::uint32_t OcclusionRays = 32;
        // Seconds a source takes to become muffled, and to clear again
        float OcclusionTime = 0.25f;

        [[nodiscard]] bool operator==(const AudioSettings&) const = default;
    };
}