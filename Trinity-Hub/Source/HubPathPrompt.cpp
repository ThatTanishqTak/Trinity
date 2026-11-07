#include "HubPathPrompt.hpp"

#include "HubText.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <utility>

namespace
{
    constexpr const char* c_PathPopup = "Choose a Path###HubPath";
}

void HubPathPrompt::Ask(Request request)
{
    if (!Trinity::FileDialog::IsSupported())
    {
        m_Text = HubText::ToUtf8(request.Folder);
        m_Prompt = std::move(request);
        m_OpenPopup = true;

        return;
    }

    m_Dialog = std::move(request);
}

void HubPathPrompt::Update()
{
    if (std::function<void()> l_Chosen = std::exchange(m_Chosen, nullptr))
    {
        l_Chosen();
    }

    if (!m_Dialog)
    {
        return;
    }

    Request l_Request = std::move(*m_Dialog);
    m_Dialog.reset();

    const std::optional<std::filesystem::path> l_Path = l_Request.Type == Kind::Folder ? Trinity::FileDialog::PickFolder(l_Request.Title, l_Request.Folder) : Trinity::FileDialog::OpenFile(l_Request.Title, l_Request.Filters, l_Request.Folder);
    if (l_Path)
    {
        l_Request.OnChosen(*l_Path);
    }
}

void HubPathPrompt::Draw()
{
    if (std::exchange(m_OpenPopup, false))
    {
        ImGui::OpenPopup(c_PathPopup);
    }

    if (!m_Prompt)
    {
        return;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool l_Open = true;
    if (!ImGui::BeginPopupModal(c_PathPopup, &l_Open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        if (!l_Open)
        {
            m_Prompt.reset();
        }

        return;
    }

    ImGui::TextUnformatted(m_Prompt->Title.c_str());
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 40.0f);
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    const bool l_Entered = ImGui::InputText("##Path", &m_Text, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();

    if ((ImGui::Button("OK") || l_Entered) && !m_Text.empty())
    {
        m_Chosen = [l_Action = std::move(m_Prompt->OnChosen), l_Path = HubText::FromUtf8(m_Text)]() { l_Action(l_Path); };
        m_Prompt.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        m_Prompt.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}