#pragma once

namespace Trinity
{
	class Timestep
	{
	public:
		Timestep(float seconds = 0.0f) : m_Seconds(seconds)
		{

		}

		operator float() const { return m_Seconds; }

		float GetSeconds() const { return m_Seconds; }
		float GetMilliseconds() const { return m_Seconds * 1000.0f; }

	private:
		float m_Seconds = 0.0f;
	};
}