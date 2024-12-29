#pragma once

#include "Trinity/Renderer/Texture.h"
#include "Trinity/Scene/Scene.h"

#include <filesystem>

namespace Trinity
{
	class ContentBrowserPanel
	{
	public:
		ContentBrowserPanel();
		ContentBrowserPanel(const Ref<Scene>& context);

		void SetContext(const Ref<Scene>& context);
		void OnImGuiRender();

	private:
		std::filesystem::path m_CurrentDirectory;
		
		Ref<Scene> m_Context;

		Ref<Texture2D> m_DirectoryIcon;
		Ref<Texture2D> m_FileIcon;
		Ref<Texture2D> m_PNGIcon;
		Ref<Texture2D> m_BackIcon;
		Ref<Texture2D> m_TextIcon;
		Ref<Texture2D> m_SceneIcon;
		Ref<Texture2D> m_FontIcon;
	};
}