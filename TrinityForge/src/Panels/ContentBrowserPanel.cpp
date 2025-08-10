#include "Trinity/trpch.h"
#include "ContentBrowserPanel.h"

#include <imgui.h>
#include <unordered_set>

namespace
{
    const std::unordered_set<std::string> s_TextureExtensions = { ".png", ".jpg", ".jpeg", ".bmp" };
}

ContentBrowserPanel::ContentBrowserPanel(const std::filesystem::path& baseDirectory) : m_BaseDirectory(baseDirectory), m_CurrentDirectory(baseDirectory)
{

}

void ContentBrowserPanel::DrawDirectory(const std::filesystem::path& path)
{
    for (auto& l_Entry : std::filesystem::directory_iterator(path))
    {
        const auto& l_Path = l_Entry.path();
        std::string l_Name = l_Path.filename().string();

        ImGui::PushID(l_Name.c_str());

        ImVec2 l_ButtonSize{ m_ThumbnailSize, m_ThumbnailSize };
        if (l_Entry.is_directory())
        {
            if (ImGui::Button("DIR", l_ButtonSize))
            {
                m_CurrentDirectory /= l_Path.filename();
            }
        }

        else
        {
            bool l_IsTexture = s_TextureExtensions.find(l_Path.extension().string()) != s_TextureExtensions.end();
            ImGui::Button(l_IsTexture ? "TEX" : "FILE", l_ButtonSize);

            if (ImGui::BeginDragDropSource())
            {
                std::string l_RelativePath = std::filesystem::relative(l_Path, m_BaseDirectory).string();
                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", l_RelativePath.c_str(), l_RelativePath.size() + 1);
                ImGui::TextUnformatted(l_Name.c_str());
                ImGui::EndDragDropSource();
            }
        }

        ImGui::TextWrapped(l_Name.c_str());
        ImGui::NextColumn();

        ImGui::PopID();
    }
}

void ContentBrowserPanel::OnUIRender()
{
    ImGui::Begin("Content Browser");

    if (m_CurrentDirectory != m_BaseDirectory)
    {
        if (ImGui::Button("<-"))
        {
            m_CurrentDirectory = m_CurrentDirectory.parent_path();
        }
    }

    float l_CellSize = m_ThumbnailSize + m_Padding;
    float l_PanelWidth = ImGui::GetContentRegionAvail().x;
    int l_ColumnCount = static_cast<int>(l_PanelWidth / l_CellSize);
    if (l_ColumnCount < 1)
    {
        l_ColumnCount = 1;
    }

    ImGui::Columns(l_ColumnCount, nullptr, false);

    DrawDirectory(m_CurrentDirectory);

    ImGui::Columns(1);

    ImGui::End();
}