#pragma once

#include "Trinity/Core/UUID.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"

#include <cstdint>

namespace Trinity
{
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