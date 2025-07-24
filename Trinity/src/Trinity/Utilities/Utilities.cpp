#include "Trinity/trpch.h"

#include "Trinity/Utilities/Utilities.h"

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
			std::vector<spdlog::sink_ptr> logSinks;
			logSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
			logSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("Trinity.log", true));

			logSinks[0]->set_pattern("%^[%T] %n: %v%$");
			logSinks[1]->set_pattern("[%T] [%l] %n: %v");

			s_CoreLogger = std::make_shared<spdlog::logger>("TRINITY", begin(logSinks), end(logSinks));
			spdlog::register_logger(s_CoreLogger);
			s_CoreLogger->set_level(spdlog::level::trace);
			s_CoreLogger->flush_on(spdlog::level::trace);

			s_ClientLogger = std::make_shared<spdlog::logger>("FORGE", begin(logSinks), end(logSinks));
			spdlog::register_logger(s_ClientLogger);
			s_ClientLogger->set_level(spdlog::level::trace);
			s_ClientLogger->flush_on(spdlog::level::trace);
		}

		std::vector<std::byte> FileManagement::ReadFile(const std::string& filePath)
        {
			if (!std::filesystem::exists(filePath))
			{
				TR_CORE_ERROR("File does not exist: {}", filePath);

				return std::vector<std::byte>();
			}

            const auto fileSize = std::filesystem::file_size(filePath);

            std::ifstream file(filePath, std::ios::binary);
            if (!file)
            {
				TR_CORE_ERROR("Failed to open file: {}", filePath);

				return std::vector<std::byte>();
            }

            std::vector<std::byte> buffer;
            buffer.resize(fileSize);
            if (fileSize > 0)
            {
                file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));
                if (!file)
                {
                    TR_CORE_ERROR("Failed to read file: {}", filePath);

					return std::vector<std::byte>();
                }
            }

            return buffer;
        }
	}
}