#include "HubLayer.hpp"

#include "Pages/InstallsPage.hpp"
#include "Pages/ProjectsPage.hpp"
#include "Pages/SettingsPage.hpp"

#include <imgui.h>

#include <format>
#include <string>
#include <utility>

namespace
{
    constexpr ImGuiWindowFlags c_ShellFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }
}

HubLayer::HubLayer(std::filesystem::path directory) : Layer("Hub"), m_Directory(std::move(directory))
{
    m_Pages.push_back(Trinity::CreateScope<ProjectsPage>());
    m_Pages.push_back(Trinity::CreateScope<InstallsPage>());
    m_Pages.push_back(Trinity::CreateScope<SettingsPage>(m_Settings, m_Directory));
}

// Only the UI is drawn, so the window gets no scene under it
void HubLayer::OnAttach()
{
    m_Settings = HubSettings::Load();
    m_Settings.Apply();

    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    l_Renderer.SetSceneCopy(false);
    l_Renderer.SetTitle("Trinity Hub");

    TR_INFO("Hub: keeping its lists and settings in {}", ToUtf8(m_Directory));
}

// One window over the whole of the main one, the sidebar down its left and the page beside it
void HubLayer::OnImGuiRender()
{
    const ImGuiViewport* l_Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(l_Viewport->WorkPos);
    ImGui::SetNextWindowSize(l_Viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool l_Open = ImGui::Begin("Trinity Hub###HubShell", nullptr, c_ShellFlags);
    ImGui::PopStyleVar(3);
    if (l_Open)
    {
        DrawSidebar();
        ImGui::SameLine(0.0f, 0.0f);

        const float l_Padding = ImGui::GetFontSize() * 1.5f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(l_Padding, l_Padding));
        if (ImGui::BeginChild("##HubPage", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            m_Pages[m_Page]->Draw();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    ImGui::End();
}

// The pages by icon and name, the chosen one lit, under Hub's name, and the engine Hub runs on at the foot
void HubLayer::DrawSidebar()
{
    const float l_Width = ImGui::GetFontSize() * 13.0f;
    const float l_Padding = ImGui::GetFontSize() * 0.75f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(l_Padding, l_Padding));
    if (ImGui::BeginChild("##HubSidebar", ImVec2(l_Width, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::TextUnformatted("Trinity Hub");
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::Spacing();

        const float l_Row = ImGui::GetFrameHeight() * 1.4f;
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        for (std::size_t it_Index = 0; it_Index < m_Pages.size(); ++it_Index)
        {
            const HubPage& l_Page = *m_Pages[it_Index];
            const std::string l_Label = std::format("{}  {}###HubPage{}", l_Page.GetIcon(), l_Page.GetName(), it_Index);
            if (ImGui::Selectable(l_Label.c_str(), it_Index == m_Page, ImGuiSelectableFlags_None, ImVec2(0.0f, l_Row)))
            {
                m_Page = it_Index;
            }
        }

        ImGui::PopStyleVar();

        const std::string l_Engine = std::format("Trinity {}", Trinity::GetVersionDescription());
        const float l_Foot = ImGui::GetWindowHeight() - l_Padding - ImGui::GetTextLineHeight();
        if (l_Foot > ImGui::GetCursorPosY())
        {
            ImGui::SetCursorPosY(l_Foot);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(l_Engine.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}