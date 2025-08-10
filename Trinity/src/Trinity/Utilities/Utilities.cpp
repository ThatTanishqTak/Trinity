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

            auto a_CreateLogger = [&](const char* name) -> std::shared_ptr<spdlog::logger>
                {
                    auto a_Logger = std::make_shared<spdlog::logger>(name, begin(l_LogSinks), end(l_LogSinks));
                    spdlog::register_logger(a_Logger);
                    a_Logger->set_level(spdlog::level::trace);
                    a_Logger->flush_on(spdlog::level::trace);

                    return a_Logger;
                };

            s_CoreLogger = a_CreateLogger("TRINITY");
            s_ClientLogger = a_CreateLogger("FORGE");
        }

		//----------------------------------------------------------------------------------------------------------------------------------------------------//

        bool FileManagement::EnsureFileExists(const std::filesystem::path& filePath)
        {
            if (!std::filesystem::exists(filePath))
            {
                TR_CORE_ERROR("File does not exist: {}", filePath.string());

                return false;
            }

            return true;
        }

        std::vector<std::byte> FileManagement::ReadFile(const std::filesystem::path& filePath)
        {
            if (!EnsureFileExists(filePath))
            {
                return std::vector<std::byte>();
            }

            const auto a_FileSize = std::filesystem::file_size(filePath);

            std::ifstream l_File(filePath, std::ios::binary);
            if (!l_File)
            {
                TR_CORE_ERROR("Failed to open file: {}", filePath.string());

                return std::vector<std::byte>();
            }

            std::vector<std::byte> l_Buffer;
            l_Buffer.resize(a_FileSize);
            if (a_FileSize > 0)
            {
                l_File.read(reinterpret_cast<char*>(l_Buffer.data()), static_cast<std::streamsize>(a_FileSize));
                if (!l_File)
                {
                    TR_CORE_ERROR("Failed to read file: {}", filePath.string());

                    return std::vector<std::byte>();
                }
            }

            TR_CORE_TRACE("File loaded: {}", filePath.string());

            return l_Buffer;
        }

        std::vector<std::byte> FileManagement::LoadTexture(const std::filesystem::path& filePath, int& width, int& height)
        {
            if (!EnsureFileExists(filePath))
            {
                return std::vector<std::byte>();
            }

            stbi_set_flip_vertically_on_load(1);

            int l_Channels = 0;
            stbi_uc* l_Pixels = stbi_load(filePath.string().c_str(), &width, &height, &l_Channels, STBI_rgb_alpha);
            if (!l_Pixels)
            {
                TR_CORE_ERROR("Failed to load texture: {}", filePath.string());

                return std::vector<std::byte>();
            }

            size_t l_ImageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
            std::vector<std::byte> l_Buffer(l_ImageSize);
            memcpy(l_Buffer.data(), l_Pixels, l_ImageSize);
            stbi_image_free(l_Pixels);

            TR_CORE_TRACE("Texture loaded: {}", filePath.string());

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