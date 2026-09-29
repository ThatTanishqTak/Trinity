#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace Trinity
{
	class Timer
	{
	public:
		Timer() { Reset(); }

		void Reset() { m_Start = std::chrono::steady_clock::now(); }

		float GetElapsedSeconds() const { return std::chrono::duration<float>(std::chrono::steady_clock::now() - m_Start).count(); }
		float GetElapsedMilliseconds() const { return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - m_Start).count(); }

	private:
		std::chrono::steady_clock::time_point m_Start;
	};

	class ScopedTimer
	{
	public:
		explicit ScopedTimer(std::string_view name) : m_Name(name) {}
		~ScopedTimer();

		ScopedTimer(const ScopedTimer&) = delete;
		ScopedTimer& operator=(const ScopedTimer&) = delete;
		ScopedTimer(ScopedTimer&&) = delete;
		ScopedTimer& operator=(ScopedTimer&&) = delete;

	private:
		std::string m_Name;
		Timer m_Timer;
	};
}

#define TR_CONCAT_IMPL(a, b) a##b
#define TR_CONCAT(a, b) TR_CONCAT_IMPL(a, b)
#define TR_SCOPED_TIMER(name) ::Trinity::ScopedTimer TR_CONCAT(l_ScopedTimer, __LINE__)(name)