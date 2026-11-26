#include "Panels/BuildPanel.hpp"

#include "EditorPayloads.hpp"

#include <Trinity/UI/Widgets.hpp>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr float c_LabelWidthInFonts = 9.0f;
    // A scene's row dragged within the list: its index as a 32-bit number
    constexpr const char* c_BuildScenePayload = "TRINITY_BUILD_SCENE";
    constexpr std::string_view c_SceneType = "Scene";
    constexpr std::size_t c_MinimumRows = 3;
    constexpr std::size_t c_MaximumRows = 10;

    // A label on the left and the widget filling the rest of the row, less what is kept after it
    void Label(const char* label, float reserve = 0.0f)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        Trinity::Widgets::ScrubWithLastItem();
        ImGui::SameLine(ImGui::GetFontSize() * c_LabelWidthInFonts);
        ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - reserve, 1.0f));
    }

    void Tooltip(const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("%s", text);
        }
    }

    // A path under Assets without /assets/, as the Content Browser shows it
    std::string_view ShortPath(std::string_view path)
    {
        constexpr std::string_view c_Assets = "/assets/";

        return path.starts_with(c_Assets) ? path.substr(c_Assets.size()) : path;
    }

    // A scene's path, or in the warning colour, Missing, or what it is while it is not a scene
    struct SceneText
    {
        std::string Text;
        bool Broken = false;
    };

    SceneText DescribeScene(const Trinity::AssetRegistry* registry, Trinity::UUID scene)
    {
        const Trinity::AssetRecord* l_Record = registry != nullptr ? registry->Find(scene) : nullptr;
        if (l_Record == nullptr)
        {
            return { std::format("{} Missing ({})", Trinity::Icons::c_Warning, scene), true };
        }

        if (l_Record->Importer != c_SceneType)
        {
            return { std::format("{} Not a scene: {}", Trinity::Icons::c_Warning, ShortPath(l_Record->Path)), true };
        }

        return { std::string(ShortPath(l_Record->Path)), false };
    }

    // The scene dragged from the Content Browser, or none while what is dragged is not one
    Trinity::UUID GetDraggedScene(const Trinity::AssetRegistry* registry)
    {
        const ImGuiPayload* l_Payload = ImGui::GetDragDropPayload();
        if (registry == nullptr || l_Payload == nullptr || !l_Payload->IsDataType(c_AssetPayload) || l_Payload->DataSize != sizeof(std::uint64_t))
        {
            return {};
        }

        std::uint64_t l_Value = 0;
        std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));
        const Trinity::AssetRecord* l_Record = registry->Find(Trinity::UUID(l_Value));

        return l_Record != nullptr && l_Record->Importer == c_SceneType ? l_Record->ID : Trinity::UUID();
    }

    // The row a scene's row dragged within the list came from, or none
    std::optional<std::size_t> GetDraggedRow()
    {
        const ImGuiPayload* l_Payload = ImGui::GetDragDropPayload();
        if (l_Payload == nullptr || !l_Payload->IsDataType(c_BuildScenePayload) || l_Payload->DataSize != sizeof(std::uint32_t))
        {
            return std::nullopt;
        }

        std::uint32_t l_Row = 0;
        std::memcpy(&l_Row, l_Payload->Data, sizeof(l_Row));

        return l_Row;
    }

    // The scene at the row given, or last. One the build has already is moved there
    void PlaceScene(Trinity::ExportSettings& settings, Trinity::ExportScene scene, std::size_t row)
    {
        const auto a_Found = std::ranges::find(settings.Scenes, scene.Scene, &Trinity::ExportScene::Scene);
        if (a_Found != settings.Scenes.end())
        {
            scene = *a_Found;
            settings.Scenes.erase(a_Found);
        }

        settings.Scenes.insert(settings.Scenes.begin() + static_cast<std::ptrdiff_t>(std::min(row, settings.Scenes.size())), scene);
    }

    // What a platform's row says while hovered: what picking it does, or what brings it
    std::string DescribePlatform(const Trinity::TargetPlatform& platform, bool buildable, bool picked)
    {
        const Trinity::TargetPlatform* l_Running = Trinity::TargetPlatforms::Find(Trinity::GetBuildPlatform());
        const std::string_view l_Here = l_Running != nullptr ? l_Running->DisplayName : Trinity::GetBuildPlatform();
        std::string l_Tip;
        if (buildable)
        {
            l_Tip = picked ? std::format("The game is built for {}", platform.DisplayName) : std::format("Builds the game for {}, its graphics APIs in the order {} keeps", platform.DisplayName, platform.DisplayName);
        }
        else if (platform.Console)
        {
            l_Tip = std::format("{} is not supported yet, and needs the platform holder's SDK", platform.DisplayName);
        }
        else if (!platform.Supported)
        {
            l_Tip = std::format("{} is not supported yet. This version of Trinity runs on {}", platform.DisplayName, l_Here);
        }
        else
        {
            l_Tip = std::format("{} games are built with a {} version of Trinity, and this one runs on {}", platform.DisplayName, platform.DisplayName, l_Here);
        }

        if (picked && !buildable)
        {
            l_Tip += ". It is the project's platform, which this version cannot build, so pick one it builds";
        }

        return l_Tip;
    }

    // A row moved to take another's place
    void MoveRow(Trinity::ExportSettings& settings, std::size_t from, std::size_t to)
    {
        if (from >= settings.Scenes.size() || from == to)
        {
            return;
        }

        const Trinity::ExportScene l_Scene = settings.Scenes[from];
        settings.Scenes.erase(settings.Scenes.begin() + static_cast<std::ptrdiff_t>(from));
        settings.Scenes.insert(settings.Scenes.begin() + static_cast<std::ptrdiff_t>(std::min(to, settings.Scenes.size())), l_Scene);
    }
}

BuildPanel::BuildPanel(EditorSession& session, ExportCommand& exporter) : Panel("Build", Trinity::Icons::c_PackageVariantClosed, DockSlot::Right), m_Session(session), m_Export(exporter)
{

}

template<typename F>
void BuildPanel::Edit(std::string field, F&& change)
{
    const Trinity::Project* l_Project = m_Session.GetProject();
    if (l_Project == nullptr)
    {
        return;
    }

    Trinity::ExportSettings l_Settings = l_Project->GetExportSettings();
    change(l_Settings);
    static_cast<void>(m_Session.EditExportSettings(l_Settings, std::move(field)));
}

// Shows the value whenever it is not typed into, so an undo shows at once. Taken once left
bool BuildPanel::TextField(const char* id, TextBuffer& buffer, const std::string& value)
{
    if (!buffer.Editing)
    {
        buffer.Text = value;
    }

    ImGui::InputText(id, &buffer.Text);
    const bool l_Left = ImGui::IsItemDeactivatedAfterEdit() && buffer.Text != value;
    buffer.Editing = ImGui::IsItemActive();

    return l_Left;
}

void BuildPanel::OnImGuiRender()
{
    const Trinity::Project* l_Project = m_Session.GetProject();
    if (l_Project == nullptr)
    {
        ImGui::TextDisabled("Open or create a project to build it");

        return;
    }

    // A copy, as an edit replaces what the project holds
    const Trinity::ExportSettings l_Settings = l_Project->GetExportSettings();
    DrawPlatforms(l_Settings);
    DrawScenes(l_Settings);
    DrawBuild(l_Settings);

    // Nothing is being dragged or typed into, so the next change is a command of its own
    if (!ImGui::IsAnyItemActive())
    {
        m_Session.GetHistory().EndMerge();
    }
}

// As Unity's Build Profiles lists them. Platforms this version cannot build are greyed
void BuildPanel::DrawPlatforms(const Trinity::ExportSettings& settings)
{
    if (!m_Platforms)
    {
        m_Platforms = ExportCommand::GetPlatforms();
    }

    ImGui::SeparatorText("Platform");
    for (const Trinity::TargetPlatform& it_Platform : Trinity::TargetPlatforms::GetAll())
    {
        const bool l_Buildable = std::ranges::find(*m_Platforms, it_Platform.Name) != m_Platforms->end();
        const bool l_Picked = settings.Platform == it_Platform.Name;
        ImGui::PushID(it_Platform.Name.data(), it_Platform.Name.data() + it_Platform.Name.size());
        ImGui::BeginDisabled(!l_Buildable);
        const std::string l_Label = l_Picked && !l_Buildable ? std::format("{} {}", Trinity::Icons::c_Warning, it_Platform.DisplayName) : std::string(it_Platform.DisplayName);
        if (ImGui::Selectable(l_Label.c_str(), l_Picked) && !l_Picked)
        {
            Edit("platform", [&it_Platform](Trinity::ExportSettings& changed) { changed.Platform = it_Platform.Name; });
        }

        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", DescribePlatform(it_Platform, l_Buildable, l_Picked).c_str());
        }

        ImGui::PopID();
    }
}

// Each row: a tick, its index among the ticked, its path, which drags it, and a cross to remove it
void BuildPanel::DrawScenes(const Trinity::ExportSettings& settings)
{
    ImGui::SeparatorText("Scenes In Build");
    ImGui::TextDisabled("The ticked scenes ship, in this order, and the first starts the game");
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const float l_RowHeight = ImGui::GetFrameHeightWithSpacing();
    const std::size_t l_Rows = std::clamp(settings.Scenes.size(), c_MinimumRows, c_MaximumRows);
    if (ImGui::BeginChild("##SceneList", ImVec2(0.0f, l_RowHeight * static_cast<float>(l_Rows) + ImGui::GetStyle().WindowPadding.y * 2.0f), ImGuiChildFlags_Borders))
    {
        if (settings.Scenes.empty())
        {
            ImGui::TextDisabled("Drag scenes here from the Content Browser");
        }
        else if (ImGui::BeginTable("##Scenes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("##On", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("##Index", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000").x);
            ImGui::TableSetupColumn("##Scene", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed);
            std::uint32_t l_Index = 0;
            for (std::size_t it_Row = 0; it_Row < settings.Scenes.size(); ++it_Row)
            {
                const Trinity::ExportScene& l_Scene = settings.Scenes[it_Row];
                ImGui::PushID(static_cast<int>(it_Row));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool l_On = l_Scene.Enabled;
                if (ImGui::Checkbox("##On", &l_On))
                {
                    Edit(l_On ? "scene ticked" : "scene unticked", [it_Row, l_On](Trinity::ExportSettings& changed) { changed.Scenes[it_Row].Enabled = l_On; });
                }

                Tooltip("Ticked, the scene ships with the game");
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (l_Scene.Enabled)
                {
                    ImGui::TextDisabled("%u", l_Index++);
                }

                ImGui::TableNextColumn();
                const SceneText l_Text = DescribeScene(l_Registry, l_Scene.Scene);
                if (l_Text.Broken)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, Trinity::Theme::GetColor(Trinity::ThemeColor::Warning));
                }

                ImGui::AlignTextToFramePadding();
                ImGui::Selectable(l_Text.Text.c_str(), false);
                if (l_Text.Broken)
                {
                    ImGui::PopStyleColor();
                    Tooltip("The project no longer has this scene, so the build cannot make the game. Remove it, or restore the scene");
                }

                if (ImGui::BeginDragDropSource())
                {
                    const std::uint32_t l_Dragged = static_cast<std::uint32_t>(it_Row);
                    ImGui::SetDragDropPayload(c_BuildScenePayload, &l_Dragged, sizeof(l_Dragged));
                    ImGui::TextUnformatted(l_Text.Text.c_str());
                    ImGui::EndDragDropSource();
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const std::optional<std::size_t> l_From = GetDraggedRow(); l_From && ImGui::AcceptDragDropPayload(c_BuildScenePayload))
                    {
                        Edit("scene order", [&](Trinity::ExportSettings& changed) { MoveRow(changed, *l_From, it_Row); });
                    }
                    else if (const Trinity::UUID l_Added = GetDraggedScene(l_Registry); l_Added && ImGui::AcceptDragDropPayload(c_AssetPayload))
                    {
                        Edit("scene added", [&](Trinity::ExportSettings& changed) { PlaceScene(changed, { l_Added, true }, it_Row); });
                    }

                    ImGui::EndDragDropTarget();
                }

                if (ImGui::BeginPopupContextItem("##Row"))
                {
                    if (ImGui::MenuItem(std::format("{} Move Up", Trinity::Icons::c_ArrowUp).c_str(), nullptr, false, it_Row > 0))
                    {
                        Edit("scene order", [it_Row](Trinity::ExportSettings& changed) { MoveRow(changed, it_Row, it_Row - 1); });
                    }

                    if (ImGui::MenuItem(std::format("{} Move Down", Trinity::Icons::c_ArrowDown).c_str(), nullptr, false, it_Row + 1 < settings.Scenes.size()))
                    {
                        Edit("scene order", [it_Row](Trinity::ExportSettings& changed) { MoveRow(changed, it_Row, it_Row + 1); });
                    }

                    if (ImGui::MenuItem(std::format("{} Remove From Build", Trinity::Icons::c_TimesCircle).c_str()))
                    {
                        Edit("scene removed", [it_Row](Trinity::ExportSettings& changed) { changed.Scenes.erase(changed.Scenes.begin() + static_cast<std::ptrdiff_t>(it_Row)); });
                    }

                    ImGui::EndPopup();
                }

                ImGui::TableNextColumn();
                if (ImGui::SmallButton(Trinity::Icons::c_TimesCircle))
                {
                    Edit("scene removed", [it_Row](Trinity::ExportSettings& changed) { changed.Scenes.erase(changed.Scenes.begin() + static_cast<std::ptrdiff_t>(it_Row)); });
                }

                Tooltip("Remove from the build");
                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    ImGui::EndChild();

    // Below the rows, as the smallest target under the pointer wins, a scene goes last
    if (ImGui::BeginDragDropTarget())
    {
        if (const std::optional<std::size_t> l_From = GetDraggedRow(); l_From && ImGui::AcceptDragDropPayload(c_BuildScenePayload))
        {
            Edit("scene order", [&](Trinity::ExportSettings& changed) { MoveRow(changed, *l_From, changed.Scenes.size()); });
        }
        else if (const Trinity::UUID l_Added = GetDraggedScene(l_Registry); l_Added && ImGui::AcceptDragDropPayload(c_AssetPayload))
        {
            Edit("scene added", [&](Trinity::ExportSettings& changed) { PlaceScene(changed, { l_Added, true }, changed.Scenes.size()); });
        }

        ImGui::EndDragDropTarget();
    }

    const Trinity::UUID l_Open = m_Session.GetSceneID();
    const bool l_CanAdd = l_Open && std::ranges::find(settings.Scenes, l_Open, &Trinity::ExportScene::Scene) == settings.Scenes.end();
    ImGui::BeginDisabled(!l_CanAdd);
    if (ImGui::Button(std::format("{} Add Open Scene", Trinity::Icons::c_Plus).c_str()))
    {
        Edit("scene added", [l_Open](Trinity::ExportSettings& changed) { changed.Scenes.push_back({ l_Open, true }); });
    }

    ImGui::EndDisabled();
    Tooltip(l_Open ? "The scene open in the Scene panel, last in the build, unless the build has it already" : "The open scene has never been saved, so it is no asset the build can hold");
}

bool BuildPanel::Build(bool run)
{
    if (!m_Session.HasProject() || m_Export.IsRunning())
    {
        return m_Session.HasProject();
    }

    const Trinity::Project& l_Project = *m_Session.GetProject();
    const std::vector<std::string> l_Problems = ExportCommand::CheckSettings(l_Project, *m_Session.GetRegistry());
    m_Refused = !l_Problems.empty();
    for (const std::string& it_Problem : l_Problems)
    {
        TR_ERROR("Build: {}, so nothing is built", it_Problem);
    }

    if (m_Refused)
    {
        return false;
    }

    m_Export.Start(l_Project.GetExportDirectory(), false, run);

    return true;
}

// Development Build, the output folder, the way to the player's settings, Build and Build And Run, or what stops them. Then Stop Game
void BuildPanel::DrawBuild(const Trinity::ExportSettings& settings)
{
    ImGui::SeparatorText("Build");
    Label("Development Build");
    bool l_Development = settings.Development;
    if (ImGui::Checkbox("##Development", &l_Development))
    {
        Edit("development build", [l_Development](Trinity::ExportSettings& changed) { changed.Development = l_Development; });
    }

    Tooltip("A Release player with logs, asserts, the profiler and debug overlays. Off, the game is built in Distribution");

    const std::string l_Browse = std::format("{} Browse...", Trinity::Icons::c_FolderOpen);
    const float l_BrowseWidth = ImGui::CalcTextSize(l_Browse.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    Label("Output Folder", l_BrowseWidth + ImGui::GetStyle().ItemSpacing.x);
    if (TextField("##Output", m_Output, settings.Output))
    {
        Edit("output folder", [this](Trinity::ExportSettings& changed) { changed.Output = m_Output.Text; });
    }

    const Trinity::Project& l_Project = *m_Session.GetProject();
    const std::u8string l_Directory = l_Project.GetExportDirectory().u8string();
    const std::string l_Where = std::format("Where the game is exported, from the project's folder unless a whole path. Each export replaces this folder: {}", std::string_view(reinterpret_cast<const char*>(l_Directory.data()), l_Directory.size()));
    Tooltip(l_Where.c_str());
    ImGui::SameLine();
    if (ImGui::Button(l_Browse.c_str()))
    {
        m_Session.Request(EditorSession::Command::ChooseExportFolder);
    }

    // As Unity's Build Profiles has it, since what the game is called and how it opens is the project's and set there
    if (m_ShowPlayerSettings)
    {
        if (ImGui::Button(std::format("{} Player Settings...", Trinity::Icons::c_Gear).c_str()))
        {
            m_ShowPlayerSettings();
        }

        Tooltip("The game's name, version and icon, its window and its graphics APIs, in Project Settings");
    }

    ImGui::Spacing();
    const bool l_Running = m_Export.IsRunning();
    const float l_Height = ImGui::GetFrameHeight() * 1.5f;
    ImGui::BeginDisabled(l_Running);
    const std::string l_Label = l_Running ? std::format("{} Building...###Build", Trinity::Icons::c_Spinner) : std::format("{} Build###Build", Trinity::Icons::c_PackageVariantClosed);
    if (ImGui::Button(l_Label.c_str(), ImVec2((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f, l_Height)))
    {
        static_cast<void>(Build(false));
    }

    const std::string l_Exports = std::format("Exports the game into the output folder, replacing it: the player, its pack, {} and the runtimes it needs", l_Project.GetLanguage() == Trinity::ScriptLanguage::CSharp ? "its C# scripts compiled ahead of time" : "its C++ module built into it");
    Tooltip(l_Running ? "Building once the project has imported and its scripts have built. The Console tells how it goes" : l_Exports.c_str());
    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Build And Run###BuildAndRun", Trinity::Icons::c_Play).c_str(), ImVec2(-FLT_MIN, l_Height)))
    {
        static_cast<void>(Build(true));
    }

    ImGui::EndDisabled();
    Tooltip("Exports the game as Build does, then starts it, its log shown in the Console as Game: lines. Ctrl+B");

    const GameRunner& l_Game = m_Export.GetGame();
    if (l_Game.IsRunning())
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(std::format("{} {} is running", Trinity::Icons::c_Play, l_Game.GetProduct()).c_str());
        ImGui::SameLine();
        if (ImGui::Button(std::format("{} Stop Game###StopGame", Trinity::Icons::c_Stop).c_str()))
        {
            m_Export.StopGame();
        }

        Tooltip("Ends the game Build And Run started, as closing its window does. Closing Forge leaves it running");
    }

    if (m_Session.IsDirty())
    {
        ImGui::TextDisabled("The open scene's unsaved changes are not built until it is saved");
    }

    if (!m_Refused)
    {
        return;
    }

    const std::vector<std::string> l_Problems = ExportCommand::CheckSettings(l_Project, *m_Session.GetRegistry());
    m_Refused = !l_Problems.empty();
    ImGui::PushStyleColor(ImGuiCol_Text, Trinity::Theme::GetColor(Trinity::ThemeColor::Error));
    for (const std::string& it_Problem : l_Problems)
    {
        ImGui::TextWrapped("%s %s", Trinity::Icons::c_Warning, it_Problem.c_str());
    }

    ImGui::PopStyleColor();
}