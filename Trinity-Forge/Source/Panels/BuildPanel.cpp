#include "Panels/BuildPanel.hpp"

#include "EditorPayloads.hpp"
#include "Panels/ConsolePanel.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <format>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr float c_LabelWidthInFonts = 9.0f;
    // A scene's row dragged within the list: its index as a 32-bit number
    constexpr const char* c_BuildScenePayload = "TRINITY_BUILD_SCENE";
    constexpr const char* c_IconPickerPopup = "##IconPicker";
    constexpr std::string_view c_SceneType = "Scene";
    // The graphics APIs a build for Windows can try, as Unity's lists them
    constexpr std::array<Trinity::GraphicsAPI, 2> c_GraphicsAPIs{ Trinity::GraphicsAPI::D3D12, Trinity::GraphicsAPI::Vulkan };
    constexpr std::size_t c_MinimumRows = 3;
    constexpr std::size_t c_MaximumRows = 10;

    // A label on the left and the widget filling the rest of the row, less what is kept for what follows it
    void Label(const char* label, float reserve = 0.0f)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
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

    // What a scene of the build shows: its path, or in the warning colour, Missing while the project has no such asset, or what it is while it is not a scene
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

    // The scene at the row given, or last, taking that row's place, and when the build has it already, moved there from where it was
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

// The text shown is the value's whenever the field is not being typed into, so an undo or another panel's change shows at once. The field is left on the frame it reports so, whichever item takes over first, and what was typed is taken then
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
    DrawScenes(l_Settings);
    DrawPlayer(l_Settings);
    DrawWindow(l_Settings);
    DrawGraphicsAPIs(l_Settings);
    DrawBuild(l_Settings);

    // Nothing is being dragged or typed into, so the next change is a command of its own
    if (!ImGui::IsAnyItemActive())
    {
        m_Session.GetHistory().EndMerge();
    }
}

// Each row a box that ticks the scene, its index among the ticked ones, which the game loads it by, its path, which drags it to another row, and a cross that takes it out. A scene dropped from the Content Browser on a row takes that row's place, and dropped below the rows goes last, and one the build has already moves there
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
                    ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Warn));
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

// The names a player keeps its saves and log under and shows, and its icon
void BuildPanel::DrawPlayer(const Trinity::ExportSettings& settings)
{
    ImGui::SeparatorText("Player");
    Label("Product Name");
    if (TextField("##Product", m_Product, settings.Product))
    {
        Edit("product name", [this](Trinity::ExportSettings& changed) { changed.Product = m_Product.Text; });
    }

    Tooltip("The game's name, which its executable is named for, its window's title shows, and its saves and log go under, as Unity's Product Name");

    Label("Company Name");
    if (TextField("##Company", m_Company, settings.Company))
    {
        Edit("company name", [this](Trinity::ExportSettings& changed) { changed.Company = m_Company.Text; });
    }

    Tooltip("Whose game it is, whose folder its saves and log go under, as Unity's Company Name");

    Label("Version");
    if (TextField("##Version", m_Version, settings.Version))
    {
        Edit("version", [this](Trinity::ExportSettings& changed) { changed.Version = m_Version.Text; });
    }

    Tooltip("The game's version, which its executable is given, as Unity's Version");

    DrawIcon(settings);
}

// A slot for one of the project's textures, as Unity's Default Icon: a button naming it that opens a list of them, which also takes one dragged from the Content Browser, and a cross that clears it
void BuildPanel::DrawIcon(const Trinity::ExportSettings& settings)
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = l_Registry != nullptr && settings.Icon ? l_Registry->Find(settings.Icon) : nullptr;
    const bool l_Broken = settings.Icon && (l_Record == nullptr || l_Record->Importer != Trinity::TextureAsset::c_AssetType);
    const std::string l_Text = !settings.Icon ? std::string("None (Texture)") : (l_Record == nullptr ? std::format("{} Missing (Texture)", Trinity::Icons::c_Warning) : (l_Broken ? std::format("{} Type Mismatch (Texture)", Trinity::Icons::c_Warning) : std::string(ShortPath(l_Record->Path))));

    const float l_ClearWidth = ImGui::GetFrameHeight();
    Label("Icon", l_ClearWidth + ImGui::GetStyle().ItemSpacing.x);
    if (l_Broken)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Warn));
    }

    if (ImGui::Button(std::format("{}###Icon", l_Text).c_str(), ImVec2(ImGui::CalcItemWidth(), 0.0f)))
    {
        m_IconFilter.clear();
        ImGui::OpenPopup(c_IconPickerPopup);
    }

    if (l_Broken)
    {
        ImGui::PopStyleColor();
    }

    Tooltip("A texture of the project, which the export makes the game's executable and window icon from. Without one the game has Trinity's");
    if (ImGui::BeginDragDropTarget())
    {
        const ImGuiPayload* l_Payload = ImGui::GetDragDropPayload();
        std::uint64_t l_Value = 0;
        if (l_Payload != nullptr && l_Payload->IsDataType(c_AssetPayload) && l_Payload->DataSize == sizeof(l_Value))
        {
            std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));
        }

        const Trinity::AssetRecord* l_Dragged = l_Registry != nullptr && l_Value != 0 ? l_Registry->Find(Trinity::UUID(l_Value)) : nullptr;
        if (l_Dragged != nullptr && l_Dragged->Importer == Trinity::TextureAsset::c_AssetType && ImGui::AcceptDragDropPayload(c_AssetPayload))
        {
            Edit("icon", [l_Dragged](Trinity::ExportSettings& changed) { changed.Icon = l_Dragged->ID; });
        }

        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!settings.Icon);
    if (ImGui::Button(std::format("{}##ClearIcon", Trinity::Icons::c_TimesCircle).c_str(), ImVec2(l_ClearWidth, 0.0f)))
    {
        Edit("icon", [](Trinity::ExportSettings& changed) { changed.Icon = {}; });
    }

    ImGui::EndDisabled();

    if (!ImGui::BeginPopup(c_IconPickerPopup))
    {
        return;
    }

    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.0f);
    ImGui::InputTextWithHint("##Filter", "Search", &m_IconFilter);
    if (ImGui::Selectable("None", !settings.Icon) && settings.Icon)
    {
        Edit("icon", [](Trinity::ExportSettings& changed) { changed.Icon = {}; });
    }

    std::vector<const Trinity::AssetRecord*> l_Textures;
    for (const Trinity::AssetRecord* it_Record : l_Registry != nullptr ? l_Registry->GetRecords() : std::vector<const Trinity::AssetRecord*>())
    {
        if (it_Record->Importer == Trinity::TextureAsset::c_AssetType && (m_IconFilter.empty() || it_Record->Path.find(m_IconFilter) != std::string::npos))
        {
            l_Textures.push_back(it_Record);
        }
    }

    for (const Trinity::AssetRecord* it_Texture : l_Textures)
    {
        const bool l_Selected = it_Texture->ID == settings.Icon;
        ImGui::PushID(it_Texture->ID.ToString().c_str());
        if (ImGui::Selectable(std::string(ShortPath(it_Texture->Path)).c_str(), l_Selected) && !l_Selected)
        {
            Edit("icon", [it_Texture](Trinity::ExportSettings& changed) { changed.Icon = it_Texture->ID; });
            ImGui::CloseCurrentPopup();
        }

        ImGui::PopID();
    }

    ImGui::EndPopup();
}

// The window a player first opens with, until its player chooses otherwise, as Unity's Resolution and Presentation. A drag is one command
void BuildPanel::DrawWindow(const Trinity::ExportSettings& settings)
{
    ImGui::SeparatorText("Window");
    Label("Fullscreen Mode");
    int l_Mode = static_cast<int>(settings.Window.Mode);
    if (ImGui::Combo("##FullscreenMode", &l_Mode, "Windowed\0Fullscreen Window\0"))
    {
        Edit("fullscreen mode", [l_Mode](Trinity::ExportSettings& changed) { changed.Window.Mode = static_cast<Trinity::FullScreenMode>(l_Mode); });
    }

    Tooltip("Windowed opens in a frame at the width and height below. Fullscreen Window covers the monitor at its own size, without a frame. Alt+Enter switches between them as the game runs");

    constexpr int c_MaxSize = static_cast<int>(Trinity::WindowSettings::c_MaxSize);
    Label("Width");
    int l_Width = static_cast<int>(settings.Window.Width);
    if (ImGui::DragInt("##Width", &l_Width, 1.0f, 1, c_MaxSize, "%d px", ImGuiSliderFlags_AlwaysClamp))
    {
        Edit("window width", [l_Width](Trinity::ExportSettings& changed) { changed.Window.Width = static_cast<std::uint32_t>(l_Width); });
    }

    Tooltip("The window's client area while windowed, in pixels");

    Label("Height");
    int l_Height = static_cast<int>(settings.Window.Height);
    if (ImGui::DragInt("##Height", &l_Height, 1.0f, 1, c_MaxSize, "%d px", ImGuiSliderFlags_AlwaysClamp))
    {
        Edit("window height", [l_Height](Trinity::ExportSettings& changed) { changed.Window.Height = static_cast<std::uint32_t>(l_Height); });
    }

    Tooltip("The window's client area while windowed, in pixels");

    Label("VSync");
    bool l_VSync = settings.Window.VSync;
    if (ImGui::Checkbox("##VSync", &l_VSync))
    {
        Edit("vsync", [l_VSync](Trinity::ExportSettings& changed) { changed.Window.VSync = l_VSync; });
    }

    Tooltip("Each frame waits for the display's refresh, so the game runs at its rate without tearing");
}

// In the order the player tries them, each the player was built without or the machine cannot run passed over, as Unity's Graphics APIs list. One is always left
void BuildPanel::DrawGraphicsAPIs(const Trinity::ExportSettings& settings)
{
    ImGui::SeparatorText("Graphics APIs");
    ImGui::TextDisabled("Tried in this order, each the machine cannot run passed over");
    for (std::size_t it_API = 0; it_API < settings.GraphicsAPIs.size(); ++it_API)
    {
        ImGui::PushID(static_cast<int>(it_API));
        const float l_Button = ImGui::GetFrameHeight();
        const float l_Right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%zu. %s", it_API + 1, std::string(Trinity::ToString(settings.GraphicsAPIs[it_API])).c_str());
        ImGui::SameLine(std::max(l_Right - l_Button * 3.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f, ImGui::GetFontSize() * c_LabelWidthInFonts));
        ImGui::BeginDisabled(it_API == 0);
        if (ImGui::Button(Trinity::Icons::c_ArrowUp, ImVec2(l_Button, 0.0f)))
        {
            Edit("graphics API order", [it_API](Trinity::ExportSettings& changed) { std::swap(changed.GraphicsAPIs[it_API], changed.GraphicsAPIs[it_API - 1]); });
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(it_API + 1 >= settings.GraphicsAPIs.size());
        if (ImGui::Button(Trinity::Icons::c_ArrowDown, ImVec2(l_Button, 0.0f)))
        {
            Edit("graphics API order", [it_API](Trinity::ExportSettings& changed) { std::swap(changed.GraphicsAPIs[it_API], changed.GraphicsAPIs[it_API + 1]); });
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(settings.GraphicsAPIs.size() <= 1);
        if (ImGui::Button(Trinity::Icons::c_TimesCircle, ImVec2(l_Button, 0.0f)))
        {
            Edit("graphics API removed", [it_API](Trinity::ExportSettings& changed) { changed.GraphicsAPIs.erase(changed.GraphicsAPIs.begin() + static_cast<std::ptrdiff_t>(it_API)); });
        }

        ImGui::EndDisabled();
        Tooltip(settings.GraphicsAPIs.size() <= 1 ? "The game needs one to draw with" : "Remove from the list");
        ImGui::PopID();
    }

    std::vector<Trinity::GraphicsAPI> l_Missing;
    std::ranges::copy_if(c_GraphicsAPIs, std::back_inserter(l_Missing), [&settings](Trinity::GraphicsAPI api) { return std::ranges::find(settings.GraphicsAPIs, api) == settings.GraphicsAPIs.end(); });
    if (l_Missing.empty())
    {
        return;
    }

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * c_LabelWidthInFonts);
    if (ImGui::BeginCombo("##AddAPI", std::format("{} Add", Trinity::Icons::c_Plus).c_str()))
    {
        for (const Trinity::GraphicsAPI it_API : l_Missing)
        {
            if (ImGui::Selectable(std::string(Trinity::ToString(it_API)).c_str()))
            {
                Edit("graphics API added", [it_API](Trinity::ExportSettings& changed) { changed.GraphicsAPIs.push_back(it_API); });
            }
        }

        ImGui::EndCombo();
    }
}

// Development Build, where the game is exported and Build, which exports it, or, when the settings stop it, says why in the panel and in the Console until nothing does
void BuildPanel::DrawBuild(const Trinity::ExportSettings& settings)
{
    ImGui::SeparatorText("Build");
    Label("Development Build");
    bool l_Development = settings.Development;
    if (ImGui::Checkbox("##Development", &l_Development))
    {
        Edit("development build", [l_Development](Trinity::ExportSettings& changed) { changed.Development = l_Development; });
    }

    Tooltip("A Release player with logs, asserts and the profiler, an overlay of the log and the frame and audio stats, and RmlUi's debugger, as Unity's Development Build. Off, the game is built in Distribution, as players get it");

    const std::string l_Browse = std::format("{} Browse...", Trinity::Icons::c_FolderOpen);
    const float l_BrowseWidth = ImGui::CalcTextSize(l_Browse.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    Label("Output Folder", l_BrowseWidth + ImGui::GetStyle().ItemSpacing.x);
    if (TextField("##Output", m_Output, settings.Output))
    {
        Edit("output folder", [this](Trinity::ExportSettings& changed) { changed.Output = m_Output.Text; });
    }

    const Trinity::Project& l_Project = *m_Session.GetProject();
    const std::u8string l_Directory = l_Project.GetExportDirectory().u8string();
    const std::string l_Where = std::format("Where the game is exported, from the project's folder unless it is a whole path. Each export replaces this folder whole, whatever it holds: {}", std::string_view(reinterpret_cast<const char*>(l_Directory.data()), l_Directory.size()));
    Tooltip(l_Where.c_str());
    ImGui::SameLine();
    if (ImGui::Button(l_Browse.c_str()))
    {
        m_Session.Request(EditorSession::Command::ChooseExportFolder);
    }

    ImGui::Spacing();
    const bool l_Running = m_Export.IsRunning();
    ImGui::BeginDisabled(l_Running);
    const std::string l_Label = l_Running ? std::format("{} Building...###Build", Trinity::Icons::c_Spinner) : std::format("{} Build###Build", Trinity::Icons::c_PackageVariantClosed);
    if (ImGui::Button(l_Label.c_str(), ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 1.5f)))
    {
        const std::vector<std::string> l_Problems = ExportCommand::CheckSettings(l_Project, *m_Session.GetRegistry());
        m_Refused = !l_Problems.empty();
        for (const std::string& it_Problem : l_Problems)
        {
            TR_ERROR("Build: {}, so nothing is built", it_Problem);
        }

        if (!m_Refused)
        {
            m_Export.Start(l_Project.GetExportDirectory());
        }
    }

    ImGui::EndDisabled();
    Tooltip(l_Running ? "Building once the project has imported and its scripts have built. The Console tells how it goes" : "Exports the game into the output folder, which it replaces whole: the player named for the product, its pack of what the ticked scenes reach, its compiled C# scripts and the runtimes it needs");
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
    ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Error));
    for (const std::string& it_Problem : l_Problems)
    {
        ImGui::TextWrapped("%s %s", Trinity::Icons::c_Warning, it_Problem.c_str());
    }

    ImGui::PopStyleColor();
}