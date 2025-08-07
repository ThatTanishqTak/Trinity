#include "Trinity/trpch.h"

#include "Trinity/Resources/ShaderWatcher.h"
#include "Trinity/Renderer/Shader.h"

#include "Trinity/Utilities/Utilities.h"

#include <algorithm>

namespace Trinity
{
	namespace Resources
	{
		std::unordered_map<std::string, ShaderWatcher::DirectoryWatch> ShaderWatcher::s_Watchers;
		bool ShaderWatcher::s_Running = false;

		static std::unique_ptr<filewatch::FileWatch<std::string>> CreateWatcher(const std::string& directory)
		{
			return std::make_unique<filewatch::FileWatch<std::string>>(directory,
				[directory](const std::string&, const filewatch::Event eventType)
				{
					if (eventType == filewatch::Event::modified || eventType == filewatch::Event::added || eventType == filewatch::Event::renamed_new)
					{
						auto a_It = ShaderWatcher::s_Watchers.find(directory);
						if (a_It != ShaderWatcher::s_Watchers.end())
						{
							for (auto* it_Shader : a_It->second.Shaders)
							{
								if (it_Shader)
								{
									it_Shader->Reload();
								}
							}
						}
					}
				});
		}

		void ShaderWatcher::Start()
		{
			if (s_Running)
			{
				return;
			}

			s_Running = true;

			for (auto& a_It : s_Watchers)
			{
				if (!a_It.second.Watcher)
				{
					a_It.second.Watcher = CreateWatcher(a_It.first);
				}
			}
			TR_CORE_TRACE("Shader watcher started");
		}

		void ShaderWatcher::Shutdown()
		{
			s_Watchers.clear();
			s_Running = false;
			TR_CORE_INFO("Shader watcher shut down");
		}

		void ShaderWatcher::Watch(Shader* shader, const std::filesystem::path& path)
		{
			if (!shader)
			{
				return;
			}

			std::string l_Directory = path.string();
			auto& a_Watch = s_Watchers[l_Directory];

			if (std::find(a_Watch.Shaders.begin(), a_Watch.Shaders.end(), shader) == a_Watch.Shaders.end())
			{
				a_Watch.Shaders.push_back(shader);
			}

			if (s_Running && !a_Watch.Watcher)
			{
				a_Watch.Watcher = CreateWatcher(l_Directory);
			}
		}
	}
}