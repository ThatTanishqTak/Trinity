#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

#include "FileWatch.hpp"

namespace Trinity
{
	class Shader;

	namespace Resources
	{
		class ShaderWatcher
		{
		public:
			static void Start();
			static void Shutdown();
			static void Watch(Shader* shader, const std::filesystem::path& path);

			struct DirectoryWatch
			{
				std::unique_ptr<filewatch::FileWatch<std::string>> Watcher;
				std::vector<Shader*> Shaders;
			};

			static std::unordered_map<std::string, DirectoryWatch> s_Watchers;
			static bool s_Running;
		};
	}
}