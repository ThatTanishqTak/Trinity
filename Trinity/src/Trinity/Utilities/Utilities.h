#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

// This ignores all warnings raised inside External headers
#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#pragma warning(pop)

namespace Trinity
{
	namespace Utilities
	{
		class Log
		{
		public:
			static void Init();

			static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
			static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

		private:
			static std::shared_ptr<spdlog::logger> s_CoreLogger;
			static std::shared_ptr<spdlog::logger> s_ClientLogger;
		};

		//----------------------------------------------------------------------------------------------------------------------------------------------------//

		class FileManagement
		{
		public:
			static bool EnsureFileExists(const std::filesystem::path& filePath);
			static std::vector<std::byte> ReadFile(const std::filesystem::path& filePath);
			static std::vector<std::byte> LoadTexture(const std::filesystem::path& filePath, int& width, int& height);
		};

		//----------------------------------------------------------------------------------------------------------------------------------------------------//

		class Time
		{
		public:
			static void Init();
			static void Update();

			static float GetFPS() { return s_FPS; }
			static float GetDeltaTime() { return s_DeltaTime; }

		private:
			static double s_LastFrameTime;
			static float s_FPS;
			static float s_DeltaTime;
		};
	}
}

template<typename OStream, glm::length_t L, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::vec<L, T, Q>& vector)
{
	return os << glm::to_string(vector);
}

template<typename OStream, glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::mat<C, R, T, Q>& matrix)
{
	return os << glm::to_string(matrix);
}

template<typename OStream, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, glm::qua<T, Q> quaternion)
{
	return os << glm::to_string(quaternion);
}

// Core log macros
#define TR_CORE_TRACE(...)      ::Trinity::Utilities::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define TR_CORE_INFO(...)       ::Trinity::Utilities::Log::GetCoreLogger()->info(__VA_ARGS__)
#define TR_CORE_WARN(...)       ::Trinity::Utilities::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define TR_CORE_ERROR(...)      ::Trinity::Utilities::Log::GetCoreLogger()->error(__VA_ARGS__)
#define TR_CORE_CRITICAL(...)   ::Trinity::Utilities::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client log macros
#define TR_TRACE(...)           ::Trinity::Utilities::Log::GetClientLogger()->trace(__VA_ARGS__)
#define TR_INFO(...)            ::Trinity::Utilities::Log::GetClientLogger()->info(__VA_ARGS__)
#define TR_WARN(...)            ::Trinity::Utilities::Log::GetClientLogger()->warn(__VA_ARGS__)
#define TR_ERROR(...)           ::Trinity::Utilities::Log::GetClientLogger()->error(__VA_ARGS__)
#define TR_CRITICAL(...)        ::Trinity::Utilities::Log::GetClientLogger()->critical(__VA_ARGS__)