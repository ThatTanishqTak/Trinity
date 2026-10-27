#include "Panels/AudioMixerPanel.hpp"

#include "Panels/ConsolePanel.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr float c_StripWidthInFonts = 7.5f;
    constexpr float c_FaderHeightInFonts = 11.0f;
    // Where a meter turns from green to yellow, and from yellow to red, in decibels
    constexpr float c_MeterWarm = -6.0f;
    constexpr float c_MeterHot = 0.0f;
    // A group dragged in the tree, by its ID, and an effect dragged along its strip, by its group's ID and its place in the chain
    constexpr const char* c_GroupPayload = "TRINITY_MIXER_GROUP";
    constexpr const char* c_EffectPayload = "TRINITY_MIXER_EFFECT";

    struct EffectPayload
    {
        std::uint64_t Group = 0;
        std::uint64_t Place = 0;
    };

    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
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

    // Whether the windows are drawn light, by how bright their background is, as the Console tells its colours
    bool IsLightTheme()
    {
        const ImVec4 l_Background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);

        return l_Background.x * 0.2126f + l_Background.y * 0.7152f + l_Background.z * 0.0722f > 0.5f;
    }

    // A column's title, with its add button at the right when it has one. Whether that was pressed
    bool ColumnHeader(std::string_view title, const char* addTooltip)
    {
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
        ImGui::AlignTextToFramePadding();
        TextLine(title);
        ImGui::PopFont();

        bool l_Add = false;
        if (addTooltip != nullptr)
        {
            const float l_Size = ImGui::GetFrameHeight();
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - l_Size));
            l_Add = ImGui::Button("+", ImVec2(l_Size, l_Size));
            ImGui::SetItemTooltip("%s", addTooltip);
        }

        ImGui::Separator();

        return l_Add;
    }

    // A name new among the items': the base, or the base with the first number free after it
    template<typename List>
    std::string FreeName(const List& items, std::string_view base)
    {
        const auto a_Taken = [&items](std::string_view name) { return std::ranges::any_of(items, [name](const auto& item) { return std::string_view(item.Name) == name; }); };
        if (!a_Taken(base))
        {
            return std::string(base);
        }

        for (std::uint32_t it_Number = 1;; ++it_Number)
        {
            std::string l_Name = std::format("{} {}", base, it_Number);
            if (!a_Taken(l_Name))
            {
                return l_Name;
            }
        }
    }

    // Why the item at that index cannot be called this among the others, which a script finds it by, or empty when it can
    template<typename List>
    std::string CheckName(const List& items, std::size_t self, std::string_view name, std::string_view what)
    {
        if (name.empty())
        {
            return std::format("A {} needs a name", what);
        }

        for (std::size_t it_Index = 0; it_Index < items.size(); ++it_Index)
        {
            if (it_Index != self && std::string_view(items[it_Index].Name) == name)
            {
                return std::format("Another {} is called {}", what, name);
            }
        }

        return {};
    }

    [[nodiscard]] const Trinity::AudioMixerGroup* FindGroupOf(const Trinity::AudioMixerData& data, Trinity::UUID effect)
    {
        for (const Trinity::AudioMixerGroup& it_Group : data.Groups)
        {
            if (std::ranges::any_of(it_Group.Effects, [effect](const Trinity::AudioEffect& other) { return other.Type != Trinity::AudioEffectType::Attenuation && other.ID == effect; }))
            {
                return &it_Group;
            }
        }

        return nullptr;
    }

    // The groups right below one, in their order
    [[nodiscard]] std::vector<const Trinity::AudioMixerGroup*> GetChildren(const Trinity::AudioMixerData& data, Trinity::UUID parent)
    {
        std::vector<const Trinity::AudioMixerGroup*> l_Children;
        for (const Trinity::AudioMixerGroup& it_Group : data.Groups)
        {
            if (it_Group.Parent == parent && it_Group.Parent)
            {
                l_Children.push_back(&it_Group);
            }
        }

        return l_Children;
    }

    // Each group as the tree shows it, from Master down, each before the groups below it, which is the order of the strips
    void CollectBranch(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group, std::vector<const Trinity::AudioMixerGroup*>& order)
    {
        order.push_back(&group);
        for (const Trinity::AudioMixerGroup* it_Child : GetChildren(data, group.ID))
        {
            CollectBranch(data, *it_Child, order);
        }
    }

    // As the exposed values list names what each is: Volume of Music, or Room of SFX Reverb in Reverb
    std::string DescribeValue(const Trinity::AudioMixerData& data, Trinity::AudioMixerValue value)
    {
        const std::optional<Trinity::AudioEffectParameter> l_Info = data.GetValueInfo(value);
        if (!l_Info)
        {
            return "nothing the mixer has";
        }

        if (const Trinity::AudioMixerGroup* l_Group = data.FindGroup(value.Owner))
        {
            return std::format("{} of {}", l_Info->Name, std::string_view(l_Group->Name));
        }

        const Trinity::AudioEffect* l_Effect = data.FindEffect(value.Owner);
        const Trinity::AudioMixerGroup* l_Group = FindGroupOf(data, value.Owner);

        return std::format("{} of {} in {}", l_Info->Name, Trinity::GetAudioEffectInfo(l_Effect->Type).Name, l_Group != nullptr ? std::string_view(l_Group->Name) : std::string_view());
    }

    // What an effect in a strip says it does when hovered
    std::string DescribeEffect(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group, const Trinity::AudioEffect& effect)
    {
        if (effect.Type == Trinity::AudioEffectType::Attenuation)
        {
            return "The group's volume, which the fader sets. What comes before it is before the fader";
        }

        std::string l_Text(Trinity::GetAudioEffectInfo(effect.Type).Name);
        if (effect.Type == Trinity::AudioEffectType::Send)
        {
            const Trinity::AudioEffect* l_Target = effect.Target ? data.FindEffect(effect.Target) : nullptr;
            const Trinity::AudioMixerGroup* l_To = l_Target != nullptr ? FindGroupOf(data, effect.Target) : nullptr;
            l_Text += l_To != nullptr ? std::format(", to the {} in {}", Trinity::GetAudioEffectInfo(l_Target->Type).Name, std::string_view(l_To->Name)) : std::string(", sending nowhere yet");
        }

        if (effect.Bypass || group.Bypass)
        {
            l_Text += effect.Bypass ? ", bypassed" : ", bypassed with its group";
        }

        return l_Text;
    }

    // Every type but Attenuation, which a group has once, as the effect menus list them. The type picked, if any
    std::optional<Trinity::AudioEffectType> DrawEffectTypes()
    {
        std::optional<Trinity::AudioEffectType> l_Picked;
        for (const Trinity::AudioEffectInfo& it_Info : Trinity::GetAudioEffectInfos())
        {
            if (it_Info.Type != Trinity::AudioEffectType::Attenuation && ImGui::MenuItem(std::string(it_Info.Name).c_str()))
            {
                l_Picked = it_Info.Type;
            }
        }

        return l_Picked;
    }

    // Each channel a bar on the fader's own scale of decibels, so 0 dB is level with the fader's, its RMS filled green, then yellow from -6 dB and red from 0 dB, and its peak a line. Two empty bars for a group nothing plays through
    void DrawMeter(const std::optional<Trinity::AudioMeter>& meter, ImVec2 size)
    {
        const ImVec2 l_Min = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
        l_DrawList.AddRectFilled(l_Min, ImVec2(l_Min.x + size.x, l_Min.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));

        const bool l_Light = IsLightTheme();
        const ImU32 l_Green = ImGui::GetColorU32(l_Light ? ImVec4(0.1f, 0.55f, 0.15f, 1.0f) : ImVec4(0.35f, 0.85f, 0.4f, 1.0f));
        const ImU32 l_Yellow = ImGui::GetColorU32(ConsolePanel::GetLevelColor(Trinity::LogLevel::Warn));
        const ImU32 l_Red = ImGui::GetColorU32(ConsolePanel::GetLevelColor(Trinity::LogLevel::Error));
        const float l_Bottom = l_Min.y + size.y;
        const auto a_Height = [&size](float decibels) { return size.y * std::clamp((decibels - Trinity::AudioMixerData::c_MinimumVolume) / (Trinity::AudioMixerData::c_MaximumVolume - Trinity::AudioMixerData::c_MinimumVolume), 0.0f, 1.0f); };
        const auto a_Color = [&](float decibels) { return decibels >= c_MeterHot ? l_Red : (decibels >= c_MeterWarm ? l_Yellow : l_Green); };

        const std::uint32_t l_Channels = meter && meter->Channels > 0 ? meter->Channels : 2;
        const float l_Gap = 1.0f;
        const float l_Width = std::max(1.0f, (size.x - l_Gap * static_cast<float>(l_Channels - 1)) / static_cast<float>(l_Channels));
        for (std::uint32_t it_Channel = 0; meter && it_Channel < l_Channels; ++it_Channel)
        {
            const float l_Left = l_Min.x + static_cast<float>(it_Channel) * (l_Width + l_Gap);
            const float l_Rms = Trinity::AudioGainToDecibels(meter->Rms[it_Channel]);
            const float l_Peak = Trinity::AudioGainToDecibels(meter->Peak[it_Channel]);
            float l_From = Trinity::AudioMixerData::c_MinimumVolume;
            for (const float it_To : { c_MeterWarm, c_MeterHot, Trinity::AudioMixerData::c_MaximumVolume })
            {
                const float l_Top = std::min(l_Rms, it_To);
                if (l_Top > l_From)
                {
                    l_DrawList.AddRectFilled(ImVec2(l_Left, l_Bottom - a_Height(l_Top)), ImVec2(l_Left + l_Width, l_Bottom - a_Height(l_From)), a_Color(l_From));
                }

                l_From = it_To;
            }

            if (l_Peak > Trinity::AudioMixerData::c_MinimumVolume)
            {
                const float l_Y = l_Bottom - a_Height(l_Peak);
                l_DrawList.AddLine(ImVec2(l_Left, l_Y), ImVec2(l_Left + l_Width, l_Y), a_Color(l_Peak), 2.0f);
            }
        }

        const float l_Zero = l_Bottom - a_Height(0.0f);
        l_DrawList.AddLine(ImVec2(l_Min.x, l_Zero), ImVec2(l_Min.x + size.x, l_Zero), ImGui::GetColorU32(ImGuiCol_TextDisabled));
    }

    // A strip's Mute, Solo or Bypass, lit in its colour while on. Whether it was pressed
    bool ToggleButton(const char* label, bool on, const ImVec4& color, float width, const char* tooltip)
    {
        if (on)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.7f));
        }

        const bool l_Pressed = ImGui::Button(label, ImVec2(width, 0.0f));
        if (on)
        {
            ImGui::PopStyleColor(2);
        }

        ImGui::SetItemTooltip("%s", tooltip);

        return l_Pressed;
    }
}

AudioMixerPanel::AudioMixerPanel(EditorSession& session) : Panel("Audio Mixer", Trinity::Icons::c_Sliders, DockSlot::Bottom), m_Session(session)
{

}

void AudioMixerPanel::SetAsset(Trinity::UUID asset)
{
    m_Asset = asset;
}

// The asset opened, while the project has it, and else the project's mixer
Trinity::UUID AudioMixerPanel::GetShownAsset() const
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = l_Registry != nullptr && m_Asset ? l_Registry->Find(m_Asset) : nullptr;
    if (l_Record != nullptr && l_Record->Importer == Trinity::AudioMixerAsset::c_AssetType)
    {
        return m_Asset;
    }

    const Trinity::Project* l_Project = m_Session.GetProject();

    return l_Project != nullptr ? l_Project->GetAudioSettings().Mixer : Trinity::UUID();
}

bool AudioMixerPanel::IsProjectMixer(Trinity::UUID asset) const
{
    const Trinity::Project* l_Project = m_Session.GetProject();

    return l_Project != nullptr && asset && l_Project->GetAudioSettings().Mixer == asset;
}

// Always but while the project's mixer plays a scene with Edit in Play Mode off, when the faders follow the mix
bool AudioMixerPanel::IsEditable(Trinity::UUID asset) const
{
    return !m_Session.IsPlaying() || !IsProjectMixer(asset) || m_Session.IsEditingMixerInPlay();
}

// Edits are made once the panel has drawn, so the data it drew from stays as it was until then. A selection follows the edit only once it has been made
void AudioMixerPanel::OnImGuiRender()
{
    m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const Trinity::UUID l_Asset = m_Session.HasProject() ? GetShownAsset() : Trinity::UUID();
    const EditorSession::AudioMixerFile* l_File = l_Asset ? m_Session.ReadAudioMixer(l_Asset) : nullptr;
    const Trinity::AssetRecord* l_Record = l_File != nullptr ? m_Session.GetRegistry()->Find(l_Asset) : nullptr;
    if (l_File == nullptr || l_Record == nullptr)
    {
        m_Renaming = Chosen::None;
        ImGui::TextDisabled("%s", m_Session.HasProject() ? "Double-click an audio mixer in the Content Browser to edit it here. Create > Audio Mixer there makes one, which is the project's mixer while it has none" : "Open or create a project to edit its audio mixers");

        return;
    }

    DrawHeader(l_Asset, *l_Record, l_File->Data ? &*l_File->Data : nullptr);
    if (!l_File->Data)
    {
        m_Renaming = Chosen::None;
        ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Error));
        ImGui::TextWrapped("%s", l_File->Error.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Once the file is fixed, it is read again as Forge gets the focus back, or at F5");

        return;
    }

    const Trinity::AudioMixerData& l_Data = *l_File->Data;
    const bool l_Editable = IsEditable(l_Asset);
    Clamp(l_Asset, l_Data);
    ReadShortcuts(l_Data, l_Editable);
    m_RenameDrawn = false;
    if (!l_Editable)
    {
        m_Renaming = Chosen::None;
    }

    if (ImGui::BeginTable("##AudioMixer", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp, ImGui::GetContentRegionAvail()))
    {
        ImGui::TableSetupColumn("Tree", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Strips", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##Tree"))
        {
            DrawSnapshots(l_Data, l_Editable);
            ImGui::Spacing();
            DrawGroups(l_Data, l_Editable);
            ImGui::Spacing();
            DrawExposed(l_Data, l_Editable);
        }

        ImGui::EndChild();

        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##Strips", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
        {
            DrawStrips(l_Asset, l_Data, l_Editable);
        }

        ImGui::EndChild();
        ImGui::EndTable();
    }

    // A rename whose field has been drawn and is no longer, as under a group folded away, ends as it is
    if (m_Renaming != Chosen::None && !m_RenameDrawn && !m_FocusRename)
    {
        m_Renaming = Chosen::None;
    }

    // Nothing is being dragged or typed into, so the next change is a command of its own
    if (!ImGui::IsAnyItemActive())
    {
        m_Session.GetHistory().EndMerge();
    }

    if (m_Pending)
    {
        Pending l_Pending = std::move(*m_Pending);
        m_Pending.reset();
        if (m_Session.EditAudioMixer(l_Asset, l_Data, l_Pending.After, l_Pending.What, std::move(l_Pending.Field)) && l_Pending.Select)
        {
            m_Session.SetMixerSelection(*l_Pending.Select);
        }
    }
}

// The selection stays inside the mixer shown as an undo or a reload takes things away, and starts at its start snapshot when another mixer is shown
void AudioMixerPanel::Clamp(Trinity::UUID asset, const Trinity::AudioMixerData& data)
{
    EditorSession::MixerSelection l_Selection = m_Session.GetMixerSelection();
    if (l_Selection.Asset != asset)
    {
        l_Selection = { asset, data.StartSnapshot,{} };
        m_Chosen = Chosen::None;
        m_Renaming = Chosen::None;
    }

    if (data.FindSnapshot(l_Selection.Snapshot) == nullptr)
    {
        l_Selection.Snapshot = data.StartSnapshot;
    }

    if (l_Selection.Item && data.FindGroup(l_Selection.Item) == nullptr && data.FindEffect(l_Selection.Item) == nullptr)
    {
        l_Selection.Item = {};
    }

    if (m_Chosen == Chosen::Exposed && m_ChosenExposed >= data.Exposed.size())
    {
        m_Chosen = Chosen::None;
    }

    // A rename of something an undo took away, or that was never made, as a group added and refused
    const bool l_RenameGone = (m_Renaming == Chosen::Snapshot && data.FindSnapshot(m_RenameID) == nullptr) || (m_Renaming == Chosen::Item && data.FindGroup(m_RenameID) == nullptr) || (m_Renaming == Chosen::Exposed && m_RenameIndex >= data.Exposed.size());
    if (l_RenameGone)
    {
        m_Renaming = Chosen::None;
    }

    if (l_Selection != m_Session.GetMixerSelection())
    {
        m_Session.SetMixerSelection(l_Selection);
    }
}

void AudioMixerPanel::Edit(Trinity::AudioMixerData after, std::string what, std::string field, std::optional<EditorSession::MixerSelection> select)
{
    m_Pending = Pending{ std::move(after), std::move(what), std::move(field), select };
}

void AudioMixerPanel::Choose(Chosen chosen, Trinity::UUID snapshot, Trinity::UUID item)
{
    EditorSession::MixerSelection l_Selection = m_Session.GetMixerSelection();
    l_Selection.Snapshot = snapshot;
    l_Selection.Item = item;
    m_Session.SetMixerSelection(l_Selection);
    m_Chosen = chosen;
    m_RenameError.clear();
}

void AudioMixerPanel::DrawHeader(Trinity::UUID asset, const Trinity::AssetRecord& record, const Trinity::AudioMixerData* data)
{
    TextLine(std::format("{} {}", Trinity::Icons::c_Sliders, record.Path));
    ImGui::SameLine();
    const bool l_Project = IsProjectMixer(asset);
    if (l_Project)
    {
        ImGui::TextDisabled("the project's mixer");
        ImGui::SetItemTooltip("Everything Forge plays goes through it, a scene playing included");
    }
    else if (ImGui::SmallButton("Set as Project Mixer"))
    {
        m_Session.SetProjectMixer(asset);
    }

    if (!l_Project)
    {
        ImGui::TextDisabled("Nothing plays through it until it is the project's mixer, so its meters stay still");

        return;
    }

    if (!m_Session.IsPlaying())
    {
        return;
    }

    constexpr const char* c_Label = "Edit in Play Mode";
    bool l_Edit = m_Session.IsEditingMixerInPlay();
    const float l_Width = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(c_Label).x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - l_Width));
    if (ImGui::Checkbox(c_Label, &l_Edit))
    {
        m_Session.SetEditMixerInPlay(l_Edit);
    }

    ImGui::SetItemTooltip("Holds the mix at the selected snapshot so it can be edited, each edit heard at once and kept once the scene stops. Off, the faders follow the mix the scene plays");
    const Trinity::AudioMixerSnapshot* l_Snapshot = data != nullptr ? data->FindSnapshot(m_Session.GetMixerSelection().Snapshot) : nullptr;
    if (l_Edit)
    {
        ImGui::TextDisabled("%s", std::format("The mix holds at {} while the scene plays, and each edit is heard at once and kept once it stops", l_Snapshot != nullptr ? std::string_view(l_Snapshot->Name) : std::string_view("the selected snapshot")).c_str());
    }
    else
    {
        ImGui::TextDisabled("Following the mix the scene plays. Edit in Play Mode holds it at the selected snapshot to edit it");
    }
}

void AudioMixerPanel::DrawRenameError(Chosen chosen)
{
    if (m_RenameErrorIn == chosen && !m_RenameError.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Error));
        ImGui::TextWrapped("%s", m_RenameError.c_str());
        ImGui::PopStyleColor();
    }
}

// The start snapshot is marked, and is neither removed nor set again. While the faders follow the mix a snapshot is chosen, and nothing is edited. Each list of the column has an ID of its own, so the headers' + and the rows' fields never meet
void AudioMixerPanel::DrawSnapshots(const Trinity::AudioMixerData& data, bool editable)
{
    ImGui::PushID("Snapshots");
    if (ColumnHeader("Snapshots", editable ? "Add a copy of the selected snapshot" : nullptr))
    {
        AddSnapshot(data, m_Session.GetMixerSelection().Snapshot);
    }

    DrawRenameError(Chosen::Snapshot);
    for (std::size_t it_Snapshot = 0; it_Snapshot < data.Snapshots.size(); ++it_Snapshot)
    {
        const Trinity::AudioMixerSnapshot& l_Snapshot = data.Snapshots[it_Snapshot];
        const bool l_Start = l_Snapshot.ID == data.StartSnapshot;
        ImGui::PushID(static_cast<int>(it_Snapshot));
        if (m_Renaming == Chosen::Snapshot && m_RenameID == l_Snapshot.ID)
        {
            DrawRenameField(data);
            ImGui::PopID();

            continue;
        }

        const EditorSession::MixerSelection l_Selection = m_Session.GetMixerSelection();
        if (ImGui::Selectable(std::format("{}###Snapshot", std::string_view(l_Snapshot.Name)).c_str(), l_Selection.Snapshot == l_Snapshot.ID, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap))
        {
            Choose(Chosen::Snapshot, l_Snapshot.ID, l_Selection.Item);
            if (editable && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                BeginRename(Chosen::Snapshot, l_Snapshot.ID, 0, l_Snapshot.Name);
            }
        }

        if (ImGui::BeginPopupContextItem("##SnapshotMenu"))
        {
            ImGui::BeginDisabled(!editable);
            if (ImGui::MenuItem("Rename", "F2"))
            {
                Choose(Chosen::Snapshot, l_Snapshot.ID, l_Selection.Item);
                BeginRename(Chosen::Snapshot, l_Snapshot.ID, 0, l_Snapshot.Name);
            }

            if (ImGui::MenuItem(std::format("{} Duplicate", Trinity::Icons::c_Copy).c_str(), "Ctrl+D"))
            {
                AddSnapshot(data, l_Snapshot.ID);
            }

            if (ImGui::MenuItem("Set as Start Snapshot", nullptr, false, !l_Start))
            {
                SetStartSnapshot(data, l_Snapshot.ID);
            }

            if (ImGui::MenuItem(std::format("{} Delete", Trinity::Icons::c_Trash).c_str(), "Delete", false, !l_Start))
            {
                RemoveSnapshot(data, l_Snapshot.ID);
            }

            ImGui::EndDisabled();
            ImGui::EndPopup();
        }

        if (l_Start)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", std::format("{} start", Trinity::Icons::c_Star).c_str());
            ImGui::SetItemTooltip("The snapshot the mixer starts at, as each Play does");
        }

        ImGui::PopID();
    }

    ImGui::PopID();
}

void AudioMixerPanel::DrawGroups(const Trinity::AudioMixerData& data, bool editable)
{
    const Trinity::UUID l_Item = m_Session.GetMixerSelection().Item;
    const Trinity::AudioMixerGroup* l_Selected = data.FindGroup(l_Item);
    l_Selected = l_Selected != nullptr ? l_Selected : FindGroupOf(data, l_Item);
    ImGui::PushID("Groups");
    if (ColumnHeader("Groups", editable ? "Add a group below the selected one" : nullptr))
    {
        AddGroup(data, l_Selected != nullptr ? l_Selected->ID : data.GetMaster().ID);
    }

    DrawRenameError(Chosen::Item);
    DrawGroupNode(data, data.GetMaster(), editable);
    ImGui::PopID();
}

// A group dragged onto another goes below it, after the groups already there. Master is never dragged, renamed or removed
void AudioMixerPanel::DrawGroupNode(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group, bool editable)
{
    const std::vector<const Trinity::AudioMixerGroup*> l_Children = GetChildren(data, group.ID);
    const bool l_Renaming = m_Renaming == Chosen::Item && m_RenameID == group.ID;
    ImGuiTreeNodeFlags l_Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap;
    l_Flags |= l_Children.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_None;
    l_Flags |= m_Session.GetMixerSelection().Item == group.ID ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None;
    ImGui::PushID(group.ID.ToString().c_str());
    const bool l_Open = ImGui::TreeNodeEx("##Group", l_Flags, "%s", l_Renaming ? "" : group.Name.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Choose(Chosen::Item, m_Session.GetMixerSelection().Snapshot, group.ID);
        if (editable && group.Parent && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            BeginRename(Chosen::Item, group.ID, 0, group.Name);
        }
    }

    if (editable && group.Parent && ImGui::BeginDragDropSource())
    {
        const std::uint64_t l_Value = group.ID.GetValue();
        ImGui::SetDragDropPayload(c_GroupPayload, &l_Value, sizeof(l_Value));
        TextLine(std::format("{} {}", Trinity::Icons::c_Sliders, std::string_view(group.Name)));
        ImGui::EndDragDropSource();
    }

    if (editable && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_GroupPayload); l_Payload != nullptr && l_Payload->DataSize == sizeof(std::uint64_t))
        {
            std::uint64_t l_Value = 0;
            std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));
            MoveGroup(data, Trinity::UUID(l_Value), group.ID, {}, std::format("below {}", std::string_view(group.Name)));
        }

        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem("##GroupMenu"))
    {
        ImGui::BeginDisabled(!editable);
        DrawGroupMenu(data, group);
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    if (l_Renaming)
    {
        ImGui::SameLine();
        DrawRenameField(data);
    }

    if (l_Open)
    {
        for (const Trinity::AudioMixerGroup* it_Child : l_Children)
        {
            DrawGroupNode(data, *it_Child, editable);
        }

        ImGui::TreePop();
    }

    ImGui::PopID();
}

// As Unity's group menu: Master only takes a group below it
void AudioMixerPanel::DrawGroupMenu(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group)
{
    if (ImGui::MenuItem("Add Child Group"))
    {
        AddGroup(data, group.ID);
    }

    if (!group.Parent)
    {
        return;
    }

    if (ImGui::MenuItem("Add Sibling Group"))
    {
        AddGroup(data, group.Parent);
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2"))
    {
        Choose(Chosen::Item, m_Session.GetMixerSelection().Snapshot, group.ID);
        BeginRename(Chosen::Item, group.ID, 0, group.Name);
    }

    if (ImGui::MenuItem(std::format("{} Duplicate", Trinity::Icons::c_Copy).c_str(), "Ctrl+D"))
    {
        DuplicateGroup(data, group.ID);
    }

    const std::vector<const Trinity::AudioMixerGroup*> l_Siblings = GetChildren(data, group.Parent);
    const std::size_t l_Index = static_cast<std::size_t>(std::ranges::find(l_Siblings, &group) - l_Siblings.begin());
    if (ImGui::MenuItem("Move Up", nullptr, false, l_Index > 0))
    {
        MoveGroup(data, group.ID, group.Parent, l_Siblings[l_Index - 1]->ID, "up");
    }

    if (ImGui::MenuItem("Move Down", nullptr, false, l_Index + 1 < l_Siblings.size()))
    {
        MoveGroup(data, group.ID, group.Parent, l_Index + 2 < l_Siblings.size() ? l_Siblings[l_Index + 2]->ID : Trinity::UUID(), "down");
    }

    if (ImGui::MenuItem(std::format("{} Remove", Trinity::Icons::c_Trash).c_str(), "Delete"))
    {
        RemoveGroup(data, group.ID);
    }
}

// Each by its name, with the value it names after it. Choosing one chooses its group or effect too
void AudioMixerPanel::DrawExposed(const Trinity::AudioMixerData& data, bool editable)
{
    ImGui::PushID("Exposed");
    ColumnHeader(std::format("Exposed Parameters ({})", data.Exposed.size()), nullptr);
    DrawRenameError(Chosen::Exposed);
    if (data.Exposed.empty())
    {
        ImGui::TextDisabled("None yet. An exposed value is named for scripts' SetFloat and GetFloat");
    }

    for (std::size_t it_Exposed = 0; it_Exposed < data.Exposed.size(); ++it_Exposed)
    {
        const Trinity::AudioExposedParameter& l_Exposed = data.Exposed[it_Exposed];
        ImGui::PushID(static_cast<int>(it_Exposed));
        if (m_Renaming == Chosen::Exposed && m_RenameIndex == it_Exposed)
        {
            DrawRenameField(data);
            ImGui::PopID();

            continue;
        }

        if (ImGui::Selectable(std::format("{}###Exposed", std::string_view(l_Exposed.Name)).c_str(), m_Chosen == Chosen::Exposed && m_ChosenExposed == it_Exposed, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap))
        {
            Choose(Chosen::Exposed, m_Session.GetMixerSelection().Snapshot, l_Exposed.Value.Owner);
            m_ChosenExposed = it_Exposed;
            if (editable && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                BeginRename(Chosen::Exposed, {}, it_Exposed, l_Exposed.Name);
            }
        }

        if (ImGui::BeginPopupContextItem("##ExposedMenu"))
        {
            ImGui::BeginDisabled(!editable);
            if (ImGui::MenuItem("Rename", "F2"))
            {
                BeginRename(Chosen::Exposed, {}, it_Exposed, l_Exposed.Name);
            }

            if (ImGui::MenuItem(std::format("{} Remove", Trinity::Icons::c_Trash).c_str(), "Delete"))
            {
                RemoveExposed(data, it_Exposed);
            }

            ImGui::EndDisabled();
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s", DescribeValue(data, l_Exposed.Value).c_str());
        ImGui::PopID();
    }

    ImGui::PopID();
}

// Master first, then each group before the groups below it, as the tree has them
void AudioMixerPanel::DrawStrips(Trinity::UUID asset, const Trinity::AudioMixerData& data, bool editable)
{
    std::vector<const Trinity::AudioMixerGroup*> l_Order;
    CollectBranch(data, data.GetMaster(), l_Order);
    for (std::size_t it_Strip = 0; it_Strip < l_Order.size(); ++it_Strip)
    {
        if (it_Strip > 0)
        {
            ImGui::SameLine();
        }

        DrawStrip(asset, data, *l_Order[it_Strip], editable);
    }
}

// The group's name, its volume in decibels, its Mute, Solo and Bypass, its fader beside its meter, and its chain. While the faders follow the mix they show where it has each volume now, and the meters move only for the project's mixer, which is the one playing
void AudioMixerPanel::DrawStrip(Trinity::UUID asset, const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group, bool editable)
{
    const float l_Font = ImGui::GetFontSize();
    const EditorSession::MixerSelection l_Selection = m_Session.GetMixerSelection();
    const bool l_Selected = l_Selection.Item == group.ID || FindGroupOf(data, l_Selection.Item) == &group;
    const std::string_view l_Name(group.Name);
    ImGui::PushID(group.ID.ToString().c_str());
    if (l_Selected)
    {
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    }

    const bool l_Visible = ImGui::BeginChild("##Strip", ImVec2(l_Font * c_StripWidthInFonts, 0.0f), ImGuiChildFlags_Borders);
    if (l_Selected)
    {
        ImGui::PopStyleColor();
    }

    if (!l_Visible)
    {
        ImGui::EndChild();
        ImGui::PopID();

        return;
    }

    if (ImGui::Selectable(std::format("{}###Name", l_Name).c_str(), l_Selection.Item == group.ID))
    {
        Choose(Chosen::Item, l_Selection.Snapshot, group.ID);
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("%s", group.Name.c_str());
    }

    const Trinity::AudioMixerValue l_Value{ group.ID, Trinity::AudioMixerValue::c_Volume };
    const Trinity::AudioMixer* l_Mixer = IsProjectMixer(asset) ? m_Session.GetMixer() : nullptr;
    const Trinity::AudioMixerSnapshot* l_Snapshot = data.FindSnapshot(l_Selection.Snapshot);
    const Trinity::AudioMixerSnapshot& l_Shown = l_Snapshot != nullptr ? *l_Snapshot : data.GetStartSnapshot();
    float l_Volume = data.GetValue(l_Shown, l_Value).value_or(0.0f);
    if (!editable && l_Mixer != nullptr)
    {
        l_Volume = l_Mixer->GetValue(l_Value).value_or(l_Volume);
    }

    const std::string l_Text = std::format("{:.1f} dB", l_Volume);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(l_Text.c_str()).x) * 0.5f));
    TextLine(l_Text);

    ImGui::BeginDisabled(!editable);
    DrawToggles(data, group);
    const float l_FaderWidth = ImGui::GetFrameHeight();
    const float l_MeterWidth = l_Font;
    const float l_Height = l_Font * c_FaderHeightInFonts;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - l_FaderWidth - ImGui::GetStyle().ItemSpacing.x - l_MeterWidth) * 0.5f));
    float l_Edited = l_Volume;
    if (ImGui::VSliderFloat("##Volume", ImVec2(l_FaderWidth, l_Height), &l_Edited, Trinity::AudioMixerData::c_MinimumVolume, Trinity::AudioMixerData::c_MaximumVolume, ""))
    {
        Trinity::AudioMixerData l_After = data;
        l_After.SetValue(l_Shown.ID, l_Value, l_Edited);
        Edit(std::move(l_After), std::format("Set volume of {} ({})", l_Name, std::string_view(l_Shown.Name)), std::format("Volume {} {}", group.ID, l_Shown.ID));
    }

    if (ImGui::IsItemActivated())
    {
        Choose(Chosen::Item, l_Shown.ID, group.ID);
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("%s", std::format("{}'s volume in {}, from -80 dB, which is silence, to +20 dB. Ctrl+click to type one", l_Name, std::string_view(l_Shown.Name)).c_str());
    }

    if (ImGui::BeginPopupContextItem("##FaderMenu"))
    {
        if (ImGui::MenuItem("Reset to 0 dB", nullptr, false, l_Volume != 0.0f))
        {
            Trinity::AudioMixerData l_After = data;
            l_After.SetValue(l_Shown.ID, l_Value, 0.0f);
            Edit(std::move(l_After), std::format("Reset volume of {} ({})", l_Name, std::string_view(l_Shown.Name)));
        }

        ImGui::EndPopup();
    }

    ImGui::EndDisabled();
    ImGui::SameLine();
    DrawMeter(l_Mixer != nullptr ? l_Mixer->GetMeter(group.ID) : std::nullopt, ImVec2(l_MeterWidth, l_Height));
    ImGui::Separator();
    DrawChain(data, group, editable);
    ImGui::EndChild();
    ImGui::PopID();
}

void AudioMixerPanel::DrawToggles(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group)
{
    const float l_Width = std::max(1.0f, (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f);
    const std::string_view l_Name(group.Name);
    const auto a_Toggle = [&](bool Trinity::AudioMixerGroup::* flag, std::string what)
    {
        Trinity::AudioMixerData l_After = data;
        Trinity::AudioMixerGroup& l_Group = *l_After.FindGroup(group.ID);
        l_Group.*flag = !(l_Group.*flag);
        Edit(std::move(l_After), std::move(what));
    };

    if (ToggleButton("M", group.Mute, ConsolePanel::GetLevelColor(Trinity::LogLevel::Error), l_Width, "Mute: silences the group and every group below it"))
    {
        a_Toggle(&Trinity::AudioMixerGroup::Mute, std::format("{} {}", group.Mute ? "Unmute" : "Mute", l_Name));
    }

    ImGui::SameLine();
    if (ToggleButton("S", group.Solo, ConsolePanel::GetLevelColor(Trinity::LogLevel::Warn), l_Width, "Solo: while any group is soloed, only the soloed groups are heard, with the groups below them"))
    {
        a_Toggle(&Trinity::AudioMixerGroup::Solo, std::format("{} {}", group.Solo ? "Unsolo" : "Solo", l_Name));
    }

    ImGui::SameLine();
    if (ToggleButton("B", group.Bypass, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive), l_Width, "Bypass: passes every effect of the group by but Attenuation"))
    {
        a_Toggle(&Trinity::AudioMixerGroup::Bypass, std::format("{} effects of {}", group.Bypass ? "Stop bypassing" : "Bypass", l_Name));
    }
}

// Each unit of the chain in its order, Attenuation among it, dimmed while bypassed, then Add..., which puts a new effect last, after the fader as Unity's does. An effect is dragged along its strip to move it, Attenuation too
void AudioMixerPanel::DrawChain(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group, bool editable)
{
    const Trinity::AudioEffectList l_Chain = Trinity::AudioMixerData::GetChain(group);
    for (std::size_t it_Place = 0; it_Place < l_Chain.size(); ++it_Place)
    {
        const Trinity::AudioEffect& l_Effect = l_Chain[it_Place];
        const bool l_Attenuation = l_Effect.Type == Trinity::AudioEffectType::Attenuation;
        const Trinity::UUID l_Item = l_Attenuation ? group.ID : l_Effect.ID;
        const bool l_Bypassed = !l_Attenuation && (l_Effect.Bypass || group.Bypass);
        const std::string l_Name(Trinity::GetAudioEffectInfo(l_Effect.Type).Name);
        ImGui::PushID(static_cast<int>(it_Place));
        if (l_Bypassed)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }

        const EditorSession::MixerSelection l_Selection = m_Session.GetMixerSelection();
        if (ImGui::Selectable(std::format("{}###Effect", l_Name).c_str(), l_Selection.Item == l_Item))
        {
            Choose(Chosen::Item, l_Selection.Snapshot, l_Item);
        }

        if (l_Bypassed)
        {
            ImGui::PopStyleColor();
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("%s", DescribeEffect(data, group, l_Effect).c_str());
        }

        if (editable && ImGui::BeginDragDropSource())
        {
            const EffectPayload l_Payload{ group.ID.GetValue(), it_Place };
            ImGui::SetDragDropPayload(c_EffectPayload, &l_Payload, sizeof(l_Payload));
            TextLine(l_Name);
            ImGui::EndDragDropSource();
        }

        // Only an effect of this strip is taken, so one dragged from another strip passes over it
        if (editable && ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* l_Dragged = ImGui::GetDragDropPayload();
            EffectPayload l_From;
            if (l_Dragged != nullptr && l_Dragged->IsDataType(c_EffectPayload) && l_Dragged->DataSize == sizeof(l_From))
            {
                std::memcpy(&l_From, l_Dragged->Data, sizeof(l_From));
                if (l_From.Group == group.ID.GetValue() && ImGui::AcceptDragDropPayload(c_EffectPayload) != nullptr)
                {
                    MoveEffect(data, group.ID, static_cast<std::size_t>(l_From.Place), it_Place);
                }
            }

            ImGui::EndDragDropTarget();
        }

        if (editable && ImGui::BeginPopupContextItem("##EffectMenu"))
        {
            DrawEffectMenu(data, group, l_Chain, it_Place);
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    ImGui::BeginDisabled(!editable);
    if (ImGui::Button("Add...", ImVec2(-FLT_MIN, 0.0f)))
    {
        ImGui::OpenPopup("##AddEffect");
    }

    ImGui::SetItemTooltip("An effect after the last");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("##AddEffect"))
    {
        if (const std::optional<Trinity::AudioEffectType> l_Type = DrawEffectTypes())
        {
            InsertEffect(data, group.ID, l_Chain.size(), *l_Type);
        }

        ImGui::EndPopup();
    }
}

// Attenuation is moved, and has effects added before and after it, but is never bypassed or removed
void AudioMixerPanel::DrawEffectMenu(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group, const Trinity::AudioEffectList& chain, std::size_t place)
{
    const Trinity::AudioEffect& l_Effect = chain[place];
    if (ImGui::BeginMenu("Add Effect Before"))
    {
        if (const std::optional<Trinity::AudioEffectType> l_Type = DrawEffectTypes())
        {
            InsertEffect(data, group.ID, place, *l_Type);
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Add Effect After"))
    {
        if (const std::optional<Trinity::AudioEffectType> l_Type = DrawEffectTypes())
        {
            InsertEffect(data, group.ID, place + 1, *l_Type);
        }

        ImGui::EndMenu();
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Move Up", nullptr, false, place > 0))
    {
        MoveEffect(data, group.ID, place, place - 1);
    }

    if (ImGui::MenuItem("Move Down", nullptr, false, place + 1 < chain.size()))
    {
        MoveEffect(data, group.ID, place, place + 1);
    }

    if (l_Effect.Type == Trinity::AudioEffectType::Attenuation)
    {
        return;
    }

    const std::string_view l_Name = Trinity::GetAudioEffectInfo(l_Effect.Type).Name;
    if (ImGui::MenuItem("Bypass", nullptr, l_Effect.Bypass))
    {
        Trinity::AudioMixerData l_After = data;
        Trinity::AudioEffect& l_Bypassed = *l_After.FindEffect(l_Effect.ID);
        l_Bypassed.Bypass = !l_Bypassed.Bypass;
        Edit(std::move(l_After), std::format("{} {} in {}", l_Effect.Bypass ? "Stop bypassing" : "Bypass", l_Name, std::string_view(group.Name)));
    }

    if (ImGui::MenuItem(std::format("{} Remove", Trinity::Icons::c_Trash).c_str(), "Delete"))
    {
        RemoveEffect(data, l_Effect.ID);
    }
}

// Typed into, and made once the field is left. Escape puts the name back, which leaves nothing to make
void AudioMixerPanel::DrawRenameField(const Trinity::AudioMixerData& data)
{
    m_RenameDrawn = true;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (std::exchange(m_FocusRename, false))
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::InputText("##Rename", &m_RenameText, ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemDeactivated())
    {
        CommitRename(data);
    }
}

// Delete, Ctrl+D and F2 act on what was chosen last while the panel has the focus, nothing is typed into, and the mixer is edited rather than followed
void AudioMixerPanel::ReadShortcuts(const Trinity::AudioMixerData& data, bool editable)
{
    if (!m_Focused || !editable || m_Renaming != Chosen::None || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        return;
    }

    const bool l_Delete = ImGui::IsKeyChordPressed(ImGuiKey_Delete);
    const bool l_Duplicate = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D);
    const bool l_Rename = ImGui::IsKeyChordPressed(ImGuiKey_F2);
    const EditorSession::MixerSelection l_Selection = m_Session.GetMixerSelection();
    if (m_Chosen == Chosen::Snapshot)
    {
        const Trinity::AudioMixerSnapshot* l_Snapshot = data.FindSnapshot(l_Selection.Snapshot);
        if (l_Snapshot == nullptr)
        {
            return;
        }

        if (l_Delete)
        {
            RemoveSnapshot(data, l_Snapshot->ID);
        }
        else if (l_Duplicate)
        {
            AddSnapshot(data, l_Snapshot->ID);
        }
        else if (l_Rename)
        {
            BeginRename(Chosen::Snapshot, l_Snapshot->ID, 0, l_Snapshot->Name);
        }
    }
    else if (m_Chosen == Chosen::Item)
    {
        const Trinity::AudioMixerGroup* l_Group = data.FindGroup(l_Selection.Item);
        if (l_Group != nullptr && l_Group->Parent)
        {
            if (l_Delete)
            {
                RemoveGroup(data, l_Group->ID);
            }
            else if (l_Duplicate)
            {
                DuplicateGroup(data, l_Group->ID);
            }
            else if (l_Rename)
            {
                BeginRename(Chosen::Item, l_Group->ID, 0, l_Group->Name);
            }
        }
        else if (l_Delete && data.FindEffect(l_Selection.Item) != nullptr)
        {
            RemoveEffect(data, l_Selection.Item);
        }
    }
    else if (m_Chosen == Chosen::Exposed && m_ChosenExposed < data.Exposed.size())
    {
        if (l_Delete)
        {
            RemoveExposed(data, m_ChosenExposed);
        }
        else if (l_Rename)
        {
            BeginRename(Chosen::Exposed, {}, m_ChosenExposed, data.Exposed[m_ChosenExposed].Name);
        }
    }
}

void AudioMixerPanel::BeginRename(Chosen chosen, Trinity::UUID id, std::size_t index, std::string_view current)
{
    m_Renaming = chosen;
    m_RenameID = id;
    m_RenameIndex = index;
    m_RenameText = std::string(current);
    m_FocusRename = true;
    m_RenameError.clear();
}

// A name the file would refuse, or one another of its kind has, which a script finds it by, is not made, and why shows under the list
void AudioMixerPanel::CommitRename(const Trinity::AudioMixerData& data)
{
    const Chosen l_Renaming = std::exchange(m_Renaming, Chosen::None);
    const std::string l_Name = Trim(m_RenameText);
    Trinity::AudioMixerData l_After = data;
    const auto a_Rename = [&](auto& items, std::size_t index, std::string_view what)
    {
        if (index >= items.size() || std::string_view(items[index].Name) == l_Name)
        {
            return;
        }

        m_RenameError = CheckName(items, index, l_Name, what);
        m_RenameErrorIn = l_Renaming;
        if (!m_RenameError.empty())
        {
            return;
        }

        const std::string l_What = std::format("Rename {} {} to {}", what, std::string_view(items[index].Name), l_Name);
        items[index].Name = Trinity::TaggedString<Trinity::MemoryTag::Audio>(std::string_view(l_Name));
        Edit(std::move(l_After), l_What);
    };

    if (l_Renaming == Chosen::Snapshot)
    {
        a_Rename(l_After.Snapshots, static_cast<std::size_t>(std::ranges::find(l_After.Snapshots, m_RenameID, &Trinity::AudioMixerSnapshot::ID) - l_After.Snapshots.begin()), "snapshot");
    }
    else if (l_Renaming == Chosen::Item)
    {
        a_Rename(l_After.Groups, static_cast<std::size_t>(std::ranges::find(l_After.Groups, m_RenameID, &Trinity::AudioMixerGroup::ID) - l_After.Groups.begin()), "group");
    }
    else if (l_Renaming == Chosen::Exposed)
    {
        a_Rename(l_After.Exposed, m_RenameIndex, "exposed value");
    }
}

void AudioMixerPanel::AddSnapshot(const Trinity::AudioMixerData& data, Trinity::UUID copyOf)
{
    Trinity::AudioMixerData l_After = data;
    const Trinity::AudioMixerSnapshot* l_From = data.FindSnapshot(copyOf);
    const std::string l_Name = FreeName(l_After.Snapshots, l_From != nullptr ? std::string_view(l_From->Name) : std::string_view("Snapshot"));
    const Trinity::UUID l_ID = l_After.AddSnapshot(l_Name, copyOf).ID;
    EditorSession::MixerSelection l_Select = m_Session.GetMixerSelection();
    l_Select.Snapshot = l_ID;
    Edit(std::move(l_After), std::format("Add snapshot {}", l_Name), {}, l_Select);
    m_Chosen = Chosen::Snapshot;
    BeginRename(Chosen::Snapshot, l_ID, 0, l_Name);
}

void AudioMixerPanel::RemoveSnapshot(const Trinity::AudioMixerData& data, Trinity::UUID snapshot)
{
    Trinity::AudioMixerData l_After = data;
    const Trinity::AudioMixerSnapshot* l_Snapshot = data.FindSnapshot(snapshot);
    if (l_Snapshot == nullptr || !l_After.RemoveSnapshot(snapshot))
    {
        return;
    }

    EditorSession::MixerSelection l_Select = m_Session.GetMixerSelection();
    l_Select.Snapshot = data.StartSnapshot;
    Edit(std::move(l_After), std::format("Remove snapshot {}", std::string_view(l_Snapshot->Name)), {}, l_Select);
}

void AudioMixerPanel::SetStartSnapshot(const Trinity::AudioMixerData& data, Trinity::UUID snapshot)
{
    Trinity::AudioMixerData l_After = data;
    const Trinity::AudioMixerSnapshot* l_Snapshot = data.FindSnapshot(snapshot);
    if (l_Snapshot != nullptr && l_After.SetStartSnapshot(snapshot))
    {
        Edit(std::move(l_After), std::format("Start at snapshot {}", std::string_view(l_Snapshot->Name)));
    }
}

void AudioMixerPanel::AddGroup(const Trinity::AudioMixerData& data, Trinity::UUID parent)
{
    Trinity::AudioMixerData l_After = data;
    const std::string l_Name = FreeName(l_After.Groups, "New Group");
    const Trinity::AudioMixerGroup* l_Group = l_After.AddGroup(parent, l_Name);
    if (l_Group == nullptr)
    {
        return;
    }

    const Trinity::UUID l_ID = l_Group->ID;
    EditorSession::MixerSelection l_Select = m_Session.GetMixerSelection();
    l_Select.Item = l_ID;
    Edit(std::move(l_After), std::format("Add group {}", l_Name), {}, l_Select);
    m_Chosen = Chosen::Item;
    BeginRename(Chosen::Item, l_ID, 0, l_Name);
}

void AudioMixerPanel::DuplicateGroup(const Trinity::AudioMixerData& data, Trinity::UUID group)
{
    Trinity::AudioMixerData l_After = data;
    const Trinity::AudioMixerGroup* l_Original = data.FindGroup(group);
    const std::string l_Name = l_Original != nullptr ? FreeName(l_After.Groups, std::string_view(l_Original->Name)) : std::string();
    const Trinity::AudioMixerGroup* l_Copy = l_Original != nullptr ? l_After.DuplicateGroup(group, l_Name) : nullptr;
    if (l_Copy == nullptr)
    {
        return;
    }

    EditorSession::MixerSelection l_Select = m_Session.GetMixerSelection();
    l_Select.Item = l_Copy->ID;
    Edit(std::move(l_After), std::format("Duplicate group {} as {}", std::string_view(l_Original->Name), l_Name), {}, l_Select);
}

// What was chosen in the branch moves up to the group's parent
void AudioMixerPanel::RemoveGroup(const Trinity::AudioMixerData& data, Trinity::UUID group)
{
    Trinity::AudioMixerData l_After = data;
    const Trinity::AudioMixerGroup* l_Group = data.FindGroup(group);
    if (l_Group == nullptr || !l_After.RemoveGroup(group))
    {
        return;
    }

    EditorSession::MixerSelection l_Select = m_Session.GetMixerSelection();
    l_Select.Item = l_Group->Parent;
    Edit(std::move(l_After), std::format("Remove group {}", std::string_view(l_Group->Name)), {}, l_Select);
}

// A move that would make a Send loop is refused as the edit is made, with why
void AudioMixerPanel::MoveGroup(const Trinity::AudioMixerData& data, Trinity::UUID group, Trinity::UUID parent, Trinity::UUID before, std::string_view way)
{
    Trinity::AudioMixerData l_After = data;
    const Trinity::AudioMixerGroup* l_Group = data.FindGroup(group);
    if (l_Group == nullptr || !l_After.MoveGroup(group, parent, before) || l_After == data)
    {
        return;
    }

    Edit(std::move(l_After), std::format("Move group {} {}", std::string_view(l_Group->Name), way));
}

void AudioMixerPanel::InsertEffect(const Trinity::AudioMixerData& data, Trinity::UUID group, std::size_t place, Trinity::AudioEffectType type)
{
    Trinity::AudioMixerData l_After = data;
    const Trinity::AudioMixerGroup* l_Group = data.FindGroup(group);
    const Trinity::AudioEffect* l_Effect = l_Group != nullptr ? l_After.InsertEffect(group, place, type) : nullptr;
    if (l_Effect == nullptr)
    {
        return;
    }

    EditorSession::MixerSelection l_Select = m_Session.GetMixerSelection();
    l_Select.Item = l_Effect->ID;
    Edit(std::move(l_After), std::format("Add {} to {}", Trinity::GetAudioEffectInfo(type).Name, std::string_view(l_Group->Name)), {}, l_Select);
    m_Chosen = Chosen::Item;
}

void AudioMixerPanel::RemoveEffect(const Trinity::AudioMixerData& data, Trinity::UUID effect)
{
    Trinity::AudioMixerData l_After = data;
    const Trinity::AudioEffect* l_Effect = data.FindEffect(effect);
    const Trinity::AudioMixerGroup* l_Group = FindGroupOf(data, effect);
    if (l_Effect == nullptr || l_Group == nullptr || !l_After.RemoveEffect(effect))
    {
        return;
    }

    EditorSession::MixerSelection l_Select = m_Session.GetMixerSelection();
    l_Select.Item = l_Group->ID;
    Edit(std::move(l_After), std::format("Remove {} from {}", Trinity::GetAudioEffectInfo(l_Effect->Type).Name, std::string_view(l_Group->Name)), {}, l_Select);
}

void AudioMixerPanel::MoveEffect(const Trinity::AudioMixerData& data, Trinity::UUID group, std::size_t from, std::size_t to)
{
    Trinity::AudioMixerData l_After = data;
    const Trinity::AudioMixerGroup* l_Group = data.FindGroup(group);
    if (l_Group == nullptr || from == to || !l_After.MoveEffect(group, from, to))
    {
        return;
    }

    const Trinity::AudioEffectList l_Chain = Trinity::AudioMixerData::GetChain(*l_Group);
    Edit(std::move(l_After), std::format("Move {} in {} {}", Trinity::GetAudioEffectInfo(l_Chain[from].Type).Name, std::string_view(l_Group->Name), to < from ? "up" : "down"));
}

void AudioMixerPanel::RemoveExposed(const Trinity::AudioMixerData& data, std::size_t index)
{
    if (index >= data.Exposed.size())
    {
        return;
    }

    Trinity::AudioMixerData l_After = data;
    l_After.Exposed.erase(l_After.Exposed.begin() + static_cast<std::ptrdiff_t>(index));
    Edit(std::move(l_After), std::format("Remove exposed value {}", std::string_view(data.Exposed[index].Name)));
    m_Chosen = Chosen::None;
}