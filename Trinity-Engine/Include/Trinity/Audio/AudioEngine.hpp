#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    class AudioClipAsset;
    class AudioMixer;

    // Where an engine's mix goes
    enum class AudioOutput : std::uint8_t
    {
        // The system's default, followed as it changes. Without one it runs as Silent, with a warning
        Device,
        // No device. Update mixes each frame's time and drops it, so sounds keep time. --headless uses it
        Silent,
        // No device and no clock. Only Render mixes, so the same calls give the same samples every run
        Offline
    };

    struct AudioEngineSpecification
    {
        AudioOutput Output = AudioOutput::Device;
        // 0 takes the device's own, or 48000 Hz and 2 channels without one
        std::uint32_t SampleRate = 0;
        std::uint32_t Channels = 0;
    };

    // An output the system offers, by the name it shows for it
    struct AudioDevice
    {
        std::string Name;
        // The one the system plays through unless told otherwise
        bool Default = false;
    };

    // Measured by the mixing thread over about the last half second of sound
    struct AudioMixTiming
    {
        std::uint32_t PeriodFrames = 0;
        double PeriodMilliseconds = 0.0;
        double AverageMilliseconds = 0.0;
        double PeakMilliseconds = 0.0;
    };

    // miniaudio behind Trinity's interface. Its device thread never allocates, nor waits on the main
    class TRINITY_API AudioEngine
    {
    public:
        explicit AudioEngine(const AudioEngineSpecification& specification = {});
        ~AudioEngine();

        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;

        // Once a frame on the main thread. Mixes the frame's time while no device does
        void Update(Timestep timestep);

        // Offline only. The output's size must be a whole number of interleaved frames
        void Render(std::span<float> output);

        // From 0 to 1, ramped so a change never clicks. The audio.volume console variable sets it
        void SetVolume(float volume);
        [[nodiscard]] float GetVolume() const;
        // Ramped as the volume is. Every sound plays on and keeps its time, as Forge's Mute Audio
        void SetMuted(bool muted);
        [[nodiscard]] bool IsMuted() const;

        // Takes a moment, so list them as a menu opens. Empty with no device. Main thread
        [[nodiscard]] std::vector<AudioDevice> ListDevices();
        // An empty name follows the system's default. False, logged, for an unknown name. Main thread
        bool SetDevice(std::string_view name);
        // The name SetDevice was given, empty while the system's default plays
        [[nodiscard]] std::string_view GetChosenDevice() const;
        [[nodiscard]] AudioMixTiming GetMixTiming() const;

        // Amplitude from 0 to 1. It ends on its own, and replaces a tone already playing
        void PlayTestTone(float frequency, float amplitude, float seconds);
        void StopTestTone();
        [[nodiscard]] bool IsTestTonePlaying() const;

        // Master alone until one is given. The test tone plays beside it, straight to the output
        [[nodiscard]] AudioMixer& GetMixer();
        [[nodiscard]] const AudioMixer& GetMixer() const;

        // Outside any scene, as Forge's previews. It holds what it plays, so the asset may go first
        void PlayPreview(const AudioClipAsset& clip);
        void StopPreview();
        // True from PlayPreview until the clip ends or is stopped, the wait for its samples included
        [[nodiscard]] bool IsPreviewPlaying() const;
        // Where the preview has got to in its clip, in the clip's frames
        [[nodiscard]] std::uint64_t GetPreviewCursor() const;

        // As FMOD's global parameters. Until one is given, each event plays its own default
        void SetGlobalParameter(std::string_view name, float value);
        // The value given, none before one is or once it is cleared
        [[nodiscard]] std::optional<float> GetGlobalParameter(std::string_view name) const;
        // Every one back to its events' defaults, as Forge has them when Play starts and stops
        void ClearGlobalParameters();

        // Silent when a Device engine found no device, or none would open as SetDevice chose one
        [[nodiscard]] AudioOutput GetOutput() const;
        // Empty without a device
        [[nodiscard]] std::string_view GetDeviceName() const;
        [[nodiscard]] std::uint32_t GetSampleRate() const;
        [[nodiscard]] std::uint32_t GetChannels() const;
        // Frames mixed since the engine started, whether heard or not
        [[nodiscard]] std::uint64_t GetMixedFrames() const;

    private:
        friend struct AudioEngineInternal;

        struct State;

        State* m_State = nullptr;
    };
}