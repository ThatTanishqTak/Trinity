#include "Trinity/Core/Log.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Trinity
{
	namespace
	{
		constexpr std::chrono::seconds s_FlushInterval{ 3 };
		constexpr size_t s_MaxLogFiles = 10;
		constexpr std::u8string_view s_LogFilePrefix = u8"Trinity_";
		constexpr std::u8string_view s_LogFileExtension = u8".log";

		struct FileSinkResult
		{
			spdlog::sink_ptr Sink;
			std::filesystem::path Path;
			std::vector<std::string> Errors;
		};

		std::string ToDisplayString(const std::filesystem::path& path)
		{
			const std::u8string l_Utf8 = path.u8string();

			return std::string(l_Utf8.begin(), l_Utf8.end());
		}

		std::filesystem::path GetUserLogDirectory()
		{
#if defined(_WIN32)
			wchar_t* l_LocalAppData = nullptr;
			size_t l_Length = 0;

			std::filesystem::path l_Directory;
			if (_wdupenv_s(&l_LocalAppData, &l_Length, L"LOCALAPPDATA") == 0 && l_LocalAppData && *l_LocalAppData)
			{
				l_Directory = std::filesystem::path(l_LocalAppData) / "Trinity" / "Logs";
			}

			std::free(l_LocalAppData);

			return l_Directory;
#elif defined(__APPLE__)
			const char* l_Home = std::getenv("HOME");

			return (l_Home && *l_Home) ? std::filesystem::path(l_Home) / "Library" / "Logs" / "Trinity" : std::filesystem::path{};
#else
			const char* l_StateHome = std::getenv("XDG_STATE_HOME");
			if (l_StateHome && std::filesystem::path(l_StateHome).is_absolute())
			{
				return std::filesystem::path(l_StateHome) / "Trinity" / "Logs";
			}

			const char* l_Home = std::getenv("HOME");

			return (l_Home && *l_Home) ? std::filesystem::path(l_Home) / ".local" / "state" / "Trinity" / "Logs" : std::filesystem::path{};
#endif
		}

		std::string GetLaunchTimestamp()
		{
			const std::time_t l_Now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

			std::tm l_LocalTime{};
#if defined(_WIN32)
			localtime_s(&l_LocalTime, &l_Now);
#else
			localtime_r(&l_Now, &l_LocalTime);
#endif

			return std::format("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}", l_LocalTime.tm_year + 1900, l_LocalTime.tm_mon + 1, l_LocalTime.tm_mday, l_LocalTime.tm_hour, l_LocalTime.tm_min, l_LocalTime.tm_sec);
		}

		std::filesystem::path MakeUniqueLogFilePath(const std::filesystem::path& directory)
		{
			const std::string l_Timestamp = GetLaunchTimestamp();

			std::error_code l_Error;
			std::filesystem::path l_Path = directory / std::format("Trinity_{}.log", l_Timestamp);

			for (uint32_t l_Suffix = 1; std::filesystem::exists(l_Path, l_Error); ++l_Suffix)
			{
				l_Path = directory / std::format("Trinity_{}_{}.log", l_Timestamp, l_Suffix);
			}

			return l_Path;
		}

		void PruneOldLogFiles(const std::filesystem::path& directory)
		{
			std::error_code l_Error;
			std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> l_LogFiles;

			for (std::filesystem::directory_iterator l_Entry(directory, l_Error), l_End; !l_Error && l_Entry != l_End; l_Entry.increment(l_Error))
			{
				const std::u8string l_Name = l_Entry->path().filename().u8string();
				if (l_Entry->is_regular_file(l_Error) && l_Name.starts_with(s_LogFilePrefix) && l_Name.ends_with(s_LogFileExtension))
				{
					l_LogFiles.emplace_back(l_Entry->last_write_time(l_Error), l_Entry->path());
				}
			}

			if (l_LogFiles.size() < s_MaxLogFiles)
			{
				return;
			}

			std::sort(l_LogFiles.begin(), l_LogFiles.end());

			const size_t l_RemoveCount = l_LogFiles.size() - (s_MaxLogFiles - 1);
			for (size_t l_Index = 0; l_Index < l_RemoveCount; ++l_Index)
			{
				std::filesystem::remove(l_LogFiles[l_Index].second, l_Error);
			}
		}

		FileSinkResult CreateFileSink()
		{
			FileSinkResult l_Result;

			std::vector<std::filesystem::path> l_Directories{ GetUserLogDirectory() };

			std::error_code l_TempError;
			const std::filesystem::path l_TempDirectory = std::filesystem::temp_directory_path(l_TempError);
			if (!l_TempError)
			{
				l_Directories.push_back(l_TempDirectory / "Trinity" / "Logs");
			}

			for (const std::filesystem::path& l_Directory : l_Directories)
			{
				if (l_Directory.empty())
				{
					continue;
				}

				try
				{
					std::error_code l_Error;
					std::filesystem::create_directories(l_Directory, l_Error);
					if (l_Error)
					{
						l_Result.Errors.push_back(std::format("Could not create {}: {}", ToDisplayString(l_Directory), l_Error.message()));

						continue;
					}

					PruneOldLogFiles(l_Directory);

					const std::filesystem::path l_Path = MakeUniqueLogFilePath(l_Directory);
					l_Result.Sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(l_Path.string(), true);
					l_Result.Path = l_Path;

					return l_Result;
				}
				catch (const std::exception& exception)
				{
					l_Result.Errors.push_back(std::format("Could not open a log file in {}: {}", ToDisplayString(l_Directory), exception.what()));
				}
			}

			return l_Result;
		}

		std::shared_ptr<spdlog::logger> CreateLogger(const char* name, const std::vector<spdlog::sink_ptr>& sinks)
		{
			std::shared_ptr<spdlog::logger> l_Logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
			spdlog::register_logger(l_Logger);
			l_Logger->set_level(spdlog::level::trace);
			l_Logger->flush_on(spdlog::level::warn);

			return l_Logger;
		}

		std::shared_ptr<spdlog::logger> CreateEarlyLogger(const char* name)
		{
			std::shared_ptr<spdlog::logger> l_Logger = std::make_shared<spdlog::logger>(name, std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
			l_Logger->set_pattern("%^[%T] %n: %v%$");
			l_Logger->set_level(spdlog::level::trace);

			return l_Logger;
		}

		std::shared_ptr<spdlog::logger> CreateSilentLogger(const char* name)
		{
			std::shared_ptr<spdlog::logger> l_Logger = std::make_shared<spdlog::logger>(name);
			l_Logger->set_level(spdlog::level::off);

			return l_Logger;
		}
	}

	std::shared_ptr<spdlog::logger>& Log::GetCoreLogger()
	{
		static std::shared_ptr<spdlog::logger>* s_Logger = new std::shared_ptr<spdlog::logger>(CreateEarlyLogger("TRINITY"));

		return *s_Logger;
	}

	std::shared_ptr<spdlog::logger>& Log::GetClientLogger()
	{
		static std::shared_ptr<spdlog::logger>* s_Logger = new std::shared_ptr<spdlog::logger>(CreateEarlyLogger("APP"));

		return *s_Logger;
	}

	void Log::Initialize()
	{
		std::vector<spdlog::sink_ptr> l_LogSinks;

		spdlog::sink_ptr l_ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		l_ConsoleSink->set_pattern("%^[%T] %n: %v%$");
		l_LogSinks.push_back(l_ConsoleSink);

		const FileSinkResult l_FileSink = CreateFileSink();
		if (l_FileSink.Sink)
		{
			l_FileSink.Sink->set_pattern("[%T] [%l] %n: %v");
			l_LogSinks.push_back(l_FileSink.Sink);
		}

		GetCoreLogger() = CreateLogger("TRINITY", l_LogSinks);
		GetClientLogger() = CreateLogger("APP", l_LogSinks);

		spdlog::flush_every(s_FlushInterval);

		for (const std::string& l_Error : l_FileSink.Errors)
		{
			TR_CORE_WARN("{}", l_Error);
		}

		if (l_FileSink.Sink)
		{
			TR_CORE_INFO("Logging to {}", ToDisplayString(l_FileSink.Path));
		}
		else
		{
			TR_CORE_ERROR("No writable log folder found, logging to the console only");
		}
	}

	void Log::Shutdown()
	{
		GetClientLogger() = CreateSilentLogger("APP");
		GetCoreLogger() = CreateSilentLogger("TRINITY");

		spdlog::shutdown();
	}
}