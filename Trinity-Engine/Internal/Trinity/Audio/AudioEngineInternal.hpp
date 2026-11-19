#pragma once

#include "Trinity/Audio/AudioEngine.hpp"
#include "Trinity/Core/UUID.hpp"

#include <miniaudio.h>

#include <cstdint>
#include <string_view>

namespace Trinity
{
    class AudioStream;
    class SoundParameterTarget;

    // What the engine's audio code reaches of an AudioEngine and no public header shows. Main thread
    struct AudioEngineInternal
    {
        [[nodiscard]] static ma_engine& GetEngine(AudioEngine& engine);
        [[nodiscard]] static ma_sound_group& GetMaster(AudioEngine& engine);
        // Plays the node into the mixer's group of that ID, or Master, until it is let go
        static void Route(AudioEngine& engine, ma_node* node, UUID group);
        static void Unroute(AudioEngine& engine, ma_node* node);
        // Refilled as the engine updates, or offline before each page it renders, until it is removed
        static void AddStream(AudioEngine& engine, AudioStream& stream);
        static void RemoveStream(AudioEngine& engine, AudioStream& stream);
        // A period ahead while a device mixes, which may be partway through one, else none
        [[nodiscard]] static std::uint32_t GetLeadFrames(AudioEngine& engine);
        // The frame of the engine's clock a change made now starts on
        [[nodiscard]] static std::uint64_t GetChangeFrame(AudioEngine& engine);
        // Read by sound events on the audio thread. Made on first ask, and kept as long as the engine
        [[nodiscard]] static SoundParameterTarget& FindGlobalParameter(AudioEngine& engine, std::string_view name);
    };
}