#include "Panels/AudioMixerInspector.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

namespace
{
    constexpr float c_LabelWidthInFonts = 8.0f;
    // Unity's names for the curves, in the enum's order
    constexpr std::array<std::string_view, 6> c_CurveLabels{ "Linear", "Smoothstep", "Squared", "Square Root", "Brickwall Start", "Brickwall End" };

    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
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

    // A label on the left and the widget filling the rest of the row
    void Label(const char* label)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(ImGui::GetFontSize() * c_LabelWidthInFonts);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // As many decimals as the value's range needs, then its unit, a percent sign as ImGui's format takes it, and a ratio's straight after the number
    std::string GetFormat(const Trinity::AudioEffectParameter& info)
    {
        const float l_Span = info.Maximum - info.Minimum;
        const int l_Decimals = l_Span >= 1000.0f ? 0 : (l_Span >= 50.0f ? 1 : (l_Span >= 5.0f ? 2 : 3));
        if (info.Unit.empty())
        {
            return std::format("%.{}f", l_Decimals);
        }

        return std::format("%.{}f{}{}", l_Decimals, info.Unit.starts_with(':') ? "" : " ", info.Unit == "%" ? std::string_view("%%") : info.Unit);
    }

    // A name scripts can read, the group's name in letters, digits and underscores, then the value's key, as MusicVolume or ReverbRoom, with a number after it from 2 while another value has it
    std::string MakeExposedName(const Trinity::AudioMixerData& data, Trinity::AudioMixerValue value)
    {
        const Trinity::AudioMixerGroup* l_Group = data.FindGroup(value.Owner);
        l_Group = l_Group != nullptr ? l_Group : FindGroupOf(data, value.Owner);
        std::string l_Base;
        for (const char it_Character : l_Group != nullptr ? std::string_view(l_Group->Name) : std::string_view())
        {
            if (std::isalnum(static_cast<unsigned char>(it_Character)) != 0 || it_Character == '_')
            {
                l_Base += it_Character;
            }
        }

        l_Base += data.GetValueInfo(value)->Key;
        if (data.FindExposed(l_Base) == nullptr)
        {
            return l_Base;
        }

        for (std::uint32_t it_Number = 2;; ++it_Number)
        {
            std::string l_Name = std::format("{}{}", l_Base, it_Number);
            if (data.FindExposed(l_Name) == nullptr)
            {
                return l_Name;
            }
        }
    }
}

AudioMixerInspector::AudioMixerInspector(EditorSession& session) : m_Session(session)
{

}

// The group the panel chose, or the group of the effect it chose. Once the mixer has neither, as after an undo, Properties goes back to the selection. Edits are made once every row is drawn
void AudioMixerInspector::Draw()
{
    const EditorSession::MixerSelection l_Selection = m_Session.GetMixerSelection();
    const EditorSession::AudioMixerFile* l_File = l_Selection.Asset ? m_Session.ReadAudioMixer(l_Selection.Asset) : nullptr;
    const Trinity::AudioMixerData* l_Data = l_File != nullptr && l_File->Data ? &*l_File->Data : nullptr;
    const Trinity::AudioMixerGroup* l_Group = l_Data != nullptr ? l_Data->FindGroup(l_Selection.Item) : nullptr;
    l_Group = l_Group != nullptr || l_Data == nullptr ? l_Group : FindGroupOf(*l_Data, l_Selection.Item);
    const Trinity::AssetRecord* l_Record = l_Group != nullptr ? m_Session.GetRegistry()->Find(l_Selection.Asset) : nullptr;
    if (l_Group == nullptr || l_Record == nullptr)
    {
        m_Session.SetInspectingMixer(false);
        m_Shown = {};

        return;
    }

    const Trinity::AudioMixerSnapshot* l_Snapshot = l_Data->FindSnapshot(l_Selection.Snapshot);
    View l_View;
    l_View.Data = l_Data;
    l_View.Snapshot = l_Snapshot != nullptr ? l_Snapshot : &l_Data->GetStartSnapshot();
    l_View.Group = l_Group;
    l_View.Editable = m_Session.IsMixerEditable(l_Selection.Asset);
    l_View.Live = l_View.Editable ? nullptr : m_Session.GetMixer();

    const std::string_view l_Name(l_Group->Name);
    TextLine(std::format("{} {}", Trinity::Icons::c_Sliders, l_Name));
    const std::string_view l_Path(l_Record->Path);
    ImGui::TextDisabled("%s", std::format("Group of {}, at snapshot {}{}", l_Path.substr(l_Path.find_last_of('/') + 1), std::string_view(l_View.Snapshot->Name), l_View.Snapshot->ID == l_Data->StartSnapshot ? ", the start snapshot" : "").c_str());
    if (!l_View.Editable)
    {
        ImGui::TextDisabled("Following the mix the scene plays. Edit in Play Mode in the Audio Mixer panel holds it at the snapshot to edit it");
    }

    ImGui::Spacing();
    const bool l_Newly = l_Selection.Item != m_Shown;
    m_Shown = l_Selection.Item;
    if (ImGui::CollapsingHeader("Group###Group", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawValue(l_View, { l_Group->ID, Trinity::AudioMixerValue::c_Pitch }, l_Name);
    }

    const Trinity::AudioEffectList l_Chain = Trinity::AudioMixerData::GetChain(*l_Group);
    for (std::size_t it_Place = 0; it_Place < l_Chain.size(); ++it_Place)
    {
        const Trinity::AudioEffect& l_Effect = l_Chain[it_Place];
        const bool l_Attenuation = l_Effect.Type == Trinity::AudioEffectType::Attenuation;
        const bool l_Chosen = !l_Attenuation && l_Effect.ID == l_Selection.Item;
        const std::string_view l_Type = Trinity::GetAudioEffectInfo(l_Effect.Type).Name;
        ImGui::PushID(static_cast<int>(it_Place));
        if (l_Newly && l_Chosen)
        {
            ImGui::SetNextItemOpen(true);
        }

        const bool l_Open = ImGui::CollapsingHeader(std::format("{}###Unit", l_Type).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (l_Newly && l_Chosen)
        {
            ImGui::SetScrollHereY(0.0f);
        }

        if (l_Open && l_Attenuation)
        {
            DrawValue(l_View, { l_Group->ID, Trinity::AudioMixerValue::c_Volume }, l_Name);
        }
        else if (l_Open)
        {
            const std::string l_Owner = std::format("{} in {}", l_Type, l_Name);
            Label("Bypass");
            bool l_Bypass = l_Effect.Bypass;
            ImGui::BeginDisabled(!l_View.Editable);
            if (ImGui::Checkbox("##Bypass", &l_Bypass))
            {
                Trinity::AudioMixerData l_After = *l_Data;
                l_After.FindEffect(l_Effect.ID)->Bypass = l_Bypass;
                Edit(std::move(l_After), std::format("{} {}", l_Bypass ? "Bypass" : "Stop bypassing", l_Owner));
            }

            ImGui::EndDisabled();
            if (l_Group->Bypass)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("and its group's Bypass passes every effect by");
            }

            if (l_Effect.Type == Trinity::AudioEffectType::Send)
            {
                DrawSendTarget(l_View, l_Effect);
            }

            for (std::size_t it_Slot = 0; it_Slot < l_Data->GetSlotCount(l_Effect.ID); ++it_Slot)
            {
                DrawValue(l_View, { l_Effect.ID, static_cast<std::uint8_t>(it_Slot) }, l_Owner);
            }
        }

        ImGui::PopID();
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
        static_cast<void>(m_Session.EditAudioMixer(l_Selection.Asset, *l_Data, l_Pending.After, l_Pending.What, std::move(l_Pending.Field)));
    }
}

// The value as the snapshot has it, or as the mix has it while the rows follow it, its label in the accent colour while a name exposes it, and a right-click on either for its menu. Pitch shows in percent, as Unity's does, and a frequency or a pitch slides along a log scale. A drag is one command
void AudioMixerInspector::DrawValue(const View& view, Trinity::AudioMixerValue value, std::string_view owner)
{
    const std::optional<Trinity::AudioEffectParameter> l_Info = view.Data->GetValueInfo(value);
    if (!l_Info)
    {
        return;
    }

    const std::string l_Label(l_Info->Name);
    const auto a_Exposed = std::ranges::find(view.Data->Exposed, value, &Trinity::AudioExposedParameter::Value);
    const bool l_IsExposed = a_Exposed != view.Data->Exposed.end();
    const Trinity::AudioTransitionCurve l_Curve = view.Data->GetCurve(*view.Snapshot, value);
    float l_Value = view.Data->GetValue(*view.Snapshot, value).value_or(l_Info->Default);
    if (view.Live != nullptr)
    {
        l_Value = view.Live->GetValue(value).value_or(l_Value);
    }

    ImGui::PushID(value.Slot);
    ImGui::AlignTextToFramePadding();
    if (l_IsExposed)
    {
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", l_Label.c_str());
    }
    else
    {
        TextLine(l_Label);
    }

    if (view.Editable && ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("##ValueMenu");
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        const std::string l_Exposed = l_IsExposed ? std::format(". Exposed to scripts as {}", std::string_view(a_Exposed->Name)) : std::string();
        const std::string l_Moves = l_Curve != Trinity::AudioTransitionCurve::Linear ? std::format(". A transition to {} moves it along {}", std::string_view(view.Snapshot->Name), c_CurveLabels[static_cast<std::size_t>(l_Curve)]) : std::string();
        ImGui::SetTooltip("%s", std::format("{} of {}{}{}. A right-click exposes it to scripts, or picks the curve a transition to the snapshot moves it along", l_Label, owner, l_Exposed, l_Moves).c_str());
    }

    ImGui::SameLine(ImGui::GetFontSize() * c_LabelWidthInFonts);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool l_Pitch = value.Slot == Trinity::AudioMixerValue::c_Pitch && view.Data->FindGroup(value.Owner) != nullptr;
    const float l_Scale = l_Pitch ? 100.0f : 1.0f;
    const ImGuiSliderFlags l_Flags = ImGuiSliderFlags_AlwaysClamp | (l_Pitch || l_Info->Unit == "Hz" ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
    float l_Shown = l_Value * l_Scale;
    ImGui::BeginDisabled(!view.Editable);
    const bool l_Edited = ImGui::SliderFloat("##Value", &l_Shown, l_Info->Minimum * l_Scale, l_Info->Maximum * l_Scale, l_Pitch ? "%.1f %%" : GetFormat(*l_Info).c_str(), l_Flags);
    ImGui::EndDisabled();
    if (l_Edited)
    {
        Trinity::AudioMixerData l_After = *view.Data;
        l_After.SetValue(view.Snapshot->ID, value, l_Shown / l_Scale);
        Edit(std::move(l_After), std::format("Set {} of {} ({})", l_Label, owner, std::string_view(view.Snapshot->Name)), std::format("Value {} {} {}", value.Owner, value.Slot, view.Snapshot->ID));
    }

    if (view.Editable && ImGui::BeginPopupContextItem("##ValueMenu"))
    {
        DrawValueMenu(view, value, l_Label, owner);
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

// Unity's menu of a mixer value: Expose or Unexpose, then the curve a transition to the snapshot edited moves it along
void AudioMixerInspector::DrawValueMenu(const View& view, Trinity::AudioMixerValue value, std::string_view label, std::string_view owner)
{
    const auto a_Exposed = std::ranges::find(view.Data->Exposed, value, &Trinity::AudioExposedParameter::Value);
    if (a_Exposed != view.Data->Exposed.end())
    {
        if (ImGui::MenuItem(std::format("Unexpose {}", std::string_view(a_Exposed->Name)).c_str()))
        {
            Trinity::AudioMixerData l_After = *view.Data;
            l_After.Exposed.erase(l_After.Exposed.begin() + (a_Exposed - view.Data->Exposed.begin()));
            Edit(std::move(l_After), std::format("Unexpose {} of {} as {}", label, owner, std::string_view(a_Exposed->Name)));
        }
    }
    else if (ImGui::MenuItem(std::format("Expose {} to Scripts", label).c_str()))
    {
        Trinity::AudioMixerData l_After = *view.Data;
        const std::string l_Name = MakeExposedName(*view.Data, value);
        l_After.Exposed.push_back({ Trinity::TaggedString<Trinity::MemoryTag::Audio>(std::string_view(l_Name)), value });
        Edit(std::move(l_After), std::format("Expose {} of {} as {}", label, owner, l_Name));
    }

    ImGui::Separator();
    const std::string_view l_Snapshot(view.Snapshot->Name);
    ImGui::TextDisabled("%s", std::format("Transition to {} along", l_Snapshot).c_str());
    const Trinity::AudioTransitionCurve l_Current = view.Data->GetCurve(*view.Snapshot, value);
    for (std::size_t it_Curve = 0; it_Curve < c_CurveLabels.size(); ++it_Curve)
    {
        const Trinity::AudioTransitionCurve l_Curve = static_cast<Trinity::AudioTransitionCurve>(it_Curve);
        if (ImGui::MenuItem(std::string(c_CurveLabels[it_Curve]).c_str(), nullptr, l_Curve == l_Current) && l_Curve != l_Current)
        {
            Trinity::AudioMixerData l_After = *view.Data;
            l_After.SetCurve(view.Snapshot->ID, value, l_Curve);
            Edit(std::move(l_After), std::format("Move {} of {} to {} along {}", label, owner, l_Snapshot, c_CurveLabels[it_Curve]));
        }
    }
}

// None, then every Receive and Duck Volume of the mixer, by its type and its group. One a Send to which would come back to it is shown, and cannot be picked
void AudioMixerInspector::DrawSendTarget(const View& view, const Trinity::AudioEffect& send)
{
    const auto a_Name = [](const Trinity::AudioEffect& effect, const Trinity::AudioMixerGroup& group) { return std::format("{} in {}", Trinity::GetAudioEffectInfo(effect.Type).Name, std::string_view(group.Name)); };
    const Trinity::AudioEffect* l_Target = send.Target ? view.Data->FindEffect(send.Target) : nullptr;
    const Trinity::AudioMixerGroup* l_To = l_Target != nullptr ? FindGroupOf(*view.Data, send.Target) : nullptr;
    const std::string_view l_From(view.Group->Name);
    Label("Send To");
    // Around the list too, which is a window of its own that ends inside it
    ImGui::BeginDisabled(!view.Editable);
    if (!ImGui::BeginCombo("##Target", l_To != nullptr ? a_Name(*l_Target, *l_To).c_str() : "None"))
    {
        ImGui::EndDisabled();

        return;
    }

    if (ImGui::Selectable("None", l_To == nullptr) && l_To != nullptr)
    {
        Trinity::AudioMixerData l_After = *view.Data;
        l_After.FindEffect(send.ID)->Target = {};
        Edit(std::move(l_After), std::format("Send {} nowhere", l_From));
    }

    for (const Trinity::AudioMixerGroup& it_Group : view.Data->Groups)
    {
        for (const Trinity::AudioEffect& it_Effect : it_Group.Effects)
        {
            if (it_Effect.Type != Trinity::AudioEffectType::Receive && it_Effect.Type != Trinity::AudioEffectType::DuckVolume)
            {
                continue;
            }

            Trinity::AudioMixerData l_After = *view.Data;
            l_After.FindEffect(send.ID)->Target = it_Effect.ID;
            const std::string l_Loop = Trinity::ValidateAudioMixer(l_After);
            const bool l_Selected = it_Effect.ID == send.Target;
            ImGui::PushID(it_Effect.ID.ToString().c_str());
            ImGui::BeginDisabled(!l_Loop.empty());
            if (ImGui::Selectable(a_Name(it_Effect, it_Group).c_str(), l_Selected) && !l_Selected)
            {
                Edit(std::move(l_After), std::format("Send {} to the {}", l_From, a_Name(it_Effect, it_Group)));
            }

            ImGui::EndDisabled();
            if (!l_Loop.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("%s", std::format("Not this one: {}", l_Loop).c_str());
            }

            ImGui::PopID();
        }
    }

    ImGui::EndCombo();
    ImGui::EndDisabled();
}

void AudioMixerInspector::Edit(Trinity::AudioMixerData after, std::string what, std::string field)
{
    m_Pending = Pending{ std::move(after), std::move(what), std::move(field) };
}