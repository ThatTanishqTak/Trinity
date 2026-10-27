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
#include <limits>
#include <new>
#include <numbers>
#include <span>
#include <string_view>
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
        constexpr std::size_t c_CurveCount = 6;

        // How a change that would step a value moves it instead: evenly in its own units, evenly in gain for a group's volume in decibels, as its fader always ramped, or not at all for a value that makes its effect anew
        enum class Glide : std::uint8_t
        {
            Even,
            Gain,
            None
        };

        // A value as the mixer plays it over the engine's clock: from where it was as its transition started, towards To, each snapshot's share of the way along that snapshot's curve, so by any frame it has moved only as far as its curves have, and at To once the transition is over. A change that would step it glides from where it was instead, over the ramp. Made on the main thread and played the same on the audio thread, so both agree on it at every frame
        struct Lane
        {
            float From = 0.0f;
            float To = 0.0f;
            // How far each curve takes it, weighted, in the curves' order
            std::array<float, c_CurveCount> Moves{};
            std::uint64_t Start = 0;
            std::uint32_t Length = 0;
            float GlideFrom = 0.0f;
            std::uint64_t GlideStart = 0;
            std::uint32_t GlideLength = 0;
            bool GlidesInGain = false;

            [[nodiscard]] bool operator==(const Lane&) const = default;
        };

        [[nodiscard]] Lane Constant(float value)
        {
            Lane l_Lane;
            l_Lane.From = value;
            l_Lane.To = value;

            return l_Lane;
        }

        // The value at a frame of the engine's clock
        [[nodiscard]] float Evaluate(const Lane& lane, std::uint64_t frame)
        {
            float l_Value = lane.To;
            if (lane.Length > 0 && frame < lane.Start + lane.Length)
            {
                const float l_Progress = frame <= lane.Start ? 0.0f : static_cast<float>(static_cast<double>(frame - lane.Start) / static_cast<double>(lane.Length));
                l_Value = lane.From;
                for (std::size_t it_Curve = 0; it_Curve < c_CurveCount; ++it_Curve)
                {
                    l_Value += lane.Moves[it_Curve] == 0.0f ? 0.0f : lane.Moves[it_Curve] * EvaluateAudioTransitionCurve(static_cast<AudioTransitionCurve>(it_Curve), l_Progress);
                }
            }

            if (lane.GlideLength == 0 || frame >= lane.GlideStart + lane.GlideLength)
            {
                return l_Value;
            }

            if (frame <= lane.GlideStart)
            {
                return lane.GlideFrom;
            }

            const float l_Share = static_cast<float>(frame - lane.GlideStart) / static_cast<float>(lane.GlideLength);
            if (!lane.GlidesInGain)
            {
                return lane.GlideFrom + (l_Value - lane.GlideFrom) * l_Share;
            }

            const float l_From = AudioDecibelsToGain(lane.GlideFrom);

            return AudioGainToDecibels(l_From + (AudioDecibelsToGain(l_Value) - l_From) * l_Share);
        }

        // The frame from which it stays at To
        [[nodiscard]] std::uint64_t SettlesAt(const Lane& lane)
        {
            return std::max(lane.Start + lane.Length, lane.GlideStart + lane.GlideLength);
        }

        // The latest of what the main thread sends the audio thread, through three copies, so neither ever waits: the main thread fills its own and swaps it into the middle, and the audio thread swaps the middle for its own whenever a newer one is there
        template<typename T>
        struct Mailbox
        {
            static constexpr std::uint8_t c_Fresh = 4;

            std::array<T, 3> Copies{};
            std::atomic<std::uint8_t> Middle{ 1 };
            // The main thread's
            std::uint8_t Back = 0;
            // The audio thread's, with how many newer ones it has taken, so each node that reads it knows when it changed
            std::uint8_t Front = 2;
            std::uint32_t Taken = 0;

            // Main thread, before any node that reads it is in the graph
            void Reset(const T& value)
            {
                Copies.fill(value);
            }

            // Main thread
            void Send(const T& value)
            {
                Copies[Back] = value;
                Back = static_cast<std::uint8_t>(Middle.exchange(static_cast<std::uint8_t>(Back | c_Fresh), std::memory_order_acq_rel) & 3);
            }

            // Audio thread
            void Receive()
            {
                if ((Middle.load(std::memory_order_relaxed) & c_Fresh) != 0)
                {
                    Front = static_cast<std::uint8_t>(Middle.exchange(Front, std::memory_order_acq_rel) & 3);
                    ++Taken;
                }
            }

            [[nodiscard]] const T& Get() const
            {
                return Copies[Front];
            }
        };

        // Where a node's frames are on the engine's clock: the graph's time is where the read the node is part of starts, and a node read more than once in it, as one below a group pitched above 1 is, carries on from where it left off
        struct NodeClock
        {
            std::uint64_t Read = std::numeric_limits<std::uint64_t>::max();
            std::uint64_t Offset = 0;

            [[nodiscard]] std::uint64_t Now(const ma_node* node)
            {
                const std::uint64_t l_Read = ma_node_graph_get_time(ma_node_get_node_graph(node));
                if (l_Read != Read)
                {
                    Read = l_Read;
                    Offset = 0;
                }

                return Read + Offset;
            }

            void Pass(std::uint32_t frames)
            {
                Offset += frames;
            }
        };

        // A group's Volume, in decibels, and Pitch as the audio thread plays them
        using GroupLanes = std::array<Lane, 2>;
        using GroupMailbox = Mailbox<GroupLanes>;
        // An effect's Wet, in percent, then its values
        using EffectLanes = std::array<Lane, AudioMixerValue::c_MaxSlots>;

        // A gain moved from where it is to the main thread's latest over the mixer's ramp, each sample an even step of what is left, and, for a group's output, the meter of what leaves it. A group's Attenuation instead plays the volume its group's lanes give it
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
            // An Attenuation's: its group's lanes, the newest of them it has taken, and the frame from which its volume stays put, which it has reached once Settled
            GroupMailbox* Lanes = nullptr;
            NodeClock Clock;
            std::uint32_t Taken = std::numeric_limits<std::uint32_t>::max();
            std::uint64_t SettledAt = 0;
            bool Settled = false;
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

        // On the audio thread. The volume where its lane is at the end of each step, ramped to evenly from where the step before left it, until the lane settles
        void ProcessAttenuation(ma_node* node, const float** framesIn, [[maybe_unused]] ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
        {
            GainNode& l_Node = *reinterpret_cast<GainNode*>(node);
            GroupMailbox& l_Lanes = *l_Node.Lanes;
            l_Lanes.Receive();
            if (l_Lanes.Taken != l_Node.Taken)
            {
                l_Node.Taken = l_Lanes.Taken;
                l_Node.SettledAt = SettlesAt(l_Lanes.Get()[AudioMixerValue::c_Volume]);
                l_Node.Settled = false;
            }

            const ma_uint32 l_Frames = *frameCountOut;
            const std::uint64_t l_Clock = l_Node.Clock.Now(node);
            l_Node.Clock.Pass(l_Frames);
            const std::uint32_t l_Channels = l_Node.Channels;
            const float* l_In = framesIn[0];
            float* l_Out = framesOut[0];
            for (ma_uint32 it_Start = 0; it_Start < l_Frames; it_Start += c_GlideFrames)
            {
                const ma_uint32 l_Run = std::min(c_GlideFrames, l_Frames - it_Start);
                const float l_From = l_Node.Gain;
                if (!l_Node.Settled)
                {
                    const std::uint64_t l_End = l_Clock + it_Start + l_Run;
                    l_Node.Gain = AudioDecibelsToGain(Evaluate(l_Lanes.Get()[AudioMixerValue::c_Volume], l_End));
                    l_Node.Settled = l_End >= l_Node.SettledAt;
                }

                for (ma_uint32 it_Frame = 0; it_Frame < l_Run; ++it_Frame)
                {
                    const float l_Gain = l_From + (l_Node.Gain - l_From) * static_cast<float>(it_Frame + 1) / static_cast<float>(l_Run);
                    const std::size_t l_Base = static_cast<std::size_t>(it_Start + it_Frame) * l_Channels;
                    for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                    {
                        l_Out[l_Base + it_Channel] = l_In[l_Base + it_Channel] * l_Gain;
                    }
                }
            }
        }

        // Each runs whether or not anything plays into it, given silence: a group's output so its meter falls to nothing, a direct input so its group's resampler, when it has one, is always fed and never holds back a sample from before a silence, and an effect so its echoes and its filter's ringing carry on into a silence and its fades end
        constexpr ma_node_vtable c_GainVTable{ &ProcessGain, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING };
        constexpr ma_node_vtable c_AttenuationVTable{ &ProcessAttenuation, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING };

        // A group's sound group: miniaudio's, its process put behind one that sets its pitch from the group's lanes as each block starts, so its pitch moves with the clock as its volume does. miniaudio reads the pitch as its process starts, once its input is in
        struct PitchedSound
        {
            ma_sound_group Sound{};
            GroupMailbox* Lanes = nullptr;
            NodeClock Clock;
            ma_node_vtable VTable{};
            decltype(ma_node_vtable::onProcess) Process = nullptr;
        };

        static_assert(std::is_standard_layout_v<PitchedSound>, "miniaudio reaches the node through its base, its first member.");

        // On the audio thread
        void ProcessPitchedSound(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
        {
            PitchedSound& l_Sound = *reinterpret_cast<PitchedSound*>(node);
            l_Sound.Lanes->Receive();
            const float l_Pitch = Evaluate(l_Sound.Lanes->Get()[AudioMixerValue::c_Pitch], l_Sound.Clock.Now(node));
            ma_sound_group_set_pitch(&l_Sound.Sound, std::clamp(l_Pitch, AudioMixerData::c_MinimumPitch, AudioMixerData::c_MaximumPitch));
            l_Sound.Process(node, framesIn, frameCountIn, framesOut, frameCountOut);
            l_Sound.Clock.Pass(*frameCountOut);
        }

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

        // An effect of a group's chain, which crosses from what comes into it to what it makes by its gate and its Wet together, and plays its Wet and values as its lanes have them, a step at a time
        struct EffectNode
        {
            ma_node_base Base{};
            AudioEffectType Type = AudioEffectType::Lowpass;
            std::uint32_t Channels = 0;
            std::uint32_t SampleRate = 0;
            std::uint32_t FadeFrames = 1;
            std::uint32_t ValueCount = 0;
            // Set by the main thread and read by the audio thread each block: the gate, 1 while it plays, and 0 while it is bypassed or crosses to dry to step out of its chain or into it, with Asked counted on with each new one, so a fade's end is known to be the latest's, and its lanes
            std::atomic<float> TargetGate{ 0.0f };
            std::atomic<std::uint32_t> Asked{ 0 };
            Mailbox<EffectLanes> Lanes;
            // The audio thread's alone once the node is in the graph: the gate's fade, each sample an even step of what is left of it, the Wet, from 0 to 1, and the values where the lanes were at the end of the last step, the newest lanes taken, and the frame from which they stay put, which they have reached once Settled
            float Gate = 0.0f;
            float RampGate = 0.0f;
            std::uint32_t GateLeft = 0;
            float Mix = 0.0f;
            std::array<float, AudioEffect::c_MaxParameters> Values{};
            NodeClock Clock;
            std::uint32_t Taken = std::numeric_limits<std::uint32_t>::max();
            std::uint64_t SettledAt = 0;
            bool Settled = false;
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
            // Written by the audio thread after each block, with the count of the gate it started from, so the main thread sees a fade to dry end
            std::atomic<float> PublishedGate{ 0.0f };
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

        // On the audio thread, which allocates nothing here. A new gate starts a fade from where it is, which ends exactly on it, and the Wet and values follow their lanes, the Wet evenly across each step and the values a step at a time, its filter's coefficients worked out again for each step they move. With the gate or the Wet at 0 what comes in leaves exactly, so an effect fully faded can be stepped out of its chain or into it with no change to the sound
        void ProcessEffect(ma_node* node, const float** framesIn, [[maybe_unused]] ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
        {
            EffectNode& l_Node = *reinterpret_cast<EffectNode*>(node);
            const std::uint32_t l_Asked = l_Node.Asked.load(std::memory_order_acquire);
            const float l_TargetGate = l_Node.TargetGate.load(std::memory_order_relaxed);
            if (l_TargetGate != l_Node.RampGate)
            {
                l_Node.RampGate = l_TargetGate;
                l_Node.GateLeft = l_Node.FadeFrames;
            }

            l_Node.Lanes.Receive();
            if (l_Node.Lanes.Taken != l_Node.Taken)
            {
                l_Node.Taken = l_Node.Lanes.Taken;
                l_Node.SettledAt = 0;
                for (std::uint32_t it_Slot = 0; it_Slot <= l_Node.ValueCount; ++it_Slot)
                {
                    l_Node.SettledAt = std::max(l_Node.SettledAt, SettlesAt(l_Node.Lanes.Get()[it_Slot]));
                }

                l_Node.Settled = false;
            }

            const ma_uint32 l_Frames = *frameCountOut;
            const std::uint64_t l_Clock = l_Node.Clock.Now(node);
            l_Node.Clock.Pass(l_Frames);
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
                const float l_MixFrom = l_Node.Mix;
                if (!l_Node.Settled)
                {
                    const std::uint64_t l_End = l_Clock + it_Start + l_Run;
                    const EffectLanes& l_Lanes = l_Node.Lanes.Get();
                    l_Node.Mix = std::clamp(Evaluate(l_Lanes[AudioMixerValue::c_Wet], l_End) / 100.0f, 0.0f, 1.0f);
                    bool l_Moved = false;
                    for (std::uint32_t it_Value = 0; it_Value < l_Node.ValueCount; ++it_Value)
                    {
                        const float l_Value = Evaluate(l_Lanes[it_Value + 1], l_End);
                        l_Moved |= l_Value != l_Node.Values[it_Value];
                        l_Node.Values[it_Value] = l_Value;
                    }

                    if (l_Moved)
                    {
                        static_cast<void>(ConfigureFilter(l_Node, nullptr));
                    }

                    l_Node.Settled = l_End >= l_Node.SettledAt;
                }

                const float l_TargetFeed = l_Node.RampGate > 0.0f && l_Node.Mix > 0.0f ? 1.0f : 0.0f;
                if (l_TargetFeed != l_Node.RampFeed)
                {
                    l_Node.RampFeed = l_TargetFeed;
                    l_Node.FeedLeft = l_Node.FadeFrames;
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
                    if (l_Node.GateLeft > 0)
                    {
                        l_Node.Gate += (l_Node.RampGate - l_Node.Gate) / static_cast<float>(l_Node.GateLeft);
                        if (--l_Node.GateLeft == 0)
                        {
                            l_Node.Gate = l_Node.RampGate;
                        }
                    }

                    const float l_Mix = l_MixFrom + (l_Node.Mix - l_MixFrom) * static_cast<float>(it_Frame + 1) / static_cast<float>(l_Run);
                    const float l_Wet = l_Node.Gate * l_Mix;
                    const std::size_t l_Base = static_cast<std::size_t>(it_Frame) * l_Channels;
                    for (std::uint32_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                    {
                        const float l_Dry = l_RunIn[l_Base + it_Channel];
                        l_RunOut[l_Base + it_Channel] = l_Wet == 0.0f ? l_Dry : l_Dry + l_Wet * (l_RunOut[l_Base + it_Channel] - l_Dry);
                    }

                    // A Send's level and wet mix on what it sends alone, as what it passes on is what comes in
                    for (std::uint32_t it_Channel = 0; l_RunSent != nullptr && it_Channel < l_Channels; ++it_Channel)
                    {
                        l_RunSent[l_Base + it_Channel] = l_RunIn[l_Base + it_Channel] * l_SendGain * l_Wet;
                    }
                }
            }

            l_Node.PublishedGate.store(l_Node.Gate, std::memory_order_relaxed);
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

        // A pitch lane that moves from now on, or stays at any pitch but 1
        bool IsPitched(const Lane& pitch, std::uint64_t now)
        {
            const bool l_Moves = SettlesAt(pitch) > now && (pitch.From != pitch.To || std::ranges::any_of(pitch.Moves, [](float move) { return move != 0.0f; }) || (pitch.GlideLength > 0 && pitch.GlideFrom != pitch.To));

            return l_Moves || IsPitched(Evaluate(pitch, now));
        }

        // An Echo's Delay and an SFX Reverb's Density, a change of which makes the effect anew, so a transition changes them once, as it ends, and they never glide
        [[nodiscard]] Glide GetEffectGlide(AudioEffectType type, std::size_t slot)
        {
            return (type == AudioEffectType::Echo && slot == 1) || (type == AudioEffectType::SFXReverb && slot == 11) ? Glide::None : Glide::Even;
        }

        // Each value of every group and effect, with how it glides and the type of its effect, Attenuation for a group's
        template<typename Function>
        void ForEachValue(const AudioMixerData& data, Function function)
        {
            for (const AudioMixerGroup& it_Group : data.Groups)
            {
                function(AudioMixerValue{ it_Group.ID, AudioMixerValue::c_Volume }, Glide::Gain, AudioEffectType::Attenuation);
                function(AudioMixerValue{ it_Group.ID, AudioMixerValue::c_Pitch }, Glide::Even, AudioEffectType::Attenuation);
                for (const AudioEffect& it_Effect : it_Group.Effects)
                {
                    for (std::size_t it_Slot = 0; it_Effect.Type != AudioEffectType::Attenuation && it_Slot <= GetAudioEffectInfo(it_Effect.Type).Parameters.size(); ++it_Slot)
                    {
                        function(AudioMixerValue{ it_Effect.ID, static_cast<std::uint8_t>(it_Slot) }, GetEffectGlide(it_Effect.Type, it_Slot), it_Effect.Type);
                    }
                }
            }
        }

        [[nodiscard]] std::uint32_t FramesFor(std::uint64_t microseconds, std::uint32_t sampleRate)
        {
            return std::max<std::uint32_t>(static_cast<std::uint32_t>((microseconds * sampleRate + 500000) / 1000000), 1);
        }

        // A unit of a group's chain: its Attenuation, which is the group's own, or an effect, with what it was made with that a change of needs another made in its place, an Echo's delay, an SFX Reverb's density and a Send's target, and the gate it is to have once in its place. A Send's second output plays into the unit it was sent to
        struct Unit
        {
            UUID ID;
            UUID Target;
            AudioEffectType Type = AudioEffectType::Attenuation;
            GainNode* Gain = nullptr;
            EffectNode* Effect = nullptr;
            std::uint32_t Delay = 0;
            float Density = 0.0f;
            float Gate = 0.0f;
            Unit* SentTo = nullptr;

            [[nodiscard]] ma_node* GetNode() const
            {
                return Gain != nullptr ? static_cast<ma_node*>(&Gain->Base) : static_cast<ma_node*>(&Effect->Base);
            }
        };

        using UnitList = std::vector<Unit*, TaggedAllocator<Unit*, MemoryTag::Audio>>;

        // Set once for each new value, so a fade the main thread waits on is the latest
        void SetGate(EffectNode& node, float gate)
        {
            if (node.TargetGate.load(std::memory_order_relaxed) != gate)
            {
                node.TargetGate.store(gate, std::memory_order_relaxed);
                node.Asked.fetch_add(1, std::memory_order_release);
            }
        }

        // Crossed to dry by a block the audio thread started after the latest gate was set
        [[nodiscard]] bool IsDry(const EffectNode& node)
        {
            return node.TargetGate.load(std::memory_order_relaxed) == 0.0f && node.Seen.load(std::memory_order_acquire) == node.Asked.load(std::memory_order_relaxed) && node.PublishedGate.load(std::memory_order_relaxed) == 0.0f;
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
        [[nodiscard]] float GetGate(const AudioMixerGroup& group, const AudioEffect& effect)
        {
            return group.Bypass || effect.Bypass ? 0.0f : 1.0f;
        }

        // An Echo's delay as a whole number of frames, which is exactly how much later it repeats
        [[nodiscard]] std::uint32_t EchoFrames(float milliseconds, std::uint32_t sampleRate)
        {
            return FramesFor(static_cast<std::uint64_t>(std::llround(static_cast<double>(milliseconds) * 1000.0)), sampleRate);
        }

        // A value on the main thread: where the snapshots take it, what plays, which is that or what code set while it holds, and what code set
        struct ValueLanes
        {
            Lane Snapshot;
            Lane Played;
            float Set = 0.0f;
            bool IsSet = false;
        };

        // A group's values or an effect's, with the effect's type, Attenuation for a group, so an effect given another type under its ID starts its values anew
        struct OwnerLanes
        {
            AudioEffectType Type = AudioEffectType::Attenuation;
            std::array<ValueLanes, AudioMixerValue::c_MaxSlots> Values{};
        };
    }

    // A group as it plays: what is sent to it straight into its direct input, which solo silences while the group only carries a soloed one, then its sound group, which every group below it plays into, then its effects in their order with Attenuation among them, then its output, with its mute and solo and its meter, into its parent's sound group. Its volume and pitch play from its lanes, as each effect's Wet and values from its own
    struct AudioMixer::State
    {
        struct Group
        {
            // Apart from the group, so it can be made again with the resampler or without
            PitchedSound* Sound = nullptr;
            GainNode Direct;
            GainNode Attenuation;
            GainNode Output;
            Unit Volume;
            GroupMailbox Lanes;
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
            // Made by this Apply, so its gains and gates start where they are to be, with no ramp, and its chain is wired at once
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
        using LaneMap = std::unordered_map<UUID, OwnerLanes, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, OwnerLanes>, MemoryTag::Audio>>;
        using WeightList = std::vector<std::pair<UUID, float>, TaggedAllocator<std::pair<UUID, float>, MemoryTag::Audio>>;

        explicit State(AudioEngine& engine) : Audio(engine), TheEngine(AudioEngineInternal::GetEngine(engine)), RampFrames(FramesFor(static_cast<std::uint64_t>(c_RampMilliseconds) * 1000, ma_engine_get_sample_rate(&TheEngine)))
        {

        }

        ~State();

        [[nodiscard]] Group* CreateGroup(bool pitched);
        void DestroyGroup(Group* group);
        [[nodiscard]] PitchedSound* CreateSound(bool pitched, GroupMailbox& lanes);
        void SetPitched(Group& group, bool pitched);
        [[nodiscard]] bool InitGain(GainNode& node, bool metered, GroupMailbox* lanes = nullptr);
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
        void UpdateGains(std::uint64_t now);
        void UpdatePitched(std::uint64_t now);

        [[nodiscard]] std::uint64_t Now() const;
        [[nodiscard]] Glide GetGlide(AudioMixerValue value) const;
        [[nodiscard]] float BlendTarget(AudioMixerValue value) const;
        [[nodiscard]] Lane MakeSnapshotLane(AudioMixerValue value, Glide glide, float from, std::uint64_t start, std::uint32_t length) const;
        void PlayValue(ValueLanes& lanes, std::uint64_t now, Glide glide) const;
        void RefreshValues(std::uint64_t now);
        void StartTransition(std::uint32_t length);
        void SendValues(UUID owner);
        void SendAllValues();
        [[nodiscard]] float PlayedValue(UUID owner, std::size_t slot, std::uint64_t now) const;
        [[nodiscard]] bool IsStale(const Unit& unit, std::uint64_t now) const;

        AudioEngine& Audio;
        ma_engine& TheEngine;
        AudioMixerData Data;
        Group* Master = nullptr;
        // Every group by its ID in the data, Master's included
        GroupMap Groups;
        RouteMap Routes;
        // Every group's and effect's values by its ID, and where the snapshots take them: the snapshots, their weights adding up to 1, and when the transition to them started and for how long
        LaneMap Lanes;
        WeightList Targets;
        std::uint64_t TransitionStart = 0;
        std::uint32_t TransitionLength = 0;
        std::uint32_t RampFrames = 1;
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

    // An Attenuation plays its group's lanes, and every other gain the latest it is set to
    bool AudioMixer::State::InitGain(GainNode& node, bool metered, GroupMailbox* lanes)
    {
        const float l_SampleRate = static_cast<float>(ma_engine_get_sample_rate(&TheEngine));
        node.Channels = ma_engine_get_channels(&TheEngine);
        node.RampFrames = RampFrames;
        node.Metered = metered;
        node.PeakFall = std::pow(10.0f, -c_PeakFallDecibelsPerSecond / 20.0f / l_SampleRate);
        node.RmsCoefficient = 1.0f - std::exp(-1.0f / (c_RmsSeconds * l_SampleRate));
        node.Lanes = lanes;

        const std::array<ma_uint32, 1> l_Channels{ node.Channels };
        ma_node_config l_Config = ma_node_config_init();
        l_Config.vtable = lanes != nullptr ? &c_AttenuationVTable : &c_GainVTable;
        l_Config.pInputChannels = l_Channels.data();
        l_Config.pOutputChannels = l_Channels.data();

        return ma_node_init(ma_engine_get_node_graph(&TheEngine), &l_Config, &TheEngine.allocationCallbacks, &node.Base) == MA_SUCCESS;
    }

    // Wired to nothing, with its process put behind the one that plays its group's pitch before anything can read it. None when it cannot be made
    PitchedSound* AudioMixer::State::CreateSound(bool pitched, GroupMailbox& lanes)
    {
        PitchedSound* l_Sound = Memory::New<PitchedSound>(MemoryTag::Audio);
        if (ma_sound_group_init(&TheEngine, MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT | (pitched ? 0u : static_cast<ma_uint32>(MA_SOUND_FLAG_NO_PITCH)), nullptr, &l_Sound->Sound) != MA_SUCCESS)
        {
            Memory::Delete(l_Sound);

            return nullptr;
        }

        l_Sound->Lanes = &lanes;
        l_Sound->VTable = *l_Sound->Sound.engineNode.baseNode.vtable;
        l_Sound->Process = l_Sound->VTable.onProcess;
        l_Sound->VTable.onProcess = &ProcessPitchedSound;
        l_Sound->Sound.engineNode.baseNode.vtable = &l_Sound->VTable;

        return l_Sound;
    }

    // What plays into the old sound group, and what it plays into, move to the new one before the old goes, which moves the sound by the sample the resampler held back
    void AudioMixer::State::SetPitched(Group& group, bool pitched)
    {
        PitchedSound* l_Sound = CreateSound(pitched, group.Lanes);
        if (l_Sound == nullptr)
        {
            TR_CORE_CRITICAL("Audio: a mixer group could not be made");

            throw std::bad_alloc();
        }

        static_cast<void>(ma_node_attach_output_bus(&l_Sound->Sound, 0, group.Wired.front()->GetNode(), 0));
        static_cast<void>(ma_node_attach_output_bus(&group.Direct.Base, 0, &l_Sound->Sound, 0));
        for (const auto& [it_ID, it_Child] : Groups)
        {
            if (it_Child->Parent == &group)
            {
                static_cast<void>(ma_node_attach_output_bus(&it_Child->Output.Base, 0, &l_Sound->Sound, 0));
            }
        }

        ma_sound_group_uninit(&group.Sound->Sound);
        Memory::Delete(group.Sound);
        group.Sound = l_Sound;
        group.Pitched = pitched;
    }

    // Wired within itself and to nothing above, its chain Attenuation alone, so the audio thread reaches none of it until Apply has set its gains and lanes and plays it into its parent. Only running out of memory fails, which Memory treats the same way
    AudioMixer::State::Group* AudioMixer::State::CreateGroup(bool pitched)
    {
        Group* l_Group = Memory::New<Group>(MemoryTag::Audio);
        l_Group->Lanes.Reset({ Constant(0.0f), Constant(1.0f) });
        l_Group->Sound = CreateSound(pitched, l_Group->Lanes);
        l_Group->Pitched = pitched;
        l_Group->HasSound = l_Group->Sound != nullptr;
        l_Group->HasDirect = l_Group->HasSound && InitGain(l_Group->Direct, false);
        l_Group->HasAttenuation = l_Group->HasDirect && InitGain(l_Group->Attenuation, false, &l_Group->Lanes);
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
        static_cast<void>(ma_node_attach_output_bus(&l_Group->Sound->Sound, 0, &l_Group->Attenuation.Base, 0));
        static_cast<void>(ma_node_attach_output_bus(&l_Group->Direct.Base, 0, &l_Group->Sound->Sound, 0));

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
            ma_sound_group_uninit(&group->Sound->Sound);
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

    // Wired to nothing and fully dry, at the values its lanes have now, its lanes in each copy of its mailbox, its filter's state, its echo's delay and its Limiter's window all made here on the main thread, so the audio thread allocates nothing for it. Only running out of memory fails, which Memory treats the same way
    Unit* AudioMixer::State::CreateEffect(const AudioEffect& effect)
    {
        const std::uint32_t l_Rate = ma_engine_get_sample_rate(&TheEngine);
        const std::uint64_t l_Now = Now();
        const OwnerLanes& l_Lanes = Lanes.at(effect.ID);
        EffectNode* l_Node = Memory::New<EffectNode>(MemoryTag::Audio);
        l_Node->Type = effect.Type;
        l_Node->Channels = ma_engine_get_channels(&TheEngine);
        l_Node->SampleRate = l_Rate;
        l_Node->FadeFrames = FramesFor(static_cast<std::uint64_t>(c_FadeMilliseconds) * 1000, l_Rate);
        l_Node->ValueCount = static_cast<std::uint32_t>(GetAudioEffectInfo(effect.Type).Parameters.size());
        EffectLanes l_Played{};
        for (std::size_t it_Slot = 0; it_Slot <= l_Node->ValueCount; ++it_Slot)
        {
            l_Played[it_Slot] = l_Lanes.Values[it_Slot].Played;
        }

        l_Node->Lanes.Reset(l_Played);
        l_Node->Mix = std::clamp(Evaluate(l_Played[AudioMixerValue::c_Wet], l_Now) / 100.0f, 0.0f, 1.0f);
        for (std::uint32_t it_Value = 0; it_Value < l_Node->ValueCount; ++it_Value)
        {
            l_Node->Values[it_Value] = Evaluate(l_Played[it_Value + 1], l_Now);
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
                l_Unit->Delay = EchoFrames(l_Node->Values[0], l_Rate);
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
                l_Unit->Density = l_Node->Values[10];
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

    // The chain the data wants, each effect found again by its ID and type, so a playing one keeps its state, and made anew when it is new, of another type, an Echo whose lane has another delay now or an SFX Reverb another density, which need delays of their own, or a Send to another target, so the old one's sends fade out of its target as the new one's fade into the other. Attenuation comes last when the data leaves it out. An effect made and let go again before it ever stepped in goes at once
    void AudioMixer::State::SetDesired(Group& group, const AudioMixerGroup& data)
    {
        const std::uint64_t l_Now = Now();
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
            const bool l_Remade = l_Unit != nullptr && (IsStale(*l_Unit, l_Now) || (it_Effect.Type == AudioEffectType::Send && l_Unit->Target != it_Effect.Target));
            if (l_Unit == nullptr || l_Unit->Type != it_Effect.Type || l_Remade)
            {
                l_Unit = CreateEffect(it_Effect);
                group.Effects.push_back(l_Unit);
            }

            l_Unit->Gate = GetGate(data, it_Effect);
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

    // The effects outside the run that keeps its order cross to dry, and once all have, step out and in, and those that left go. A fresh group plays nothing yet, so its chain is wired at once with every gate where it is to be
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
                    it_Unit->Effect->Gate = it_Unit->Gate;
                    it_Unit->Effect->RampGate = it_Unit->Gate;
                    it_Unit->Effect->Feed = it_Unit->Gate > 0.0f && it_Unit->Effect->Mix > 0.0f ? 1.0f : 0.0f;
                    it_Unit->Effect->RampFeed = it_Unit->Effect->Feed;
                }

                if (it_Unit->Effect != nullptr)
                {
                    SetGate(*it_Unit->Effect, it_Unit->Gate);
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
            SetGate(*it_Unit->Effect, l_Stays ? it_Unit->Gate : 0.0f);
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
                SetGate(*it_Unit->Effect, it_Unit->Gate);
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
        const auto a_Before = [&group](std::size_t index) { return index == 0 ? static_cast<ma_node*>(&group.Sound->Sound) : group.Wired[index - 1]->GetNode(); };
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

    // A frame the audio thread has yet to reach, so a change made now starts there on every node alike: a device's period on from the frame the engine is at, since the device may be partway through mixing it, or that frame itself when the main thread mixes
    std::uint64_t AudioMixer::State::Now() const
    {
        return ma_engine_get_time_in_pcm_frames(&TheEngine) + AudioEngineInternal::GetLeadFrames(Audio);
    }

    Glide AudioMixer::State::GetGlide(AudioMixerValue value) const
    {
        for (const AudioMixerGroup& it_Group : Data.Groups)
        {
            if (it_Group.ID == value.Owner)
            {
                return value.Slot == AudioMixerValue::c_Volume ? Glide::Gain : Glide::Even;
            }

            const auto a_Effect = std::ranges::find_if(it_Group.Effects, [&value](const AudioEffect& effect) { return effect.Type != AudioEffectType::Attenuation && effect.ID == value.Owner; });
            if (a_Effect != it_Group.Effects.end())
            {
                return GetEffectGlide(a_Effect->Type, value.Slot);
            }
        }

        return Glide::Even;
    }

    // Where the snapshots take a value once their transition is over: each one's own, within the value's range, at its weight
    float AudioMixer::State::BlendTarget(AudioMixerValue value) const
    {
        const AudioEffectParameter l_Info = *Data.GetValueInfo(value);
        float l_Target = 0.0f;
        for (const auto& [it_ID, it_Weight] : Targets)
        {
            l_Target += it_Weight * std::clamp(*Data.GetValue(*Data.FindSnapshot(it_ID), value), l_Info.Minimum, l_Info.Maximum);
        }

        return l_Target;
    }

    // From where the value is towards the snapshots' blend, each snapshot's share of the way along its curve, or, for a value that makes its effect anew, all of it at the end
    Lane AudioMixer::State::MakeSnapshotLane(AudioMixerValue value, Glide glide, float from, std::uint64_t start, std::uint32_t length) const
    {
        const AudioEffectParameter l_Info = *Data.GetValueInfo(value);
        Lane l_Lane;
        l_Lane.From = from;
        l_Lane.Start = start;
        l_Lane.Length = length;
        for (const auto& [it_ID, it_Weight] : Targets)
        {
            const AudioMixerSnapshot& l_Snapshot = *Data.FindSnapshot(it_ID);
            const float l_Target = std::clamp(*Data.GetValue(l_Snapshot, value), l_Info.Minimum, l_Info.Maximum);
            const AudioTransitionCurve l_Curve = glide == Glide::None ? AudioTransitionCurve::BrickwallEnd : Data.GetCurve(l_Snapshot, value);
            l_Lane.To += it_Weight * l_Target;
            l_Lane.Moves[static_cast<std::size_t>(l_Curve)] += it_Weight * (l_Target - from);
        }

        return l_Lane;
    }

    // What plays from now on: the snapshots' lane, or the value code set, gliding over the ramp from where it was when it would step, but for a value that makes its effect anew, which steps
    void AudioMixer::State::PlayValue(ValueLanes& lanes, std::uint64_t now, Glide glide) const
    {
        Lane l_Played = lanes.IsSet ? Constant(lanes.Set) : lanes.Snapshot;
        const float l_Was = Evaluate(lanes.Played, now);
        if (glide != Glide::None && Evaluate(l_Played, now) != l_Was)
        {
            l_Played.GlideFrom = l_Was;
            l_Played.GlideStart = now;
            l_Played.GlideLength = RampFrames;
            l_Played.GlidesInGain = glide == Glide::Gain;
        }

        lanes.Played = l_Played;
    }

    // The lanes made again from the data an Apply gives: a snapshot gone from the transition leaves it and the rest share its weight, a value of a new group or effect starts where the snapshots take it, one of a group or effect that is gone is let go, and one whose snapshots changed moves to them from where its transition started, gliding when it would step. A value no name exposes any more is no longer set
    void AudioMixer::State::RefreshValues(std::uint64_t now)
    {
        std::erase_if(Targets, [this](const std::pair<UUID, float>& target) { return Data.FindSnapshot(target.first) == nullptr; });
        if (Targets.empty())
        {
            Targets.emplace_back(Data.StartSnapshot, 1.0f);
        }

        float l_Weights = 0.0f;
        for (const auto& [it_ID, it_Weight] : Targets)
        {
            l_Weights += it_Weight;
        }

        for (auto& [it_ID, it_Weight] : Targets)
        {
            it_Weight /= l_Weights;
        }

        LaneMap l_Lanes;
        ForEachValue(Data, [&](AudioMixerValue value, Glide glide, AudioEffectType type)
        {
            const auto a_Found = Lanes.find(value.Owner);
            const bool l_Old = a_Found != Lanes.end() && a_Found->second.Type == type;
            const auto [a_Entry, a_Made] = l_Lanes.try_emplace(value.Owner);
            if (a_Made && l_Old)
            {
                a_Entry->second = a_Found->second;
            }

            a_Entry->second.Type = type;
            ValueLanes& l_Value = a_Entry->second.Values[value.Slot];
            if (!l_Old)
            {
                l_Value.Snapshot = Constant(BlendTarget(value));
                l_Value.Played = l_Value.Snapshot;

                return;
            }

            const bool l_WasSet = l_Value.IsSet;
            const Lane l_Snapshot = MakeSnapshotLane(value, glide, l_Value.Snapshot.From, TransitionStart, TransitionLength);
            l_Value.IsSet = l_Value.IsSet && std::ranges::find(Data.Exposed, value, &AudioExposedParameter::Value) != Data.Exposed.end();
            if (l_Snapshot != l_Value.Snapshot || l_WasSet != l_Value.IsSet)
            {
                l_Value.Snapshot = l_Snapshot;
                PlayValue(l_Value, now, glide);
            }
        });

        Lanes.swap(l_Lanes);
    }

    // Every value from where the snapshots have it now towards the targets' blend over the length, played unless code set it, and the groups whose pitch is to move given the resampler before it starts
    void AudioMixer::State::StartTransition(std::uint32_t length)
    {
        const std::uint64_t l_Now = Now();
        TransitionStart = l_Now;
        TransitionLength = length;
        ForEachValue(Data, [&](AudioMixerValue value, Glide glide, AudioEffectType)
        {
            ValueLanes& l_Value = Lanes.at(value.Owner).Values[value.Slot];
            l_Value.Snapshot = MakeSnapshotLane(value, glide, Evaluate(l_Value.Snapshot, l_Now), l_Now, length);
            PlayValue(l_Value, l_Now, glide);
        });

        SendAllValues();
        UpdatePitched(l_Now);
    }

    // A group's lanes to its Attenuation and its sound group, or an effect's to each unit made for it of its type
    void AudioMixer::State::SendValues(UUID owner)
    {
        const OwnerLanes& l_Lanes = Lanes.at(owner);
        if (const auto a_Group = Groups.find(owner); a_Group != Groups.end())
        {
            a_Group->second->Lanes.Send({ l_Lanes.Values[AudioMixerValue::c_Volume].Played, l_Lanes.Values[AudioMixerValue::c_Pitch].Played });

            return;
        }

        EffectLanes l_Played{};
        for (std::size_t it_Slot = 0; it_Slot < AudioMixerValue::c_MaxSlots; ++it_Slot)
        {
            l_Played[it_Slot] = l_Lanes.Values[it_Slot].Played;
        }

        for (const auto& [it_ID, it_Group] : Groups)
        {
            for (Unit* it_Unit : it_Group->Effects)
            {
                if (it_Unit->ID == owner && it_Unit->Type == l_Lanes.Type)
                {
                    it_Unit->Effect->Lanes.Send(l_Played);
                }
            }
        }
    }

    // A unit leaving whose effect the data no longer has, or has as another type, plays on as it was
    void AudioMixer::State::SendAllValues()
    {
        for (const auto& [it_ID, it_Group] : Groups)
        {
            SendValues(it_ID);
            for (Unit* it_Unit : it_Group->Effects)
            {
                const auto a_Lanes = Lanes.find(it_Unit->ID);
                if (a_Lanes == Lanes.end() || a_Lanes->second.Type != it_Unit->Type)
                {
                    continue;
                }

                EffectLanes l_Played{};
                for (std::size_t it_Slot = 0; it_Slot < AudioMixerValue::c_MaxSlots; ++it_Slot)
                {
                    l_Played[it_Slot] = a_Lanes->second.Values[it_Slot].Played;
                }

                it_Unit->Effect->Lanes.Send(l_Played);
            }
        }
    }

    float AudioMixer::State::PlayedValue(UUID owner, std::size_t slot, std::uint64_t now) const
    {
        const auto a_Lanes = Lanes.find(owner);

        return a_Lanes != Lanes.end() ? Evaluate(a_Lanes->second.Values[slot].Played, now) : 0.0f;
    }

    // An Echo whose delay, or an SFX Reverb whose density, its lane has moved from since it was made, which a new one is to take over from. One whose effect is gone, or is now of another type, is let be
    bool AudioMixer::State::IsStale(const Unit& unit, std::uint64_t now) const
    {
        const auto a_Lanes = Lanes.find(unit.ID);
        if (a_Lanes == Lanes.end() || a_Lanes->second.Type != unit.Type)
        {
            return false;
        }

        if (unit.Type == AudioEffectType::Echo)
        {
            return EchoFrames(PlayedValue(unit.ID, 1, now), ma_engine_get_sample_rate(&TheEngine)) != unit.Delay;
        }

        return unit.Type == AudioEffectType::SFXReverb && PlayedValue(unit.ID, 11, now) != unit.Density;
    }

    // A group whose pitch is to move, or stay off 1, takes the resampler, and one back at 1 for good lets it go
    void AudioMixer::State::UpdatePitched(std::uint64_t now)
    {
        for (const auto& [it_ID, it_Group] : Groups)
        {
            const bool l_Pitched = IsPitched(Lanes.at(it_ID).Values[AudioMixerValue::c_Pitch].Played, now);
            if (it_Group->Pitched != l_Pitched)
            {
                SetPitched(*it_Group, l_Pitched);
            }
        }
    }

    // As Unity's mixer has it: mute silences a group and so everything below it, and while any group is soloed, a group neither soloed nor below a soloed one is silent unless one below it is soloed, when it carries only that. A group made by this Apply takes its gains and its volume at once
    void AudioMixer::State::UpdateGains(std::uint64_t now)
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
            const float l_Direct = l_AnySolo && !l_Soloed ? 0.0f : 1.0f;
            Group& l_Group = *Groups.at(it_Group.ID);
            for (const auto& [it_Node, it_Gain] : { std::pair(&l_Group.Output, l_Output), std::pair(&l_Group.Direct, l_Direct) })
            {
                if (l_Group.Fresh)
                {
                    it_Node->Gain = it_Gain;
                    it_Node->RampGain = it_Gain;
                }

                it_Node->TargetGain.store(it_Gain, std::memory_order_relaxed);
            }

            if (l_Group.Fresh)
            {
                l_Group.Attenuation.Gain = AudioDecibelsToGain(Evaluate(Lanes.at(it_Group.ID).Values[AudioMixerValue::c_Volume].Played, now));
            }
        }

        UpdatePitched(now);
    }

    // Master alone, into the engine's output, at the start snapshot
    AudioMixer::AudioMixer(AudioEngine& engine) : m_State(Memory::New<State>(MemoryTag::Audio, engine))
    {
        State& l_State = *m_State;
        const std::uint64_t l_Now = l_State.Now();
        l_State.Data = AudioMixerData::MakeDefault();
        l_State.Master = l_State.CreateGroup(false);
        l_State.Groups.emplace(l_State.Data.GetMaster().ID, l_State.Master);
        l_State.RefreshValues(l_Now);
        l_State.UpdateGains(l_Now);
        l_State.SetDesired(*l_State.Master, l_State.Data.GetMaster());
        l_State.Master->Fresh = false;
        l_State.SendAllValues();
        static_cast<void>(ma_node_attach_output_bus(&l_State.Master->Output.Base, 0, &AudioEngineInternal::GetMaster(engine), 0));
    }

    AudioMixer::~AudioMixer()
    {
        Memory::Delete(m_State);
    }

    // The groups are made, kept and wired before any is let go, so a group that leaves takes nothing with it that stays: what was sent to it has moved to Master, and the groups below it that stay have moved to their new parents. The values move to the new data's snapshots, the transition under way carrying on towards them
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

        // A copy of its own, so a smaller mixer lets go of what a larger one held. Each effect is made at the values its lanes give it
        l_State.Groups = std::move(l_Groups);
        l_State.Data = AudioMixerData(data);
        const std::uint64_t l_Now = l_State.Now();
        l_State.RefreshValues(l_Now);
        l_State.UpdateGains(l_Now);
        for (const AudioMixerGroup& it_Group : data.Groups)
        {
            State::Group& l_Group = *l_State.Groups.at(it_Group.ID);
            l_State.SetDesired(l_Group, it_Group);
            l_Group.Fresh = false;
        }

        l_State.SendAllValues();

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
                static_cast<void>(ma_node_attach_output_bus(&l_Group.Output.Base, 0, &l_Parent->Sound->Sound, 0));
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

    // Allocates nothing while no chain is changing. An Echo or an SFX Reverb whose delay or density a transition or code has changed is made anew and crossed over to. Once a chain has changed, its Sends move to the units now in their targets' chains, and a group whose pitch has come back to 1 for good lets its resampler go
    void AudioMixer::Update()
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A mixer changes on the main thread.");

        State& l_State = *m_State;
        const std::uint64_t l_Now = l_State.Now();
        bool l_Changing = false;
        for (const auto& [it_ID, it_Group] : l_State.Groups)
        {
            if (std::ranges::any_of(it_Group->Desired, [&l_State, l_Now](const Unit* unit) { return unit->Effect != nullptr && l_State.IsStale(*unit, l_Now); }))
            {
                l_State.SetDesired(*it_Group, *l_State.Data.FindGroup(it_ID));
                l_Changing = true;
            }
            else if (it_Group->Pending)
            {
                l_State.Converge(*it_Group);
                l_Changing = true;
            }
        }

        l_State.UpdatePitched(l_Now);
        if (l_Changing)
        {
            l_State.ConnectSends();
        }
    }

    bool AudioMixer::TransitionToSnapshot(std::string_view name, float seconds)
    {
        const std::array<std::string_view, 1> l_Names{ name };
        const std::array<float, 1> l_Weights{ 1.0f };

        return TransitionToSnapshots(l_Names, l_Weights, seconds);
    }

    // The weights scaled to add up to 1, and a snapshot without weight left out
    bool AudioMixer::TransitionToSnapshots(std::span<const std::string_view> names, std::span<const float> weights, float seconds)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A mixer changes on the main thread.");

        State& l_State = *m_State;
        float l_Weights = 0.0f;
        for (std::size_t it_Name = 0; it_Name < names.size() && it_Name < weights.size(); ++it_Name)
        {
            if (l_State.Data.FindSnapshot(names[it_Name]) == nullptr || !std::isfinite(weights[it_Name]) || weights[it_Name] < 0.0f)
            {
                return false;
            }

            l_Weights += weights[it_Name];
        }

        if (names.size() != weights.size() || !(l_Weights > 0.0f) || !std::isfinite(l_Weights) || !std::isfinite(seconds))
        {
            return false;
        }

        l_State.Targets.clear();
        for (std::size_t it_Name = 0; it_Name < names.size(); ++it_Name)
        {
            if (weights[it_Name] > 0.0f)
            {
                l_State.Targets.emplace_back(l_State.Data.FindSnapshot(names[it_Name])->ID, weights[it_Name] / l_Weights);
            }
        }

        l_State.StartTransition(static_cast<std::uint32_t>(std::llround(static_cast<double>(std::max(seconds, 0.0f)) * ma_engine_get_sample_rate(&l_State.TheEngine))));

        return true;
    }

    bool AudioMixer::SetFloat(std::string_view name, float value)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A mixer changes on the main thread.");

        State& l_State = *m_State;
        const AudioExposedParameter* l_Exposed = l_State.Data.FindExposed(name);
        if (l_Exposed == nullptr || !std::isfinite(value))
        {
            return false;
        }

        const AudioEffectParameter l_Info = *l_State.Data.GetValueInfo(l_Exposed->Value);
        const std::uint64_t l_Now = l_State.Now();
        ValueLanes& l_Value = l_State.Lanes.at(l_Exposed->Value.Owner).Values[l_Exposed->Value.Slot];
        l_Value.Set = std::clamp(value, l_Info.Minimum, l_Info.Maximum);
        l_Value.IsSet = true;
        l_State.PlayValue(l_Value, l_Now, l_State.GetGlide(l_Exposed->Value));
        l_State.SendValues(l_Exposed->Value.Owner);
        l_State.UpdatePitched(l_Now);

        return true;
    }

    std::optional<float> AudioMixer::GetFloat(std::string_view name) const
    {
        const State& l_State = *m_State;
        const AudioExposedParameter* l_Exposed = l_State.Data.FindExposed(name);
        if (l_Exposed == nullptr)
        {
            return std::nullopt;
        }

        const ValueLanes& l_Value = l_State.Lanes.at(l_Exposed->Value.Owner).Values[l_Exposed->Value.Slot];

        return l_Value.IsSet ? l_Value.Set : Evaluate(l_Value.Snapshot, ma_engine_get_time_in_pcm_frames(&l_State.TheEngine));
    }

    bool AudioMixer::ClearFloat(std::string_view name)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A mixer changes on the main thread.");

        State& l_State = *m_State;
        const AudioExposedParameter* l_Exposed = l_State.Data.FindExposed(name);
        if (l_Exposed == nullptr)
        {
            return false;
        }

        const std::uint64_t l_Now = l_State.Now();
        ValueLanes& l_Value = l_State.Lanes.at(l_Exposed->Value.Owner).Values[l_Exposed->Value.Slot];
        l_Value.IsSet = false;
        l_State.PlayValue(l_Value, l_Now, l_State.GetGlide(l_Exposed->Value));
        l_State.SendValues(l_Exposed->Value.Owner);
        l_State.UpdatePitched(l_Now);

        return true;
    }

    void AudioMixer::Reset()
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A mixer changes on the main thread.");

        State& l_State = *m_State;
        for (auto& [it_ID, it_Lanes] : l_State.Lanes)
        {
            for (ValueLanes& it_Value : it_Lanes.Values)
            {
                it_Value.IsSet = false;
            }
        }

        l_State.Targets.assign(1, { l_State.Data.StartSnapshot, 1.0f });
        l_State.StartTransition(0);
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