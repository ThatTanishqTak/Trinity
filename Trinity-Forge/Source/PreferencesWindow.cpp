#include "PreferencesWindow.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstddef>
#include <format>
#include <span>
#include <utility>

namespace
{
    constexpr float c_LabelWidthInFonts = 11.0f;

    struct SectionInfo
    {
        const char* Icon;
        std::string_view Name;
        std::span<const std::string_view> Settings;
    };

    // Each section's settings by the names its rows show, which the search goes through
    constexpr std::array<std::string_view, 3> c_GeneralSettings{ "UI Scale", "Theme", "Multi-viewport" };
    constexpr std::array<std::string_view, 2> c_ExternalToolsSettings{ "Script Editor", "Wait for Debugger at Play" };
    constexpr std::array<std::string_view, 1> c_ColorsSettings{ "Play Mode Tint" };
    constexpr std::array<std::string_view, 4> c_SceneViewSettings{ "Camera Fly Speed", "Move Snap", "Rotate Snap", "Scale Snap" };

    // In Section order
    constexpr std::array<SectionInfo, 4> c_Sections
    { {
        { Trinity::Icons::c_Monitor, "General", c_GeneralSettings },
        { Trinity::Icons::c_FileCode, "External Tools", c_ExternalToolsSettings },
        { Trinity::Icons::c_PaintBrush, "Colors", c_ColorsSettings },
        { Trinity::Icons::c_Cube, "Scene View", c_SceneViewSettings }
    } };

    // ui.scale's choices, as Unity offers its UI scaling's
    constexpr std::array<float, 10> c_ScalePresets{ 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 4.0f };

    struct ThemeInfo
    {
        std::string_view Key;
        const char* Name;
    };

    // ui.theme's, ImGui's own three
    constexpr std::array<ThemeInfo, 3> c_Themes{ { { "dark", "Dark" },{ "light", "Light" },{ "classic", "Classic" } } };

    bool ContainsIgnoringCase(std::string_view text, std::string_view part)
    {
        const auto a_Lower = [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); };

        return !std::ranges::search(text, part, {}, a_Lower, a_Lower).empty();
    }

    // A label on the left and the widget filling the rest of the row
    void Label(std::string_view label)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.data(), label.data() + label.size());
        ImGui::SameLine(ImGui::GetFontSize() * c_LabelWidthInFonts);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    void Tooltip(const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", text);
        }
    }

    // As its Console line shows it, or nothing where Forge has no variable of that name
    std::string GetVariableText(std::string_view name)
    {
        const Trinity::ConsoleVariableBase* l_Variable = Trinity::ConsoleVariables::Find(name);

        return l_Variable != nullptr ? l_Variable->ToString() : std::string();
    }

    float GetScale()
    {
        const Trinity::ConsoleVariableBase* l_Variable = Trinity::ConsoleVariables::Find("ui.scale");

        return l_Variable != nullptr && l_Variable->GetType() == Trinity::ConsoleVariableType::Float ? static_cast<const Trinity::ConsoleVariable<float>*>(l_Variable)->Get() : 1.0f;
    }

    std::string FormatPercent(float scale)
    {
        return std::format("{:.0f}%", scale * 100.0f);
    }

    // Under the section's settings, below a gap
    bool ResetButton()
    {
        ImGui::Spacing();
        ImGui::Separator();

        return ImGui::Button(std::format("{} Reset to Defaults", Trinity::Icons::c_Refresh).c_str());
    }
}

PreferencesWindow::PreferencesWindow(EditorSession& session) : m_Session(session)
{

}

void PreferencesWindow::Open()
{
    m_Open = true;
    m_FocusRequested = true;
}

// A search for a section's name shows all of it, and one for a setting's name the rows it names
bool PreferencesWindow::Lists(Section section) const
{
    const SectionInfo& l_Info = c_Sections[static_cast<std::size_t>(section)];

    return m_Search.empty() || ContainsIgnoringCase(l_Info.Name, m_Search) || std::ranges::any_of(l_Info.Settings, [this](std::string_view setting) { return ContainsIgnoringCase(setting, m_Search); });
}

bool PreferencesWindow::Shows(Section section, std::string_view setting) const
{
    return m_Search.empty() || ContainsIgnoringCase(c_Sections[static_cast<std::size_t>(section)].Name, m_Search) || ContainsIgnoringCase(setting, m_Search);
}

// Centred the first time, then where it was left, which imgui.ini keeps. A section the search leaves nothing of drops off the list, and the first one it leaves is shown in place of one it hides
void PreferencesWindow::Draw()
{
    if (!m_Open)
    {
        return;
    }

    const float l_Font = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(l_Font * 46.0f, l_Font * 24.0f), ImGuiCond_FirstUseEver);
    if (m_FocusRequested)
    {
        ImGui::SetNextWindowFocus();
        m_FocusRequested = false;
    }

    if (!ImGui::Begin(std::format("{} Preferences###ForgePreferences", Trinity::Icons::c_Sliders).c_str(), &m_Open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();

        return;
    }

    if (!Lists(m_Section))
    {
        for (std::size_t it_Index = 0; it_Index < c_Sections.size(); ++it_Index)
        {
            if (Lists(static_cast<Section>(it_Index)))
            {
                m_Section = static_cast<Section>(it_Index);

                break;
            }
        }
    }

    ImGui::BeginChild("##Sections", ImVec2(l_Font * 12.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##Search", std::format("{} Search", Trinity::Icons::c_Search).c_str(), &m_Search);
    ImGui::Spacing();
    for (std::size_t it_Index = 0; it_Index < c_Sections.size(); ++it_Index)
    {
        const Section l_Section = static_cast<Section>(it_Index);
        if (Lists(l_Section) && ImGui::Selectable(std::format("{} {}", c_Sections[it_Index].Icon, c_Sections[it_Index].Name).c_str(), m_Section == l_Section))
        {
            m_Section = l_Section;
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##Settings");
    if (!Lists(m_Section))
    {
        ImGui::TextDisabled("No setting is called anything like %s", m_Search.c_str());
    }
    else
    {
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
        ImGui::SeparatorText(c_Sections[static_cast<std::size_t>(m_Section)].Name.data());
        ImGui::PopFont();
        switch (m_Section)
        {
            case Section::General:
            {
                DrawGeneral();

                break;
            }
            case Section::ExternalTools:
            {
                DrawExternalTools();

                break;
            }
            case Section::Colors:
            {
                DrawColors();

                break;
            }
            case Section::SceneView:
            {
                DrawSceneView();

                break;
            }
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

// Unity's General and UI Scaling pages: the console variables kept with the preferences, set as they are picked. The scale goes by steps, as a slider would resize the UI under the mouse as it moved
void PreferencesWindow::DrawGeneral()
{
    if (Shows(Section::General, "UI Scale"))
    {
        const float l_Scale = GetScale();
        Label("UI Scale");
        if (ImGui::BeginCombo("##Scale", FormatPercent(l_Scale).c_str()))
        {
            for (const float it_Preset : c_ScalePresets)
            {
                if (ImGui::Selectable(FormatPercent(it_Preset).c_str(), it_Preset == l_Scale))
                {
                    m_Session.SetPreferenceVariable("ui.scale", std::format("{}", it_Preset));
                }
            }

            ImGui::EndCombo();
        }

        Tooltip(std::format("On top of the monitor's own scale, which Forge follows by itself, so text is {:.0f} px now. The Console's ui.scale takes any size from {} to {}", ImGui::GetFontSize(), Trinity::ImGuiLayer::c_MinimumUserScale, Trinity::ImGuiLayer::c_MaximumUserScale).c_str());
    }

    if (Shows(Section::General, "Theme"))
    {
        const std::string l_Theme = GetVariableText("ui.theme");
        const auto a_Known = std::ranges::find(c_Themes, std::string_view(l_Theme), &ThemeInfo::Key);
        Label("Theme");
        if (ImGui::BeginCombo("##Theme", a_Known != c_Themes.end() ? a_Known->Name : l_Theme.c_str()))
        {
            for (const ThemeInfo& it_Theme : c_Themes)
            {
                if (ImGui::Selectable(it_Theme.Name, it_Theme.Key == l_Theme))
                {
                    m_Session.SetPreferenceVariable("ui.theme", it_Theme.Key);
                }
            }

            ImGui::EndCombo();
        }

        Tooltip("The colours the editor is drawn in, ImGui's own three. While a scene plays, the backgrounds lean towards the Play Mode Tint over any of them");
    }

    if (Shows(Section::General, "Multi-viewport"))
    {
        // Greyed out where the platform has no windows for ImGui to float panels in, as the View menu's item is
        const bool l_Supported = (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_PlatformHasViewports) != 0;
        bool l_Viewports = l_Supported && GetVariableText("ui.viewports") == "true";
        Label("Multi-viewport");
        ImGui::BeginDisabled(!l_Supported);
        if (ImGui::Checkbox("##Viewports", &l_Viewports))
        {
            m_Session.SetPreferenceVariable("ui.viewports", l_Viewports ? "true" : "false");
        }

        ImGui::EndDisabled();
        Tooltip(l_Supported ? "Panels and windows dragged out of Forge's window become windows of their own, as the View menu's Multi-viewport does" : "This platform has no windows for panels to float in");
    }

    if (ResetButton())
    {
        for (const std::string_view it_Name : EditorPreferences::c_Variables)
        {
            if (const Trinity::ConsoleVariableBase* l_Variable = Trinity::ConsoleVariables::Find(it_Name))
            {
                m_Session.SetPreferenceVariable(it_Name, l_Variable->DefaultToString());
            }
        }
    }

    Tooltip("UI Scale 100%, the Dark theme, and multi-viewport on");
}

// Unity's External Tools page: the script editor, with one not on this machine greyed, as the Scripts menu shows them, and whether Play waits for its debugger
void PreferencesWindow::DrawExternalTools()
{
    if (Shows(Section::ExternalTools, "Script Editor"))
    {
        Label("Script Editor");
        if (ImGui::BeginCombo("##ScriptEditor", std::string(IdeLauncher::ToString(m_Session.GetScriptEditor())).c_str()))
        {
            for (const ScriptEditor it_Editor : c_ScriptEditors)
            {
                const bool l_Available = IdeLauncher::IsAvailable(it_Editor);
                if (ImGui::Selectable(std::string(IdeLauncher::ToString(it_Editor)).c_str(), m_Session.GetScriptEditor() == it_Editor, l_Available ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled))
                {
                    m_Session.SetScriptEditor(it_Editor);
                }

                if (!l_Available)
                {
                    Tooltip(std::format("{} was not found on this machine", IdeLauncher::ToString(it_Editor)).c_str());
                }
            }

            ImGui::EndCombo();
        }

        Tooltip("What scripts and build errors open in, and whose debugger Attach Debugger attaches. System opens a file in whatever the system opens it with");
    }

    if (Shows(Section::ExternalTools, "Wait for Debugger at Play"))
    {
        bool l_Wait = m_Session.GetWaitForDebugger();
        Label("Wait for Debugger at Play");
        if (ImGui::Checkbox("##WaitForDebugger", &l_Wait))
        {
            m_Session.SetWaitForDebugger(l_Wait);
        }

        Tooltip("Play waits until a native or a managed debugger is attached, so breakpoints in OnCreate are hit");
    }

    if (ResetButton())
    {
        m_Session.SetScriptEditor(ScriptEditor::System);
        m_Session.SetWaitForDebugger(false);
    }

    Tooltip("System as the script editor, and Play not waiting for a debugger");
}

// Unity's Colors page, which has Forge's one colour of its own
void PreferencesWindow::DrawColors()
{
    EditorPreferences l_Preferences = m_Session.GetPreferences();
    if (Shows(Section::Colors, "Play Mode Tint"))
    {
        Label("Play Mode Tint");
        if (ImGui::ColorEdit3("##PlayModeTint", &l_Preferences.PlayModeTint.x))
        {
            m_Session.SetPreferences(l_Preferences);
        }

        Tooltip("While a scene plays, the windows' backgrounds lean towards it, and further while it is paused, so it is plain at a glance that nothing done then stays");
    }

    if (ResetButton())
    {
        l_Preferences.PlayModeTint = EditorPreferences::c_DefaultPlayModeTint;
        m_Session.SetPreferences(l_Preferences);
    }

    Tooltip("The blue Forge started with");
}

// The 3D camera's speed in a scene not shown before, and the gizmo's snap steps, which the Scene panel's toolbar shows and changes too
void PreferencesWindow::DrawSceneView()
{
    EditorPreferences l_Preferences = m_Session.GetPreferences();
    bool l_Changed = false;
    if (Shows(Section::SceneView, "Camera Fly Speed"))
    {
        Label("Camera Fly Speed");
        l_Changed = ImGui::DragFloat("##CameraSpeed", &l_Preferences.CameraSpeed, 1.0f, EditorCamera3D::c_MinimumSpeed, EditorCamera3D::c_MaximumSpeed, "%.2f units a second", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic) || l_Changed;
        Tooltip("How fast the 3D camera flies at first in a scene the Scene panel has not shown before. The wheel changes it while flying, and each scene keeps its own");
    }

    const std::array<std::pair<std::string_view, const char*>, 3> l_Steps{ { { "Move Snap", "%g units" },{ "Rotate Snap", "%g\xC2\xB0" },{ "Scale Snap", "%g" } } };
    for (std::size_t it_Axis = 0; it_Axis < l_Steps.size(); ++it_Axis)
    {
        const auto& [l_Name, l_Format] = l_Steps[it_Axis];
        if (Shows(Section::SceneView, l_Name))
        {
            ImGui::PushID(static_cast<int>(it_Axis));
            Label(l_Name);
            l_Changed = ImGui::DragFloat("##Step", &l_Preferences.SnapSteps[static_cast<glm::length_t>(it_Axis)], 0.01f, EditorPreferences::c_MinimumSnap, EditorPreferences::c_MaximumSnap, l_Format, ImGuiSliderFlags_AlwaysClamp) || l_Changed;
            Tooltip("The step a gizmo drag goes by while snapping is on, or Ctrl is held with it off, as the Scene panel's toolbar shows it");
            ImGui::PopID();
        }
    }

    if (l_Changed)
    {
        m_Session.SetPreferences(l_Preferences);
    }

    if (ResetButton())
    {
        l_Preferences.CameraSpeed = EditorCamera3D::c_DefaultSpeed;
        l_Preferences.SnapSteps = EditorPreferences::c_DefaultSnapSteps;
        m_Session.SetPreferences(l_Preferences);
    }

    Tooltip("A fly speed of 5, and snapping by 0.5 units, 15 degrees and 0.1 of scale");
}