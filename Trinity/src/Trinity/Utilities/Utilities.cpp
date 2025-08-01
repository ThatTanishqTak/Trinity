#include "Trinity/trpch.h"

#include "Trinity/Utilities/Utilities.h"

#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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

		std::vector<std::byte> FileManagement::LoadTexture(const std::string& filePath, int& width, int& height)
		{
			stbi_set_flip_vertically_on_load(1);

			int l_Channels = 0;
			stbi_uc* l_Pixels = stbi_load(filePath.c_str(), &width, &height, &l_Channels, STBI_rgb_alpha);
			if (!l_Pixels)
			{
				TR_CORE_ERROR("Failed to load texture: {}", filePath);
				return std::vector<std::byte>();
			}

			size_t l_ImageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
			std::vector<std::byte> l_Buffer(l_ImageSize);
			memcpy(l_Buffer.data(), l_Pixels, l_ImageSize);
			stbi_image_free(l_Pixels);

			TR_CORE_TRACE("Texture loaded: {}", filePath);

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