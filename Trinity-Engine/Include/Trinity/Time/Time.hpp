#pragma once

#include "Trinity/Time/Timestep.hpp"

#include <cstdint>

namespace Trinity
{
	class Application;

	class Time
	{
	public:
		Time() = delete;

		static Timestep GetDeltaTime();
		static Timestep GetUnscaledDeltaTime();

		static double GetElapsedTime();
		static double GetUnscaledElapsedTime();

		static uint64_t GetFrameCount();

		static float GetTimeScale();
		static void SetTimeScale(float timeScale);

		static Timestep GetFixedDeltaTime();
		static void SetFixedDeltaTime(Timestep fixedDeltaTime);
		static float GetFixedAlpha();

		static Timestep GetMaxDeltaTime();
		static void SetMaxDeltaTime(Timestep maxDeltaTime);

		static uint32_t GetMaxFixedStepsPerFrame();
		static void SetMaxFixedStepsPerFrame(uint32_t maxSteps);

	private:
		friend class Application;

		static void Reset();
		static void Tick();
		static bool ConsumeFixedStep();
	};
}