#include "Trinity/Audio/AudioMixer.hpp"

#include "Trinity/Audio/AudioEngine.hpp"
#include "Trinity/Audio/AudioEngineInternal.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Memory.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <new>
#include <numbers>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        // A meter's peak falls this fast once passed, and its RMS is over about this long
        constexpr float c_PeakFallDecibelsPerSecond = 20.0f;
        constexpr float c_RmsSeconds = 0.3f;
        // An effect's values glide a step this many frames long at a time, its filter's coefficients worked out again for each, and what an Echo takes in is faded through a scratch of this many samples, which fewer frames fill when there are more than eight channels
        constexpr std::uint32_t c_GlideFrames = 32;
        constexpr std::uint32_t c_ScratchSamples = c_GlideFrames * 8;
        // Under Nyquist by enough that a filter's formula holds at any rate the engine runs at
        constexpr double c_HighestFrequencyRatio = 0.45;
        // A Duck Volume's detector follows a level up at once and down this fast, so it holds over the dips of a low tone
        constexpr float c_DetectorReleaseMilliseconds = 5.0f;

        // A gain moved from where it is to the main thread's latest over the mixer's ramp, each sample an even step of what is left, and, for a group's output, the meter of what leaves it
        struct GainNode
        {
            ma_node_base Base{};
            // Set by the main thread and read by the audio thread each block
            std::atomic<float> TargetGain{ 1.0f };
            // The audio thread's alone once the node is in the graph
            float Gain = 1.0f;
            float RampGain = 1.0f;
            std::uint32_t RampLeft = 0;
            std::uint32_t RampFrames = 1;
            std::uint32_t Channels = 0;
            bool Metered = false;
            float PeakFall = 1.0f;
            float RmsCoefficient = 1.0f;
            std::array<float, AudioMeter::c_MaxChannels> Peak{};
            std::array<float, AudioMeter::c_MaxChannels> MeanSquare{};
            // Written by the audio thread after each block and read by any
            std::array<std::atomic<float>, AudioMeter::c_MaxChannels> PublishedPeak{};
            std::array<std::atomic<float>, AudioMeter::c_MaxChannels> PublishedMeanSquare{};
        };

        static_assert(std::is_standard_layout_v<GainNode>, "miniaudio reaches the node through its base, its first member.");

        // On the audio thread. A new gain starts a ramp from where the gain is, which ends exactly on it
        void ProcessGain(ma_node* node, const float** framesIn, [[maybe_unused]] ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
        {
            GainNode& l_Node = *reinterpret_cast<GainNode*>(node);
            const float l_Target = l_Node.TargetGain.load(std::memory_order_relaxed);
            if (l_Target != l_Node.RampGain)
            {
                l_Node.RampGain = l_Target;
                l_Node.RampLeft = l_Node.RampFrames;
            }

            const ma_uint32 l_Frames = *frameCountOut;
            const std::uint32_t l_Channels = l_Node.Channels;
            const std::uint32_t l_Metered = l_Node.Metered ? std::min<std::uint32_t>(l_Channels, AudioMeter::c_MaxChannels) : 0;
            const float* l_In = framesIn[0];
            float* l_Out = framesOut[0];
            for (ma_uint32 it_Frame = 0; it_Frame < l_Frames; ++it_Frame)
            {
                if (l_Node.RampLeft > 0)
                {
                    l_Node.Gain += (l_Node.RampGain - l_Node.Gain) / static_cast<float>(l_Node.RampLeft);
                    if (--l_Node.RampLeft == 0)
                    {
                        l_Node.Gain = l_Node.RampGain;
                    }
                }

                const std::size_t l_Base = static_cast<std::size_t>(it_Frame) * l_Channels;
                for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                {
                    l_Out[l_Base + it_Channel] = l_In[l_Base + it_Channel] * l_Node.Gain;
                }

                for (std::uint32_t it_Channel = 0; it_Channel < l_Metered; ++it_Channel)
                {
                    const float l_Sample = l_Out[l_Base + it_Channel];
                    l_Node.Peak[it_Channel] = std::max(std::abs(l_Sample), l_Node.Peak[it_Channel] * l_Node.PeakFall);
                    l_Node.MeanSquare[it_Channel] += l_Node.RmsCoefficient * (l_Sample * l_Sample - l_Node.MeanSquare[it_Channel]);
                }
            }

            for (std::uint32_t it_Channel = 0; it_Channel < l_Metered; ++it_Channel)
            {
                l_Node.PublishedPeak[it_Channel].store(l_Node.Peak[it_Channel], std::memory_order_relaxed);
                l_Node.PublishedMeanSquare[it_Channel].store(l_Node.MeanSquare[it_Channel], std::memory_order_relaxed);
            }
        }

        // Each runs whether or not anything plays into it, given silence: a group's output so its meter falls to nothing, a direct input so its group's resampler, when it has one, is always fed and never holds back a sample from before a silence, and an effect so its echoes and its filter's ringing carry on into a silence and its fades end
        constexpr ma_node_vtable c_GainVTable{ &ProcessGain, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING };

        // miniaudio's filters, of which an effect has the one its type names, and its delay for Echo
        union EffectFilters
        {
            ma_lpf2 Lowpass;
            ma_hpf2 Highpass;
            ma_bpf2 Bandpass;
            ma_notch2 Notch;
            ma_peak2 Peak;
            ma_loshelf2 LowShelf;
            ma_hishelf2 HighShelf;
            ma_delay Delay;
        };

        // The SFX Reverb's state, every buffer of it in one allocation made on the main thread: the delay its early reflections and its late reverberation are read from, the allpasses smearing what goes into the late reverberation, and the eight lines of its feedback delay network, whose lengths its density sets. Then what its values work out to, which the audio thread works out again as they glide
        struct ReverbState
        {
            static constexpr std::size_t c_Lines = 8;
            static constexpr std::size_t c_Diffusers = 4;
            static constexpr std::size_t c_Taps = 8;

            float* Block = nullptr;
            float* PreDelay = nullptr;
            std::uint32_t PreDelayLength = 0;
            std::uint32_t PreDelayCursor = 0;
            std::array<std::uint32_t, c_Taps> TapOffsets{};
            std::array<float, c_Taps> TapGains{};
            std::array<float*, c_Diffusers> Diffusers{};
            std::array<std::uint32_t, c_Diffusers> DiffuserLengths{};
            std::array<std::uint32_t, c_Diffusers> DiffuserCursors{};
            std::array<float*, c_Lines> Lines{};
            std::array<std::uint32_t, c_Lines> LineLengths{};
            std::array<std::uint32_t, c_Lines> LineCursors{};
            std::array<float, c_Lines> Damped{};
            float High = 0.0f;
            float Low = 0.0f;
            float DryGain = 1.0f;
            float RoomGain = 1.0f;
            float ReflectionsGain = 0.0f;
            float ReverbGain = 1.0f;
            float HighCoefficient = 0.0f;
            float LowCoefficient = 0.0f;
            float LowGain = 1.0f;
            float Diffusion = 0.0f;
            std::uint32_t EarlyDelay = 0;
            std::uint32_t LateDelay = 0;
            std::array<float, c_Lines> LineGains{};
            std::array<float, c_Lines> LineDamping{};
        };

        // The lines' lengths at full density, apart enough that no two share a mode, the diffusers', and the early reflections' taps after the Reflect Delay
        constexpr std::array<double, ReverbState::c_Lines> c_ReverbLineMilliseconds{ 29.7, 37.1, 41.1, 43.7, 47.9, 53.3, 57.1, 61.3 };
        constexpr std::array<double, ReverbState::c_Diffusers> c_ReverbDiffuserMilliseconds{ 4.77, 3.59, 12.73, 9.30 };
        constexpr std::array<double, ReverbState::c_Taps> c_ReverbTapMilliseconds{ 0.0, 3.1, 5.3, 7.9, 10.7, 13.3, 16.1, 19.7 };
        // The longest Reflect Delay with the longest Reverb Delay after it, which the delay holds
        constexpr double c_ReverbLongestDelayMilliseconds = 400.0;

        // A row of the 8-point Hadamard matrix, whose rows are orthogonal, so each channel takes the lines and the taps in a pattern of its own and hears them uncorrelated
        [[nodiscard]] float HadamardSign(std::size_t row, std::size_t column)
        {
            return std::popcount(row & column) % 2 == 0 ? 1.0f : -1.0f;
        }

        // Its lines a density's share of their full lengths, from 40% at none, each kept longer than the one before, and its taps each 0.8 of the one before, together at unit energy
        [[nodiscard]] ReverbState* CreateReverb(std::uint32_t sampleRate, float density)
        {
            ReverbState* l_Reverb = Memory::New<ReverbState>(MemoryTag::Audio);
            const double l_Rate = static_cast<double>(sampleRate);
            const auto a_Frames = [l_Rate](double milliseconds) { return static_cast<std::uint32_t>(std::llround(milliseconds * l_Rate / 1000.0)); };
            float l_Energy = 0.0f;
            for (std::size_t it_Tap = 0; it_Tap < ReverbState::c_Taps; ++it_Tap)
            {
                l_Reverb->TapOffsets[it_Tap] = a_Frames(c_ReverbTapMilliseconds[it_Tap]);
                l_Reverb->TapGains[it_Tap] = std::pow(0.8f, static_cast<float>(it_Tap));
                l_Energy += l_Reverb->TapGains[it_Tap] * l_Reverb->TapGains[it_Tap];
            }

            for (float& it_Gain : l_Reverb->TapGains)
            {
                it_Gain /= std::sqrt(l_Energy);
            }

            l_Reverb->PreDelayLength = a_Frames(c_ReverbLongestDelayMilliseconds + c_ReverbTapMilliseconds.back()) + 2;
            std::size_t l_Floats = l_Reverb->PreDelayLength;
            for (std::size_t it_Diffuser = 0; it_Diffuser < ReverbState::c_Diffusers; ++it_Diffuser)
            {
                l_Reverb->DiffuserLengths[it_Diffuser] = std::max<std::uint32_t>(a_Frames(c_ReverbDiffuserMilliseconds[it_Diffuser]), 1);
                l_Floats += l_Reverb->DiffuserLengths[it_Diffuser];
            }

            const double l_Scale = 0.4 + 0.6 * static_cast<double>(density) / 100.0;
            for (std::size_t it_Line = 0; it_Line < ReverbState::c_Lines; ++it_Line)
            {
                const std::uint32_t l_Length = std::max<std::uint32_t>(a_Frames(c_ReverbLineMilliseconds[it_Line] * l_Scale), 2);
                l_Reverb->LineLengths[it_Line] = it_Line > 0 && l_Length <= l_Reverb->LineLengths[it_Line - 1] ? l_Reverb->LineLengths[it_Line - 1] + 1 : l_Length;
                l_Floats += l_Reverb->LineLengths[it_Line];
            }

            l_Reverb->Block = static_cast<float*>(Memory::Allocate(l_Floats * sizeof(float), MemoryTag::Audio, alignof(float)));
            std::fill(l_Reverb->Block, l_Reverb->Block + l_Floats, 0.0f);
            float* l_Next = l_Reverb->Block;
            l_Reverb->PreDelay = l_Next;
            l_Next += l_Reverb->PreDelayLength;
            for (std::size_t it_Diffuser = 0; it_Diffuser < ReverbState::c_Diffusers; ++it_Diffuser)
            {
                l_Reverb->Diffusers[it_Diffuser] = l_Next;
                l_Next += l_Reverb->DiffuserLengths[it_Diffuser];
            }

            for (std::size_t it_Line = 0; it_Line < ReverbState::c_Lines; ++it_Line)
            {
                l_Reverb->Lines[it_Line] = l_Next;
                l_Next += l_Reverb->LineLengths[it_Line];
            }

            return l_Reverb;
        }

        void DestroyReverb(ReverbState* reverb)
        {
            Memory::Free(reverb->Block);
            Memory::Delete(reverb);
        }

        // An effect of a group's chain, which crosses from what comes into it to what it makes by its wet mix, each sample an even step of what is left of the fade, and glides its values to the main thread's latest over the ramp
        struct EffectNode
        {
            ma_node_base Base{};
            AudioEffectType Type = AudioEffectType::Lowpass;
            std::uint32_t Channels = 0;
            std::uint32_t SampleRate = 0;
            std::uint32_t FadeFrames = 1;
            std::uint32_t RampFrames = 1;
            std::uint32_t ValueCount = 0;
            // Set by the main thread and read by the audio thread each block, Asked counted on with each new wet, so a fade's end is known to be the latest's
            std::atomic<float> TargetWet{ 0.0f };
            std::atomic<std::uint32_t> Asked{ 0 };
            std::array<std::atomic<float>, AudioEffect::c_MaxParameters> TargetValues{};
            // The audio thread's alone once the node is in the graph
            float Wet = 0.0f;
            float RampWet = 0.0f;
            std::uint32_t WetLeft = 0;
            std::array<float, AudioEffect::c_MaxParameters> Values{};
            std::array<float, AudioEffect::c_MaxParameters> RampValues{};
            std::uint32_t ValuesLeft = 0;
            // What an Echo, a Limiter or an SFX Reverb takes into its delay, faded out while it is to be dry and in once it is to be wet, so its delay never starts or stops on a step, which it would play back a delay later as a click. A bypassed one so takes in nothing, as Unity's bypass processes nothing
            float Feed = 0.0f;
            float RampFeed = 0.0f;
            std::uint32_t FeedLeft = 0;
            std::array<float, c_ScratchSamples> Scratch{};
            EffectFilters Filters{};
            bool HasFilter = false;
            // The Compressor's and the Duck Volume's level, followed at its attack and release, and the Duck Volume's reduction in decibels
            float Envelope = 0.0f;
            float Reduction = 0.0f;
            ReverbState* Reverb = nullptr;
            // The Limiter's window, a lookahead and one frame long: the frames it holds back, the gain each needs to keep under the ceiling, and the released lowest of those, averaged into the gain it applies, so the gain has fallen far enough by the time a peak leaves
            float* Held = nullptr;
            float* Needed = nullptr;
            float* Smoothed = nullptr;
            std::uint32_t Window = 0;
            std::uint32_t Cursor = 0;
            float Released = 1.0f;
            double SmoothedSum = 0.0;
            // Written by the audio thread after each block, with the count of the wet it started from, so the main thread sees a fade to dry end
            std::atomic<float> PublishedWet{ 0.0f };
            std::atomic<std::uint32_t> Seen{ 0 };
        };

        static_assert(std::is_standard_layout_v<EffectNode>, "miniaudio reaches the node through its base, its first member.");

        [[nodiscard]] double ClampFrequency(float frequency, std::uint32_t sampleRate)
        {
            return std::min(static_cast<double>(frequency), c_HighestFrequencyRatio * static_cast<double>(sampleRate));
        }

        // As wide a peaking filter as the octave range, by the analog filter's relation
        [[nodiscard]] double OctavesToQ(double octaves)
        {
            const double l_Ratio = std::exp2(octaves);

            return std::sqrt(l_Ratio) / (l_Ratio - 1.0);
        }

        [[nodiscard]] float DecibelsToLinear(float decibels)
        {
            return std::pow(10.0f, decibels / 20.0f);
        }

        // A one-pole step for a time constant in milliseconds
        [[nodiscard]] float Coefficient(float milliseconds, std::uint32_t sampleRate)
        {
            return 1.0f - std::exp(-1000.0f / (std::max(milliseconds, 0.001f) * static_cast<float>(sampleRate)));
        }

        // A one-pole step that goes 99% of the way in a time in milliseconds, and all of it at once for none
        [[nodiscard]] float Coefficient99(float milliseconds, std::uint32_t sampleRate)
        {
            const float l_Frames = milliseconds * static_cast<float>(sampleRate) / 1000.0f;

            return l_Frames <= 1.0f ? 1.0f : 1.0f - std::pow(0.01f, 1.0f / l_Frames);
        }

        // A one-pole low-pass's coefficient that passes everything at DC and a power gain at a frequency, by the frequency's cosine, as EAX's reverbs damp their highs. None for a gain of 1 or more, which it cannot boost
        [[nodiscard]] float OnePoleForGain(float gain, float cosine)
        {
            if (gain >= 0.9999f)
            {
                return 0.0f;
            }

            const float l_Gain = std::max(gain, 0.001f);

            return (1.0f - l_Gain * cosine - std::sqrt(std::max(2.0f * l_Gain * (1.0f - cosine) - l_Gain * l_Gain * (1.0f - cosine * cosine), 0.0f))) / (1.0f - l_Gain);
        }

        // The values as the reverb plays them: its levels from millibels, the Room's highs damped to its HF level at the HF reference and its lows below the LF reference set to its LF level, its delays in frames, its diffusers' gain, and each line's gain a pass, which takes it down 60 dB in the decay time at DC, and in the decay time by the HF ratio at the HF reference. A ratio of 1 or more decays the highs as the lows
        void ConfigureReverb(EffectNode& node)
        {
            ReverbState& l_Reverb = *node.Reverb;
            const std::array<float, AudioEffect::c_MaxParameters>& l_Values = node.Values;
            const double l_Rate = static_cast<double>(node.SampleRate);
            const auto a_Millibels = [](float millibels) { return std::pow(10.0f, millibels / 2000.0f); };
            const float l_HighCosine = static_cast<float>(std::cos(2.0 * std::numbers::pi * ClampFrequency(l_Values[11], node.SampleRate) / l_Rate));
            l_Reverb.DryGain = a_Millibels(l_Values[0]);
            l_Reverb.RoomGain = a_Millibels(l_Values[1]);
            l_Reverb.HighCoefficient = OnePoleForGain(a_Millibels(l_Values[2]) * a_Millibels(l_Values[2]), l_HighCosine);
            l_Reverb.ReflectionsGain = a_Millibels(l_Values[5]);
            l_Reverb.EarlyDelay = static_cast<std::uint32_t>(std::llround(static_cast<double>(l_Values[6]) * l_Rate));
            l_Reverb.ReverbGain = a_Millibels(l_Values[7]);
            l_Reverb.LateDelay = l_Reverb.EarlyDelay + static_cast<std::uint32_t>(std::llround(static_cast<double>(l_Values[8]) * l_Rate));
            l_Reverb.Diffusion = 0.6f * l_Values[9] / 100.0f;
            l_Reverb.LowGain = a_Millibels(l_Values[12]);
            l_Reverb.LowCoefficient = static_cast<float>(std::exp(-2.0 * std::numbers::pi * ClampFrequency(l_Values[13], node.SampleRate) / l_Rate));
            for (std::size_t it_Line = 0; it_Line < ReverbState::c_Lines; ++it_Line)
            {
                const double l_Frames = static_cast<double>(l_Reverb.LineLengths[it_Line]);
                const float l_Low = static_cast<float>(std::pow(10.0, -3.0 * l_Frames / (static_cast<double>(l_Values[3]) * l_Rate)));
                const float l_High = static_cast<float>(std::pow(10.0, -3.0 * l_Frames / (static_cast<double>(l_Values[3]) * static_cast<double>(l_Values[4]) * l_Rate)));
                l_Reverb.LineGains[it_Line] = l_Low;
                l_Reverb.LineDamping[it_Line] = l_Values[4] < 1.0f ? OnePoleForGain((l_High / l_Low) * (l_High / l_Low), l_HighCosine) : 0.0f;
            }
        }

        // The filter's coefficients for its values now. Init makes its state as well, on the main thread, and otherwise the coefficients are worked out again in place, which allocates nothing
        ma_result ConfigureFilter(EffectNode& node, const ma_allocation_callbacks* callbacks)
        {
            const std::array<float, AudioEffect::c_MaxParameters>& l_Values = node.Values;
            const ma_uint32 l_Channels = node.Channels;
            const ma_uint32 l_Rate = node.SampleRate;
            const bool l_Init = callbacks != nullptr;
            switch (node.Type)
            {
                case AudioEffectType::Lowpass:
                {
                    const ma_lpf2_config l_Config = ma_lpf2_config_init(ma_format_f32, l_Channels, l_Rate, ClampFrequency(l_Values[0], l_Rate), l_Values[1]);

                    return l_Init ? ma_lpf2_init(&l_Config, callbacks, &node.Filters.Lowpass) : ma_lpf2_reinit(&l_Config, &node.Filters.Lowpass);
                }
                case AudioEffectType::Highpass:
                {
                    const ma_hpf2_config l_Config = ma_hpf2_config_init(ma_format_f32, l_Channels, l_Rate, ClampFrequency(l_Values[0], l_Rate), l_Values[1]);

                    return l_Init ? ma_hpf2_init(&l_Config, callbacks, &node.Filters.Highpass) : ma_hpf2_reinit(&l_Config, &node.Filters.Highpass);
                }
                case AudioEffectType::Bandpass:
                {
                    const ma_bpf2_config l_Config = ma_bpf2_config_init(ma_format_f32, l_Channels, l_Rate, ClampFrequency(l_Values[0], l_Rate), l_Values[1]);

                    return l_Init ? ma_bpf2_init(&l_Config, callbacks, &node.Filters.Bandpass) : ma_bpf2_reinit(&l_Config, &node.Filters.Bandpass);
                }
                case AudioEffectType::Notch:
                {
                    const ma_notch2_config l_Config = ma_notch2_config_init(ma_format_f32, l_Channels, l_Rate, l_Values[1], ClampFrequency(l_Values[0], l_Rate));

                    return l_Init ? ma_notch2_init(&l_Config, callbacks, &node.Filters.Notch) : ma_notch2_reinit(&l_Config, &node.Filters.Notch);
                }
                case AudioEffectType::ParamEQ:
                {
                    const ma_peak2_config l_Config = ma_peak2_config_init(ma_format_f32, l_Channels, l_Rate, 20.0 * std::log10(static_cast<double>(l_Values[2])), OctavesToQ(l_Values[1]), ClampFrequency(l_Values[0], l_Rate));

                    return l_Init ? ma_peak2_init(&l_Config, callbacks, &node.Filters.Peak) : ma_peak2_reinit(&l_Config, &node.Filters.Peak);
                }
                case AudioEffectType::LowShelf:
                {
                    const ma_loshelf2_config l_Config = ma_loshelf2_config_init(ma_format_f32, l_Channels, l_Rate, l_Values[1], l_Values[2], ClampFrequency(l_Values[0], l_Rate));

                    return l_Init ? ma_loshelf2_init(&l_Config, callbacks, &node.Filters.LowShelf) : ma_loshelf2_reinit(&l_Config, &node.Filters.LowShelf);
                }
                case AudioEffectType::HighShelf:
                {
                    const ma_hishelf2_config l_Config = ma_hishelf2_config_init(ma_format_f32, l_Channels, l_Rate, l_Values[1], l_Values[2], ClampFrequency(l_Values[0], l_Rate));

                    return l_Init ? ma_hishelf2_init(&l_Config, callbacks, &node.Filters.HighShelf) : ma_hishelf2_reinit(&l_Config, &node.Filters.HighShelf);
                }
                case AudioEffectType::SFXReverb:
                {
                    ConfigureReverb(node);

                    return MA_SUCCESS;
                }
                default:
                {
                    return MA_SUCCESS;
                }
            }
        }

        // What the effect makes of a run of frames, before its wet mix: a filter's output, which for Bandpass is brought down by its Q to pass its centre at 0 dB, what comes in with the Echo's echoes on it, the compressed and the limited, the reverberated, what comes in with what is sent added for a Receive and lowered by it for a Duck Volume, and for a Send what comes in. The feed is each frame's fade of what goes into a delay, and the side what is sent to a Receive or a Duck Volume
        void ProcessEffectRun(EffectNode& node, const float* in, float* out, ma_uint32 frames, std::span<const float> feed, const float* side)
        {
            const std::uint32_t l_Channels = node.Channels;
            const std::size_t l_Samples = static_cast<std::size_t>(frames) * l_Channels;
            switch (node.Type)
            {
                case AudioEffectType::Lowpass:
                {
                    static_cast<void>(ma_lpf2_process_pcm_frames(&node.Filters.Lowpass, out, in, frames));

                    break;
                }
                case AudioEffectType::Highpass:
                {
                    static_cast<void>(ma_hpf2_process_pcm_frames(&node.Filters.Highpass, out, in, frames));

                    break;
                }
                case AudioEffectType::Bandpass:
                {
                    static_cast<void>(ma_bpf2_process_pcm_frames(&node.Filters.Bandpass, out, in, frames));
                    const float l_Scale = 1.0f / node.Values[1];
                    for (std::size_t it_Sample = 0; it_Sample < l_Samples; ++it_Sample)
                    {
                        out[it_Sample] *= l_Scale;
                    }

                    break;
                }
                case AudioEffectType::Notch:
                {
                    static_cast<void>(ma_notch2_process_pcm_frames(&node.Filters.Notch, out, in, frames));

                    break;
                }
                case AudioEffectType::ParamEQ:
                {
                    static_cast<void>(ma_peak2_process_pcm_frames(&node.Filters.Peak, out, in, frames));

                    break;
                }
                case AudioEffectType::LowShelf:
                {
                    static_cast<void>(ma_loshelf2_process_pcm_frames(&node.Filters.LowShelf, out, in, frames));

                    break;
                }
                case AudioEffectType::HighShelf:
                {
                    static_cast<void>(ma_hishelf2_process_pcm_frames(&node.Filters.HighShelf, out, in, frames));

                    break;
                }
                case AudioEffectType::Echo:
                {
                    for (ma_uint32 it_Frame = 0; it_Frame < frames; ++it_Frame)
                    {
                        for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                        {
                            const std::size_t l_Sample = static_cast<std::size_t>(it_Frame) * l_Channels + it_Channel;
                            node.Scratch[l_Sample] = in[l_Sample] * feed[it_Frame];
                        }
                    }

                    ma_delay_set_decay(&node.Filters.Delay, node.Values[1] / 100.0f);
                    static_cast<void>(ma_delay_process_pcm_frames(&node.Filters.Delay, out, node.Scratch.data(), frames));
                    for (std::size_t it_Sample = 0; it_Sample < l_Samples; ++it_Sample)
                    {
                        out[it_Sample] += in[it_Sample];
                    }

                    break;
                }
                case AudioEffectType::Compressor:
                {
                    // The loudest channel's level, so the channels are compressed alike and the image holds
                    const float l_Threshold = node.Values[0];
                    const float l_Slope = 1.0f - 1.0f / node.Values[1];
                    const float l_Attack = Coefficient(node.Values[2], node.SampleRate);
                    const float l_Release = Coefficient(node.Values[3], node.SampleRate);
                    const float l_MakeUp = node.Values[4];
                    for (ma_uint32 it_Frame = 0; it_Frame < frames; ++it_Frame)
                    {
                        const std::size_t l_Base = static_cast<std::size_t>(it_Frame) * l_Channels;
                        float l_Level = 0.0f;
                        for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                        {
                            l_Level = std::max(l_Level, std::abs(in[l_Base + it_Channel]));
                        }

                        node.Envelope += (l_Level > node.Envelope ? l_Attack : l_Release) * (l_Level - node.Envelope);
                        const float l_Over = 20.0f * std::log10(std::max(node.Envelope, 1e-9f)) - l_Threshold;
                        const float l_Gain = DecibelsToLinear(l_MakeUp - (l_Over > 0.0f ? l_Over * l_Slope : 0.0f));
                        for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                        {
                            out[l_Base + it_Channel] = in[l_Base + it_Channel] * l_Gain;
                        }
                    }

                    break;
                }
                case AudioEffectType::Limiter:
                {
                    const float l_Ceiling = DecibelsToLinear(node.Values[0]);
                    const float l_Release = Coefficient(node.Values[1], node.SampleRate);
                    const float l_Input = DecibelsToLinear(node.Values[2]);
                    const std::uint32_t l_Window = node.Window;
                    for (ma_uint32 it_Frame = 0; it_Frame < frames; ++it_Frame)
                    {
                        const std::size_t l_Base = static_cast<std::size_t>(it_Frame) * l_Channels;
                        float l_Peak = 0.0f;
                        for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                        {
                            l_Peak = std::max(l_Peak, std::abs(in[l_Base + it_Channel] * feed[it_Frame]) * l_Input);
                        }

                        node.Needed[node.Cursor] = l_Peak > l_Ceiling ? l_Ceiling / l_Peak : 1.0f;
                        float l_Lowest = 1.0f;
                        for (std::uint32_t it_Slot = 0; it_Slot < l_Window; ++it_Slot)
                        {
                            l_Lowest = std::min(l_Lowest, node.Needed[it_Slot]);
                        }

                        // Falling at once and rising at the release, never above the lowest the window needs
                        node.Released = l_Lowest < node.Released ? l_Lowest : node.Released + (l_Lowest - node.Released) * l_Release;
                        node.SmoothedSum += static_cast<double>(node.Released) - static_cast<double>(node.Smoothed[node.Cursor]);
                        node.Smoothed[node.Cursor] = node.Released;
                        const float l_Gain = static_cast<float>(node.SmoothedSum / static_cast<double>(l_Window)) * l_Input;

                        // The frame a lookahead old leaves from the slot after the cursor, which the newest takes once it has
                        const std::uint32_t l_Oldest = node.Cursor + 1 == l_Window ? 0 : node.Cursor + 1;
                        for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                        {
                            out[l_Base + it_Channel] = std::clamp(node.Held[static_cast<std::size_t>(l_Oldest) * l_Channels + it_Channel] * l_Gain, -l_Ceiling, l_Ceiling);
                            node.Held[static_cast<std::size_t>(node.Cursor) * l_Channels + it_Channel] = in[l_Base + it_Channel] * feed[it_Frame];
                        }

                        // Summed again from the slots once a window round, so rounding never builds up
                        node.Cursor = l_Oldest;
                        if (node.Cursor == 0)
                        {
                            node.SmoothedSum = 0.0;
                            for (std::uint32_t it_Slot = 0; it_Slot < l_Window; ++it_Slot)
                            {
                                node.SmoothedSum += static_cast<double>(node.Smoothed[it_Slot]);
                            }
                        }
                    }

                    break;
                }
                case AudioEffectType::SFXReverb:
                {
                    ReverbState& l_Reverb = *node.Reverb;
                    const float l_Scale = 1.0f / std::sqrt(static_cast<float>(ReverbState::c_Lines));
                    const float l_Share = 2.0f / static_cast<float>(ReverbState::c_Lines);
                    const std::uint32_t l_Length = l_Reverb.PreDelayLength;
                    const auto a_Back = [&l_Reverb, l_Length](std::uint32_t back) { return l_Reverb.PreDelay[(l_Reverb.PreDelayCursor + l_Length - back) % l_Length]; };
                    for (ma_uint32 it_Frame = 0; it_Frame < frames; ++it_Frame)
                    {
                        const std::size_t l_Base = static_cast<std::size_t>(it_Frame) * l_Channels;
                        float l_Mono = 0.0f;
                        for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                        {
                            l_Mono += in[l_Base + it_Channel];
                        }

                        // The Room, shaped at its HF and LF references, into the delay its reflections and reverberation come from
                        l_Mono *= feed[it_Frame] / static_cast<float>(l_Channels);
                        l_Reverb.High = (1.0f - l_Reverb.HighCoefficient) * l_Mono + l_Reverb.HighCoefficient * l_Reverb.High;
                        l_Reverb.Low = (1.0f - l_Reverb.LowCoefficient) * l_Reverb.High + l_Reverb.LowCoefficient * l_Reverb.Low;
                        l_Reverb.PreDelay[l_Reverb.PreDelayCursor] = l_Reverb.RoomGain * (l_Reverb.High - l_Reverb.Low + l_Reverb.LowGain * l_Reverb.Low);

                        // The late reverberation's input, smeared by the diffusers' allpasses
                        float l_Late = a_Back(l_Reverb.LateDelay) * l_Reverb.ReverbGain;
                        for (std::size_t it_Diffuser = 0; it_Diffuser < ReverbState::c_Diffusers; ++it_Diffuser)
                        {
                            std::uint32_t& l_Cursor = l_Reverb.DiffuserCursors[it_Diffuser];
                            const float l_Delayed = l_Reverb.Diffusers[it_Diffuser][l_Cursor];
                            const float l_Out = l_Delayed - l_Reverb.Diffusion * l_Late;
                            l_Reverb.Diffusers[it_Diffuser][l_Cursor] = l_Late + l_Reverb.Diffusion * l_Out;
                            l_Late = l_Out;
                            l_Cursor = l_Cursor + 1 == l_Reverb.DiffuserLengths[it_Diffuser] ? 0 : l_Cursor + 1;
                        }

                        // Each line's oldest, damped by its gain and its one-pole, then mixed back by Householder's reflection, which loses nothing, so the lines decay only as their gains have them
                        std::array<float, ReverbState::c_Lines> l_Lines{};
                        float l_Sum = 0.0f;
                        for (std::size_t it_Line = 0; it_Line < ReverbState::c_Lines; ++it_Line)
                        {
                            const float l_Leaving = l_Reverb.Lines[it_Line][l_Reverb.LineCursors[it_Line]];
                            l_Reverb.Damped[it_Line] = l_Reverb.LineGains[it_Line] * (1.0f - l_Reverb.LineDamping[it_Line]) * l_Leaving + l_Reverb.LineDamping[it_Line] * l_Reverb.Damped[it_Line];
                            l_Lines[it_Line] = l_Reverb.Damped[it_Line];
                            l_Sum += l_Lines[it_Line];
                        }

                        const float l_Mirror = l_Sum * l_Share;
                        for (std::size_t it_Line = 0; it_Line < ReverbState::c_Lines; ++it_Line)
                        {
                            std::uint32_t& l_Cursor = l_Reverb.LineCursors[it_Line];
                            l_Reverb.Lines[it_Line][l_Cursor] = l_Lines[it_Line] - l_Mirror + HadamardSign(3, it_Line) * l_Late * l_Scale;
                            l_Cursor = l_Cursor + 1 == l_Reverb.LineLengths[it_Line] ? 0 : l_Cursor + 1;
                        }

                        for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                        {
                            const std::size_t l_Row = it_Channel % 7 + 1;
                            float l_Early = 0.0f;
                            for (std::size_t it_Tap = 0; it_Tap < ReverbState::c_Taps; ++it_Tap)
                            {
                                l_Early += l_Reverb.TapGains[it_Tap] * HadamardSign(l_Row, it_Tap) * a_Back(l_Reverb.EarlyDelay + l_Reverb.TapOffsets[it_Tap]);
                            }

                            float l_Tail = 0.0f;
                            for (std::size_t it_Line = 0; it_Line < ReverbState::c_Lines; ++it_Line)
                            {
                                l_Tail += HadamardSign(l_Row, it_Line) * l_Lines[it_Line];
                            }

                            out[l_Base + it_Channel] = l_Reverb.DryGain * in[l_Base + it_Channel] + l_Reverb.ReflectionsGain * l_Early + l_Tail * l_Scale;
                        }

                        l_Reverb.PreDelayCursor = l_Reverb.PreDelayCursor + 1 == l_Length ? 0 : l_Reverb.PreDelayCursor + 1;
                    }

                    break;
                }
                case AudioEffectType::Receive:
                {
                    for (std::size_t it_Sample = 0; it_Sample < l_Samples; ++it_Sample)
                    {
                        out[it_Sample] = in[it_Sample] + side[it_Sample];
                    }

                    break;
                }
                case AudioEffectType::DuckVolume:
                {
                    // What it listens to, the sends at the sidechain mix and its own input at the rest, followed up at once and down over a few milliseconds. Its reduction is its ratio's over the threshold, softened across the knee, and comes at the attack and goes at the release
                    const float l_Threshold = node.Values[0];
                    const float l_Slope = 1.0f - 100.0f / node.Values[1];
                    const float l_Attack = Coefficient99(node.Values[2], node.SampleRate);
                    const float l_Release = Coefficient99(node.Values[3], node.SampleRate);
                    const float l_MakeUp = node.Values[4];
                    const float l_Knee = node.Values[5];
                    const float l_Mix = node.Values[6] / 100.0f;
                    const float l_Fall = Coefficient(c_DetectorReleaseMilliseconds, node.SampleRate);
                    for (ma_uint32 it_Frame = 0; it_Frame < frames; ++it_Frame)
                    {
                        const std::size_t l_Base = static_cast<std::size_t>(it_Frame) * l_Channels;
                        float l_Level = 0.0f;
                        for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                        {
                            l_Level = std::max(l_Level, std::abs(l_Mix * side[l_Base + it_Channel] + (1.0f - l_Mix) * in[l_Base + it_Channel]));
                        }

                        node.Envelope = l_Level > node.Envelope ? l_Level : node.Envelope + (l_Level - node.Envelope) * l_Fall;
                        const float l_Over = 20.0f * std::log10(std::max(node.Envelope, 1e-9f)) - l_Threshold;
                        float l_Target = 0.0f;
                        if (l_Knee > 0.0f && std::abs(l_Over) <= l_Knee / 2.0f)
                        {
                            l_Target = l_Slope * (l_Over + l_Knee / 2.0f) * (l_Over + l_Knee / 2.0f) / (2.0f * l_Knee);
                        }
                        else if (l_Over > 0.0f)
                        {
                            l_Target = l_Slope * l_Over;
                        }

                        node.Reduction += (l_Target > node.Reduction ? l_Attack : l_Release) * (l_Target - node.Reduction);
                        const float l_Gain = DecibelsToLinear(l_MakeUp - node.Reduction);
                        for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                        {
                            out[l_Base + it_Channel] = in[l_Base + it_Channel] * l_Gain;
                        }
                    }

                    break;
                }
                default:
                {
                    std::copy(in, in + l_Samples, out);

                    break;
                }
            }
        }

        // On the audio thread, which allocates nothing here. A new wet starts a fade from where it is, and new values a glide, both of which end exactly on them. At a wet of 0 what comes in leaves exactly, so an effect fully faded can be stepped out of its chain or into it with no change to the sound
        void ProcessEffect(ma_node* node, const float** framesIn, [[maybe_unused]] ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
        {
            EffectNode& l_Node = *reinterpret_cast<EffectNode*>(node);
            const std::uint32_t l_Asked = l_Node.Asked.load(std::memory_order_acquire);
            const float l_TargetWet = l_Node.TargetWet.load(std::memory_order_relaxed);
            if (l_TargetWet != l_Node.RampWet)
            {
                l_Node.RampWet = l_TargetWet;
                l_Node.WetLeft = l_Node.FadeFrames;
            }

            const float l_TargetFeed = l_TargetWet > 0.0f ? 1.0f : 0.0f;
            if (l_TargetFeed != l_Node.RampFeed)
            {
                l_Node.RampFeed = l_TargetFeed;
                l_Node.FeedLeft = l_Node.FadeFrames;
            }

            bool l_NewValues = false;
            for (std::uint32_t it_Value = 0; it_Value < l_Node.ValueCount; ++it_Value)
            {
                const float l_Target = l_Node.TargetValues[it_Value].load(std::memory_order_relaxed);
                l_NewValues |= l_Target != l_Node.RampValues[it_Value];
                l_Node.RampValues[it_Value] = l_Target;
            }

            if (l_NewValues)
            {
                l_Node.ValuesLeft = l_Node.RampFrames;
            }

            const ma_uint32 l_Frames = *frameCountOut;
            const std::uint32_t l_Channels = l_Node.Channels;
            const float* l_In = framesIn[0];
            float* l_Out = framesOut[0];
            const float* l_Side = l_Node.Type == AudioEffectType::Receive || l_Node.Type == AudioEffectType::DuckVolume ? framesIn[1] : nullptr;
            float* l_Sent = l_Node.Type == AudioEffectType::Send ? framesOut[1] : nullptr;
            const ma_uint32 l_RunFrames = std::clamp<ma_uint32>(c_ScratchSamples / std::max<std::uint32_t>(l_Channels, 1), 1, c_GlideFrames);
            std::array<float, c_GlideFrames> l_Feed{};
            for (ma_uint32 it_Start = 0; it_Start < l_Frames; it_Start += l_RunFrames)
            {
                const ma_uint32 l_Run = std::min(l_RunFrames, l_Frames - it_Start);
                if (l_Node.ValuesLeft > 0)
                {
                    const std::uint32_t l_Step = std::min(l_Run, l_Node.ValuesLeft);
                    for (std::uint32_t it_Value = 0; it_Value < l_Node.ValueCount; ++it_Value)
                    {
                        l_Node.Values[it_Value] += (l_Node.RampValues[it_Value] - l_Node.Values[it_Value]) * static_cast<float>(l_Step) / static_cast<float>(l_Node.ValuesLeft);
                    }

                    l_Node.ValuesLeft -= l_Step;
                    if (l_Node.ValuesLeft == 0)
                    {
                        l_Node.Values = l_Node.RampValues;
                    }

                    static_cast<void>(ConfigureFilter(l_Node, nullptr));
                }

                for (ma_uint32 it_Frame = 0; it_Frame < l_Run; ++it_Frame)
                {
                    if (l_Node.FeedLeft > 0)
                    {
                        l_Node.Feed += (l_Node.RampFeed - l_Node.Feed) / static_cast<float>(l_Node.FeedLeft);
                        if (--l_Node.FeedLeft == 0)
                        {
                            l_Node.Feed = l_Node.RampFeed;
                        }
                    }

                    l_Feed[it_Frame] = l_Node.Feed;
                }

                const std::size_t l_Offset = static_cast<std::size_t>(it_Start) * l_Channels;
                const float* l_RunIn = l_In + l_Offset;
                float* l_RunOut = l_Out + l_Offset;
                float* l_RunSent = l_Sent != nullptr ? l_Sent + l_Offset : nullptr;
                const float l_SendGain = l_Sent != nullptr ? AudioDecibelsToGain(l_Node.Values[0]) : 0.0f;
                ProcessEffectRun(l_Node, l_RunIn, l_RunOut, l_Run, std::span<const float>(l_Feed).first(l_Run), l_Side != nullptr ? l_Side + l_Offset : nullptr);
                for (ma_uint32 it_Frame = 0; it_Frame < l_Run; ++it_Frame)
                {
                    if (l_Node.WetLeft > 0)
                    {
                        l_Node.Wet += (l_Node.RampWet - l_Node.Wet) / static_cast<float>(l_Node.WetLeft);
                        if (--l_Node.WetLeft == 0)
                        {
                            l_Node.Wet = l_Node.RampWet;
                        }
                    }

                    const std::size_t l_Base = static_cast<std::size_t>(it_Frame) * l_Channels;
                    for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                    {
                        const float l_Dry = l_RunIn[l_Base + it_Channel];
                        l_RunOut[l_Base + it_Channel] = l_Node.Wet == 0.0f ? l_Dry : l_Dry + l_Node.Wet * (l_RunOut[l_Base + it_Channel] - l_Dry);
                    }

                    // A Send's level and wet mix on what it sends alone, as what it passes on is what comes in
                    for (std::uint32_t it_Channel = 0; l_RunSent != nullptr && it_Channel < l_Channels; ++it_Channel)
                    {
                        l_RunSent[l_Base + it_Channel] = l_RunIn[l_Base + it_Channel] * l_SendGain * l_Node.Wet;
                    }
                }
            }

            l_Node.PublishedWet.store(l_Node.Wet, std::memory_order_relaxed);
            l_Node.Seen.store(l_Asked, std::memory_order_release);
        }

        constexpr ma_node_vtable c_EffectVTable{ &ProcessEffect, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING };
        // A Send's second output is what it sends, and a Receive's or a Duck Volume's second input what is sent to it, which many Sends may play into
        constexpr ma_node_vtable c_SendVTable{ &ProcessEffect, nullptr, 1, 2, MA_NODE_FLAG_CONTINUOUS_PROCESSING };
        constexpr ma_node_vtable c_ReturnVTable{ &ProcessEffect, nullptr, 2, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING };

        [[nodiscard]] bool TakesSends(AudioEffectType type)
        {
            return type == AudioEffectType::Receive || type == AudioEffectType::DuckVolume;
        }

        // Only a pitch other than 1 takes the resampler, which runs at any pitch once there and holds the sound back by a sample
        bool IsPitched(float pitch)
        {
            return std::clamp(pitch, AudioMixerData::c_MinimumPitch, AudioMixerData::c_MaximumPitch) != 1.0f;
        }

        [[nodiscard]] std::uint32_t FramesFor(std::uint64_t microseconds, std::uint32_t sampleRate)
        {
            return std::max<std::uint32_t>(static_cast<std::uint32_t>((microseconds * sampleRate + 500000) / 1000000), 1);
        }

        // A unit of a group's chain: its Attenuation, which is the group's own, or an effect, with what it was made with that a change of needs another made in its place, an Echo's delay, an SFX Reverb's density and a Send's target, and the wet it is to have once in its place. A Send's second output plays into the unit it was sent to
        struct Unit
        {
            UUID ID;
            UUID Target;
            AudioEffectType Type = AudioEffectType::Attenuation;
            GainNode* Gain = nullptr;
            EffectNode* Effect = nullptr;
            std::uint32_t Delay = 0;
            float Density = 0.0f;
            float Wet = 0.0f;
            Unit* SentTo = nullptr;

            [[nodiscard]] ma_node* GetNode() const
            {
                return Gain != nullptr ? static_cast<ma_node*>(&Gain->Base) : static_cast<ma_node*>(&Effect->Base);
            }
        };

        using UnitList = std::vector<Unit*, TaggedAllocator<Unit*, MemoryTag::Audio>>;

        // Set once for each new value, so a fade the main thread waits on is the latest
        void SetWet(EffectNode& node, float wet)
        {
            if (node.TargetWet.load(std::memory_order_relaxed) != wet)
            {
                node.TargetWet.store(wet, std::memory_order_relaxed);
                node.Asked.fetch_add(1, std::memory_order_release);
            }
        }

        // Crossed to dry by a block the audio thread started after the latest wet was set
        [[nodiscard]] bool IsDry(const EffectNode& node)
        {
            return node.TargetWet.load(std::memory_order_relaxed) == 0.0f && node.Seen.load(std::memory_order_acquire) == node.Asked.load(std::memory_order_relaxed) && node.PublishedWet.load(std::memory_order_relaxed) == 0.0f;
        }

        // The longest run of units both chains have in the same order, which play on through a change of the chain while the rest cross to dry, step out or in, and cross back. Attenuation is always among them, since it is never faded
        [[nodiscard]] UnitList FindKept(const UnitList& wired, const UnitList& desired, Unit* volume)
        {
            const auto a_Common = [](std::span<Unit* const> a, std::span<Unit* const> b, UnitList& kept)
            {
                const std::size_t l_Columns = b.size() + 1;
                std::vector<std::uint32_t, TaggedAllocator<std::uint32_t, MemoryTag::Audio>> l_Lengths((a.size() + 1) * l_Columns, 0);
                for (std::size_t it_A = a.size(); it_A-- > 0;)
                {
                    for (std::size_t it_B = b.size(); it_B-- > 0;)
                    {
                        l_Lengths[it_A * l_Columns + it_B] = a[it_A] == b[it_B] ? l_Lengths[(it_A + 1) * l_Columns + it_B + 1] + 1 : std::max(l_Lengths[(it_A + 1) * l_Columns + it_B], l_Lengths[it_A * l_Columns + it_B + 1]);
                    }
                }

                for (std::size_t it_A = 0, it_B = 0; it_A < a.size() && it_B < b.size();)
                {
                    if (a[it_A] == b[it_B])
                    {
                        kept.push_back(a[it_A]);
                        ++it_A;
                        ++it_B;
                    }
                    else if (l_Lengths[(it_A + 1) * l_Columns + it_B] >= l_Lengths[it_A * l_Columns + it_B + 1])
                    {
                        ++it_A;
                    }
                    else
                    {
                        ++it_B;
                    }
                }
            };

            const std::size_t l_InWired = static_cast<std::size_t>(std::ranges::find(wired, volume) - wired.begin());
            const std::size_t l_InDesired = static_cast<std::size_t>(std::ranges::find(desired, volume) - desired.begin());
            UnitList l_Kept;
            a_Common(std::span(wired).first(l_InWired), std::span(desired).first(l_InDesired), l_Kept);
            l_Kept.push_back(volume);
            a_Common(std::span(wired).subspan(l_InWired + 1), std::span(desired).subspan(l_InDesired + 1), l_Kept);

            return l_Kept;
        }

        [[nodiscard]] bool Contains(const UnitList& units, const Unit* unit)
        {
            return std::ranges::find(units, unit) != units.end();
        }

        // Which group plays into which, a child into its parent and a Send's group into its target's
        using EdgeList = std::vector<std::pair<const void*, const void*>, TaggedAllocator<std::pair<const void*, const void*>, MemoryTag::Audio>>;

        // Whether a walk along the edges from one group comes to another, which it does at once when they are the same
        [[nodiscard]] bool Reaches(const EdgeList& edges, const void* from, const void* to)
        {
            if (from == to)
            {
                return true;
            }

            std::vector<const void*, TaggedAllocator<const void*, MemoryTag::Audio>> l_Pending{ from };
            std::vector<const void*, TaggedAllocator<const void*, MemoryTag::Audio>> l_Seen{ from };
            while (!l_Pending.empty())
            {
                const void* l_At = l_Pending.back();
                l_Pending.pop_back();
                for (const auto& [it_From, it_To] : edges)
                {
                    if (it_From != l_At || std::ranges::find(l_Seen, it_To) != l_Seen.end())
                    {
                        continue;
                    }

                    if (it_To == to)
                    {
                        return true;
                    }

                    l_Seen.push_back(it_To);
                    l_Pending.push_back(it_To);
                }
            }

            return false;
        }

        // Off for every effect of a bypassed group, as Unity's group Bypass passes its effects by, Attenuation aside
        [[nodiscard]] float GetWet(const AudioMixerGroup& group, const AudioEffect& effect)
        {
            return group.Bypass || effect.Bypass ? 0.0f : std::clamp(effect.Wet, 0.0f, 100.0f) / 100.0f;
        }

        // As the audio thread plays it, so a value outside its range plays at its nearest end
        [[nodiscard]] float ClampValue(const AudioEffect& effect, std::size_t index)
        {
            const AudioEffectParameter& l_Parameter = GetAudioEffectInfo(effect.Type).Parameters[index];

            return std::clamp(effect.Parameters[index], l_Parameter.Minimum, l_Parameter.Maximum);
        }

        // An Echo's delay as a whole number of frames, which is exactly how much later it repeats
        [[nodiscard]] std::uint32_t EchoFrames(const AudioEffect& effect, std::uint32_t sampleRate)
        {
            return FramesFor(static_cast<std::uint64_t>(std::llround(static_cast<double>(ClampValue(effect, 0)) * 1000.0)), sampleRate);
        }
    }

    // A group as it plays: what is sent to it straight into its direct input, which solo silences while the group only carries a soloed one, then its sound group, which every group below it plays into, then its effects in their order with Attenuation among them, then its output, with its mute and solo and its meter, into its parent's sound group
    struct AudioMixer::State
    {
        struct Group
        {
            // Apart from the group, so it can be made again with the resampler or without
            ma_sound_group* Sound = nullptr;
            GainNode Direct;
            GainNode Attenuation;
            GainNode Output;
            Unit Volume;
            // The chain as the graph plays it, the chain the data wants, and every effect the group has made, in its chain or waiting to step in
            UnitList Wired;
            UnitList Desired;
            UnitList Effects;
            Group* Parent = nullptr;
            bool Pitched = false;
            bool HasSound = false;
            bool HasDirect = false;
            bool HasAttenuation = false;
            bool HasOutput = false;
            // Made by this Apply, so its gains and wets start where they are to be, with no ramp, and its chain is wired at once
            bool Fresh = true;
            bool Kept = false;
            // Its chain is not yet as the data wants, waiting for effects to cross to dry
            bool Pending = false;
        };

        // What a node was sent to, kept so it goes there again once the group is back, and where it plays now
        struct Destination
        {
            UUID GroupID;
            Group* Target = nullptr;
        };

        using GroupMap = std::unordered_map<UUID, Group*, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, Group*>, MemoryTag::Audio>>;
        using RouteMap = std::unordered_map<void*, Destination, std::hash<void*>, std::equal_to<void*>, TaggedAllocator<std::pair<void* const, Destination>, MemoryTag::Audio>>;

        explicit State(AudioEngine& engine) : TheEngine(AudioEngineInternal::GetEngine(engine))
        {

        }

        ~State();

        [[nodiscard]] Group* CreateGroup(bool pitched);
        void DestroyGroup(Group* group);
        [[nodiscard]] ma_sound_group* CreateSound(bool pitched);
        void SetPitched(Group& group, bool pitched);
        [[nodiscard]] bool InitGain(GainNode& node, bool metered);
        [[nodiscard]] Unit* CreateEffect(const AudioEffect& effect);
        void DestroyEffect(Unit* unit);
        void SetDesired(Group& group, const AudioMixerGroup& data);
        void Converge(Group& group);
        void Rewire(Group& group, const UnitList& kept);
        void Settle(Group& group);
        void LoosenSends(const AudioMixerData& data);
        void ConnectSends();
        [[nodiscard]] Unit* FindTarget(UUID id) const;
        [[nodiscard]] const Group* FindGroupOf(const Unit* unit) const;
        [[nodiscard]] Group* FindOrMaster(UUID id) const;
        void UpdateGains();

        ma_engine& TheEngine;
        AudioMixerData Data;
        Group* Master = nullptr;
        // Every group by its ID in the data, Master's included
        GroupMap Groups;
        RouteMap Routes;
        // Set as the mixer goes, when no Send is left to let go of what it sends to
        bool Closing = false;
    };

    AudioMixer::State::~State()
    {
        Closing = true;
        for (const auto& [it_ID, it_Group] : Groups)
        {
            DestroyGroup(it_Group);
        }
    }

    bool AudioMixer::State::InitGain(GainNode& node, bool metered)
    {
        const float l_SampleRate = static_cast<float>(ma_engine_get_sample_rate(&TheEngine));
        node.Channels = ma_engine_get_channels(&TheEngine);
        node.RampFrames = FramesFor(static_cast<std::uint64_t>(c_RampMilliseconds) * 1000, ma_engine_get_sample_rate(&TheEngine));
        node.Metered = metered;
        node.PeakFall = std::pow(10.0f, -c_PeakFallDecibelsPerSecond / 20.0f / l_SampleRate);
        node.RmsCoefficient = 1.0f - std::exp(-1.0f / (c_RmsSeconds * l_SampleRate));

        const std::array<ma_uint32, 1> l_Channels{ node.Channels };
        ma_node_config l_Config = ma_node_config_init();
        l_Config.vtable = &c_GainVTable;
        l_Config.pInputChannels = l_Channels.data();
        l_Config.pOutputChannels = l_Channels.data();

        return ma_node_init(ma_engine_get_node_graph(&TheEngine), &l_Config, &TheEngine.allocationCallbacks, &node.Base) == MA_SUCCESS;
    }

    // Its sound group starts in the engine's output for a moment, with nothing in it. None when it cannot be made
    ma_sound_group* AudioMixer::State::CreateSound(bool pitched)
    {
        ma_sound_group* l_Sound = Memory::New<ma_sound_group>(MemoryTag::Audio);
        if (ma_sound_group_init(&TheEngine, MA_SOUND_FLAG_NO_SPATIALIZATION | (pitched ? 0u : static_cast<ma_uint32>(MA_SOUND_FLAG_NO_PITCH)), nullptr, l_Sound) != MA_SUCCESS)
        {
            Memory::Delete(l_Sound);

            return nullptr;
        }

        return l_Sound;
    }

    // What plays into the old sound group, and what it plays into, move to the new one before the old goes, which moves the sound by the sample the resampler held back
    void AudioMixer::State::SetPitched(Group& group, bool pitched)
    {
        ma_sound_group* l_Sound = CreateSound(pitched);
        if (l_Sound == nullptr)
        {
            TR_CORE_CRITICAL("Audio: a mixer group could not be made");

            throw std::bad_alloc();
        }

        static_cast<void>(ma_node_attach_output_bus(l_Sound, 0, group.Wired.front()->GetNode(), 0));
        static_cast<void>(ma_node_attach_output_bus(&group.Direct.Base, 0, l_Sound, 0));
        for (const auto& [it_ID, it_Child] : Groups)
        {
            if (it_Child->Parent == &group)
            {
                static_cast<void>(ma_node_attach_output_bus(&it_Child->Output.Base, 0, l_Sound, 0));
            }
        }

        ma_sound_group_uninit(group.Sound);
        Memory::Delete(group.Sound);
        group.Sound = l_Sound;
        group.Pitched = pitched;
    }

    // Wired within itself and to nothing above, its chain Attenuation alone, so the audio thread reaches none of it until Apply has set its gains and plays it into its parent. Only running out of memory fails, which Memory treats the same way
    AudioMixer::State::Group* AudioMixer::State::CreateGroup(bool pitched)
    {
        Group* l_Group = Memory::New<Group>(MemoryTag::Audio);
        l_Group->Sound = CreateSound(pitched);
        l_Group->Pitched = pitched;
        l_Group->HasSound = l_Group->Sound != nullptr;
        l_Group->HasDirect = l_Group->HasSound && InitGain(l_Group->Direct, false);
        l_Group->HasAttenuation = l_Group->HasDirect && InitGain(l_Group->Attenuation, false);
        l_Group->HasOutput = l_Group->HasAttenuation && InitGain(l_Group->Output, true);
        if (!l_Group->HasOutput)
        {
            TR_CORE_CRITICAL("Audio: a mixer group could not be made");
            DestroyGroup(l_Group);

            throw std::bad_alloc();
        }

        l_Group->Volume.Gain = &l_Group->Attenuation;
        l_Group->Wired.push_back(&l_Group->Volume);
        l_Group->Desired.push_back(&l_Group->Volume);
        static_cast<void>(ma_node_attach_output_bus(&l_Group->Attenuation.Base, 0, &l_Group->Output.Base, 0));
        static_cast<void>(ma_node_attach_output_bus(l_Group->Sound, 0, &l_Group->Attenuation.Base, 0));
        static_cast<void>(ma_node_attach_output_bus(&l_Group->Direct.Base, 0, l_Group->Sound, 0));

        return l_Group;
    }

    // Whatever plays into it is let go with it, its effects first
    void AudioMixer::State::DestroyGroup(Group* group)
    {
        for (Unit* it_Unit : group->Effects)
        {
            DestroyEffect(it_Unit);
        }

        if (group->HasDirect)
        {
            ma_node_uninit(&group->Direct.Base, &TheEngine.allocationCallbacks);
        }

        if (group->HasSound)
        {
            ma_sound_group_uninit(group->Sound);
            Memory::Delete(group->Sound);
        }

        if (group->HasAttenuation)
        {
            ma_node_uninit(&group->Attenuation.Base, &TheEngine.allocationCallbacks);
        }

        if (group->HasOutput)
        {
            ma_node_uninit(&group->Output.Base, &TheEngine.allocationCallbacks);
        }

        Memory::Delete(group);
    }

    // Wired to nothing and fully dry, at its values with no glide, its filter's state, its echo's delay and its Limiter's window all made here on the main thread, so the audio thread allocates nothing for it. Only running out of memory fails, which Memory treats the same way
    Unit* AudioMixer::State::CreateEffect(const AudioEffect& effect)
    {
        const std::uint32_t l_Rate = ma_engine_get_sample_rate(&TheEngine);
        EffectNode* l_Node = Memory::New<EffectNode>(MemoryTag::Audio);
        l_Node->Type = effect.Type;
        l_Node->Channels = ma_engine_get_channels(&TheEngine);
        l_Node->SampleRate = l_Rate;
        l_Node->FadeFrames = FramesFor(static_cast<std::uint64_t>(c_FadeMilliseconds) * 1000, l_Rate);
        l_Node->RampFrames = FramesFor(static_cast<std::uint64_t>(c_RampMilliseconds) * 1000, l_Rate);
        l_Node->ValueCount = static_cast<std::uint32_t>(GetAudioEffectInfo(effect.Type).Parameters.size());
        for (std::uint32_t it_Value = 0; it_Value < l_Node->ValueCount; ++it_Value)
        {
            l_Node->Values[it_Value] = ClampValue(effect, it_Value);
            l_Node->RampValues[it_Value] = l_Node->Values[it_Value];
            l_Node->TargetValues[it_Value].store(l_Node->Values[it_Value], std::memory_order_relaxed);
        }

        Unit* l_Unit = Memory::New<Unit>(MemoryTag::Audio);
        l_Unit->ID = effect.ID;
        l_Unit->Type = effect.Type;
        l_Unit->Effect = l_Node;

        bool l_Made = true;
        switch (effect.Type)
        {
            case AudioEffectType::Echo:
            {
                l_Unit->Delay = EchoFrames(effect, l_Rate);
                ma_delay_config l_Config = ma_delay_config_init(l_Node->Channels, l_Rate, l_Unit->Delay, l_Node->Values[1] / 100.0f);
                // The first echo a whole delay after what it echoes, at its full level, and each after it lower by the decay
                l_Config.delayStart = MA_TRUE;
                l_Made = ma_delay_init(&l_Config, &TheEngine.allocationCallbacks, &l_Node->Filters.Delay) == MA_SUCCESS;
                l_Node->HasFilter = l_Made;

                break;
            }
            case AudioEffectType::Compressor:
            case AudioEffectType::Receive:
            case AudioEffectType::DuckVolume:
            {
                break;
            }
            case AudioEffectType::Send:
            {
                l_Unit->Target = effect.Target;

                break;
            }
            case AudioEffectType::SFXReverb:
            {
                l_Unit->Density = ClampValue(effect, 10);
                l_Node->Reverb = CreateReverb(l_Rate, l_Unit->Density);
                ConfigureReverb(*l_Node);

                break;
            }
            case AudioEffectType::Limiter:
            {
                l_Node->Window = FramesFor(c_LimiterLookaheadMicroseconds, l_Rate) + 1;
                const std::size_t l_Floats = static_cast<std::size_t>(l_Node->Window) * (l_Node->Channels + 2);
                l_Node->Held = static_cast<float*>(Memory::Allocate(l_Floats * sizeof(float), MemoryTag::Audio, alignof(float)));
                l_Node->Needed = l_Node->Held + static_cast<std::size_t>(l_Node->Window) * l_Node->Channels;
                l_Node->Smoothed = l_Node->Needed + l_Node->Window;
                std::fill(l_Node->Held, l_Node->Needed, 0.0f);
                std::fill(l_Node->Needed, l_Node->Held + l_Floats, 1.0f);
                l_Node->SmoothedSum = static_cast<double>(l_Node->Window);

                break;
            }
            default:
            {
                l_Made = ConfigureFilter(*l_Node, &TheEngine.allocationCallbacks) == MA_SUCCESS;
                l_Node->HasFilter = l_Made;

                break;
            }
        }

        const std::array<ma_uint32, 2> l_Channels{ l_Node->Channels, l_Node->Channels };
        ma_node_config l_Config = ma_node_config_init();
        l_Config.vtable = effect.Type == AudioEffectType::Send ? &c_SendVTable : (TakesSends(effect.Type) ? &c_ReturnVTable : &c_EffectVTable);
        l_Config.pInputChannels = l_Channels.data();
        l_Config.pOutputChannels = l_Channels.data();
        if (!l_Made || ma_node_init(ma_engine_get_node_graph(&TheEngine), &l_Config, &TheEngine.allocationCallbacks, &l_Node->Base) != MA_SUCCESS)
        {
            TR_CORE_CRITICAL("Audio: a mixer effect could not be made");
            DestroyEffect(l_Unit);

            throw std::bad_alloc();
        }

        return l_Unit;
    }

    // Out of the graph first, which miniaudio waits for the audio thread to let go of, with what is sent to it, then its state. One that never got as far as the graph has only its state
    void AudioMixer::State::DestroyEffect(Unit* unit)
    {
        EffectNode* l_Node = unit->Effect;
        if (ma_node_get_node_graph(&l_Node->Base) != nullptr)
        {
            ma_node_uninit(&l_Node->Base, &TheEngine.allocationCallbacks);
        }

        if (l_Node->HasFilter)
        {
            switch (l_Node->Type)
            {
                case AudioEffectType::Lowpass: ma_lpf2_uninit(&l_Node->Filters.Lowpass, &TheEngine.allocationCallbacks); break;
                case AudioEffectType::Highpass: ma_hpf2_uninit(&l_Node->Filters.Highpass, &TheEngine.allocationCallbacks); break;
                case AudioEffectType::Bandpass: ma_bpf2_uninit(&l_Node->Filters.Bandpass, &TheEngine.allocationCallbacks); break;
                case AudioEffectType::Notch: ma_notch2_uninit(&l_Node->Filters.Notch, &TheEngine.allocationCallbacks); break;
                case AudioEffectType::ParamEQ: ma_peak2_uninit(&l_Node->Filters.Peak, &TheEngine.allocationCallbacks); break;
                case AudioEffectType::LowShelf: ma_loshelf2_uninit(&l_Node->Filters.LowShelf, &TheEngine.allocationCallbacks); break;
                case AudioEffectType::HighShelf: ma_hishelf2_uninit(&l_Node->Filters.HighShelf, &TheEngine.allocationCallbacks); break;
                case AudioEffectType::Echo: ma_delay_uninit(&l_Node->Filters.Delay, &TheEngine.allocationCallbacks); break;
                default: break;
            }
        }

        if (l_Node->Reverb != nullptr)
        {
            DestroyReverb(l_Node->Reverb);
        }

        // A Send that played into it was let go as it left the graph. As the mixer goes, every group goes with it, so none is left to tell
        for (auto it_Group = Closing ? Groups.end() : Groups.begin(); it_Group != Groups.end(); ++it_Group)
        {
            for (Unit* it_Send : it_Group->second->Effects)
            {
                if (it_Send->SentTo == unit)
                {
                    it_Send->SentTo = nullptr;
                }
            }
        }

        Memory::Free(l_Node->Held);
        Memory::Delete(l_Node);
        Memory::Delete(unit);
    }

    // The chain the data wants, each effect found again by its ID and type, so a playing one keeps its state, and made anew when it is new, of another type, an Echo of another delay or an SFX Reverb of another density, which need delays of their own, or a Send to another target, so the old one's sends fade out of its target as the new one's fade into the other. Attenuation comes last when the data leaves it out. An effect made and let go again before it ever stepped in goes at once
    void AudioMixer::State::SetDesired(Group& group, const AudioMixerGroup& data)
    {
        const std::uint32_t l_Rate = ma_engine_get_sample_rate(&TheEngine);
        UnitList l_Desired;
        for (const AudioEffect& it_Effect : data.Effects)
        {
            if (it_Effect.Type == AudioEffectType::Attenuation)
            {
                l_Desired.push_back(&group.Volume);

                continue;
            }

            const auto a_Found = std::ranges::find_if(group.Desired, [&it_Effect](const Unit* unit) { return unit->Effect != nullptr && unit->ID == it_Effect.ID; });
            Unit* l_Unit = a_Found != group.Desired.end() ? *a_Found : nullptr;
            const bool l_Remade = l_Unit != nullptr && ((it_Effect.Type == AudioEffectType::Echo && l_Unit->Delay != EchoFrames(it_Effect, l_Rate)) || (it_Effect.Type == AudioEffectType::SFXReverb && l_Unit->Density != ClampValue(it_Effect, 10)) || (it_Effect.Type == AudioEffectType::Send && l_Unit->Target != it_Effect.Target));
            if (l_Unit == nullptr || l_Unit->Type != it_Effect.Type || l_Remade)
            {
                l_Unit = CreateEffect(it_Effect);
                group.Effects.push_back(l_Unit);
            }

            for (std::uint32_t it_Value = 0; it_Value < l_Unit->Effect->ValueCount; ++it_Value)
            {
                l_Unit->Effect->TargetValues[it_Value].store(ClampValue(it_Effect, it_Value), std::memory_order_relaxed);
            }

            l_Unit->Wet = GetWet(data, it_Effect);
            l_Desired.push_back(l_Unit);
        }

        if (!Contains(l_Desired, &group.Volume))
        {
            l_Desired.push_back(&group.Volume);
        }

        for (auto it_Unit = group.Effects.begin(); it_Unit != group.Effects.end();)
        {
            if (!Contains(l_Desired, *it_Unit) && !Contains(group.Wired, *it_Unit))
            {
                DestroyEffect(*it_Unit);
                it_Unit = group.Effects.erase(it_Unit);
            }
            else
            {
                ++it_Unit;
            }
        }

        group.Desired = std::move(l_Desired);
        group.Pending = true;
        Converge(group);
    }

    // The effects outside the run that keeps its order cross to dry, and once all have, step out and in, and those that left go. A fresh group plays nothing yet, so its chain is wired at once with every wet where it is to be
    void AudioMixer::State::Converge(Group& group)
    {
        if (group.Fresh || group.Wired == group.Desired)
        {
            if (group.Wired != group.Desired)
            {
                Rewire(group, FindKept(group.Wired, group.Desired, &group.Volume));
            }

            for (Unit* it_Unit : group.Wired)
            {
                if (it_Unit->Effect != nullptr && group.Fresh)
                {
                    it_Unit->Effect->Wet = it_Unit->Wet;
                    it_Unit->Effect->RampWet = it_Unit->Wet;
                    it_Unit->Effect->Feed = it_Unit->Wet > 0.0f ? 1.0f : 0.0f;
                    it_Unit->Effect->RampFeed = it_Unit->Effect->Feed;
                }

                if (it_Unit->Effect != nullptr)
                {
                    SetWet(*it_Unit->Effect, it_Unit->Wet);
                }
            }

            Settle(group);

            return;
        }

        const UnitList l_Kept = FindKept(group.Wired, group.Desired, &group.Volume);
        bool l_Dry = true;
        for (Unit* it_Unit : group.Wired)
        {
            if (it_Unit->Effect == nullptr)
            {
                continue;
            }

            const bool l_Stays = Contains(l_Kept, it_Unit);
            SetWet(*it_Unit->Effect, l_Stays ? it_Unit->Wet : 0.0f);
            l_Dry = l_Dry && (l_Stays || IsDry(*it_Unit->Effect));
        }

        if (!l_Dry)
        {
            return;
        }

        Rewire(group, l_Kept);
        for (Unit* it_Unit : group.Wired)
        {
            if (it_Unit->Effect != nullptr)
            {
                SetWet(*it_Unit->Effect, it_Unit->Wet);
            }
        }

        Settle(group);
    }

    // A chain back to Attenuation alone gives back what its lists grew to, so Audio reads as it did before its first effect
    void AudioMixer::State::Settle(Group& group)
    {
        group.Pending = false;
        if (group.Effects.empty() && (group.Effects.capacity() > 0 || group.Wired.capacity() > 1 || group.Desired.capacity() > 1))
        {
            UnitList().swap(group.Effects);
            UnitList(1, &group.Volume).swap(group.Wired);
            UnitList(1, &group.Volume).swap(group.Desired);
        }
    }

    // Each step leaves one path from the sound group to the output through every kept unit and no loop, so the audio thread hears the chain whole whenever it runs between them: a unit steps out by joining the one before it to the one after it, then leaving, and steps in by playing into the one after its place, then taking the one before it. Every unit stepping is dry, and passes on exactly what comes in, so none of this is heard. Those the data no longer has go once out
    void AudioMixer::State::Rewire(Group& group, const UnitList& kept)
    {
        const auto a_Before = [&group](std::size_t index) { return index == 0 ? static_cast<ma_node*>(group.Sound) : group.Wired[index - 1]->GetNode(); };
        const auto a_After = [&group](std::size_t index) { return index == group.Wired.size() ? static_cast<ma_node*>(&group.Output.Base) : group.Wired[index]->GetNode(); };
        for (std::size_t it_Index = 0; it_Index < group.Wired.size();)
        {
            Unit* l_Unit = group.Wired[it_Index];
            if (Contains(kept, l_Unit))
            {
                ++it_Index;

                continue;
            }

            static_cast<void>(ma_node_attach_output_bus(a_Before(it_Index), 0, a_After(it_Index + 1), 0));
            static_cast<void>(ma_node_detach_output_bus(l_Unit->GetNode(), 0));
            group.Wired.erase(group.Wired.begin() + static_cast<std::ptrdiff_t>(it_Index));
        }

        for (std::size_t it_Index = 0; it_Index < group.Desired.size(); ++it_Index)
        {
            Unit* l_Unit = group.Desired[it_Index];
            if (Contains(kept, l_Unit))
            {
                continue;
            }

            const std::size_t l_At = it_Index == 0 ? 0 : static_cast<std::size_t>(std::ranges::find(group.Wired, group.Desired[it_Index - 1]) - group.Wired.begin()) + 1;
            static_cast<void>(ma_node_attach_output_bus(l_Unit->GetNode(), 0, a_After(l_At), 0));
            static_cast<void>(ma_node_attach_output_bus(a_Before(l_At), 0, l_Unit->GetNode(), 0));
            group.Wired.insert(group.Wired.begin() + static_cast<std::ptrdiff_t>(l_At), l_Unit);
        }

        for (auto it_Unit = group.Effects.begin(); it_Unit != group.Effects.end();)
        {
            if (!Contains(group.Wired, *it_Unit))
            {
                DestroyEffect(*it_Unit);
                it_Unit = group.Effects.erase(it_Unit);
            }
            else
            {
                ++it_Unit;
            }
        }
    }

    // The Receive or Duck Volume a Send names: the one in a chain while there is one, as one taking another's place does not step in until the other has left, and otherwise the one waiting to step in
    Unit* AudioMixer::State::FindTarget(UUID id) const
    {
        Unit* l_Waiting = nullptr;
        for (const auto& [it_ID, it_Group] : Groups)
        {
            for (Unit* it_Unit : it_Group->Wired)
            {
                if (it_Unit->Effect != nullptr && it_Unit->ID == id && TakesSends(it_Unit->Type))
                {
                    return it_Unit;
                }
            }

            for (Unit* it_Unit : it_Group->Desired)
            {
                if (it_Unit->Effect != nullptr && it_Unit->ID == id && TakesSends(it_Unit->Type))
                {
                    l_Waiting = it_Unit;
                }
            }
        }

        return id ? l_Waiting : nullptr;
    }

    const AudioMixer::State::Group* AudioMixer::State::FindGroupOf(const Unit* unit) const
    {
        for (const auto& [it_ID, it_Group] : Groups)
        {
            if (Contains(it_Group->Effects, unit))
            {
                return it_Group;
            }
        }

        return nullptr;
    }

    // Before any group changes its parent, a Send stops sending where a walk through the parents old and new and the Sends kept before it would come back to its group, so no step of the change has a loop, through which miniaudio would read forever. One sending into a group that is going stops too. Data that keeps to its rules loses none of its own Sends here, only those left over from the mix before it that the new parents would loop
    void AudioMixer::State::LoosenSends(const AudioMixerData& data)
    {
        EdgeList l_Edges;
        for (const AudioMixerGroup& it_Group : data.Groups)
        {
            const Group* l_Group = Groups.at(it_Group.ID);
            if (it_Group.Parent)
            {
                l_Edges.emplace_back(l_Group, Groups.at(it_Group.Parent));
            }

            if (l_Group->Parent != nullptr)
            {
                l_Edges.emplace_back(l_Group, l_Group->Parent);
            }
        }

        for (const auto& [it_ID, it_Group] : Groups)
        {
            for (Unit* it_Send : it_Group->Effects)
            {
                if (it_Send->SentTo == nullptr)
                {
                    continue;
                }

                const Group* l_Target = FindGroupOf(it_Send->SentTo);
                if (l_Target == nullptr || Reaches(l_Edges, l_Target, it_Group))
                {
                    static_cast<void>(ma_node_detach_output_bus(&it_Send->Effect->Base, 1));
                    it_Send->SentTo = nullptr;
                }
                else
                {
                    l_Edges.emplace_back(it_Group, l_Target);
                }
            }
        }
    }

    // Each Send plays into the Receive or Duck Volume its target finds, unless that would make a loop through the parents and the Sends taken before it: Sends in their chains first, then those stepping in, then those leaving, so one leaving gives way. Every Send that is to move stops before any starts, so no step has a loop
    void AudioMixer::State::ConnectSends()
    {
        EdgeList l_Edges;
        for (const AudioMixerGroup& it_Group : Data.Groups)
        {
            if (it_Group.Parent)
            {
                l_Edges.emplace_back(Groups.at(it_Group.ID), Groups.at(it_Group.Parent));
            }
        }

        std::vector<std::pair<Unit*, Unit*>, TaggedAllocator<std::pair<Unit*, Unit*>, MemoryTag::Audio>> l_Wanted;
        for (int it_Pass = 0; it_Pass < 3; ++it_Pass)
        {
            for (const auto& [it_ID, it_Group] : Groups)
            {
                for (Unit* it_Send : it_Group->Effects)
                {
                    const bool l_Wired = Contains(it_Group->Wired, it_Send);
                    const bool l_Desired = Contains(it_Group->Desired, it_Send);
                    if (it_Send->Type != AudioEffectType::Send || (l_Wired && l_Desired ? 0 : (l_Desired ? 1 : 2)) != it_Pass)
                    {
                        continue;
                    }

                    Unit* l_Target = FindTarget(it_Send->Target);
                    const Group* l_TargetGroup = l_Target != nullptr ? FindGroupOf(l_Target) : nullptr;
                    if (l_TargetGroup != nullptr && Reaches(l_Edges, l_TargetGroup, it_Group))
                    {
                        l_Target = nullptr;
                    }
                    else if (l_TargetGroup != nullptr)
                    {
                        l_Edges.emplace_back(it_Group, l_TargetGroup);
                    }

                    l_Wanted.emplace_back(it_Send, l_Target);
                }
            }
        }

        for (const auto& [it_Send, it_Target] : l_Wanted)
        {
            if (it_Send->SentTo != nullptr && it_Send->SentTo != it_Target)
            {
                static_cast<void>(ma_node_detach_output_bus(&it_Send->Effect->Base, 1));
                it_Send->SentTo = nullptr;
            }
        }

        for (const auto& [it_Send, it_Target] : l_Wanted)
        {
            if (it_Target != nullptr && it_Send->SentTo != it_Target)
            {
                static_cast<void>(ma_node_attach_output_bus(&it_Send->Effect->Base, 1, &it_Target->Effect->Base, 1));
                it_Send->SentTo = it_Target;
            }
        }
    }

    AudioMixer::State::Group* AudioMixer::State::FindOrMaster(UUID id) const
    {
        const auto a_Found = id ? Groups.find(id) : Groups.end();

        return a_Found != Groups.end() ? a_Found->second : Master;
    }

    // As Unity's mixer has it: mute silences a group and so everything below it, and while any group is soloed, a group neither soloed nor below a soloed one is silent unless one below it is soloed, when it carries only that. A group made by this Apply takes its gains at once
    void AudioMixer::State::UpdateGains()
    {
        const bool l_AnySolo = std::ranges::any_of(Data.Groups, &AudioMixerGroup::Solo);
        for (const AudioMixerGroup& it_Group : Data.Groups)
        {
            bool l_Soloed = false;
            for (const AudioMixerGroup* it_Above = &it_Group; it_Above != nullptr && !l_Soloed; it_Above = Data.FindGroup(it_Above->Parent))
            {
                l_Soloed = it_Above->Solo;
            }

            bool l_Carries = false;
            for (const AudioMixerGroup& it_Other : Data.Groups)
            {
                for (const AudioMixerGroup* it_Above = it_Other.Solo ? Data.FindGroup(it_Other.Parent) : nullptr; it_Above != nullptr && !l_Carries; it_Above = Data.FindGroup(it_Above->Parent))
                {
                    l_Carries = it_Above->ID == it_Group.ID;
                }
            }

            const bool l_Silenced = it_Group.Mute || (l_AnySolo && !l_Soloed && !l_Carries);
            const float l_Output = l_Silenced ? 0.0f : 1.0f;
            const float l_Volume = AudioDecibelsToGain(it_Group.Volume);
            const float l_Direct = l_AnySolo && !l_Soloed ? 0.0f : 1.0f;
            Group& l_Group = *Groups.at(it_Group.ID);
            for (const auto& [it_Node, it_Gain] : { std::pair(&l_Group.Output, l_Output), std::pair(&l_Group.Attenuation, l_Volume), std::pair(&l_Group.Direct, l_Direct) })
            {
                if (l_Group.Fresh)
                {
                    it_Node->Gain = it_Gain;
                    it_Node->RampGain = it_Gain;
                }

                it_Node->TargetGain.store(it_Gain, std::memory_order_relaxed);
            }

            if (l_Group.Pitched != IsPitched(it_Group.Pitch))
            {
                SetPitched(l_Group, IsPitched(it_Group.Pitch));
            }

            ma_sound_group_set_pitch(l_Group.Sound, std::clamp(it_Group.Pitch, AudioMixerData::c_MinimumPitch, AudioMixerData::c_MaximumPitch));
        }
    }

    // Master alone, into the engine's output
    AudioMixer::AudioMixer(AudioEngine& engine) : m_State(Memory::New<State>(MemoryTag::Audio, engine))
    {
        State& l_State = *m_State;
        l_State.Data = AudioMixerData::MakeDefault();
        l_State.Master = l_State.CreateGroup(false);
        l_State.Groups.emplace(l_State.Data.GetMaster().ID, l_State.Master);
        l_State.UpdateGains();
        l_State.SetDesired(*l_State.Master, l_State.Data.GetMaster());
        l_State.Master->Fresh = false;
        static_cast<void>(ma_node_attach_output_bus(&l_State.Master->Output.Base, 0, &AudioEngineInternal::GetMaster(engine), 0));
    }

    AudioMixer::~AudioMixer()
    {
        Memory::Delete(m_State);
    }

    // The groups are made, kept and wired before any is let go, so a group that leaves takes nothing with it that stays: what was sent to it has moved to Master, and the groups below it that stay have moved to their new parents
    bool AudioMixer::Apply(const AudioMixerData& data)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A mixer changes on the main thread.");

        if (const std::string l_Error = ValidateAudioMixer(data); !l_Error.empty())
        {
            TR_CORE_ERROR("Audio: the mixer is left as it was, since {}", l_Error);

            return false;
        }

        State& l_State = *m_State;
        if (data == l_State.Data)
        {
            return true;
        }

        for (const auto& [it_ID, it_Group] : l_State.Groups)
        {
            it_Group->Kept = false;
        }

        State::GroupMap l_Groups;
        for (const AudioMixerGroup& it_Group : data.Groups)
        {
            State::Group* l_Group = l_State.Master;
            if (it_Group.Parent)
            {
                const auto a_Old = l_State.Groups.find(it_Group.ID);
                l_Group = a_Old != l_State.Groups.end() && a_Old->second != l_State.Master ? a_Old->second : l_State.CreateGroup(IsPitched(it_Group.Pitch));
            }

            l_Group->Kept = true;
            l_Groups.emplace(it_Group.ID, l_Group);
        }

        std::vector<State::Group*, TaggedAllocator<State::Group*, MemoryTag::Audio>> l_Gone;
        for (const auto& [it_ID, it_Group] : l_State.Groups)
        {
            if (!it_Group->Kept)
            {
                l_Gone.push_back(it_Group);
            }
        }

        // A copy of its own, so a smaller mixer lets go of what a larger one held
        l_State.Groups = std::move(l_Groups);
        l_State.Data = AudioMixerData(data);
        l_State.UpdateGains();
        for (const AudioMixerGroup& it_Group : data.Groups)
        {
            State::Group& l_Group = *l_State.Groups.at(it_Group.ID);
            l_State.SetDesired(l_Group, it_Group);
            l_Group.Fresh = false;
        }

        // A group that is going sends nothing more, and a Send that would loop through the parents old or new stops, before any group changes its parent
        for (State::Group* it_Group : l_Gone)
        {
            for (Unit* it_Send : it_Group->Effects)
            {
                if (it_Send->SentTo != nullptr)
                {
                    static_cast<void>(ma_node_detach_output_bus(&it_Send->Effect->Base, 1));
                    it_Send->SentTo = nullptr;
                }
            }
        }

        l_State.LoosenSends(data);
        for (const AudioMixerGroup& it_Group : data.Groups)
        {
            State::Group& l_Group = *l_State.Groups.at(it_Group.ID);
            State::Group* l_Parent = it_Group.Parent ? l_State.Groups.at(it_Group.Parent) : nullptr;
            if (l_Parent != nullptr && l_Group.Parent != l_Parent)
            {
                static_cast<void>(ma_node_attach_output_bus(&l_Group.Output.Base, 0, l_Parent->Sound, 0));
                l_Group.Parent = l_Parent;
            }
        }

        for (auto& [it_Node, it_Route] : l_State.Routes)
        {
            State::Group* l_Target = l_State.FindOrMaster(it_Route.GroupID);
            if (l_Target != it_Route.Target)
            {
                static_cast<void>(ma_node_attach_output_bus(static_cast<ma_node*>(it_Node), 0, &l_Target->Direct.Base, 0));
                it_Route.Target = l_Target;
            }
        }

        l_State.ConnectSends();
        for (State::Group* it_Group : l_Gone)
        {
            l_State.DestroyGroup(it_Group);
        }

        return true;
    }

    // Allocates nothing while no chain is changing. Once one has, its Sends move to the units now in their targets' chains
    void AudioMixer::Update()
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A mixer changes on the main thread.");

        bool l_Changing = false;
        for (const auto& [it_ID, it_Group] : m_State->Groups)
        {
            if (it_Group->Pending)
            {
                m_State->Converge(*it_Group);
                l_Changing = true;
            }
        }

        if (l_Changing)
        {
            m_State->ConnectSends();
        }
    }

    const AudioMixerData& AudioMixer::GetData() const
    {
        return m_State->Data;
    }

    std::optional<AudioMeter> AudioMixer::GetMeter(UUID group) const
    {
        const auto a_Found = group ? m_State->Groups.find(group) : m_State->Groups.end();
        if (a_Found == m_State->Groups.end())
        {
            return std::nullopt;
        }

        const GainNode& l_Output = a_Found->second->Output;
        AudioMeter l_Meter;
        l_Meter.Channels = std::min<std::uint32_t>(l_Output.Channels, AudioMeter::c_MaxChannels);
        for (std::uint32_t it_Channel = 0; it_Channel < l_Meter.Channels; ++it_Channel)
        {
            l_Meter.Peak[it_Channel] = l_Output.PublishedPeak[it_Channel].load(std::memory_order_relaxed);
            l_Meter.Rms[it_Channel] = std::sqrt(std::max(l_Output.PublishedMeanSquare[it_Channel].load(std::memory_order_relaxed), 0.0f));
        }

        return l_Meter;
    }

    void AudioMixer::Route(void* node, UUID group)
    {
        State& l_State = *m_State;
        State::Group* l_Target = l_State.FindOrMaster(group);
        const auto [a_Route, a_New] = l_State.Routes.try_emplace(node);
        a_Route->second.GroupID = group;
        if (a_New || a_Route->second.Target != l_Target)
        {
            static_cast<void>(ma_node_attach_output_bus(static_cast<ma_node*>(node), 0, &l_Target->Direct.Base, 0));
            a_Route->second.Target = l_Target;
        }
    }

    // An emptied map keeps its buckets, so it is replaced, and Audio reads as it did before the first sound
    void AudioMixer::Unroute(void* node)
    {
        State& l_State = *m_State;
        l_State.Routes.erase(node);
        if (l_State.Routes.empty())
        {
            State::RouteMap().swap(l_State.Routes);
        }
    }
}