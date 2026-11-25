#include "Panels/ProjectSettingsPanel.hpp"

#include "EditorPayloads.hpp"

#include <Trinity/UI/Widgets.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr float c_LabelWidthInFonts = 9.0f;
    constexpr float c_ListWidthInFonts = 12.0f;
    // Narrower than this, as docked beside the Scene, the sections are a drop-down over the settings and not a list beside them
    constexpr float c_ListLayoutWidthInFonts = 34.0f;
    // As Unity's Audio settings bound its voices
    constexpr int c_MaxRealVoices = 255;
    constexpr int c_MaxVirtualVoices = 4095;
    constexpr int c_MaxOcclusionRays = 1024;
    constexpr const char* c_NoMixer = "None, Master alone";
    constexpr const char* c_IconPickerPopup = "##IconPicker";

    struct SectionInfo
    {
        const char* Icon;
        const char* Name;
        std::span<const std::string_view> Settings;
    };

    // Each section's settings by the names its rows show, which the search goes through
    constexpr std::array<std::string_view, 4> c_PlayerSettings{ "Product Name", "Company Name", "Version", "Icon" };
    constexpr std::array<std::string_view, 4> c_WindowSettings{ "Fullscreen Mode", "Width", "Height", "VSync" };
    constexpr std::array<std::string_view, 1> c_GraphicsSettings{ "Graphics APIs" };
    constexpr std::array<std::string_view, 7> c_AudioSettings{ "Mixer", "Real Voices", "Virtual Voices", "Doppler Factor", "Occlusion Layers", "Occlusion Rays", "Occlusion Time" };
    constexpr std::array<std::string_view, 3> c_PhysicsSettings{ "Gravity 3D", "Gravity 2D", "Step Rate" };
    constexpr std::array<std::string_view, 2> c_CollisionLayerSettings{ "Layer Names", "Which Layers Collide" };

    // In Section order
    constexpr std::array<SectionInfo, 6> c_Sections
    { {
        { Trinity::Icons::c_Gamepad, "Player", c_PlayerSettings },
        { Trinity::Icons::c_WindowRestore, "Window", c_WindowSettings },
        { Trinity::Icons::c_Monitor, "Graphics", c_GraphicsSettings },
        { Trinity::Icons::c_VolumeUp, "Audio", c_AudioSettings },
        { Trinity::Icons::c_CubeOutline, "Physics", c_PhysicsSettings },
        { Trinity::Icons::c_Clone, "Collision Layers", c_CollisionLayerSettings }
    } };

    bool ContainsIgnoringCase(std::string_view text, std::string_view part)
    {
        const auto a_Lower = [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); };

        return !std::ranges::search(text, part, {}, a_Lower, a_Lower).empty();
    }

    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    // What a section says of itself under its name, dim, and wrapped where the panel is narrow
    void Hint(std::string_view text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::PushTextWrapPos(0.0f);
        TextLine(text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    // A label on the left and the widget filling the rest of the row, less what is kept after it
    void Label(const char* label, float reserve = 0.0f)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        Trinity::Widgets::ScrubWithLastItem();
        ImGui::SameLine(ImGui::GetFontSize() * c_LabelWidthInFonts);
        ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - reserve, 1.0f));
    }

    // Without the spaces around it, which would only look like a name
    std::string Trim(std::string_view text)
    {
        const std::size_t l_First = text.find_first_not_of(" \t");
        if (l_First == std::string_view::npos)
        {
            return {};
        }

        return std::string(text.substr(l_First, text.find_last_not_of(" \t") - l_First + 1));
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

    // The platform's own order, made from its defaults the first time it changes
    std::vector<Trinity::GraphicsAPI>& GetOrder(Trinity::ExportSettings& settings)
    {
        std::vector<Trinity::GraphicsAPI> l_Current = settings.GetGraphicsAPIs();

        return settings.GraphicsAPIs.try_emplace(settings.Platform, std::move(l_Current)).first->second;
    }

    // Everything, Nothing, one layer by its name, or how many, with a box for each layer
    bool LayerMaskCombo(const char* id, std::uint32_t& mask, const Trinity::CollisionLayers& layers)
    {
        const std::uint32_t l_Mask = mask & Trinity::c_AllCollisionLayers;
        const int l_Count = std::popcount(l_Mask);
        const std::string l_Preview = l_Mask == Trinity::c_AllCollisionLayers ? std::string("Everything") : (l_Count == 0 ? std::string("Nothing") : (l_Count == 1 ? layers.GetName(static_cast<std::uint32_t>(std::countr_zero(l_Mask))) : std::format("{} layers", l_Count)));
        if (!ImGui::BeginCombo(id, l_Preview.c_str(), ImGuiComboFlags_HeightLarge))
        {
            return false;
        }

        bool l_Changed = false;
        if (ImGui::Selectable("Everything", l_Mask == Trinity::c_AllCollisionLayers) && l_Mask != Trinity::c_AllCollisionLayers)
        {
            mask = Trinity::c_AllCollisionLayers;
            l_Changed = true;
        }

        if (ImGui::Selectable("Nothing", l_Mask == 0) && l_Mask != 0)
        {
            mask = 0;
            l_Changed = true;
        }

        ImGui::Separator();
        for (std::uint32_t it_Layer = 0; it_Layer < Trinity::c_MaxCollisionLayers; ++it_Layer)
        {
            ImGui::PushID(static_cast<int>(it_Layer));
            bool l_On = (l_Mask & (1u << it_Layer)) != 0;
            if (ImGui::Checkbox(std::format("{}: {}", it_Layer, layers.GetName(it_Layer)).c_str(), &l_On))
            {
                mask = l_Mask ^ (1u << it_Layer);
                l_Changed = true;
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();

        return l_Changed;
    }
}

ProjectSettingsPanel::ProjectSettingsPanel(EditorSession& session) : Panel("Project Settings", Trinity::Icons::c_Gear, DockSlot::Floating), m_Session(session)
{

}

void ProjectSettingsPanel::ShowSection(Section section)
{
    m_Section = section;
    m_Search.clear();
    SetOpen(true);
    RequestFocus();
}

template<typename F>
void ProjectSettingsPanel::Edit(std::string field, F&& change)
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
bool ProjectSettingsPanel::TextField(const char* id, TextBuffer& buffer, const std::string& value)
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

// A search for a section's name shows all of it, and one for a setting's name the rows it names
bool ProjectSettingsPanel::Lists(Section section) const
{
    const SectionInfo& l_Info = c_Sections[static_cast<std::size_t>(section)];

    return m_Search.empty() || ContainsIgnoringCase(l_Info.Name, m_Search) || std::ranges::any_of(l_Info.Settings, [this](std::string_view setting) { return ContainsIgnoringCase(setting, m_Search); });
}

bool ProjectSettingsPanel::Shows(Section section, std::string_view setting) const
{
    return m_Search.empty() || ContainsIgnoringCase(c_Sections[static_cast<std::size_t>(section)].Name, m_Search) || ContainsIgnoringCase(setting, m_Search);
}

// As Preferences is laid out: the sections under a search, and the chosen one's settings. A section the search empties is unlisted
void ProjectSettingsPanel::OnImGuiRender()
{
    const Trinity::Project* l_Project = m_Session.GetProject();
    if (l_Project == nullptr)
    {
        ImGui::TextDisabled("Open or create a project to change its settings");

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

    const float l_Font = ImGui::GetFontSize();
    if (ImGui::GetContentRegionAvail().x >= l_Font * c_ListLayoutWidthInFonts)
    {
        ImGui::BeginChild("##Sections", ImVec2(l_Font * c_ListWidthInFonts, 0.0f), ImGuiChildFlags_Borders);
        ImGui::SetNextItemWidth(-FLT_MIN);
        Trinity::Widgets::SearchField("##Search", "Search", m_Search);
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
    }
    else
    {
        const SectionInfo& l_Chosen = c_Sections[static_cast<std::size_t>(m_Section)];
        ImGui::SetNextItemWidth(-FLT_MIN);
        Trinity::Widgets::SearchField("##Search", "Search", m_Search);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##Section", std::format("{} {}", l_Chosen.Icon, l_Chosen.Name).c_str(), ImGuiComboFlags_HeightLarge))
        {
            for (std::size_t it_Index = 0; it_Index < c_Sections.size(); ++it_Index)
            {
                const Section l_Section = static_cast<Section>(it_Index);
                if (Lists(l_Section) && ImGui::Selectable(std::format("{} {}", c_Sections[it_Index].Icon, c_Sections[it_Index].Name).c_str(), m_Section == l_Section))
                {
                    m_Section = l_Section;
                }
            }

            ImGui::EndCombo();
        }
    }

    ImGui::BeginChild("##Settings");
    if (!Lists(m_Section))
    {
        ImGui::TextDisabled("No setting is called anything like %s", m_Search.c_str());
    }
    else
    {
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
        ImGui::SeparatorText(c_Sections[static_cast<std::size_t>(m_Section)].Name);
        ImGui::PopFont();

        // A copy, as an edit replaces what the project holds
        const Trinity::ExportSettings l_Export = l_Project->GetExportSettings();
        const bool l_Playing = m_Session.IsPlaying();
        switch (m_Section)
        {
            case Section::Player:
            {
                DrawPlayer(l_Export);

                break;
            }
            case Section::Window:
            {
                DrawWindow(l_Export);

                break;
            }
            case Section::Graphics:
            {
                DrawGraphics(l_Export);

                break;
            }
            case Section::Audio:
            {
                DrawAudio(l_Project->GetAudioSettings(), l_Project->GetCollisionLayers(), l_Playing);

                break;
            }
            case Section::Physics:
            {
                Hint(l_Playing ? "The scene playing keeps the settings it started with, so nothing here changes until it stops" : "Saved with the project as they change, and taken by a scene as it starts playing");
                ImGui::BeginDisabled(l_Playing);
                DrawPhysics(l_Project->GetPhysicsSettings());
                ImGui::EndDisabled();

                break;
            }
            case Section::CollisionLayers:
            {
                Hint(l_Playing ? "The scene playing keeps the layers it started with, so nothing here changes until it stops" : "Saved with the project as they change, and taken by a scene as it starts playing");
                ImGui::BeginDisabled(l_Playing);
                DrawCollisionLayers(l_Project->GetPhysicsSettings());
                ImGui::EndDisabled();

                break;
            }
        }
    }

    ImGui::EndChild();

    // Nothing is being dragged or typed into, so the next change is a command of its own
    if (!ImGui::IsAnyItemActive())
    {
        m_Session.GetHistory().EndMerge();
    }
}

// The names a player keeps its saves and log under and shows, and its icon
void ProjectSettingsPanel::DrawPlayer(const Trinity::ExportSettings& settings)
{
    if (Shows(Section::Player, "Product Name"))
    {
        Label("Product Name");
        if (TextField("##Product", m_Product, settings.Product))
        {
            Edit("product name", [this](Trinity::ExportSettings& changed) { changed.Product = m_Product.Text; });
        }

        Tooltip("The game's name: its executable, window title, saves and log are named for it, as Unity's Product Name");
    }

    if (Shows(Section::Player, "Company Name"))
    {
        Label("Company Name");
        if (TextField("##Company", m_Company, settings.Company))
        {
            Edit("company name", [this](Trinity::ExportSettings& changed) { changed.Company = m_Company.Text; });
        }

        Tooltip("Whose game it is, whose folder its saves and log go under, as Unity's Company Name");
    }

    if (Shows(Section::Player, "Version"))
    {
        Label("Version");
        if (TextField("##Version", m_Version, settings.Version))
        {
            Edit("version", [this](Trinity::ExportSettings& changed) { changed.Version = m_Version.Text; });
        }

        Tooltip("The game's version, which its executable is given, as Unity's Version");
    }

    if (Shows(Section::Player, "Icon"))
    {
        DrawIcon(settings);
    }
}

// A slot for one of the project's textures, as Unity's Default Icon, which also takes a drop
void ProjectSettingsPanel::DrawIcon(const Trinity::ExportSettings& settings)
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = l_Registry != nullptr && settings.Icon ? l_Registry->Find(settings.Icon) : nullptr;
    const bool l_Broken = settings.Icon && (l_Record == nullptr || l_Record->Importer != Trinity::TextureAsset::c_AssetType);
    const std::string l_Text = !settings.Icon ? std::string("None (Texture)") : (l_Record == nullptr ? std::format("{} Missing (Texture)", Trinity::Icons::c_Warning) : (l_Broken ? std::format("{} Type Mismatch (Texture)", Trinity::Icons::c_Warning) : std::string(ShortPath(l_Record->Path))));

    const float l_ClearWidth = ImGui::GetFrameHeight();
    Label("Icon", l_ClearWidth + ImGui::GetStyle().ItemSpacing.x);
    if (l_Broken)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Trinity::Theme::GetColor(Trinity::ThemeColor::Warning));
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

    Tooltip("A texture of the project, which the export makes the game's icon from. Without one the game has Trinity's");
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
    Trinity::Widgets::SearchField("##Filter", "Search", m_IconFilter);
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

// The window a player first opens with, as Unity's Resolution and Presentation
void ProjectSettingsPanel::DrawWindow(const Trinity::ExportSettings& settings)
{
    if (Shows(Section::Window, "Fullscreen Mode"))
    {
        Label("Fullscreen Mode");
        int l_Mode = static_cast<int>(settings.Window.Mode);
        if (ImGui::Combo("##FullscreenMode", &l_Mode, "Windowed\0Fullscreen Window\0"))
        {
            Edit("fullscreen mode", [l_Mode](Trinity::ExportSettings& changed) { changed.Window.Mode = static_cast<Trinity::FullScreenMode>(l_Mode); });
        }

        Tooltip("Windowed opens in a frame at the width and height below. Fullscreen Window covers the monitor. Alt+Enter switches");
    }

    constexpr int c_MaxSize = static_cast<int>(Trinity::WindowSettings::c_MaxSize);
    if (Shows(Section::Window, "Width"))
    {
        Label("Width");
        int l_Width = static_cast<int>(settings.Window.Width);
        if (Trinity::Widgets::DragInt("##Width", &l_Width, 1.0f, 1, c_MaxSize, "%d px", ImGuiSliderFlags_AlwaysClamp))
        {
            Edit("window width", [l_Width](Trinity::ExportSettings& changed) { changed.Window.Width = static_cast<std::uint32_t>(l_Width); });
        }

        Tooltip("The window's client area while windowed, in pixels");
    }

    if (Shows(Section::Window, "Height"))
    {
        Label("Height");
        int l_Height = static_cast<int>(settings.Window.Height);
        if (Trinity::Widgets::DragInt("##Height", &l_Height, 1.0f, 1, c_MaxSize, "%d px", ImGuiSliderFlags_AlwaysClamp))
        {
            Edit("window height", [l_Height](Trinity::ExportSettings& changed) { changed.Window.Height = static_cast<std::uint32_t>(l_Height); });
        }

        Tooltip("The window's client area while windowed, in pixels");
    }

    if (Shows(Section::Window, "VSync"))
    {
        Label("VSync");
        bool l_VSync = settings.Window.VSync;
        if (ImGui::Checkbox("##VSync", &l_VSync))
        {
            Edit("vsync", [l_VSync](Trinity::ExportSettings& changed) { changed.Window.VSync = l_VSync; });
        }

        Tooltip("Each frame waits for the display's refresh, so the game runs at its rate without tearing");
    }
}

// In the order the player tries them. Each platform keeps its own order. One is always left
void ProjectSettingsPanel::DrawGraphics(const Trinity::ExportSettings& settings)
{
    const Trinity::TargetPlatform* l_Platform = Trinity::TargetPlatforms::Find(settings.Platform);
    const std::vector<Trinity::GraphicsAPI> l_APIs = settings.GetGraphicsAPIs();
    if (l_Platform == nullptr || l_Platform->GraphicsAPIs.empty())
    {
        Hint(std::format("Trinity has no graphics API for {} yet", l_Platform != nullptr ? l_Platform->DisplayName : std::string_view(settings.Platform)));

        return;
    }

    Hint(std::format("Tried in this order on {}, the platform picked in the Build panel, each the machine cannot run passed over", l_Platform->DisplayName));
    for (std::size_t it_API = 0; it_API < l_APIs.size(); ++it_API)
    {
        ImGui::PushID(static_cast<int>(it_API));
        const float l_Button = ImGui::GetFrameHeight();
        const float l_Right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%zu. %s", it_API + 1, std::string(Trinity::ToString(l_APIs[it_API])).c_str());
        ImGui::SameLine(std::max(l_Right - l_Button * 3.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f, ImGui::GetFontSize() * c_LabelWidthInFonts));
        ImGui::BeginDisabled(it_API == 0);
        if (ImGui::Button(Trinity::Icons::c_ArrowUp, ImVec2(l_Button, 0.0f)))
        {
            Edit("graphics API order", [it_API](Trinity::ExportSettings& changed) { std::vector<Trinity::GraphicsAPI>& l_Order = GetOrder(changed); std::swap(l_Order[it_API], l_Order[it_API - 1]); });
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(it_API + 1 >= l_APIs.size());
        if (ImGui::Button(Trinity::Icons::c_ArrowDown, ImVec2(l_Button, 0.0f)))
        {
            Edit("graphics API order", [it_API](Trinity::ExportSettings& changed) { std::vector<Trinity::GraphicsAPI>& l_Order = GetOrder(changed); std::swap(l_Order[it_API], l_Order[it_API + 1]); });
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(l_APIs.size() <= 1);
        if (ImGui::Button(Trinity::Icons::c_TimesCircle, ImVec2(l_Button, 0.0f)))
        {
            Edit("graphics API removed", [it_API](Trinity::ExportSettings& changed) { std::vector<Trinity::GraphicsAPI>& l_Order = GetOrder(changed); l_Order.erase(l_Order.begin() + static_cast<std::ptrdiff_t>(it_API)); });
        }

        ImGui::EndDisabled();
        Tooltip(l_APIs.size() <= 1 ? "The game needs one to draw with" : "Remove from the list");
        ImGui::PopID();
    }

    std::vector<Trinity::GraphicsAPI> l_Missing;
    std::ranges::copy_if(l_Platform->GraphicsAPIs, std::back_inserter(l_Missing), [&l_APIs](Trinity::GraphicsAPI api) { return std::ranges::find(l_APIs, api) == l_APIs.end(); });
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
                Edit("graphics API added", [it_API](Trinity::ExportSettings& changed) { GetOrder(changed).push_back(it_API); });
            }
        }

        ImGui::EndCombo();
    }
}

// Virtual voices are never fewer than real ones, as Unity keeps them. A drag is one command
void ProjectSettingsPanel::DrawAudio(Trinity::AudioSettings settings, const Trinity::CollisionLayers& layers, bool playing)
{
    Hint(playing ? "The scene playing hears another mixer at once, and keeps the rest as it started with until it stops" : "Saved with the project as they change. The mixer is heard at once, and the rest taken by a scene as it starts playing");
    if (Shows(Section::Audio, "Mixer"))
    {
        DrawMixer(settings);
    }

    ImGui::BeginDisabled(playing);
    if (Shows(Section::Audio, "Real Voices"))
    {
        Label("Real Voices");
        int l_Real = static_cast<int>(settings.RealVoices);
        if (Trinity::Widgets::DragInt("##RealVoices", &l_Real, 0.2f, 1, c_MaxRealVoices, "%d at once", ImGuiSliderFlags_AlwaysClamp))
        {
            settings.RealVoices = static_cast<std::uint32_t>(l_Real);
            settings.VirtualVoices = std::max(settings.VirtualVoices, settings.RealVoices);
            m_Session.EditAudioSettings(settings, "real voices");
        }

        Tooltip("Sounds mixed at once, the most important first, then the loudest. The rest go on in time, unheard");
    }

    if (Shows(Section::Audio, "Virtual Voices"))
    {
        Label("Virtual Voices");
        int l_Virtual = static_cast<int>(settings.VirtualVoices);
        if (Trinity::Widgets::DragInt("##VirtualVoices", &l_Virtual, 1.0f, static_cast<int>(settings.RealVoices), c_MaxVirtualVoices, "%d at once", ImGuiSliderFlags_AlwaysClamp))
        {
            settings.VirtualVoices = static_cast<std::uint32_t>(l_Virtual);
            m_Session.EditAudioSettings(settings, "virtual voices");
        }

        Tooltip("Sounds playing at once, heard or not. Beyond it the least important stop");
    }

    if (Shows(Section::Audio, "Doppler Factor"))
    {
        Label("Doppler Factor");
        if (Trinity::Widgets::DragFloat("##DopplerFactor", &settings.DopplerFactor, 0.01f, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
        {
            m_Session.EditAudioSettings(settings, "Doppler factor");
        }

        Tooltip("Scales every source's Doppler Level, 0 taking the Doppler effect away");
    }

    if (Shows(Section::Audio, "Occlusion Layers"))
    {
        Label("Occlusion Layers");
        if (LayerMaskCombo("##OcclusionLayers", settings.OcclusionLayers, layers))
        {
            m_Session.EditAudioSettings(settings, "occlusion layers");
        }

        Tooltip("The layers whose colliders muffle a source with Occlusion on, between it and the listener");
    }

    if (Shows(Section::Audio, "Occlusion Rays"))
    {
        Label("Occlusion Rays");
        int l_Rays = static_cast<int>(settings.OcclusionRays);
        if (Trinity::Widgets::DragInt("##OcclusionRays", &l_Rays, 0.2f, 0, c_MaxOcclusionRays, "%d a frame", ImGuiSliderFlags_AlwaysClamp))
        {
            settings.OcclusionRays = static_cast<std::uint32_t>(l_Rays);
            m_Session.EditAudioSettings(settings, "occlusion rays");
        }

        Tooltip("Rays cast a frame at most, from the listener to the occluded sources in turn. 0 checks none, so nothing is muffled");
    }

    if (Shows(Section::Audio, "Occlusion Time"))
    {
        Label("Occlusion Time");
        if (Trinity::Widgets::DragFloat("##OcclusionTime", &settings.OcclusionTime, 0.01f, 0.0f, 10.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp))
        {
            m_Session.EditAudioSettings(settings, "occlusion time");
        }

        Tooltip("How long a source takes to become muffled once blocked, and to clear again");
    }

    ImGui::EndDisabled();
}

// None, then the project's audio mixers by path. A missing one shows so, leaving Master alone
void ProjectSettingsPanel::DrawMixer(Trinity::AudioSettings settings)
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    std::vector<const Trinity::AssetRecord*> l_Mixers;
    for (const Trinity::AssetRecord* it_Record : l_Registry != nullptr ? l_Registry->GetRecords() : std::vector<const Trinity::AssetRecord*>())
    {
        if (it_Record->Importer == Trinity::AudioMixerAsset::c_AssetType)
        {
            l_Mixers.push_back(it_Record);
        }
    }

    std::ranges::sort(l_Mixers, {}, &Trinity::AssetRecord::Path);
    const auto a_Current = std::ranges::find(l_Mixers, settings.Mixer, &Trinity::AssetRecord::ID);
    const std::string l_Preview = !settings.Mixer ? std::string(c_NoMixer) : (a_Current != l_Mixers.end() ? (*a_Current)->Path : std::format("{} Missing, Master alone", Trinity::Icons::c_Warning));
    Label("Mixer");
    if (ImGui::BeginCombo("##Mixer", l_Preview.c_str(), ImGuiComboFlags_HeightLarge))
    {
        if (ImGui::Selectable(c_NoMixer, !settings.Mixer) && settings.Mixer)
        {
            Trinity::AudioSettings l_Changed = settings;
            l_Changed.Mixer = {};
            m_Session.EditAudioSettings(l_Changed, "mixer");
        }

        for (const Trinity::AssetRecord* it_Mixer : l_Mixers)
        {
            const bool l_Selected = it_Mixer->ID == settings.Mixer;
            ImGui::PushID(it_Mixer->ID.ToString().c_str());
            if (ImGui::Selectable(it_Mixer->Path.c_str(), l_Selected) && !l_Selected)
            {
                Trinity::AudioSettings l_Changed = settings;
                l_Changed.Mixer = it_Mixer->ID;
                m_Session.EditAudioSettings(l_Changed, "mixer");
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    Tooltip("The Audio Mixer everything Forge plays goes through. Create > Audio Mixer in the Content Browser makes one");
}

// Gravity in each world and the fixed steps a second both take. A drag is one command
void ProjectSettingsPanel::DrawPhysics(Trinity::PhysicsSettings settings)
{
    if (Shows(Section::Physics, "Gravity 3D"))
    {
        Label("Gravity 3D");
        if (Trinity::Widgets::DragFloat3("##Gravity3D", &settings.Gravity3D.x, 0.05f, -1000.0f, 1000.0f, "%.2f m/s\xC2\xB2", ImGuiSliderFlags_AlwaysClamp))
        {
            m_Session.EditPhysicsSettings(settings, "3D gravity");
        }
    }

    if (Shows(Section::Physics, "Gravity 2D"))
    {
        Label("Gravity 2D");
        if (Trinity::Widgets::DragFloat2("##Gravity2D", &settings.Gravity2D.x, 0.05f, -1000.0f, 1000.0f, "%.2f m/s\xC2\xB2", ImGuiSliderFlags_AlwaysClamp))
        {
            m_Session.EditPhysicsSettings(settings, "2D gravity");
        }

        Tooltip("In the XY plane the 2D world lies in, apart from the 3D world's, so a 2D game can fall at its own rate");
    }

    if (Shows(Section::Physics, "Step Rate"))
    {
        Label("Step Rate");
        int l_Rate = static_cast<int>(settings.StepRate);
        if (Trinity::Widgets::DragInt("##StepRate", &l_Rate, 0.5f, static_cast<int>(Trinity::SimulationClock::c_MinimumRate), static_cast<int>(Trinity::SimulationClock::c_MaximumRate), "%d steps a second", ImGuiSliderFlags_AlwaysClamp))
        {
            settings.StepRate = static_cast<std::uint32_t>(l_Rate);
            m_Session.EditPhysicsSettings(settings, "step rate");
        }

        Tooltip(std::format("Each step {:.2f} ms. Both worlds step this often, whatever the frame rate, and at most {} times a frame", 1000.0 / std::max(settings.StepRate, 1u), Trinity::SimulationClock::c_MaxStepsPerFrame).c_str());
    }
}

// The layers' names, then which of them collide with which
void ProjectSettingsPanel::DrawCollisionLayers(Trinity::PhysicsSettings settings)
{
    if (Shows(Section::CollisionLayers, "Layer Names"))
    {
        DrawLayerNames(settings);
    }

    if (Shows(Section::CollisionLayers, "Which Layers Collide"))
    {
        ImGui::SeparatorText("Which Layers Collide");
        DrawCollisionMatrix(settings);
    }
}

// A name is one command, made as its field is left
void ProjectSettingsPanel::DrawLayerNames(Trinity::PhysicsSettings settings)
{
    for (std::uint32_t it_Layer = 0; it_Layer < Trinity::c_MaxCollisionLayers; ++it_Layer)
    {
        ImGui::PushID(static_cast<int>(it_Layer));
        ImGui::AlignTextToFramePadding();
        // Padded with a space, which is a digit wide only in a font of one width
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Mono), 0.0f);
        TextLine(std::format("{:>2}", it_Layer));
        ImGui::PopFont();
        ImGui::SameLine(ImGui::GetFontSize() * 2.5f);
        ImGui::SetNextItemWidth(-FLT_MIN);

        if (ImGui::GetActiveID() != ImGui::GetID("##Name"))
        {
            m_Names[it_Layer] = settings.Layers.Names[it_Layer];
        }

        ImGui::InputTextWithHint("##Name", std::format("Layer {}", it_Layer).c_str(), &m_Names[it_Layer]);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            Trinity::PhysicsSettings l_Named = settings;
            l_Named.Layers.Names[it_Layer] = Trim(m_Names[it_Layer]);
            m_Session.EditPhysicsSettings(l_Named, std::format("layer {} name", it_Layer));
        }

        ImGui::PopID();
    }
}

// Layers down the rows and, last first, across the columns, so each pair has one box
void ProjectSettingsPanel::DrawCollisionMatrix(Trinity::PhysicsSettings settings)
{
    const Trinity::CollisionLayers& l_Layers = settings.Layers;
    constexpr int c_Columns = static_cast<int>(Trinity::c_MaxCollisionLayers) + 1;
    const ImGuiTableFlags l_Flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_HighlightHoveredColumn;
    if (!ImGui::BeginTable("##CollisionMatrix", c_Columns, l_Flags))
    {
        return;
    }

    ImGui::TableSetupScrollFreeze(1, 0);
    ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder);
    for (std::uint32_t it_Column = 0; it_Column < Trinity::c_MaxCollisionLayers; ++it_Column)
    {
        ImGui::TableSetupColumn(l_Layers.GetName(Trinity::c_MaxCollisionLayers - 1 - it_Column).c_str(), ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed);
    }

    ImGui::TableAngledHeadersRow();
    for (std::uint32_t it_Row = 0; it_Row < Trinity::c_MaxCollisionLayers; ++it_Row)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        TextLine(l_Layers.GetName(it_Row));

        for (std::uint32_t it_Column = 0; it_Column < Trinity::c_MaxCollisionLayers; ++it_Column)
        {
            const std::uint32_t l_Other = Trinity::c_MaxCollisionLayers - 1 - it_Column;
            if (l_Other < it_Row)
            {
                continue;
            }

            ImGui::TableSetColumnIndex(static_cast<int>(it_Column) + 1);
            ImGui::PushID(static_cast<int>(it_Row * Trinity::c_MaxCollisionLayers + l_Other));
            bool l_Collides = l_Layers.Collides(it_Row, l_Other);
            if (ImGui::Checkbox("##Collides", &l_Collides))
            {
                Trinity::PhysicsSettings l_Toggled = settings;
                l_Toggled.Layers.SetCollides(it_Row, l_Other, l_Collides);
                m_Session.EditPhysicsSettings(l_Toggled, std::format("collision of {} and {}", l_Layers.GetName(it_Row), l_Layers.GetName(l_Other)));
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("%s", std::format("{} and {} {}", l_Layers.GetName(it_Row), l_Layers.GetName(l_Other), l_Collides ? "collide" : "pass through each other").c_str());
            }

            ImGui::PopID();
        }
    }

    ImGui::EndTable();
}