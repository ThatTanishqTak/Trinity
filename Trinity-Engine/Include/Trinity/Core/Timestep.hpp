#pragma once

#include <chrono>
#include <cstdint>

namespace Trinity
{
    // A frame's time, exact in nanoseconds as Application measures it, and in seconds for code that only needs those
    class Timestep
    {
    public:
        constexpr Timestep(float seconds = 0.0f) : m_Duration(static_cast<std::int64_t>(static_cast<double>(seconds) * 1e9 + (seconds < 0.0f ? -0.5 : 0.5)))
        {

        }

        constexpr explicit Timestep(std::chrono::nanoseconds duration) : m_Duration(duration)
        {

        }

        constexpr operator float() const { return GetSeconds(); }

        [[nodiscard]] constexpr float GetSeconds() const { return static_cast<float>(static_cast<double>(m_Duration.count()) * 1e-9); }
        [[nodiscard]] constexpr float GetMilliseconds() const { return static_cast<float>(static_cast<double>(m_Duration.count()) * 1e-6); }
        [[nodiscard]] constexpr std::chrono::nanoseconds GetDuration() const { return m_Duration; }

    private:
        std::chrono::nanoseconds m_Duration;
    };
}