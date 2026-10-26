#pragma once

#include "Trinity/Audio/AudioEngine.hpp"

#include <miniaudio.h>

namespace Trinity
{
    class AudioStream;

    // What the engine's own audio code reaches of an AudioEngine and no public header shows: the miniaudio engine sounds mix in, the group they play into, and the streams the engine refills as it mixes. Main thread
    struct AudioEngineInternal
    {
        [[nodiscard]] static ma_engine& GetEngine(AudioEngine& engine);
        [[nodiscard]] static ma_sound_group& GetMaster(AudioEngine& engine);
        // Refilled from then on as the engine updates, or offline before each page it renders, until it is removed, before it goes
        static void AddStream(AudioEngine& engine, AudioStream& stream);
        static void RemoveStream(AudioEngine& engine, AudioStream& stream);
    };
}