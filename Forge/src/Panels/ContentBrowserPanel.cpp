#include "trpch.h"
#include "ContentBrowserPanel.h"

#include <imgui/imgui.h>

#include <filesystem>

namespace Trinity
{
	extern const std::filesystem::path g_AssetPath = "assets";

	ContentBrowserPanel::ContentBrowserPanel() : m_CurrentDirectory(g_AssetPath)
	{
		m_DirectoryIcon = Texture2D::Create("Resources/Icons/ContentBrowser/DirectoryIcon.png");
		m_FileIcon = Texture2D::Create("Resources/Icons/ContentBrowser/FileIcon.png");
		m_PNGIcon = Texture2D::Create("Resources/Icons/ContentBrowser/PNGIcon.png");
		m_BackIcon = Texture2D::Create("Resources/Icons/ContentBrowser/PNGIcon.png");
		m_TextIcon = Texture2D::Create("Resources/Icons/ContentBrowser/TextIcon.png");
		m_SceneIcon = Texture2D::Create("Resources/Icons/ContentBrowser/SceneIcon.png");
		m_FontIcon = Texture2D::Create("Resources/Icons/ContentBrowser/FontIcon.png");
	}

	ContentBrowserPanel::ContentBrowserPanel(const Ref<Scene>& context)
	{
		SetContext(context);
	}

	void ContentBrowserPanel::SetContext(const Ref<Scene>& context)
	{
		m_Context = context;
	}

	void ContentBrowserPanel::OnImGuiRender()
	{
		ImGui::Begin("Content Browser");
		{
			if (m_Context)
			{
				if (m_CurrentDirectory != std::filesystem::path(g_AssetPath))
				{
					if (ImGui::Button("<-"))
					{
						m_CurrentDirectory = m_CurrentDirectory.parent_path();
					}
				}

				static float padding = 16.0f;
				static float thumbnailSize = 70.0f;

				float cellSize = thumbnailSize + padding;
				float panelWidth = ImGui::GetContentRegionAvail().x;

				int columnCount = (int)(panelWidth / cellSize);
				if (columnCount < 1)
				{
					columnCount = 1;
				}
				ImGui::Columns(columnCount, 0, false);

				for (auto& directoryEntry : std::filesystem::directory_iterator(m_CurrentDirectory))
				{
					const auto& path = directoryEntry.path();

					auto relativePath = std::filesystem::relative(path, g_AssetPath);
					std::string filenameString = relativePath.filename().string();
					ImGui::PushID(filenameString.c_str());

					Ref<Texture2D> icon = directoryEntry.is_directory() ? m_DirectoryIcon : m_FileIcon;

					if (icon->GetRendererID() == m_FileIcon->GetRendererID())
					{
						if (relativePath.extension() == ".png")
						{
							icon = m_PNGIcon;
						}

						else if (relativePath.extension() == ".txt")
						{
							icon = m_TextIcon;
						}

						else if (relativePath.extension() == ".trinity")
						{
							icon = m_SceneIcon;
						}

						else if (relativePath.extension() == ".ttf")
						{
							icon = m_FontIcon;
						}

						else
						{
							icon = m_FileIcon;
						}
					}

					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
					ImGui::ImageButton((ImTextureID)icon->GetRendererID(), { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });

					if (ImGui::BeginDragDropSource())
					{
						const wchar_t* itemPath = relativePath.c_str();
						ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));

						ImGui::EndDragDropSource();
					}

					ImGui::PopStyleColor(1);
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						if (directoryEntry.is_directory())
						{
							m_CurrentDirectory /= path.filename();
						}
					}

					ImGui::TextWrapped(filenameString.c_str());
					ImGui::NextColumn();

					ImGui::PopID();
				}

				// TODO: Add functions to "Add/Delete files and folders"
				if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					if (ImGui::IsAnyItemHovered())
					{
						TR_CORE_INFO("Item Property Menu");
					}
					else
					{
						if (ImGui::BeginPopupContextWindow())
						{
							if (ImGui::MenuItem("New Folder"))
							{
								TR_CORE_INFO("Add Item Menu");
							}

							ImGui::EndPopup();
						}
					}
				}

				ImGui::End();
			}
		}
	}
}