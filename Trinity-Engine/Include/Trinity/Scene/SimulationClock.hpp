#pragma once

#include "Trinity/Core/Export.hpp"

#include <chrono>
#include <cstdint>

namespace Trinity
{
    // Fixed steps for a simulation. Each frame's time, clamped to c_MaxFrameTime, is added up, and whole steps are taken from it, at most c_MaxStepsPerFrame a frame. A frame owed more drops the rest, so a long hitch slows the simulation down rather than making it catch up over the frames after. What is left is how far rendering lies between the last two steps. Time is counted in nanoseconds, so steps add up exactly
    class TRINITY_API SimulationClock
    {
    public:
        static constexpr std::uint32_t c_DefaultRate = 60;
        static constexpr std::uint32_t c_MinimumRate = 1;
        static constexpr std::uint32_t c_MaximumRate = 1000;
        static constexpr std::uint32_t c_MaxStepsPerFrame = 8;
        static constexpr std::chrono::nanoseconds c_MaxFrameTime{ 250'000'000 };

        explicit SimulationClock(std::uint32_t rate = c_DefaultRate);

        // Steps per second, clamped to c_MinimumRate and c_MaximumRate, from the next frame on. Time already added carries over, less any whole steps of the new length
        void SetRate(std::uint32_t rate);
        // No time added and no steps taken, as a simulation starts
        void Reset();

        // Adds a frame's time, and returns how many steps to take for it
        [[nodiscard]] std::uint32_t Advance(std::chrono::nanoseconds frameTime);

        [[nodiscard]] std::uint32_t GetRate() const { return m_Rate; }
        [[nodiscard]] std::chrono::nanoseconds GetStep() const { return m_Step; }
        [[nodiscard]] float GetStepSeconds() const { return static_cast<float>(static_cast<double>(m_Step.count()) * 1e-9); }
        // How far past the last step the frame lies, as a fraction of a step, in [0, 1): what rendering interpolates between the last two steps by
        [[nodiscard]] float GetInterpolation() const;
        // Since the last reset: steps taken, and steps that capped frames dropped
        [[nodiscard]] std::uint64_t GetStepCount() const { return m_StepCount; }
        [[nodiscard]] std::uint64_t GetDroppedSteps() const { return m_DroppedSteps; }

    private:
        std::uint32_t m_Rate = c_DefaultRate;
        std::chrono::nanoseconds m_Step{ 0 };
        std::chrono::nanoseconds m_Accumulated{ 0 };
        std::uint64_t m_StepCount = 0;
        std::uint64_t m_DroppedSteps = 0;
    };
}