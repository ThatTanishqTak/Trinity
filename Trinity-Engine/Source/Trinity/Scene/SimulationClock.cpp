#include "Trinity/Scene/SimulationClock.hpp"

#include <algorithm>
#include <limits>

namespace Trinity
{
    namespace
    {
        // The largest float below 1, which a fraction just short of a whole step rounds to rather than to 1
        constexpr float c_BelowOne = 1.0f - std::numeric_limits<float>::epsilon() * 0.5f;
    }

    SimulationClock::SimulationClock(std::uint32_t rate)
    {
        SetRate(rate);
    }

    // A step is the nearest whole nanosecond to 1 / rate, so 60 steps a second are 16666667 ns each
    void SimulationClock::SetRate(std::uint32_t rate)
    {
        m_Rate = std::clamp(rate, c_MinimumRate, c_MaximumRate);
        m_Step = std::chrono::nanoseconds((1'000'000'000 + m_Rate / 2) / m_Rate);
        m_Accumulated %= m_Step;
    }

    void SimulationClock::Reset()
    {
        m_Accumulated = std::chrono::nanoseconds(0);
        m_StepCount = 0;
        m_DroppedSteps = 0;
    }

    // Whatever is left after the steps taken is less than a step, whether the frame was capped or not
    std::uint32_t SimulationClock::Advance(std::chrono::nanoseconds frameTime)
    {
        m_Accumulated += std::clamp(frameTime, std::chrono::nanoseconds(0), c_MaxFrameTime);

        const std::int64_t l_Due = m_Accumulated / m_Step;
        const std::uint32_t l_Steps = static_cast<std::uint32_t>(std::min<std::int64_t>(l_Due, c_MaxStepsPerFrame));
        m_Accumulated %= m_Step;
        m_StepCount += l_Steps;
        m_DroppedSteps += static_cast<std::uint64_t>(l_Due - l_Steps);

        return l_Steps;
    }

    float SimulationClock::GetInterpolation() const
    {
        return std::min(static_cast<float>(static_cast<double>(m_Accumulated.count()) / static_cast<double>(m_Step.count())), c_BelowOne);
    }
}