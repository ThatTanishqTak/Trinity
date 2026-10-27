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

    // What the engine's own audio code reaches of an AudioEngine and no public header shows: the miniaudio engine sounds mix in, the group everything plays into last, the mixer group a node plays into, and the streams the engine refills as it mixes. Main thread
    struct AudioEngineInternal
    {
        [[nodiscard]] static ma_engine& GetEngine(AudioEngine& engine);
        [[nodiscard]] static ma_sound_group& GetMaster(AudioEngine& engine);
        // Plays the node into the mixer's group of that ID, or Master, and again into it once the group comes back after going, until it is let go, which it is before it goes
        static void Route(AudioEngine& engine, ma_node* node, UUID group);
        static void Unroute(AudioEngine& engine, ma_node* node);
        // Refilled from then on as the engine updates, or offline before each page it renders, until it is removed, before it goes
        static void AddStream(AudioEngine& engine, AudioStream& stream);
        static void RemoveStream(AudioEngine& engine, AudioStream& stream);
        // How far ahead of the engine's clock a change is to start so every node of the mix starts it together: a period while a device mixes, which may be partway through one, and none while the main thread does
        [[nodiscard]] static std::uint32_t GetLeadFrames(AudioEngine& engine);
        // The frame of the engine's clock a change made now starts on, that far ahead of where the engine is
        [[nodiscard]] static std::uint64_t GetChangeFrame(AudioEngine& engine);
        // The global parameter of that name, which sound events read on the audio thread, made the first time it is asked for and kept as long as the engine is
        [[nodiscard]] static SoundParameterTarget& FindGlobalParameter(AudioEngine& engine, std::string_view name);
    };
}