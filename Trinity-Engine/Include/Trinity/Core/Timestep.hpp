#pragma once

namespace Trinity
{
    class Timestep
    {
    public:
        constexpr Timestep(float seconds = 0.0f) : m_Seconds(seconds) {}

        constexpr operator float() const { return m_Seconds; }

        [[nodiscard]] constexpr float GetSeconds() const { return m_Seconds; }
        [[nodiscard]] constexpr float GetMilliseconds() const { return m_Seconds * 1000.0f; }

    private:
        float m_Seconds;
    };
}
