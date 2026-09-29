#include "Trinity/Time/Time.hpp"

#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <chrono>

namespace Trinity
{
	namespace
	{
		struct TimeState
		{
			std::chrono::steady_clock::time_point LastFrame = std::chrono::steady_clock::now();

			float DeltaTime = 0.0f;
			float UnscaledDeltaTime = 0.0f;

			double ElapsedTime = 0.0;
			double UnscaledElapsedTime = 0.0;

			uint64_t FrameCount = 0;

			float TimeScale = 1.0f;
			float MaxDeltaTime = 0.25f;

			float FixedDeltaTime = 1.0f / 60.0f;
			float FixedAccumulator = 0.0f;
			uint32_t MaxFixedStepsPerFrame = 8;
		};

		TimeState s_State;
	}

	Timestep Time::GetDeltaTime()
	{
		return s_State.DeltaTime;
	}

	Timestep Time::GetUnscaledDeltaTime()
	{
		return s_State.UnscaledDeltaTime;
	}

	double Time::GetElapsedTime()
	{
		return s_State.ElapsedTime;
	}

	double Time::GetUnscaledElapsedTime()
	{
		return s_State.UnscaledElapsedTime;
	}

	uint64_t Time::GetFrameCount()
	{
		return s_State.FrameCount;
	}

	float Time::GetTimeScale()
	{
		return s_State.TimeScale;
	}

	void Time::SetTimeScale(float timeScale)
	{
		if (timeScale < 0.0f)
		{
			TR_CORE_WARN("Time scale cannot be negative ({}), clamping to 0", timeScale);
		}

		s_State.TimeScale = std::max(timeScale, 0.0f);
	}

	Timestep Time::GetFixedDeltaTime()
	{
		return s_State.FixedDeltaTime;
	}

	void Time::SetFixedDeltaTime(Timestep fixedDeltaTime)
	{
		if (fixedDeltaTime.GetSeconds() <= 0.0f)
		{
			TR_CORE_WARN("Fixed delta time must be positive ({}), ignoring", fixedDeltaTime.GetSeconds());

			return;
		}

		s_State.FixedDeltaTime = fixedDeltaTime;
		s_State.FixedAccumulator = 0.0f;
	}

	float Time::GetFixedAlpha()
	{
		return s_State.FixedAccumulator / s_State.FixedDeltaTime;
	}

	Timestep Time::GetMaxDeltaTime()
	{
		return s_State.MaxDeltaTime;
	}

	void Time::SetMaxDeltaTime(Timestep maxDeltaTime)
	{
		if (maxDeltaTime.GetSeconds() <= 0.0f)
		{
			TR_CORE_WARN("Max delta time must be positive ({}), ignoring", maxDeltaTime.GetSeconds());

			return;
		}

		s_State.MaxDeltaTime = maxDeltaTime;
	}

	uint32_t Time::GetMaxFixedStepsPerFrame()
	{
		return s_State.MaxFixedStepsPerFrame;
	}

	void Time::SetMaxFixedStepsPerFrame(uint32_t maxSteps)
	{
		if (maxSteps == 0)
		{
			TR_CORE_WARN("Max fixed steps per frame must be at least 1, ignoring");

			return;
		}

		s_State.MaxFixedStepsPerFrame = maxSteps;
	}

	void Time::Reset()
	{
		s_State.LastFrame = std::chrono::steady_clock::now();

		s_State.DeltaTime = 0.0f;
		s_State.UnscaledDeltaTime = 0.0f;
		s_State.ElapsedTime = 0.0;
		s_State.UnscaledElapsedTime = 0.0;
		s_State.FrameCount = 0;
		s_State.FixedAccumulator = 0.0f;
	}

	void Time::Tick()
	{
		const std::chrono::steady_clock::time_point l_Now = std::chrono::steady_clock::now();
		const float l_RawDeltaTime = std::chrono::duration<float>(l_Now - s_State.LastFrame).count();
		s_State.LastFrame = l_Now;

		s_State.UnscaledDeltaTime = std::min(l_RawDeltaTime, s_State.MaxDeltaTime);
		s_State.DeltaTime = s_State.UnscaledDeltaTime * s_State.TimeScale;

		s_State.UnscaledElapsedTime += s_State.UnscaledDeltaTime;
		s_State.ElapsedTime += s_State.DeltaTime;

		++s_State.FrameCount;

		const float l_MaxAccumulated = s_State.FixedDeltaTime * static_cast<float>(s_State.MaxFixedStepsPerFrame);
		s_State.FixedAccumulator = std::min(s_State.FixedAccumulator + s_State.DeltaTime, l_MaxAccumulated);
	}

	bool Time::ConsumeFixedStep()
	{
		if (s_State.FixedAccumulator < s_State.FixedDeltaTime)
		{
			return false;
		}

		s_State.FixedAccumulator -= s_State.FixedDeltaTime;

		return true;
	}
}