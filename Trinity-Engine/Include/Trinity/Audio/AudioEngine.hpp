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
        // The system's default output, followed when the default changes. Without one the engine runs as Silent, with one warning
        Device,
        // No device. Update mixes each frame's time and drops it, so sounds keep time as they would on a device. What --headless uses
        Silent,
        // No device and no clock. Only Render mixes, so the same calls give the same samples on every run
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

    // How long mixing takes, as the thread that mixes measures it over about the last half second of sound: the device's own, or the main thread's while there is none. The period is what each mix covers, the device's period or, without one, a silent mix's chunk
    struct AudioMixTiming
    {
        std::uint32_t PeriodFrames = 0;
        double PeriodMilliseconds = 0.0;
        double AverageMilliseconds = 0.0;
        double PeakMilliseconds = 0.0;
    };

    // miniaudio's engine behind Trinity's own interface, so no public header includes miniaudio and a console backend can replace what is behind it. Everything it allocates is under the Audio tag. The output device mixes on its own thread, which never allocates and never waits on the main thread
    class TRINITY_API AudioEngine
    {
    public:
        explicit AudioEngine(const AudioEngineSpecification& specification = {});
        ~AudioEngine();

        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;

        // Once a frame on the main thread. Logs a move to another device, starts the refills of the streams playing, and mixes the frame's time while no device does
        void Update(Timestep timestep);

        // Offline only: mixes the next frames into output, interleaved, whose size is a whole number of frames. Streams refill on the calling thread as it goes, so a render gives the same samples each time
        void Render(std::span<float> output);

        // The volume of everything, from 0 to 1, ramped so that a change never clicks. The audio.volume console variable sets the application's
        void SetVolume(float volume);
        [[nodiscard]] float GetVolume() const;
        // Silences everything at once, ramped as the volume is so it never clicks, while every sound plays on and keeps its time, as Forge's Mute Audio does
        void SetMuted(bool muted);
        [[nodiscard]] bool IsMuted() const;

        // The system's outputs as it lists them now, which takes a moment, so a menu lists them as it opens. Empty for an engine that plays to no device, as one made offline or silent does. Main thread
        [[nodiscard]] std::vector<AudioDevice> ListDevices();
        // Plays through the output of that name from the next period on, or through the system's default for an empty name, following it as it changes, the mix going on as it was, at the engine's own rate, which the device converts from. Main thread. False, logged, for an engine with no device to choose, or a name no output has, and the output plays on as it was, or for an output that would not open, and the default plays in its place
        bool SetDevice(std::string_view name);
        // The name SetDevice was given, empty while the system's default plays
        [[nodiscard]] std::string_view GetChosenDevice() const;
        [[nodiscard]] AudioMixTiming GetMixTiming() const;

        // A sine at an amplitude from 0 to 1, for checking the output. It ends on its own after its time, and replaces a tone already playing
        void PlayTestTone(float frequency, float amplitude, float seconds);
        void StopTestTone();
        [[nodiscard]] bool IsTestTonePlaying() const;

        // The project's mixer, Master alone until it is given one, which every scene's sound plays through, and the preview into its Master. The test tone plays beside it, straight to the output
        [[nodiscard]] AudioMixer& GetMixer();
        [[nodiscard]] const AudioMixer& GetMixer() const;

        // A clip on its own, outside any scene, as Forge's previews play it, once at its own pitch. A new one replaces it, and a clip whose samples are still loading starts once they are in. It holds what it plays, so the asset may go first
        void PlayPreview(const AudioClipAsset& clip);
        void StopPreview();
        // True from PlayPreview until the clip ends or is stopped, the wait for its samples included
        [[nodiscard]] bool IsPreviewPlaying() const;
        // Where the preview has got to in its clip, in the clip's frames
        [[nodiscard]] std::uint64_t GetPreviewCursor() const;

        // As FMOD's global parameters: a value for every sound event playing with a global parameter by that name, each event hearing it from the same period, and seeking towards it at its own speed. Until one is given, and once it is cleared, each event plays its own default. A name given for the first time takes room once, and nothing is allocated after that
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