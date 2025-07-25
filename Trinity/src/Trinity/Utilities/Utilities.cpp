#include "Trinity/trpch.h"

#include "Trinity/Utilities/Utilities.h"

#include <GLFW/glfw3.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace Trinity
{
	namespace Utilities
	{
		std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
		std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

		void Log::Init()
		{
			std::vector<spdlog::sink_ptr> l_LogSinks;
			l_LogSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
			l_LogSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("Trinity.log", true));

			l_LogSinks[0]->set_pattern("%^[%T] %n: %v%$");
			l_LogSinks[1]->set_pattern("[%T] [%l] %n: %v");

			s_CoreLogger = std::make_shared<spdlog::logger>("TRINITY", begin(l_LogSinks), end(l_LogSinks));
			spdlog::register_logger(s_CoreLogger);
			s_CoreLogger->set_level(spdlog::level::trace);
			s_CoreLogger->flush_on(spdlog::level::trace);

			s_ClientLogger = std::make_shared<spdlog::logger>("FORGE", begin(l_LogSinks), end(l_LogSinks));
			spdlog::register_logger(s_ClientLogger);
			s_ClientLogger->set_level(spdlog::level::trace);
			s_ClientLogger->flush_on(spdlog::level::trace);
		}

		//----------------------------------------------------------------------------------------------------------------------------------------------------//

		std::vector<std::byte> FileManagement::ReadFile(const std::string& filePath)
        {
			if (!std::filesystem::exists(filePath))
			{
				TR_CORE_ERROR("File does not exist: {}", filePath);

				return std::vector<std::byte>();
			}

            const auto l_FileSize = std::filesystem::file_size(filePath);

            std::ifstream l_File(filePath, std::ios::binary);
            if (!l_File)
            {
				TR_CORE_ERROR("Failed to open l_File: {}", filePath);

				return std::vector<std::byte>();
            }

            std::vector<std::byte> l_Buffer;
            l_Buffer.resize(l_FileSize);
            if (l_FileSize > 0)
            {
                l_File.read(reinterpret_cast<char*>(l_Buffer.data()), static_cast<std::streamsize>(l_FileSize));
                if (!l_File)
                {
                    TR_CORE_ERROR("Failed to read l_File: {}", filePath);

					return std::vector<std::byte>();
                }
            }

			TR_CORE_TRACE("File loaded: {}", filePath);

            return l_Buffer;
        }

		//----------------------------------------------------------------------------------------------------------------------------------------------------//

		float Time::s_FPS = 0.0f;
		float Time::s_DeltaTime = 0.0f;
		double Time::s_LastFrameTime = 0.0;

		void Time::Init()
		{
			s_LastFrameTime = glfwGetTime();
		}

		void Time::Update()
		{
			double l_CurrentTime = glfwGetTime();

			s_DeltaTime = static_cast<float>(l_CurrentTime - s_LastFrameTime);
			s_FPS = 1.0f / s_DeltaTime;

			s_LastFrameTime = l_CurrentTime;
		}
	}
}