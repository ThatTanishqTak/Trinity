#include "Trinity/Audio/AudioEngine.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <new>
#include <system_error>
#include <vector>

namespace Trinity
{
    namespace
    {
        // What an engine mixes at without a device, or when the device leaves the choice to it
        constexpr std::uint32_t c_DefaultSampleRate = 48000;
        constexpr std::uint32_t c_DefaultChannels = 2;
        // Volume changes ramp over this long, so none clicks
        constexpr std::uint32_t c_VolumeRampMilliseconds = 20;
        // A silent mix covers at most a quarter of a second a frame, as the simulation clock does, so a stall never makes a long mix, in pieces of this many frames
        constexpr std::int64_t c_MaxSilentNanoseconds = 250'000'000;
        constexpr std::uint32_t c_SilentChunkFrames = 1024;
        // WASAPI stops the device for a moment while it moves it to a new default, so only a device stopped this long counts as gone
        constexpr std::int64_t c_DeviceGoneNanoseconds = 1'000'000'000;
        constexpr std::uint32_t c_TestToneFadeMilliseconds = 10;

        // The engine that plays out, which the console reaches. Offline engines are left to whoever made them
        AudioEngine* s_ConsoleEngine = nullptr;

        float ToVolume(float value)
        {
            return value > 0.0f ? std::min(value, 1.0f) : 0.0f;
        }

        void OnVolumeChanged(float volume)
        {
            if (s_ConsoleEngine != nullptr)
            {
                s_ConsoleEngine->SetVolume(volume);
            }
        }

        ConsoleVariable<float> s_VolumeVariable("audio.volume", 1.0f, "The volume of all sound, from 0 to 1", ConsoleVariableFlags::None, &OnVolumeChanged);

#if !defined(TR_DISTRIBUTION)
        // A4 at -6 dBFS, for long enough to change the default output while it plays
        constexpr float c_TestToneFrequency = 440.0f;
        constexpr float c_TestToneAmplitude = 0.5011872336f;
        constexpr float c_TestToneSeconds = 10.0f;

        // Up to values.size() numbers above 0, separated by spaces, each replacing its default in turn. False for anything else
        bool ParsePositiveNumbers(std::string_view text, std::span<float> values)
        {
            for (std::size_t it_Index = 0;; ++it_Index)
            {
                const std::size_t l_Start = text.find_first_not_of(' ');
                if (l_Start == std::string_view::npos)
                {
                    return true;
                }

                if (it_Index == values.size())
                {
                    return false;
                }

                text.remove_prefix(l_Start);
                float l_Value = 0.0f;
                const auto [a_End, a_Error] = std::from_chars(text.data(), text.data() + text.size(), l_Value);
                if (a_Error != std::errc{} || !std::isfinite(l_Value) || l_Value <= 0.0f || (a_End != text.data() + text.size() && *a_End != ' '))
                {
                    return false;
                }

                values[it_Index] = l_Value;
                text.remove_prefix(static_cast<std::size_t>(a_End - text.data()));
            }
        }

        // A tone on the application's output to check it by ear, or an end to the one playing
        void TestToneCommand(std::string_view arguments)
        {
            if (s_ConsoleEngine == nullptr)
            {
                TR_CORE_WARN("audio.test_tone: this application has no audio");

                return;
            }

            if (s_ConsoleEngine->IsTestTonePlaying())
            {
                s_ConsoleEngine->StopTestTone();

                return;
            }

            std::array<float, 2> l_Values = { c_TestToneFrequency, c_TestToneSeconds };
            if (!ParsePositiveNumbers(arguments, l_Values))
            {
                TR_CORE_WARN("audio.test_tone takes a frequency in hertz and a time in seconds, both above 0, not '{}'", arguments);

                return;
            }

            s_ConsoleEngine->PlayTestTone(l_Values[0], c_TestToneAmplitude, l_Values[1]);
            TR_CORE_INFO("Audio: a {} Hz tone at -6 dBFS for {} s", l_Values[0], l_Values[1]);
        }

        ConsoleCommand s_TestToneCommand("audio.test_tone", "Plays a 440 Hz sine at -6 dBFS for 10 seconds, or stops the one playing. Takes a frequency in hertz and a time in seconds", &TestToneCommand);
#endif

        // Everything miniaudio allocates is under the Audio tag. It takes a null pointer as running out
        void* AllocateAudio(std::size_t size, void*)
        {
            return Memory::TryAllocate(size, MemoryTag::Audio);
        }

        void* ReallocateAudio(void* memory, std::size_t size, void*)
        {
            return Memory::Reallocate(memory, size, MemoryTag::Audio);
        }

        void FreeAudio(void* memory, void*)
        {
            Memory::Free(memory);
        }

        ma_allocation_callbacks GetAllocationCallbacks()
        {
            ma_allocation_callbacks l_Callbacks{};
            l_Callbacks.onMalloc = &AllocateAudio;
            l_Callbacks.onRealloc = &ReallocateAudio;
            l_Callbacks.onFree = &FreeAudio;

            return l_Callbacks;
        }

        std::uint64_t ToFrames(std::uint32_t milliseconds, std::uint32_t sampleRate)
        {
            return static_cast<std::uint64_t>(milliseconds) * sampleRate / 1000;
        }
    }

    struct AudioEngine::State
    {
        ~State();

        // miniaudio's callbacks: data on the device's thread, notifications on the system's. Neither allocates nor waits
        static void OnDeviceData(ma_device* device, void* output, const void* input, ma_uint32 frameCount);
        static void OnDeviceNotification(const ma_device_notification* notification);

        bool OpenDevice(const AudioEngineSpecification& specification);
        // True when the device now plays somewhere else than the name held
        bool ReadDeviceName();
        void MixSilently(Timestep timestep);

        ma_context Context{};
        ma_device Device{};
        ma_engine Engine{};
        ma_sound_group Master{};
        ma_waveform ToneWaveform{};
        ma_sound Tone{};

        AudioOutput Output = AudioOutput::Offline;
        bool HasContext = false;
        bool HasDevice = false;
        bool HasEngine = false;
        bool HasMaster = false;
        bool HasTone = false;
        float Volume = 1.0f;
        std::array<char, MA_MAX_DEVICE_NAME_LENGTH + 1> DeviceName{};

        // Set by the system's thread when the device has moved to a new default output
        std::atomic<bool> Rerouted{ false };
        // Held by whichever thread mixes: the device's, or the main thread while the device is stopped. Neither waits for it, so the device's thread never blocks on the main thread
        std::atomic_flag Mixing;

        // A silent mix's time owed, in nanoseconds times the sample rate so that no part of a frame is lost, and what it mixes into
        std::uint64_t OwedTicks = 0;
        std::vector<float, TaggedAllocator<float, MemoryTag::Audio>> Scratch;
        std::int64_t StoppedNanoseconds = 0;
        bool ReportedStopped = false;
    };

    // The device stops first, so nothing is mixing while the rest goes
    AudioEngine::State::~State()
    {
        if (HasDevice)
        {
            ma_device_uninit(&Device);
        }

        if (HasTone)
        {
            ma_sound_uninit(&Tone);
            ma_waveform_uninit(&ToneWaveform);
        }

        if (HasMaster)
        {
            ma_sound_group_uninit(&Master);
        }

        if (HasEngine)
        {
            ma_engine_uninit(&Engine);
        }

        if (HasContext)
        {
            ma_context_uninit(&Context);
        }
    }

    void AudioEngine::State::OnDeviceData(ma_device* device, void* output, const void*, ma_uint32 frameCount)
    {
        State& l_State = *static_cast<State*>(device->pUserData);

        // The main thread mixes only while the device is stopped, so it can hold this only as the device starts again, and that one period stays silent, as miniaudio clears it
        if (l_State.Mixing.test_and_set(std::memory_order_acquire))
        {
            return;
        }

        ma_engine_read_pcm_frames(&l_State.Engine, output, frameCount, nullptr);
        l_State.Mixing.clear(std::memory_order_release);
    }

    void AudioEngine::State::OnDeviceNotification(const ma_device_notification* notification)
    {
        if (notification->type == ma_device_notification_type_rerouted)
        {
            static_cast<State*>(notification->pDevice->pUserData)->Rerouted.store(true, std::memory_order_release);
        }
    }

    // The system's default output, in its own format unless the specification names one. False, with one warning, when there is none
    bool AudioEngine::State::OpenDevice(const AudioEngineSpecification& specification)
    {
        ma_context_config l_ContextConfig = ma_context_config_init();
        l_ContextConfig.allocationCallbacks = GetAllocationCallbacks();

        ma_result l_Result = ma_context_init(nullptr, 0, &l_ContextConfig, &Context);
        if (l_Result != MA_SUCCESS)
        {
            TR_CORE_WARN("Audio: no output device ({}), so sound is silent", ma_result_description(l_Result));

            return false;
        }

        // With no device ID it is the default output, which miniaudio follows when the default changes, such as when headphones are plugged in
        ma_device_config l_DeviceConfig = ma_device_config_init(ma_device_type_playback);
        l_DeviceConfig.playback.format = ma_format_f32;
        l_DeviceConfig.playback.channels = specification.Channels;
        l_DeviceConfig.sampleRate = specification.SampleRate;
        l_DeviceConfig.dataCallback = &OnDeviceData;
        l_DeviceConfig.notificationCallback = &OnDeviceNotification;
        l_DeviceConfig.pUserData = this;

        l_Result = ma_device_init(&Context, &l_DeviceConfig, &Device);
        if (l_Result != MA_SUCCESS)
        {
            TR_CORE_WARN("Audio: no output device ({}), so sound is silent", ma_result_description(l_Result));
            ma_context_uninit(&Context);

            return false;
        }

        HasContext = true;
        HasDevice = true;
        ReadDeviceName();

        return true;
    }

    // The system's thread wrote the name before it set Rerouted, which Update has read
    bool AudioEngine::State::ReadDeviceName()
    {
        const std::string_view l_Name(Device.playback.name);
        if (l_Name == std::string_view(DeviceName.data()))
        {
            return false;
        }

        const std::size_t l_Length = l_Name.copy(DeviceName.data(), DeviceName.size() - 1);
        DeviceName[l_Length] = '\0';

        return true;
    }

    // Mixes the frame's time and drops it, so sounds keep their time without a device to hear them on
    void AudioEngine::State::MixSilently(Timestep timestep)
    {
        const std::int64_t l_Nanoseconds = std::clamp<std::int64_t>(timestep.GetDuration().count(), 0, c_MaxSilentNanoseconds);
        OwedTicks += static_cast<std::uint64_t>(l_Nanoseconds) * ma_engine_get_sample_rate(&Engine);
        std::uint64_t l_Frames = OwedTicks / 1'000'000'000;
        OwedTicks %= 1'000'000'000;

        // The device's thread holds it only in the moment the device starts again, and then the time is the device's to mix
        if (l_Frames == 0 || Mixing.test_and_set(std::memory_order_acquire))
        {
            return;
        }

        while (l_Frames > 0)
        {
            const std::uint64_t l_Chunk = std::min<std::uint64_t>(l_Frames, c_SilentChunkFrames);
            ma_engine_read_pcm_frames(&Engine, Scratch.data(), l_Chunk, nullptr);
            l_Frames -= l_Chunk;
        }

        Mixing.clear(std::memory_order_release);
    }

    AudioEngine::AudioEngine(const AudioEngineSpecification& specification)
    {
        TR_PROFILE_FUNCTION();

        m_State = Memory::New<State>(MemoryTag::Audio);
        State& l_State = *m_State;
        l_State.Output = specification.Output;

        std::uint32_t l_SampleRate = specification.SampleRate != 0 ? specification.SampleRate : c_DefaultSampleRate;
        std::uint32_t l_Channels = specification.Channels != 0 ? specification.Channels : c_DefaultChannels;
        if (l_State.Output == AudioOutput::Device)
        {
            if (l_State.OpenDevice(specification))
            {
                l_SampleRate = l_State.Device.sampleRate;
                l_Channels = l_State.Device.playback.channels;
            }
            else
            {
                l_State.Output = AudioOutput::Silent;
            }
        }

        // The engine never opens a device of its own: the device's callback, Update or Render reads from it, so every output mixes through the same graph. No resource manager, and so no thread of miniaudio's, since clips load on the job system
        ma_engine_config l_EngineConfig = ma_engine_config_init();
        l_EngineConfig.noDevice = MA_TRUE;
        l_EngineConfig.channels = l_Channels;
        l_EngineConfig.sampleRate = l_SampleRate;
        l_EngineConfig.allocationCallbacks = GetAllocationCallbacks();

        ma_result l_Result = ma_engine_init(&l_EngineConfig, &l_State.Engine);
        if (l_Result == MA_SUCCESS)
        {
            l_State.HasEngine = true;
            l_Result = ma_sound_group_init(&l_State.Engine, MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, nullptr, &l_State.Master);
        }

        // Only running out of memory gets here, which Memory treats the same way
        if (l_Result != MA_SUCCESS)
        {
            TR_CORE_CRITICAL("Audio: the mixer could not start ({})", ma_result_description(l_Result));
            Memory::Delete(m_State);
            m_State = nullptr;

            throw std::bad_alloc();
        }

        l_State.HasMaster = true;

        if (l_State.Output != AudioOutput::Offline)
        {
            TR_CORE_ASSERT(s_ConsoleEngine == nullptr, "Only one audio engine plays out at a time.");
            s_ConsoleEngine = this;

            l_State.Volume = ToVolume(s_VolumeVariable.Get());
            ma_sound_group_set_fade_in_pcm_frames(&l_State.Master, l_State.Volume, l_State.Volume, 0);
            l_State.Scratch.resize(static_cast<std::size_t>(c_SilentChunkFrames) * l_Channels);
        }

        if (l_State.HasDevice)
        {
            l_Result = ma_device_start(&l_State.Device);
            if (l_Result != MA_SUCCESS)
            {
                TR_CORE_WARN("Audio: the output device would not start ({}), so sound is silent", ma_result_description(l_Result));
                ma_device_uninit(&l_State.Device);
                ma_context_uninit(&l_State.Context);
                l_State.HasDevice = false;
                l_State.HasContext = false;
                l_State.DeviceName = {};
                l_State.Output = AudioOutput::Silent;
            }
        }

        if (l_State.HasDevice)
        {
            TR_CORE_INFO("Audio: '{}' through {}, {} Hz, {} channel(s)", l_State.DeviceName.data(), ma_get_backend_name(l_State.Context.backend), l_SampleRate, l_Channels);
        }
        else if (l_State.Output == AudioOutput::Silent)
        {
            TR_CORE_INFO("Audio: silent, {} Hz, {} channel(s)", l_SampleRate, l_Channels);
        }
    }

    AudioEngine::~AudioEngine()
    {
        if (s_ConsoleEngine == this)
        {
            s_ConsoleEngine = nullptr;
        }

        Memory::Delete(m_State);
    }

    void AudioEngine::Update(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "The audio engine updates on the main thread.");

        State& l_State = *m_State;
        if (l_State.Output == AudioOutput::Offline)
        {
            return;
        }

        if (l_State.HasDevice)
        {
            // Windows moves the output once for each role the new default takes, so only a new name is logged
            if (l_State.Rerouted.exchange(false, std::memory_order_acquire) && l_State.ReadDeviceName())
            {
                TR_CORE_INFO("Audio: moved to '{}', {} Hz, {} channel(s)", l_State.DeviceName.data(), l_State.Device.playback.internalSampleRate, l_State.Device.playback.internalChannels);
            }

            if (ma_device_get_state(&l_State.Device) == ma_device_state_started)
            {
                l_State.OwedTicks = 0;
                l_State.StoppedNanoseconds = 0;
                l_State.ReportedStopped = false;

                return;
            }

            // Stopped while moving to a new default, or for good when the last output went, so sounds keep their time here meanwhile
            l_State.StoppedNanoseconds += timestep.GetDuration().count();
            if (!l_State.ReportedStopped && l_State.StoppedNanoseconds >= c_DeviceGoneNanoseconds)
            {
                TR_CORE_WARN("Audio: the output device went away, so sound is silent until an output returns");
                l_State.ReportedStopped = true;
            }
        }

        l_State.MixSilently(timestep);
    }

    void AudioEngine::Render(std::span<float> output)
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        TR_CORE_ASSERT(l_State.Output == AudioOutput::Offline, "Only an offline audio engine renders on request.");

        const std::uint32_t l_Channels = ma_engine_get_channels(&l_State.Engine);
        TR_CORE_ASSERT(output.size() % l_Channels == 0, "Render takes whole frames.");

        if (l_State.Output != AudioOutput::Offline || output.empty())
        {
            return;
        }

        ma_uint64 l_Read = 0;
        ma_engine_read_pcm_frames(&l_State.Engine, output.data(), output.size() / l_Channels, &l_Read);
        std::ranges::fill(output.subspan(static_cast<std::size_t>(l_Read) * l_Channels), 0.0f);
    }

    void AudioEngine::SetVolume(float volume)
    {
        State& l_State = *m_State;
        l_State.Volume = ToVolume(volume);
        ma_sound_group_set_fade_in_pcm_frames(&l_State.Master, -1.0f, l_State.Volume, ToFrames(c_VolumeRampMilliseconds, ma_engine_get_sample_rate(&l_State.Engine)));
    }

    float AudioEngine::GetVolume() const
    {
        return m_State->Volume;
    }

    void AudioEngine::PlayTestTone(float frequency, float amplitude, float seconds)
    {
        State& l_State = *m_State;
        if (l_State.HasTone)
        {
            ma_sound_uninit(&l_State.Tone);
            ma_waveform_uninit(&l_State.ToneWaveform);
            l_State.HasTone = false;
        }

        const std::uint32_t l_SampleRate = ma_engine_get_sample_rate(&l_State.Engine);
        const ma_waveform_config l_Config = ma_waveform_config_init(ma_format_f32, ma_engine_get_channels(&l_State.Engine), l_SampleRate, ma_waveform_type_sine, amplitude, frequency);
        if (ma_waveform_init(&l_Config, &l_State.ToneWaveform) != MA_SUCCESS)
        {
            return;
        }

        // Neither placed nor pitched, so the tone reaches the output exactly as it is generated
        if (ma_sound_init_from_data_source(&l_State.Engine, &l_State.ToneWaveform, MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, &l_State.Master, &l_State.Tone) != MA_SUCCESS)
        {
            TR_CORE_ERROR("Audio: the test tone could not start");
            ma_waveform_uninit(&l_State.ToneWaveform);

            return;
        }

        l_State.HasTone = true;

        // It fades out over its last moment, so its end never clicks
        const std::uint64_t l_Length = static_cast<std::uint64_t>(std::max(static_cast<double>(seconds), 0.0) * l_SampleRate);
        ma_sound_set_stop_time_with_fade_in_pcm_frames(&l_State.Tone, ma_engine_get_time_in_pcm_frames(&l_State.Engine) + l_Length, ToFrames(c_TestToneFadeMilliseconds, l_SampleRate));
        ma_sound_start(&l_State.Tone);
    }

    void AudioEngine::StopTestTone()
    {
        State& l_State = *m_State;
        if (l_State.HasTone)
        {
            ma_sound_stop_with_fade_in_pcm_frames(&l_State.Tone, ToFrames(c_TestToneFadeMilliseconds, ma_engine_get_sample_rate(&l_State.Engine)));
        }
    }

    bool AudioEngine::IsTestTonePlaying() const
    {
        return m_State->HasTone && ma_sound_is_playing(&m_State->Tone);
    }

    AudioOutput AudioEngine::GetOutput() const
    {
        return m_State->Output;
    }

    std::string_view AudioEngine::GetDeviceName() const
    {
        return m_State->DeviceName.data();
    }

    std::uint32_t AudioEngine::GetSampleRate() const
    {
        return ma_engine_get_sample_rate(&m_State->Engine);
    }

    std::uint32_t AudioEngine::GetChannels() const
    {
        return ma_engine_get_channels(&m_State->Engine);
    }

    std::uint64_t AudioEngine::GetMixedFrames() const
    {
        return ma_engine_get_time_in_pcm_frames(&m_State->Engine);
    }
}