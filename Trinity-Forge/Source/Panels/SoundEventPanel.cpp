#include "Panels/SoundEventPanel.hpp"

#include "EditorPayloads.hpp"
#include "Panels/ConsolePanel.hpp"

#include <ImCurveEdit.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    // A container dragged in the tree, by its ID
    constexpr const char* c_ContainerPayload = "TRINITY_SOUND_CONTAINER";
    constexpr float c_LabelWidthInFonts = 8.0f;
    constexpr float c_SliderWidthInFonts = 9.0f;
    constexpr float c_CurveHeightInFonts = 12.0f;
    // A stop fades over this many seconds, so it never clicks
    constexpr float c_StopFade = 0.05f;
    // ImCurveEdit counts its x in frames and shows one frame past its last, so the parameter's range is spread over this many, which that one more barely moves
    constexpr float c_CurveFrames = 1000.0f;
    // How far below and above the curves' ranges the editor shows at first
    constexpr float c_CurveMargin = 0.05f;
    // The most points a curve takes in the editor. ImCurveEdit names chosen points by their places whatever curves it is given next, so each curve it reads has room for this many, and a place it names past a curve's points reads its last
    constexpr std::size_t c_MostPoints = 256;

    struct TypeInfo
    {
        Trinity::SoundContainerType Type;
        const char* Name;
        const char* Icon;
        const char* Tooltip;
    };

    // In the enum's order
    constexpr std::array<TypeInfo, 5> c_Types
    { {
        { Trinity::SoundContainerType::Clip, "Clip", Trinity::Icons::c_FileAudio, "Plays its clip, once or looping" },
        { Trinity::SoundContainerType::Random, "Random", Trinity::Icons::c_Random, "Plays one of its children each time it is triggered, picked at random by their weights, as Unity's Audio Random Container does, and can trigger itself" },
        { Trinity::SoundContainerType::Sequence, "Sequence", Trinity::Icons::c_ListOrdered, "Plays its children in turn, each from the sample the one before it ends on" },
        { Trinity::SoundContainerType::Layer, "Layer", Trinity::Icons::c_Clone, "Plays all its children at once, from the same sample" },
        { Trinity::SoundContainerType::Switch, "Switch", Trinity::Icons::c_Sitemap, "Plays the children whose case is the label its parameter has as it is triggered, or else those with none" }
    } };

    const TypeInfo& GetTypeInfo(Trinity::SoundContainerType type)
    {
        return c_Types[std::min(static_cast<std::size_t>(type), c_Types.size() - 1)];
    }

    // What a curve moves, over the range the editor shows it on, its value where it changes nothing, how its values read, and its colour on a dark and on a light background
    struct TargetInfo
    {
        Trinity::SoundCurveTarget Target;
        const char* Name;
        float Minimum;
        float Maximum;
        float Rest;
        const char* Format;
        ImVec4 Dark;
        ImVec4 Light;
    };

    // In the enum's order, which is the order the editor counts its curves in
    constexpr std::array<TargetInfo, 3> c_Targets
    { {
        { Trinity::SoundCurveTarget::Volume, "Volume", Trinity::SoundEventData::c_MinimumVolume, Trinity::SoundEventData::c_MaximumVolume, 0.0f, "%.1f dB", ImVec4(0.40f, 0.85f, 0.45f, 1.0f), ImVec4(0.10f, 0.55f, 0.15f, 1.0f) },
        { Trinity::SoundCurveTarget::Pitch, "Pitch", Trinity::SoundEventData::c_MinimumPitch, Trinity::SoundEventData::c_MaximumPitch, 1.0f, "\xC3\x97%.2f", ImVec4(0.40f, 0.70f, 1.00f, 1.0f), ImVec4(0.10f, 0.35f, 0.80f, 1.0f) },
        { Trinity::SoundCurveTarget::Cutoff, "Cutoff", Trinity::SoundEventData::c_MinimumCutoff, Trinity::SoundEventData::c_MaximumCutoff, Trinity::SoundEventData::c_MaximumCutoff, "%.0f Hz", ImVec4(1.00f, 0.65f, 0.30f, 1.0f), ImVec4(0.75f, 0.40f, 0.05f, 1.0f) }
    } };

    const TargetInfo& GetTargetInfo(Trinity::SoundCurveTarget target)
    {
        return c_Targets[std::min(static_cast<std::size_t>(target), c_Targets.size() - 1)];
    }

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

    // A file's name without its folders and its extension, as a clip dropped in names its container
    std::string GetStem(std::string_view path)
    {
        const std::size_t l_Slash = path.find_last_of('/');
        const std::string_view l_Name = l_Slash == std::string_view::npos ? path : path.substr(l_Slash + 1);

        return std::string(l_Name.substr(0, l_Name.find_last_of('.')));
    }

    // Whether the windows are drawn light, by how bright their background is, as the Console tells its colours
    bool IsLightTheme()
    {
        const ImVec4 l_Background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);

        return l_Background.x * 0.2126f + l_Background.y * 0.7152f + l_Background.z * 0.0722f > 0.5f;
    }

    ImVec4 GetTargetColor(const TargetInfo& target)
    {
        return IsLightTheme() ? target.Light : target.Dark;
    }

    std::string FormatTarget(const TargetInfo& target, float value)
    {
        std::array<char, 64> l_Text{};
        std::snprintf(l_Text.data(), l_Text.size(), target.Format, static_cast<double>(value));

        return std::string(l_Text.data());
    }

    // As ImGui's formats read it, with any % in it shown as it is
    std::string EscapeFormat(std::string_view text)
    {
        std::string l_Escaped;
        for (const char it_Character : text)
        {
            l_Escaped += it_Character == '%' ? std::string_view("%%") : std::string_view(&it_Character, 1);
        }

        return l_Escaped;
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

    // A label new among the parameter's labels
    std::string FreeLabel(const Trinity::SoundParameter& parameter)
    {
        for (std::size_t it_Number = parameter.Labels.size() + 1;; ++it_Number)
        {
            std::string l_Label = std::format("Label {}", it_Number);
            if (std::ranges::find(parameter.Labels, std::string_view(l_Label)) == parameter.Labels.end())
            {
                return l_Label;
            }
        }
    }

    // The containers right below one, in their order
    std::vector<const Trinity::SoundContainer*> GetChildren(const Trinity::SoundEventData& data, Trinity::UUID parent)
    {
        std::vector<const Trinity::SoundContainer*> l_Children;
        for (const Trinity::SoundContainer& it_Container : data.Containers)
        {
            if (it_Container.Parent == parent && it_Container.Parent)
            {
                l_Children.push_back(&it_Container);
            }
        }

        return l_Children;
    }

    // The targets a container has a curve of by the parameter, in the order the editor counts its curves
    std::vector<Trinity::SoundCurveTarget> GetShownTargets(const Trinity::SoundContainer& container, Trinity::UUID parameter)
    {
        std::vector<Trinity::SoundCurveTarget> l_Targets;
        for (const TargetInfo& it_Target : c_Targets)
        {
            if (std::ranges::any_of(container.Curves, [&](const Trinity::SoundCurve& curve) { return curve.Parameter == parameter && curve.Target == it_Target.Target; }))
            {
                l_Targets.push_back(it_Target.Target);
            }
        }

        return l_Targets;
    }

    bool IsAudioClip(const Trinity::AssetRecord* record)
    {
        return record != nullptr && record->Importer == Trinity::AudioClipAsset::c_AssetType;
    }

    // The label at a discrete value
    std::string_view GetLabel(const Trinity::SoundParameter& parameter, float value)
    {
        if (parameter.Labels.empty())
        {
            return {};
        }

        const std::size_t l_Index = static_cast<std::size_t>(std::clamp(std::lround(value), 0L, static_cast<long>(parameter.Labels.size() - 1)));

        return std::string_view(parameter.Labels[l_Index]);
    }

    // A parameter's value as the panel shows it: a discrete one's as its label
    std::string FormatParameter(const Trinity::SoundParameter& parameter, float value)
    {
        if (parameter.Kind == Trinity::SoundParameterKind::Discrete)
        {
            return std::string(GetLabel(parameter, value));
        }

        return std::format("{:.2f}", value);
    }

    bool BeginFields(const char* id)
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
        {
            return false;
        }

        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * c_LabelWidthInFonts);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        return true;
    }

    // A row's label, with its tooltip, and the field after it as wide as its column
    void Row(const char* label, const char* tooltip = nullptr)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (tooltip != nullptr)
        {
            ImGui::SetItemTooltip("%s", tooltip);
        }

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // Every type, those a container with children cannot become disabled. The type picked, if any
    std::optional<Trinity::SoundContainerType> DrawTypeItems(bool hasChildren, std::optional<Trinity::SoundContainerType> current = {})
    {
        std::optional<Trinity::SoundContainerType> l_Picked;
        for (const TypeInfo& it_Type : c_Types)
        {
            const bool l_Allowed = it_Type.Type != Trinity::SoundContainerType::Clip || !hasChildren;
            if (ImGui::MenuItem(std::format("{} {}", it_Type.Icon, it_Type.Name).c_str(), nullptr, current == it_Type.Type, l_Allowed))
            {
                l_Picked = it_Type.Type;
            }

            ImGui::SetItemTooltip("%s", l_Allowed ? it_Type.Tooltip : "A container with children cannot be a Clip, which holds none");
        }

        return l_Picked;
    }

    // The curves of one container by one parameter as ImCurveEdit draws and edits them: a point's x along the parameter's range, spread over c_CurveFrames, and its y over its own curve's range from 0 to 1, so Volume, Pitch and Cutoff share one view and the straight lines drawn between points are the ones heard. Points are moved and added on a copy of the container's curves, which the panel makes an edit of once the editor has drawn. A point moves only as far as its neighbours, so each keeps its place, and on a discrete parameter only to a label's. A place ImCurveEdit names past a curve's points is read as its last point and never edited
    class CurveView final : public ImCurveEdit::Delegate
    {
    public:
        CurveView(const Trinity::SoundContainer& container, const Trinity::SoundParameter& parameter, float bottom, float top, bool& editing) : m_Curves(container.Curves), m_Parameter(parameter), m_Min(0.0f, bottom), m_Max(c_CurveFrames - 1.0f, top), m_Spare(c_MostPoints, ImVec2(0.0f, 0.0f)), m_Editing(editing)
        {
            for (const Trinity::SoundCurveTarget it_Target : GetShownTargets(container, parameter.ID))
            {
                const auto a_Found = std::ranges::find_if(m_Curves, [&](const Trinity::SoundCurve& curve) { return curve.Parameter == parameter.ID && curve.Target == it_Target; });
                m_Shown.push_back(static_cast<std::size_t>(a_Found - m_Curves.begin()));
            }

            m_Points.resize(m_Shown.size());
            for (std::size_t it_Curve = 0; it_Curve < m_Shown.size(); ++it_Curve)
            {
                Fill(it_Curve);
            }
        }

        size_t GetCurveCount() override { return m_Shown.size(); }
        ImVec2& GetMin() override { return m_Min; }
        ImVec2& GetMax() override { return m_Max; }

        size_t GetPointCount(size_t curve) override
        {
            return curve < m_Shown.size() ? Curve(curve).Points.size() : 0;
        }

        uint32_t GetCurveColor(size_t curve) override
        {
            return curve < m_Shown.size() ? ImGui::ColorConvertFloat4ToU32(GetTargetColor(GetTarget(curve))) : 0;
        }

        ImVec2* GetPoints(size_t curve) override
        {
            return curve < m_Points.size() ? m_Points[curve].data() : m_Spare.data();
        }

        unsigned int GetBackgroundColor() override
        {
            return ImGui::GetColorU32(ImGuiCol_FrameBg);
        }

        void BeginEdit(int) override
        {
            m_Editing = true;
        }

        void EndEdit() override
        {
            m_Editing = false;
        }

        // A point given back where it already is moves nowhere, so a click that starts a drag changes nothing
        int EditPoint(size_t curve, int point, ImVec2 value) override
        {
            if (curve >= m_Shown.size() || point < 0 || static_cast<std::size_t>(point) >= Curve(curve).Points.size())
            {
                return point;
            }

            const ImVec2 l_Shown = m_Points[curve][static_cast<std::size_t>(point)];
            if (std::abs(value.x - l_Shown.x) < 1.0e-3f && std::abs(value.y - l_Shown.y) < 1.0e-5f)
            {
                return point;
            }

            auto& l_Points = Curve(curve).Points;
            const std::size_t l_Index = static_cast<std::size_t>(point);
            const float l_Lowest = l_Index > 0 ? Above(l_Points[l_Index - 1].x) : m_Parameter.GetMinimum();
            const float l_Highest = l_Index + 1 < l_Points.size() ? Below(l_Points[l_Index + 1].x) : m_Parameter.GetMaximum();
            glm::vec2 l_Moved(Snap(ToParameter(value.x)), FromNormal(GetTarget(curve), value.y));
            l_Moved.x = l_Lowest <= l_Highest ? std::clamp(l_Moved.x, l_Lowest, l_Highest) : l_Points[l_Index].x;
            if (l_Moved != l_Points[l_Index])
            {
                l_Points[l_Index] = l_Moved;
                m_Changed = true;
                Fill(curve);
            }

            return point;
        }

        // Where no point already is, on a discrete parameter at a label no point has
        void AddPoint(size_t curve, ImVec2 value) override
        {
            if (curve >= m_Shown.size() || Curve(curve).Points.size() >= c_MostPoints)
            {
                return;
            }

            auto& l_Points = Curve(curve).Points;
            const glm::vec2 l_Point(Snap(ToParameter(value.x)), FromNormal(GetTarget(curve), value.y));
            const auto a_After = std::ranges::upper_bound(l_Points, l_Point.x, {}, [](const glm::vec2& point) { return point.x; });
            if ((a_After != l_Points.begin() && (a_After - 1)->x >= l_Point.x) || (a_After != l_Points.end() && a_After->x <= l_Point.x))
            {
                return;
            }

            l_Points.insert(a_After, l_Point);
            m_Changed = true;
            Fill(curve);
        }

        [[nodiscard]] bool IsChanged() const { return m_Changed; }
        [[nodiscard]] const decltype(Trinity::SoundContainer::Curves)& GetCurves() const { return m_Curves; }
        [[nodiscard]] const TargetInfo& GetTarget(std::size_t curve) const { return GetTargetInfo(m_Curves[m_Shown[curve]].Target); }

        // Along the parameter's range, from the editor's frames, and back
        [[nodiscard]] float ToParameter(float frame) const
        {
            const float l_Lowest = m_Parameter.GetMinimum();

            return l_Lowest + std::clamp(frame / c_CurveFrames, 0.0f, 1.0f) * (m_Parameter.GetMaximum() - l_Lowest);
        }

        [[nodiscard]] float ToFrame(float value) const
        {
            const float l_Lowest = m_Parameter.GetMinimum();
            const float l_Span = m_Parameter.GetMaximum() - l_Lowest;

            return l_Span > 0.0f ? (value - l_Lowest) / l_Span * c_CurveFrames : 0.0f;
        }

        [[nodiscard]] static float ToNormal(const TargetInfo& target, float value)
        {
            return (value - target.Minimum) / (target.Maximum - target.Minimum);
        }

        [[nodiscard]] static float FromNormal(const TargetInfo& target, float normal)
        {
            return target.Minimum + std::clamp(normal, 0.0f, 1.0f) * (target.Maximum - target.Minimum);
        }

    private:
        Trinity::SoundCurve& Curve(std::size_t curve) { return m_Curves[m_Shown[curve]]; }

        [[nodiscard]] bool IsDiscrete() const { return m_Parameter.Kind == Trinity::SoundParameterKind::Discrete; }
        [[nodiscard]] float Snap(float value) const { return IsDiscrete() ? std::round(value) : value; }
        [[nodiscard]] float Above(float value) const { return IsDiscrete() ? value + 1.0f : std::nextafter(value, std::numeric_limits<float>::max()); }
        [[nodiscard]] float Below(float value) const { return IsDiscrete() ? value - 1.0f : std::nextafter(value, std::numeric_limits<float>::lowest()); }

        // Room past the points for every place ImCurveEdit may still name, each reading the last point
        void Fill(std::size_t curve)
        {
            const auto& l_Points = Curve(curve).Points;
            std::vector<ImVec2>& l_Shown = m_Points[curve];
            l_Shown.assign(std::max(l_Points.size(), c_MostPoints), ImVec2(0.0f, 0.0f));
            for (std::size_t it_Point = 0; it_Point < l_Shown.size(); ++it_Point)
            {
                const glm::vec2& l_Point = l_Points.empty() ? glm::vec2(0.0f) : l_Points[std::min(it_Point, l_Points.size() - 1)];
                l_Shown[it_Point] = ImVec2(ToFrame(l_Point.x), ToNormal(GetTarget(curve), l_Point.y));
            }
        }

        decltype(Trinity::SoundContainer::Curves) m_Curves;
        const Trinity::SoundParameter& m_Parameter;
        std::vector<std::size_t> m_Shown;
        std::vector<std::vector<ImVec2>> m_Points;
        ImVec2 m_Min;
        ImVec2 m_Max;
        std::vector<ImVec2> m_Spare;
        bool& m_Editing;
        bool m_Changed = false;
    };
}

SoundEventPanel::SoundEventPanel(EditorSession& session) : Panel("Sound Event", Trinity::Icons::c_Music, DockSlot::Bottom), m_Session(session)
{

}

void SoundEventPanel::SetAsset(Trinity::UUID asset)
{
    m_Asset = asset;
}

// The asset opened, while the project has it as a sound event
Trinity::UUID SoundEventPanel::GetShownAsset() const
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = l_Registry != nullptr && m_Asset ? l_Registry->Find(m_Asset) : nullptr;

    return l_Record != nullptr && l_Record->Importer == Trinity::SoundEventAsset::c_AssetType ? m_Asset : Trinity::UUID();
}

// Edits are made once the panel has drawn, so the data it drew from stays as it was until then. A selection follows the edit only once it has been made. The audition forgets the values it was given as another event is shown
void SoundEventPanel::OnImGuiRender()
{
    m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const Trinity::UUID l_Asset = m_Session.HasProject() ? GetShownAsset() : Trinity::UUID();
    if (l_Asset != m_Auditioned)
    {
        if (m_Auditioned)
        {
            m_Session.GetAudition().Stop();
            m_Session.GetAudition().ClearParameters();
        }

        m_Auditioned = l_Asset;
    }

    const EditorSession::SoundEventFile* l_File = l_Asset ? m_Session.ReadSoundEvent(l_Asset) : nullptr;
    const Trinity::AssetRecord* l_Record = l_File != nullptr ? m_Session.GetRegistry()->Find(l_Asset) : nullptr;
    if (l_File == nullptr || l_Record == nullptr)
    {
        m_TextKey.clear();
        ImGui::TextDisabled("%s", m_Session.HasProject() ? "Double-click a sound event in the Content Browser to edit it here. Create > Sound Event there makes one" : "Open or create a project to edit its sound events");

        return;
    }

    TextLine(std::format("{} {}", Trinity::Icons::c_Music, l_Record->Path));
    if (!l_File->Data)
    {
        m_TextKey.clear();
        ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Error));
        ImGui::TextWrapped("%s", l_File->Error.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Once the file is fixed, it is read again as Forge gets the focus back, or at F5");

        return;
    }

    const Trinity::SoundEventData& l_Data = *l_File->Data;
    Clamp(l_Asset, l_Data);
    ReadShortcuts(l_Data);
    DrawAudition(l_Asset, l_Data);
    ImGui::Separator();

    if (ImGui::BeginTable("##SoundEvent", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp, ImGui::GetContentRegionAvail()))
    {
        ImGui::TableSetupColumn("Tree", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##Tree", ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.6f), ImGuiChildFlags_ResizeY))
        {
            DrawTree(l_Data);
        }

        ImGui::EndChild();
        if (ImGui::BeginChild("##Parameters"))
        {
            DrawParameters(l_Data);
        }

        ImGui::EndChild();

        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##Details"))
        {
            const EditorSession::SoundEventSelection& l_Selection = m_Session.GetSoundEventSelection();
            const Trinity::SoundParameter* l_Parameter = m_Chosen == Chosen::Parameter ? l_Data.FindParameter(l_Selection.Parameter) : nullptr;
            if (l_Parameter != nullptr)
            {
                m_CurveFocused = false;
                DrawParameterDetails(l_Data, *l_Parameter);
            }
            else
            {
                const Trinity::SoundContainer* l_Container = l_Data.FindContainer(l_Selection.Container);
                DrawContainerDetails(l_Asset, l_Data, l_Container != nullptr ? *l_Container : l_Data.GetRoot());
            }
        }

        ImGui::EndChild();
        ImGui::EndTable();
    }

    // Nothing is being dragged or typed into, nor a curve's points dragged, so the next change is a command of its own
    if (!ImGui::IsAnyItemActive() && !m_CurveEditing)
    {
        m_Session.GetHistory().EndMerge();
    }

    if (m_Pending)
    {
        Pending l_Pending = std::move(*m_Pending);
        m_Pending.reset();
        if (m_Session.EditSoundEvent(l_Asset, l_Data, l_Pending.After, l_Pending.What, std::move(l_Pending.Field)))
        {
            if (l_Pending.Select)
            {
                m_Session.SetSoundEventSelection(*l_Pending.Select);
            }

            if (l_Pending.Choose)
            {
                m_Chosen = *l_Pending.Choose;
            }
        }
    }
}

// The selection stays inside the event shown as an undo or a reload takes things away, and starts at its root and its first parameter when another event is shown. Curves are drawn by a parameter while it has any
void SoundEventPanel::Clamp(Trinity::UUID asset, const Trinity::SoundEventData& data)
{
    EditorSession::SoundEventSelection l_Selection = m_Session.GetSoundEventSelection();
    if (l_Selection.Asset != asset)
    {
        l_Selection = { asset, data.GetRoot().ID,{} };
        m_Chosen = Chosen::Container;
        m_TextKey.clear();
        m_Error.clear();
    }

    if (data.FindContainer(l_Selection.Container) == nullptr)
    {
        l_Selection.Container = data.GetRoot().ID;
    }

    if (data.FindParameter(l_Selection.Parameter) == nullptr)
    {
        l_Selection.Parameter = data.Parameters.empty() ? Trinity::UUID() : data.Parameters.front().ID;
    }

    if (m_Chosen == Chosen::Parameter && !l_Selection.Parameter)
    {
        m_Chosen = Chosen::Container;
    }

    if (l_Selection != m_Session.GetSoundEventSelection())
    {
        m_Session.SetSoundEventSelection(l_Selection);
    }
}

void SoundEventPanel::Edit(Trinity::SoundEventData after, std::string what, std::string field, std::optional<EditorSession::SoundEventSelection> select, std::optional<Chosen> choose)
{
    m_Pending = Pending{ std::move(after), std::move(what), std::move(field), select, choose };
}

// A parameter chosen is also the one the curves are drawn by
void SoundEventPanel::Choose(Chosen chosen, Trinity::UUID id)
{
    EditorSession::SoundEventSelection l_Selection = m_Session.GetSoundEventSelection();
    (chosen == Chosen::Container ? l_Selection.Container : l_Selection.Parameter) = id;
    m_Session.SetSoundEventSelection(l_Selection);
    m_Chosen = chosen;
    m_Error.clear();
}

std::optional<std::string> SoundEventPanel::TextField(const char* label, const std::string& key, std::string_view value)
{
    std::string l_Shown(value);
    std::string& l_Text = m_TextKey == key ? m_Text : l_Shown;
    if (m_FocusKey == key)
    {
        ImGui::SetKeyboardFocusHere();
        m_FocusKey.clear();
    }

    ImGui::InputText(label, &l_Text, ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemActivated() && m_TextKey != key)
    {
        m_Text = l_Text;
        m_TextKey = key;
    }

    if (!ImGui::IsItemDeactivated())
    {
        return std::nullopt;
    }

    const std::string l_Typed = m_TextKey == key ? m_Text : l_Text;
    if (m_TextKey == key)
    {
        m_TextKey.clear();
    }

    return ImGui::IsItemDeactivatedAfterEdit() ? std::optional(Trim(l_Typed)) : std::nullopt;
}

// Play starts the event from its root, as a game would, and a slider for each parameter gives the instance its value, a global one the engine's, which every instance by its name hears. A parameter that seeks shows where it has got to as a tick on its slider
void SoundEventPanel::DrawAudition(Trinity::UUID asset, const Trinity::SoundEventData& data)
{
    SoundEventAudition& l_Audition = m_Session.GetAudition();
    const bool l_Playing = l_Audition.IsPlaying(asset);
    if (ImGui::Button(std::format("{} Play", Trinity::Icons::c_Play).c_str()))
    {
        l_Audition.Play(asset);
    }

    ImGui::SetItemTooltip("Plays the event from its root, as a game would, a step in front of a listener of its own and through the project's mixer, with the values the sliders give. Pressed again, it starts over");
    ImGui::SameLine();
    ImGui::BeginDisabled(!l_Playing);
    if (ImGui::Button(std::format("{} Stop", Trinity::Icons::c_Stop).c_str()))
    {
        l_Audition.Stop(c_StopFade);
    }

    ImGui::EndDisabled();
    const float l_Spacing = ImGui::GetStyle().ItemSpacing.x;
    const float l_SliderWidth = ImGui::GetFontSize() * c_SliderWidthInFonts;
    for (const Trinity::SoundParameter& it_Parameter : data.Parameters)
    {
        const std::string l_Name(std::string_view(it_Parameter.Name));
        const std::string l_Label = it_Parameter.Global ? std::format("{} {}", Trinity::Icons::c_Globe, l_Name) : l_Name;
        ImGui::SameLine(0.0f, l_Spacing * 3.0f);
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(l_Label.c_str()).x + l_Spacing + l_SliderWidth)
        {
            ImGui::NewLine();
        }

        ImGui::PushID(it_Parameter.ID.ToString().c_str());
        ImGui::AlignTextToFramePadding();
        TextLine(l_Label);
        ImGui::SetItemTooltip("%s", it_Parameter.Global ? "Global: the engine's value, which every instance by this name hears, a scene's too, until a script gives another" : "Given to the instance playing, and to each one Play starts until another event is shown");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(l_SliderWidth);
        const std::optional<float> l_Given = it_Parameter.Global ? l_Audition.GetGlobalParameter(l_Name) : l_Audition.GetParameter(l_Name);
        float l_Value = it_Parameter.Clamp(l_Given.value_or(it_Parameter.Default));
        if (it_Parameter.Kind == Trinity::SoundParameterKind::Discrete)
        {
            const long l_Index = std::lround(l_Value);
            if (ImGui::BeginCombo("##Value", std::string(GetLabel(it_Parameter, l_Value)).c_str()))
            {
                for (std::size_t it_Label = 0; it_Label < it_Parameter.Labels.size(); ++it_Label)
                {
                    ImGui::PushID(static_cast<int>(it_Label));
                    if (ImGui::Selectable(std::string(std::string_view(it_Parameter.Labels[it_Label])).c_str(), static_cast<long>(it_Label) == l_Index))
                    {
                        l_Audition.SetParameter(l_Name, static_cast<float>(it_Label), it_Parameter.Global);
                    }

                    ImGui::PopID();
                }

                ImGui::EndCombo();
            }
        }
        else
        {
            if (ImGui::SliderFloat("##Value", &l_Value, it_Parameter.Minimum, it_Parameter.Maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp))
            {
                l_Audition.SetParameter(l_Name, l_Value, it_Parameter.Global);
            }

            ImGui::SetItemTooltip("Ctrl+click to type a value");
            const std::optional<float> l_Heard = l_Playing && it_Parameter.SeekSpeed > 0.0f ? l_Audition.GetHeardParameter(l_Name) : std::nullopt;
            if (l_Heard)
            {
                const ImVec2 l_Min = ImGui::GetItemRectMin();
                const ImVec2 l_Max = ImGui::GetItemRectMax();
                const float l_Share = std::clamp((*l_Heard - it_Parameter.Minimum) / (it_Parameter.Maximum - it_Parameter.Minimum), 0.0f, 1.0f);
                const float l_X = l_Min.x + (l_Max.x - l_Min.x) * l_Share;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(l_X, l_Min.y), ImVec2(l_X, l_Max.y), ImGui::GetColorU32(ImGuiCol_PlotHistogram), 2.0f);
            }
        }

        ImGui::PopID();
    }
}

// The root, and the room below the tree, which takes what is dropped there for the root
void SoundEventPanel::DrawTree(const Trinity::SoundEventData& data)
{
    ImGui::PushID("Containers");
    const Trinity::SoundContainer* l_Chosen = data.FindContainer(m_Session.GetSoundEventSelection().Container);
    if (ColumnHeader("Containers", "Add a container below the chosen one, or beside it when that is a Clip"))
    {
        ImGui::OpenPopup("##AddContainer");
    }

    if (ImGui::BeginPopup("##AddContainer"))
    {
        const Trinity::UUID l_Parent = l_Chosen == nullptr ? data.GetRoot().ID : (l_Chosen->Type != Trinity::SoundContainerType::Clip ? l_Chosen->ID : l_Chosen->Parent);
        if (const std::optional<Trinity::SoundContainerType> l_Type = DrawTypeItems(false))
        {
            AddContainer(data, l_Parent ? l_Parent : data.GetRoot().ID, *l_Type);
        }

        ImGui::EndPopup();
    }

    DrawContainerNode(data, data.GetRoot());
    const ImVec2 l_Room(std::max(ImGui::GetContentRegionAvail().x, 1.0f), std::max(ImGui::GetContentRegionAvail().y, ImGui::GetFrameHeight()));
    ImGui::InvisibleButton("##Below", l_Room);
    ImGui::SetItemTooltip("Drop clips from the Content Browser here to add them to the root, or a container to move it there");
    AcceptDrop(data, data.GetRoot(), true);
    ImGui::PopID();
}

// The root is renamed and given another type, but is never dragged, duplicated or removed. A container dragged onto another goes below it, or before it when it is a Clip, and a clip dropped on one is added below it, or beside it when it is a Clip
void SoundEventPanel::DrawContainerNode(const Trinity::SoundEventData& data, const Trinity::SoundContainer& container)
{
    const std::vector<const Trinity::SoundContainer*> l_Children = GetChildren(data, container.ID);
    const TypeInfo& l_Type = GetTypeInfo(container.Type);
    const bool l_Chosen = m_Chosen == Chosen::Container && m_Session.GetSoundEventSelection().Container == container.ID;
    ImGuiTreeNodeFlags l_Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap;
    l_Flags |= l_Children.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_None;
    l_Flags |= l_Chosen ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None;
    ImGui::PushID(container.ID.ToString().c_str());
    const bool l_Open = ImGui::TreeNodeEx("##Container", l_Flags, "%s", std::format("{} {}", l_Type.Icon, std::string_view(container.Name)).c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        Choose(Chosen::Container, container.ID);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            BeginRename(Chosen::Container, container.ID);
        }
    }

    if (container.Parent && ImGui::BeginDragDropSource())
    {
        const std::uint64_t l_Value = container.ID.GetValue();
        ImGui::SetDragDropPayload(c_ContainerPayload, &l_Value, sizeof(l_Value));
        TextLine(std::format("{} {}", l_Type.Icon, std::string_view(container.Name)));
        ImGui::EndDragDropSource();
    }

    AcceptDrop(data, container, false);
    if (ImGui::BeginPopupContextItem("##ContainerMenu"))
    {
        DrawContainerMenu(data, container);
        ImGui::EndPopup();
    }

    // What it is, beside its name: its type, a Clip's lack of a clip, and a Switch's child's case
    std::string l_Note = container.Type == Trinity::SoundContainerType::Clip ? std::string(container.Clip ? "" : "no clip") : std::string(l_Type.Name);
    const Trinity::SoundContainer* l_Parent = container.Parent ? data.FindContainer(container.Parent) : nullptr;
    if (l_Parent != nullptr && l_Parent->Type == Trinity::SoundContainerType::Switch)
    {
        l_Note += std::format("{}{}", l_Note.empty() ? "" : ", ", container.Case.empty() ? std::string("for every other label") : std::format("for {}", std::string_view(container.Case)));
    }

    if (!l_Note.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", l_Note.c_str());
    }

    if (l_Open)
    {
        for (const Trinity::SoundContainer* it_Child : l_Children)
        {
            DrawContainerNode(data, *it_Child);
        }

        ImGui::TreePop();
    }

    ImGui::PopID();
}

void SoundEventPanel::DrawContainerMenu(const Trinity::SoundEventData& data, const Trinity::SoundContainer& container)
{
    const bool l_HasChildren = !GetChildren(data, container.ID).empty();
    if (container.Type != Trinity::SoundContainerType::Clip && ImGui::BeginMenu("Add"))
    {
        if (const std::optional<Trinity::SoundContainerType> l_Type = DrawTypeItems(false))
        {
            AddContainer(data, container.ID, *l_Type);
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Type"))
    {
        if (const std::optional<Trinity::SoundContainerType> l_Type = DrawTypeItems(l_HasChildren, container.Type); l_Type && *l_Type != container.Type)
        {
            SetContainerType(data, container.ID, *l_Type);
        }

        ImGui::EndMenu();
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2"))
    {
        BeginRename(Chosen::Container, container.ID);
    }

    if (!container.Parent)
    {
        return;
    }

    if (ImGui::MenuItem(std::format("{} Duplicate", Trinity::Icons::c_Copy).c_str(), "Ctrl+D"))
    {
        DuplicateContainer(data, container.ID);
    }

    const std::vector<const Trinity::SoundContainer*> l_Siblings = GetChildren(data, container.Parent);
    const std::size_t l_Index = static_cast<std::size_t>(std::ranges::find(l_Siblings, &container) - l_Siblings.begin());
    if (ImGui::MenuItem("Move Up", nullptr, false, l_Index > 0))
    {
        MoveContainer(data, container.ID, container.Parent, l_Siblings[l_Index - 1]->ID, "up");
    }

    if (ImGui::MenuItem("Move Down", nullptr, false, l_Index + 1 < l_Siblings.size()))
    {
        MoveContainer(data, container.ID, container.Parent, l_Index + 2 < l_Siblings.size() ? l_Siblings[l_Index + 2]->ID : Trinity::UUID(), "down");
    }

    if (ImGui::MenuItem(std::format("{} Remove", Trinity::Icons::c_Trash).c_str(), "Delete"))
    {
        RemoveContainer(data, container.ID);
    }
}

// Only a clip among the assets is taken, so any other passes over
void SoundEventPanel::AcceptDrop(const Trinity::SoundEventData& data, const Trinity::SoundContainer& target, bool end)
{
    if (!ImGui::BeginDragDropTarget())
    {
        return;
    }

    const bool l_Into = end || target.Type != Trinity::SoundContainerType::Clip;
    if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_ContainerPayload); l_Payload != nullptr && l_Payload->DataSize == sizeof(std::uint64_t))
    {
        std::uint64_t l_Value = 0;
        std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));
        if (l_Into)
        {
            MoveContainer(data, Trinity::UUID(l_Value), target.ID, {}, std::format("into {}", std::string_view(target.Name)));
        }
        else if (target.Parent)
        {
            MoveContainer(data, Trinity::UUID(l_Value), target.Parent, target.ID, std::format("before {}", std::string_view(target.Name)));
        }
    }

    const ImGuiPayload* l_Dragged = ImGui::GetDragDropPayload();
    if (l_Dragged != nullptr && l_Dragged->IsDataType(c_AssetPayload) && l_Dragged->DataSize == sizeof(std::uint64_t))
    {
        std::uint64_t l_Value = 0;
        std::memcpy(&l_Value, l_Dragged->Data, sizeof(l_Value));
        const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
        if (IsAudioClip(l_Registry != nullptr ? l_Registry->Find(Trinity::UUID(l_Value)) : nullptr) && ImGui::AcceptDragDropPayload(c_AssetPayload) != nullptr)
        {
            const Trinity::UUID l_Parent = l_Into ? target.ID : target.Parent;
            if (l_Parent)
            {
                AddContainer(data, l_Parent, Trinity::SoundContainerType::Clip, Trinity::UUID(l_Value));
            }
        }
    }

    ImGui::EndDragDropTarget();
}

// Each by its name, with what it is after it. Choosing one draws the curves by it
void SoundEventPanel::DrawParameters(const Trinity::SoundEventData& data)
{
    ImGui::PushID("Parameters");
    if (ColumnHeader(std::format("Parameters ({})", data.Parameters.size()), "Add a parameter"))
    {
        ImGui::OpenPopup("##AddParameter");
    }

    if (ImGui::BeginPopup("##AddParameter"))
    {
        if (ImGui::MenuItem("Continuous"))
        {
            AddParameter(data, Trinity::SoundParameterKind::Continuous);
        }

        ImGui::SetItemTooltip("A value along a range, as an engine's RPM, which seeks towards each value it is given");
        if (ImGui::MenuItem("Discrete"))
        {
            AddParameter(data, Trinity::SoundParameterKind::Discrete);
        }

        ImGui::SetItemTooltip("One of its labels, as the surface a footstep falls on, which a Switch picks by");
        ImGui::EndPopup();
    }

    if (data.Parameters.empty())
    {
        ImGui::TextDisabled("None yet. A parameter is a value the game gives each instance, which its curves and Switches follow");
    }

    const Trinity::UUID l_Chosen = m_Chosen == Chosen::Parameter ? m_Session.GetSoundEventSelection().Parameter : Trinity::UUID();
    for (const Trinity::SoundParameter& it_Parameter : data.Parameters)
    {
        ImGui::PushID(it_Parameter.ID.ToString().c_str());
        if (ImGui::Selectable(std::format("{} {}###Parameter", Trinity::Icons::c_Sliders, std::string_view(it_Parameter.Name)).c_str(), l_Chosen == it_Parameter.ID, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap))
        {
            Choose(Chosen::Parameter, it_Parameter.ID);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                BeginRename(Chosen::Parameter, it_Parameter.ID);
            }
        }

        if (ImGui::BeginPopupContextItem("##ParameterMenu"))
        {
            if (ImGui::MenuItem("Rename", "F2"))
            {
                BeginRename(Chosen::Parameter, it_Parameter.ID);
            }

            if (ImGui::MenuItem(std::format("{} Remove", Trinity::Icons::c_Trash).c_str(), "Delete"))
            {
                RemoveParameter(data, it_Parameter.ID);
            }

            ImGui::EndPopup();
        }

        ImGui::SameLine();
        const std::string l_Kind = it_Parameter.Kind == Trinity::SoundParameterKind::Discrete ? std::format("{} label{}", it_Parameter.Labels.size(), it_Parameter.Labels.size() == 1 ? "" : "s") : std::format("{:g} to {:g}", it_Parameter.Minimum, it_Parameter.Maximum);
        ImGui::TextDisabled("%s", std::format("{}{}", l_Kind, it_Parameter.Global ? ", global" : "").c_str());
        ImGui::PopID();
    }

    ImGui::PopID();
}

// Its name and type, its values, those its parent and its type give it, the event's own with the root's, and its curves
void SoundEventPanel::DrawContainerDetails(Trinity::UUID asset, const Trinity::SoundEventData& data, const Trinity::SoundContainer& container)
{
    const std::string_view l_Name(container.Name);
    const Trinity::SoundContainer* l_Parent = container.Parent ? data.FindContainer(container.Parent) : nullptr;
    const std::vector<const Trinity::SoundContainer*> l_Children = GetChildren(data, container.ID);
    const auto a_Change = [&](std::string what, std::string field, const std::function<void(Trinity::SoundContainer&)>& change)
    {
        Trinity::SoundEventData l_After = data;
        change(*l_After.FindContainer(container.ID));
        Edit(std::move(l_After), std::move(what), std::move(field));
    };

    // A value's slider, or its drag where its range is open, shown multiplied as a share is in percent
    const auto a_Float = [&](const char* label, const char* tooltip, float Trinity::SoundContainer::* member, float minimum, float maximum, const char* format, std::string_view what, float shown = 1.0f, bool drag = false)
    {
        Row(label, tooltip);
        float l_Value = container.*member * shown;
        const std::string l_ID = std::format("##{}", label);
        const bool l_Changed = drag ? ImGui::DragFloat(l_ID.c_str(), &l_Value, (maximum - minimum) * shown * 0.001f, minimum * shown, maximum * shown, format, ImGuiSliderFlags_AlwaysClamp) : ImGui::SliderFloat(l_ID.c_str(), &l_Value, minimum * shown, maximum * shown, format, ImGuiSliderFlags_AlwaysClamp);
        if (l_Changed)
        {
            a_Change(std::format("Set {} of {}", what, l_Name), std::format("{} {}", label, container.ID), [&](Trinity::SoundContainer& changed) { changed.*member = std::clamp(l_Value / shown, minimum, maximum); });
        }
    };

    ImGui::PushID(container.ID.ToString().c_str());
    if (BeginFields("##Container"))
    {
        Row("Name");
        const std::string l_Key = std::format("Name {}", container.ID);
        if (const std::optional<std::string> l_Typed = TextField("##Name", l_Key, l_Name))
        {
            if (l_Typed->empty())
            {
                m_Error = "A container needs a name";
                m_ErrorKey = l_Key;
            }
            else
            {
                RenameContainer(data, container.ID, *l_Typed);
            }
        }

        if (m_ErrorKey == l_Key && !m_Error.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Error));
            ImGui::TextWrapped("%s", m_Error.c_str());
            ImGui::PopStyleColor();
        }

        Row("Type", "What it does as it is triggered");
        if (ImGui::BeginCombo("##Type", std::format("{} {}", GetTypeInfo(container.Type).Icon, GetTypeInfo(container.Type).Name).c_str()))
        {
            if (const std::optional<Trinity::SoundContainerType> l_Type = DrawTypeItems(!l_Children.empty(), container.Type); l_Type && *l_Type != container.Type)
            {
                SetContainerType(data, container.ID, *l_Type);
            }

            ImGui::EndCombo();
        }

        a_Float("Volume", "In decibels, added to those above it", &Trinity::SoundContainer::Volume, Trinity::SoundEventData::c_MinimumVolume, Trinity::SoundEventData::c_MaximumVolume, "%.1f dB", "volume");
        a_Float("Volume Random", "How far its volume is varied at random either way each time it is triggered", &Trinity::SoundContainer::VolumeRandom, 0.0f, Trinity::SoundEventData::c_MaximumVolumeRandom, "\xC2\xB1 %.1f dB", "volume spread");
        a_Float("Pitch", "How fast it plays, which raises or lowers its pitch with it, multiplied with those above it", &Trinity::SoundContainer::Pitch, Trinity::SoundEventData::c_MinimumPitch, Trinity::SoundEventData::c_MaximumPitch, "\xC3\x97%.2f", "pitch");
        a_Float("Pitch Random", "How far its pitch is varied at random either way each time it is triggered, as a share of it", &Trinity::SoundContainer::PitchRandom, 0.0f, Trinity::SoundEventData::c_MaximumPitchRandom, "\xC2\xB1 %.1f%%", "pitch spread", 100.0f);
        a_Float("Delay", "Seconds before it starts, each time it is triggered", &Trinity::SoundContainer::Delay, 0.0f, 60.0f, "%.3f s", "delay", 1.0f, true);
        a_Float("Delay Random", "Up to how many seconds more it waits, at random", &Trinity::SoundContainer::DelayRandom, 0.0f, 60.0f, "+ %.3f s", "delay spread", 1.0f, true);
        if (l_Parent != nullptr && l_Parent->Type == Trinity::SoundContainerType::Random)
        {
            a_Float("Weight", "Its share of its Random's picks: twice another's is picked twice as often", &Trinity::SoundContainer::Weight, 0.0f, 100.0f, "%.2f", "weight", 1.0f, true);
        }

        const Trinity::SoundParameter* l_SwitchBy = l_Parent != nullptr && l_Parent->Type == Trinity::SoundContainerType::Switch ? data.FindParameter(l_Parent->Parameter) : nullptr;
        if (l_Parent != nullptr && l_Parent->Type == Trinity::SoundContainerType::Switch)
        {
            Row("Case", "The label of its Switch's parameter it plays for. With none, it plays for every label no other child has");
            ImGui::BeginDisabled(l_SwitchBy == nullptr);
            const std::string l_Preview = container.Case.empty() ? std::string("Every other label") : std::string(std::string_view(container.Case));
            if (ImGui::BeginCombo("##Case", l_SwitchBy != nullptr ? l_Preview.c_str() : "Its Switch picks by no parameter"))
            {
                if (ImGui::Selectable("Every other label", container.Case.empty()) && !container.Case.empty())
                {
                    a_Change(std::format("Play {} for every other label", l_Name), {}, [](Trinity::SoundContainer& changed) { changed.Case.clear(); });
                }

                for (std::size_t it_Label = 0; l_SwitchBy != nullptr && it_Label < l_SwitchBy->Labels.size(); ++it_Label)
                {
                    const std::string_view l_Label(l_SwitchBy->Labels[it_Label]);
                    ImGui::PushID(static_cast<int>(it_Label));
                    if (ImGui::Selectable(std::string(l_Label).c_str(), std::string_view(container.Case) == l_Label) && std::string_view(container.Case) != l_Label)
                    {
                        a_Change(std::format("Play {} for {}", l_Name, l_Label), {}, [l_Label](Trinity::SoundContainer& changed) { changed.Case = Trinity::TaggedString<Trinity::MemoryTag::Audio>(l_Label); });
                    }

                    ImGui::PopID();
                }

                ImGui::EndCombo();
            }

            ImGui::EndDisabled();
        }

        switch (container.Type)
        {
            case Trinity::SoundContainerType::Clip:
            {
                Row("Clip", "The clip it plays. Drag one here from the Content Browser, or pick one");
                DrawClipSlot(data, container);
                Row("Loop", "Plays its clip over and over for as long as the event plays, so nothing after it starts");
                bool l_Loop = container.Loop;
                if (ImGui::Checkbox("##Loop", &l_Loop))
                {
                    a_Change(std::format("{} {}", l_Loop ? "Loop" : "Stop looping", l_Name), {}, [l_Loop](Trinity::SoundContainer& changed) { changed.Loop = l_Loop; });
                }

                break;
            }
            case Trinity::SoundContainerType::Random:
            {
                Row("Avoid Repeat", "How many of its last picks it never picks again, as Unity's Avoid Repeating Last does, so a footstep never comes twice in a row");
                int l_Avoid = static_cast<int>(container.AvoidRepeat);
                const int l_Most = std::max(static_cast<int>(l_Children.size()) - 1, 0);
                if (ImGui::SliderInt("##AvoidRepeat", &l_Avoid, 0, l_Most, l_Avoid == 0 ? "%d, off" : "%d", ImGuiSliderFlags_AlwaysClamp))
                {
                    a_Change(std::format("Set Avoid Repeat of {}", l_Name), std::format("AvoidRepeat {}", container.ID), [l_Avoid](Trinity::SoundContainer& changed) { changed.AvoidRepeat = static_cast<std::uint32_t>(std::max(l_Avoid, 0)); });
                }

                Row("Auto Trigger", "Triggers itself on an interval for as long as the event plays, or a number of times, as Unity's Automatic Trigger does");
                bool l_Auto = container.AutoTrigger;
                if (ImGui::Checkbox("##AutoTrigger", &l_Auto))
                {
                    a_Change(std::format("{} {} trigger itself", l_Auto ? "Make" : "Stop", l_Name), {}, [l_Auto](Trinity::SoundContainer& changed) { changed.AutoTrigger = l_Auto; });
                }

                ImGui::BeginDisabled(!container.AutoTrigger);
                Row("Timing", "Pulse spaces its triggers from the start of one to the start of the next, and Offset from the end of one to the start of the next");
                int l_Timing = static_cast<int>(container.Timing);
                if (ImGui::Combo("##Timing", &l_Timing, "Pulse\0Offset\0"))
                {
                    a_Change(std::format("Set timing of {}", l_Name), {}, [l_Timing](Trinity::SoundContainer& changed) { changed.Timing = static_cast<Trinity::SoundTriggerTiming>(l_Timing); });
                }

                Row("Interval", "Between how many seconds and how many its triggers are spaced, picked at random each time");
                float l_Lowest = container.IntervalMinimum;
                float l_Highest = container.IntervalMaximum;
                if (ImGui::DragFloatRange2("##Interval", &l_Lowest, &l_Highest, 0.01f, 0.0f, 3600.0f, "%.2f s", "%.2f s", ImGuiSliderFlags_AlwaysClamp))
                {
                    a_Change(std::format("Set interval of {}", l_Name), std::format("Interval {}", container.ID), [l_Lowest, l_Highest](Trinity::SoundContainer& changed)
                    {
                        changed.IntervalMinimum = std::min(l_Lowest, l_Highest);
                        changed.IntervalMaximum = std::max(l_Lowest, l_Highest);
                    });
                }

                Row("Trigger Count", "How many times it triggers itself, or 0 for as long as the event plays");
                int l_Count = static_cast<int>(container.TriggerCount);
                if (ImGui::DragInt("##TriggerCount", &l_Count, 0.1f, 0, 100000, l_Count == 0 ? "%d, forever" : "%d", ImGuiSliderFlags_AlwaysClamp))
                {
                    a_Change(std::format("Set trigger count of {}", l_Name), std::format("TriggerCount {}", container.ID), [l_Count](Trinity::SoundContainer& changed) { changed.TriggerCount = static_cast<std::uint32_t>(std::max(l_Count, 0)); });
                }

                ImGui::EndDisabled();
                break;
            }
            case Trinity::SoundContainerType::Sequence:
            {
                Row("Loop", "Starts again from its first child once its last ends, for as long as the event plays");
                bool l_Loop = container.Loop;
                if (ImGui::Checkbox("##Loop", &l_Loop))
                {
                    a_Change(std::format("{} {}", l_Loop ? "Loop" : "Stop looping", l_Name), {}, [l_Loop](Trinity::SoundContainer& changed) { changed.Loop = l_Loop; });
                }

                break;
            }
            case Trinity::SoundContainerType::Switch:
            {
                Row("Parameter", "The discrete parameter whose label picks the children it plays");
                const Trinity::SoundParameter* l_By = data.FindParameter(container.Parameter);
                if (ImGui::BeginCombo("##Parameter", l_By != nullptr ? std::string(std::string_view(l_By->Name)).c_str() : "None"))
                {
                    if (ImGui::Selectable("None", l_By == nullptr) && l_By != nullptr)
                    {
                        Trinity::SoundEventData l_After = data;
                        if (l_After.SetSwitchParameter(container.ID, {}))
                        {
                            Edit(std::move(l_After), std::format("Make {} pick by no parameter", l_Name));
                        }
                    }

                    for (const Trinity::SoundParameter& it_Parameter : data.Parameters)
                    {
                        if (it_Parameter.Kind != Trinity::SoundParameterKind::Discrete)
                        {
                            continue;
                        }

                        ImGui::PushID(it_Parameter.ID.ToString().c_str());
                        if (ImGui::Selectable(std::string(std::string_view(it_Parameter.Name)).c_str(), l_By == &it_Parameter) && l_By != &it_Parameter)
                        {
                            Trinity::SoundEventData l_After = data;
                            if (l_After.SetSwitchParameter(container.ID, it_Parameter.ID))
                            {
                                Edit(std::move(l_After), std::format("Make {} pick by {}", l_Name, std::string_view(it_Parameter.Name)));
                            }
                        }

                        ImGui::PopID();
                    }

                    ImGui::EndCombo();
                }

                break;
            }
            case Trinity::SoundContainerType::Layer:
            {
                break;
            }
        }

        ImGui::EndTable();
    }

    ImGui::PopID();
    if (!container.Parent)
    {
        DrawEventSettings(data);
    }

    DrawCurves(asset, data, container);
}

// The event's own settings, which the root's details show: the mixer group it plays into, how many of it play at once, and how it is heard in 3D when played on its own, as the audition plays it
void SoundEventPanel::DrawEventSettings(const Trinity::SoundEventData& data)
{
    ImGui::SeparatorText("Event");
    const auto a_Change = [&](std::string what, std::string field, const std::function<void(Trinity::SoundEventData&)>& change)
    {
        Trinity::SoundEventData l_After = data;
        change(l_After);
        Edit(std::move(l_After), std::move(what), std::move(field));
    };

    const auto a_Float = [&](const char* label, const char* tooltip, float Trinity::SoundEventData::* member, float minimum, float maximum, const char* format, bool logarithmic = false)
    {
        Row(label, tooltip);
        float l_Value = data.*member;
        const std::string l_ID = std::format("##{}", label);
        if (ImGui::SliderFloat(l_ID.c_str(), &l_Value, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp | (logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None)))
        {
            a_Change(std::format("Set {} of the event", label), label, [&](Trinity::SoundEventData& changed) { changed.*member = std::clamp(l_Value, minimum, maximum); });
        }
    };

    if (!BeginFields("##Event"))
    {
        return;
    }

    // Master stands for none, and a group the project's mixer lacks plays into Master
    Row("Output", "The group of the project's mixer it plays into when played on its own, as the audition and PlayAtPoint play it. An AudioSource playing it uses its own unless that names none");
    const Trinity::AudioMixer* l_Mixer = m_Session.GetMixer();
    const Trinity::AudioMixerData* l_MixerData = l_Mixer != nullptr ? &l_Mixer->GetData() : nullptr;
    const Trinity::AudioMixerGroup* l_Output = l_MixerData != nullptr && data.Output ? l_MixerData->FindGroup(data.Output) : nullptr;
    const std::string l_Master = l_MixerData != nullptr ? std::string(std::string_view(l_MixerData->GetMaster().Name)) : std::string("Master");
    const std::string l_Preview = !data.Output ? l_Master : (l_Output != nullptr ? std::string(std::string_view(l_Output->Name)) : std::format("{} Missing, plays into {}", Trinity::Icons::c_Warning, l_Master));
    if (ImGui::BeginCombo("##Output", l_Preview.c_str(), ImGuiComboFlags_HeightLarge))
    {
        const std::span<const Trinity::AudioMixerGroup> l_Groups = l_MixerData != nullptr ? std::span<const Trinity::AudioMixerGroup>(l_MixerData->Groups) : std::span<const Trinity::AudioMixerGroup>();
        std::vector<std::uint32_t> l_Depths(l_Groups.size(), 0);
        for (std::size_t it_Group = 0; it_Group < l_Groups.size(); ++it_Group)
        {
            for (std::size_t it_Parent = 0; it_Parent < it_Group; ++it_Parent)
            {
                l_Depths[it_Group] = l_Groups[it_Parent].ID == l_Groups[it_Group].Parent ? l_Depths[it_Parent] + 1 : l_Depths[it_Group];
            }

            const bool l_IsMaster = it_Group == 0;
            const bool l_Selected = data.Output == l_Groups[it_Group].ID || (l_IsMaster && !data.Output);
            ImGui::PushID(l_Groups[it_Group].ID.ToString().c_str());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + static_cast<float>(l_Depths[it_Group]) * ImGui::GetStyle().IndentSpacing);
            if (ImGui::Selectable(std::string(std::string_view(l_Groups[it_Group].Name)).c_str(), l_Selected) && !l_Selected)
            {
                const Trinity::UUID l_Group = l_IsMaster ? Trinity::UUID() : l_Groups[it_Group].ID;
                a_Change(std::format("Play the event into {}", std::string_view(l_Groups[it_Group].Name)), {}, [l_Group](Trinity::SoundEventData& changed) { changed.Output = l_Group; });
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    Row("Priority", "Which sounds are mixed when more play than the project's real voices: 0 first and 256 last");
    int l_Priority = static_cast<int>(std::min(data.Priority, Trinity::SoundEventData::c_LowestPriority));
    if (ImGui::SliderInt("##Priority", &l_Priority, 0, static_cast<int>(Trinity::SoundEventData::c_LowestPriority), "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        a_Change("Set priority of the event", "Priority", [l_Priority](Trinity::SoundEventData& changed) { changed.Priority = static_cast<std::uint32_t>(l_Priority); });
    }

    Row("Max Instances", "How many of it play at once, 0 for any number");
    int l_Instances = static_cast<int>(data.MaxInstances);
    if (ImGui::DragInt("##MaxInstances", &l_Instances, 0.1f, 0, 1000, l_Instances == 0 ? "%d, no limit" : "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        a_Change("Set instance limit of the event", "MaxInstances", [l_Instances](Trinity::SoundEventData& changed) { changed.MaxInstances = static_cast<std::uint32_t>(std::max(l_Instances, 0)); });
    }

    ImGui::BeginDisabled(data.MaxInstances == 0);
    Row("Steal", "What a play beyond the limit does: stop the one that began first, stop the quietest, or not play");
    int l_Steal = static_cast<int>(data.Steal);
    if (ImGui::Combo("##Steal", &l_Steal, "Oldest\0Quietest\0None\0"))
    {
        a_Change("Set stealing of the event", {}, [l_Steal](Trinity::SoundEventData& changed) { changed.Steal = static_cast<Trinity::SoundStealMode>(l_Steal); });
    }

    ImGui::EndDisabled();
    a_Float("Cooldown", "Seconds after an instance starts before another may", &Trinity::SoundEventData::Cooldown, 0.0f, 10.0f, "%.2f s");
    Row("Spatialize", "Heard from where it plays in 3D. Off, it is heard as it is");
    bool l_Spatialize = data.Spatialize;
    if (ImGui::Checkbox("##Spatialize", &l_Spatialize))
    {
        a_Change(std::format("{} the event", l_Spatialize ? "Spatialize" : "Stop spatializing"), {}, [l_Spatialize](Trinity::SoundEventData& changed) { changed.Spatialize = l_Spatialize; });
    }

    ImGui::BeginDisabled(!data.Spatialize);
    a_Float("Doppler Level", "How much movement shifts its pitch", &Trinity::SoundEventData::DopplerLevel, 0.0f, 5.0f, "%.2f");
    a_Float("Spread", "How far it spreads around the listener, in degrees", &Trinity::SoundEventData::Spread, 0.0f, 360.0f, "%.0f\xC2\xB0");
    Row("Volume Rolloff", "How its volume falls between the minimum and maximum distances");
    int l_Rolloff = static_cast<int>(data.Rolloff);
    if (ImGui::Combo("##Rolloff", &l_Rolloff, "Logarithmic\0Linear\0Exponential\0"))
    {
        a_Change("Set rolloff of the event", {}, [l_Rolloff](Trinity::SoundEventData& changed) { changed.Rolloff = static_cast<Trinity::AudioRolloff>(l_Rolloff); });
    }

    Row("Min Distance", "Closer than this it is at full volume");
    float l_Nearest = data.MinDistance;
    if (ImGui::DragFloat("##MinDistance", &l_Nearest, 0.05f, 0.01f, 100000.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
    {
        a_Change("Set nearest distance of the event", "MinDistance", [l_Nearest](Trinity::SoundEventData& changed)
        {
            changed.MinDistance = l_Nearest;
            changed.MaxDistance = std::max(changed.MaxDistance, l_Nearest);
        });
    }

    Row("Max Distance", "Beyond this it falls no further");
    float l_Farthest = data.MaxDistance;
    if (ImGui::DragFloat("##MaxDistance", &l_Farthest, 0.5f, data.MinDistance, 100000.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
    {
        a_Change("Set farthest distance of the event", "MaxDistance", [l_Farthest](Trinity::SoundEventData& changed) { changed.MaxDistance = std::max(l_Farthest, changed.MinDistance); });
    }

    Row("Occlusion", "Muffled while the scene's physics stands between it and the listener");
    bool l_Occlusion = data.Occlusion;
    if (ImGui::Checkbox("##Occlusion", &l_Occlusion))
    {
        a_Change(std::format("{} occlusion of the event", l_Occlusion ? "Turn on" : "Turn off"), {}, [l_Occlusion](Trinity::SoundEventData& changed) { changed.Occlusion = l_Occlusion; });
    }

    ImGui::BeginDisabled(!data.Occlusion);
    a_Float("Occlusion Volume", "Its volume behind something, from 0 to 1", &Trinity::SoundEventData::OcclusionVolume, 0.0f, 1.0f, "%.2f");
    a_Float("Occlusion Cutoff", "The low-pass cutoff it is heard through behind something", &Trinity::SoundEventData::OcclusionCutoff, Trinity::SoundEventData::c_MinimumCutoff, Trinity::SoundEventData::c_MaximumCutoff, "%.0f Hz", true);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::EndTable();
}

// The clip's name, or why it has none, which opens a list of the project's clips to pick from, and takes a clip dropped on it
void SoundEventPanel::DrawClipSlot(const Trinity::SoundEventData& data, const Trinity::SoundContainer& container)
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = container.Clip && l_Registry != nullptr ? l_Registry->Find(container.Clip) : nullptr;
    const std::string l_Text = !container.Clip ? std::string("None") : (IsAudioClip(l_Record) ? std::format("{} {}", Trinity::Icons::c_FileAudio, GetStem(l_Record->Path)) : std::format("{} Missing clip", Trinity::Icons::c_Warning));
    if (ImGui::Button(std::format("{}###Clip", l_Text).c_str(), ImVec2(-FLT_MIN, 0.0f)))
    {
        m_ClipFilter.clear();
        ImGui::OpenPopup("##ClipPicker");
    }

    if (ImGui::BeginDragDropTarget())
    {
        const ImGuiPayload* l_Dragged = ImGui::GetDragDropPayload();
        if (l_Dragged != nullptr && l_Dragged->IsDataType(c_AssetPayload) && l_Dragged->DataSize == sizeof(std::uint64_t))
        {
            std::uint64_t l_Value = 0;
            std::memcpy(&l_Value, l_Dragged->Data, sizeof(l_Value));
            if (IsAudioClip(l_Registry != nullptr ? l_Registry->Find(Trinity::UUID(l_Value)) : nullptr) && ImGui::AcceptDragDropPayload(c_AssetPayload) != nullptr)
            {
                SetClip(data, container.ID, Trinity::UUID(l_Value));
            }
        }

        ImGui::EndDragDropTarget();
    }

    if (!ImGui::BeginPopup("##ClipPicker"))
    {
        return;
    }

    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    ImGui::InputTextWithHint("##Filter", "Search", &m_ClipFilter);
    if (ImGui::Selectable("None", !container.Clip) && container.Clip)
    {
        SetClip(data, container.ID, {});
    }

    std::vector<const Trinity::AssetRecord*> l_Clips;
    for (const Trinity::AssetRecord* it_Record : l_Registry != nullptr ? l_Registry->GetRecords() : std::vector<const Trinity::AssetRecord*>())
    {
        const auto a_Matches = [this](std::string_view path) { return std::ranges::search(path, m_ClipFilter, [](char left, char right) { return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right)); }).begin() != path.end() || m_ClipFilter.empty(); };
        if (IsAudioClip(it_Record) && a_Matches(it_Record->Path))
        {
            l_Clips.push_back(it_Record);
        }
    }

    std::ranges::sort(l_Clips, {}, [](const Trinity::AssetRecord* record) { return record->Path; });
    for (const Trinity::AssetRecord* it_Record : l_Clips)
    {
        ImGui::PushID(it_Record->ID.ToString().c_str());
        if (ImGui::Selectable(std::format("{} {}", Trinity::Icons::c_FileAudio, GetStem(it_Record->Path)).c_str(), it_Record->ID == container.Clip) && it_Record->ID != container.Clip)
        {
            SetClip(data, container.ID, it_Record->ID);
        }

        ImGui::SetItemTooltip("%s", it_Record->Path.c_str());
        ImGui::PopID();
    }

    ImGui::EndPopup();
}

// Its name, kind and whether it is global, and a continuous one's range, default and seek speed, or a discrete one's labels and default
void SoundEventPanel::DrawParameterDetails(const Trinity::SoundEventData& data, const Trinity::SoundParameter& parameter)
{
    const std::string_view l_Name(parameter.Name);
    const auto a_Change = [&](std::string what, std::string field, const std::function<void(Trinity::SoundParameter&)>& change)
    {
        Trinity::SoundEventData l_After = data;
        Trinity::SoundParameter& l_Changed = *l_After.FindParameter(parameter.ID);
        change(l_Changed);
        l_Changed.Default = l_Changed.Clamp(l_Changed.Default);
        Edit(std::move(l_After), std::move(what), std::move(field));
    };

    const auto a_ShowError = [this](const std::string& key)
    {
        if (m_ErrorKey == key && !m_Error.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Error));
            ImGui::TextWrapped("%s", m_Error.c_str());
            ImGui::PopStyleColor();
        }
    };

    ImGui::PushID(parameter.ID.ToString().c_str());
    if (!BeginFields("##Parameter"))
    {
        ImGui::PopID();

        return;
    }

    Row("Name", "What the game gives it by, which no other parameter of the event has");
    const std::string l_Key = std::format("Name {}", parameter.ID);
    if (const std::optional<std::string> l_Typed = TextField("##Name", l_Key, l_Name))
    {
        RenameParameter(data, parameter.ID, *l_Typed);
    }

    a_ShowError(l_Key);
    Row("Kind", "Continuous is a value along a range, which seeks towards each value it is given. Discrete is one of its labels, which a Switch picks by");
    int l_Kind = parameter.Kind == Trinity::SoundParameterKind::Continuous ? 0 : 1;
    if (ImGui::Combo("##Kind", &l_Kind, "Continuous\0Discrete\0"))
    {
        Trinity::SoundEventData l_After = data;
        const Trinity::SoundParameterKind l_To = l_Kind == 0 ? Trinity::SoundParameterKind::Continuous : Trinity::SoundParameterKind::Discrete;
        if (l_To != parameter.Kind && l_After.SetParameterKind(parameter.ID, l_To))
        {
            Edit(std::move(l_After), std::format("Make {} {}", l_Name, l_Kind == 0 ? "continuous" : "discrete"));
        }
    }

    Row("Global", "Given by the game once for every instance by its name, as a time of day is, rather than to each instance");
    bool l_Global = parameter.Global;
    if (ImGui::Checkbox("##Global", &l_Global))
    {
        a_Change(std::format("Make {} {}", l_Name, l_Global ? "global" : "local"), {}, [l_Global](Trinity::SoundParameter& changed) { changed.Global = l_Global; });
    }

    if (parameter.Kind == Trinity::SoundParameterKind::Continuous)
    {
        Row("Minimum");
        float l_Lowest = parameter.Minimum;
        if (ImGui::DragFloat("##Minimum", &l_Lowest, 0.01f, -100000.0f, std::nextafter(parameter.Maximum, -FLT_MAX), "%.3f", ImGuiSliderFlags_AlwaysClamp))
        {
            a_Change(std::format("Set minimum of {}", l_Name), std::format("Minimum {}", parameter.ID), [l_Lowest](Trinity::SoundParameter& changed) { changed.Minimum = std::min(l_Lowest, std::nextafter(changed.Maximum, -FLT_MAX)); });
        }

        Row("Maximum");
        float l_Highest = parameter.Maximum;
        if (ImGui::DragFloat("##Maximum", &l_Highest, 0.01f, std::nextafter(parameter.Minimum, FLT_MAX), 100000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
        {
            a_Change(std::format("Set maximum of {}", l_Name), std::format("Maximum {}", parameter.ID), [l_Highest](Trinity::SoundParameter& changed) { changed.Maximum = std::max(l_Highest, std::nextafter(changed.Minimum, FLT_MAX)); });
        }

        Row("Default", "Its value until the game gives one");
        float l_Default = parameter.Default;
        if (ImGui::SliderFloat("##Default", &l_Default, parameter.Minimum, parameter.Maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp))
        {
            a_Change(std::format("Set default of {}", l_Name), std::format("Default {}", parameter.ID), [l_Default](Trinity::SoundParameter& changed) { changed.Default = l_Default; });
        }

        Row("Seek Speed", "How fast it moves towards each value it is given, in its own units a second, as an engine's RPM climbs. 0 jumps at once");
        float l_Speed = parameter.SeekSpeed;
        if (ImGui::DragFloat("##SeekSpeed", &l_Speed, 0.01f, 0.0f, 100000.0f, l_Speed == 0.0f ? "%.3f, at once" : "%.3f / s", ImGuiSliderFlags_AlwaysClamp))
        {
            a_Change(std::format("Set seek speed of {}", l_Name), std::format("SeekSpeed {}", parameter.ID), [l_Speed](Trinity::SoundParameter& changed) { changed.SeekSpeed = std::max(l_Speed, 0.0f); });
        }

        ImGui::EndTable();
        ImGui::PopID();

        return;
    }

    Row("Default", "Its label until the game gives one");
    if (ImGui::BeginCombo("##Default", std::string(GetLabel(parameter, parameter.Default)).c_str()))
    {
        for (std::size_t it_Label = 0; it_Label < parameter.Labels.size(); ++it_Label)
        {
            ImGui::PushID(static_cast<int>(it_Label));
            if (ImGui::Selectable(std::string(std::string_view(parameter.Labels[it_Label])).c_str(), std::lround(parameter.Default) == static_cast<long>(it_Label)))
            {
                a_Change(std::format("Set default of {} to {}", l_Name, std::string_view(parameter.Labels[it_Label])), {}, [it_Label](Trinity::SoundParameter& changed) { changed.Default = static_cast<float>(it_Label); });
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    for (std::size_t it_Label = 0; it_Label < parameter.Labels.size(); ++it_Label)
    {
        const std::string_view l_Label(parameter.Labels[it_Label]);
        ImGui::PushID(static_cast<int>(it_Label));
        Row(it_Label == 0 ? "Labels" : "", it_Label == 0 ? "Its values, from 0 in this order, each a case a Switch's children play for. A label renamed renames the cases of it" : nullptr);
        const float l_Remove = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - l_Remove - ImGui::GetStyle().ItemSpacing.x, 1.0f));
        const std::string l_LabelKey = std::format("Label {} {}", parameter.ID, it_Label);
        if (const std::optional<std::string> l_Typed = TextField("##Label", l_LabelKey, l_Label); l_Typed && *l_Typed != l_Label)
        {
            Trinity::SoundEventData l_After = data;
            if (l_After.RenameLabel(parameter.ID, it_Label, *l_Typed))
            {
                Edit(std::move(l_After), std::format("Rename label {} of {} to {}", l_Label, l_Name, *l_Typed));
                m_Error.clear();
            }
            else
            {
                m_Error = l_Typed->empty() ? std::string("A label needs a name") : std::format("{} already has a label called {}", l_Name, *l_Typed);
                m_ErrorKey = l_LabelKey;
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(parameter.Labels.size() == 1);
        if (ImGui::Button(Trinity::Icons::c_Trash, ImVec2(l_Remove, l_Remove)))
        {
            Trinity::SoundEventData l_After = data;
            if (l_After.RemoveLabel(parameter.ID, it_Label))
            {
                Edit(std::move(l_After), std::format("Remove label {} of {}", l_Label, l_Name));
            }
        }

        ImGui::SetItemTooltip("%s", parameter.Labels.size() == 1 ? "A discrete parameter keeps one label at least" : "Removes the label, whose cases then play for every other label");
        ImGui::EndDisabled();
        a_ShowError(l_LabelKey);
        ImGui::PopID();
    }

    Row("");
    if (ImGui::Button(std::format("{} Add Label", Trinity::Icons::c_Plus).c_str()))
    {
        Trinity::SoundEventData l_After = data;
        const std::string l_Label = FreeLabel(parameter);
        if (l_After.AddLabel(parameter.ID, l_Label))
        {
            Edit(std::move(l_After), std::format("Add label {} to {}", l_Label, l_Name));
            m_FocusKey = std::format("Label {} {}", parameter.ID, parameter.Labels.size());
        }
    }

    ImGui::EndTable();
    ImGui::PopID();
}

// Picked by a parameter, with a button to add or remove each of Volume, Pitch and Cutoff, the three in ImCurveEdit over the parameter's range, each over its own range up it, so the lines drawn are the lines heard. A tick marks where the audition has the parameter, and the values there show as the editor is hovered
void SoundEventPanel::DrawCurves(Trinity::UUID asset, const Trinity::SoundEventData& data, const Trinity::SoundContainer& container)
{
    ImGui::SeparatorText(std::format("{} Curves", Trinity::Icons::c_LineChart).c_str());
    const Trinity::SoundParameter* l_Parameter = data.FindParameter(m_Session.GetSoundEventSelection().Parameter);
    if (l_Parameter == nullptr)
    {
        m_CurveFocused = false;
        ImGui::TextDisabled("A curve moves the container's volume, pitch or cutoff by a parameter as it plays. Add a parameter on the left to draw one by it");

        return;
    }

    const std::string_view l_ParameterName(l_Parameter->Name);
    ImGui::AlignTextToFramePadding();
    TextLine("By");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * c_SliderWidthInFonts);
    if (ImGui::BeginCombo("##By", std::string(l_ParameterName).c_str()))
    {
        for (const Trinity::SoundParameter& it_Parameter : data.Parameters)
        {
            ImGui::PushID(it_Parameter.ID.ToString().c_str());
            if (ImGui::Selectable(std::string(std::string_view(it_Parameter.Name)).c_str(), &it_Parameter == l_Parameter))
            {
                EditorSession::SoundEventSelection l_Selection = m_Session.GetSoundEventSelection();
                l_Selection.Parameter = it_Parameter.ID;
                m_Session.SetSoundEventSelection(l_Selection);
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    ImGui::SetItemTooltip("The parameter the curves below are drawn by");
    for (const TargetInfo& it_Target : c_Targets)
    {
        const bool l_Has = data.FindCurve(container.ID, l_Parameter->ID, it_Target.Target) != nullptr;
        ImGui::PushID(it_Target.Name);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
        const float l_Swatch = ImGui::GetTextLineHeight();
        ImGui::ColorButton("##Swatch", GetTargetColor(it_Target), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoDragDrop, ImVec2(l_Swatch, l_Swatch));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (l_Has)
        {
            TextLine(it_Target.Name);
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            if (ImGui::SmallButton(Trinity::Icons::c_Trash))
            {
                RemoveCurve(data, container.ID, l_Parameter->ID, it_Target.Target);
            }

            ImGui::SetItemTooltip("%s", std::format("Removes the {} curve by {}", it_Target.Name, l_ParameterName).c_str());
        }
        else if (ImGui::SmallButton(std::format("{} {}", Trinity::Icons::c_Plus, it_Target.Name).c_str()))
        {
            AddCurve(data, container.ID, l_Parameter->ID, it_Target.Target);
        }
        else
        {
            ImGui::SetItemTooltip("%s", std::format("A {} curve by {}, level where it changes nothing across the parameter's range", it_Target.Name, l_ParameterName).c_str());
        }

        ImGui::PopID();
    }

    // The view is fitted again for other curves
    const std::string l_Key = GetCurveKey(container.ID, l_Parameter->ID);
    if (l_Key != m_ViewKey)
    {
        m_ViewKey = l_Key;
        m_ViewBottom = -c_CurveMargin;
        m_ViewTop = 1.0f + c_CurveMargin;
    }

    CurveView l_View(container, *l_Parameter, m_ViewBottom, m_ViewTop, m_CurveEditing);
    ImVector<ImCurveEdit::EditPoint> l_Selected;
    const float l_Width = std::max(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize());
    ImCurveEdit::Edit(l_View, ImVec2(l_Width, ImGui::GetFontSize() * c_CurveHeightInFonts), ImGui::GetID("##CurveEditor"), nullptr, &l_Selected);
    const ImVec2 l_Min = ImGui::GetItemRectMin();
    const ImVec2 l_Max = ImGui::GetItemRectMax();
    const bool l_Hovered = ImGui::IsItemHovered();
    m_CurveFocused = l_View.focused;
    m_ViewBottom = l_View.GetMin().y;
    m_ViewTop = l_View.GetMax().y;
    m_LastPoints.clear();
    for (const ImCurveEdit::EditPoint& it_Point : l_Selected)
    {
        m_LastPoints.push_back({ it_Point.curveIndex, it_Point.pointIndex });
    }

    const auto a_At = [&](float x) { return l_View.ToParameter((x - l_Min.x) / std::max(l_Max.x - l_Min.x, 1.0f) * c_CurveFrames); };
    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    SoundEventAudition& l_Audition = m_Session.GetAudition();
    if (const std::optional<float> l_Heard = l_Audition.IsPlaying(asset) ? l_Audition.GetHeardParameter(std::string(l_ParameterName)) : std::nullopt)
    {
        const float l_X = l_Min.x + l_View.ToFrame(*l_Heard) / c_CurveFrames * (l_Max.x - l_Min.x);
        l_DrawList.AddLine(ImVec2(l_X, l_Min.y), ImVec2(l_X, l_Max.y), ImGui::GetColorU32(ImGuiCol_PlotHistogram), 1.5f);
    }

    if (l_Hovered && !m_CurveEditing && !ImGui::IsPopupOpen("##CurveMenu"))
    {
        const float l_Value = a_At(ImGui::GetMousePos().x);
        std::string l_Text = std::format("{} {}", l_ParameterName, FormatParameter(*l_Parameter, l_Value));
        for (const TargetInfo& it_Target : c_Targets)
        {
            if (const Trinity::SoundCurve* l_Curve = data.FindCurve(container.ID, l_Parameter->ID, it_Target.Target))
            {
                l_Text += std::format("\n{}: {}", it_Target.Name, FormatTarget(it_Target, l_Curve->Evaluate(l_Parameter->Kind == Trinity::SoundParameterKind::Discrete ? std::round(l_Value) : l_Value)));
            }
        }

        ImGui::SetTooltip("%s", l_Text.c_str());
    }

    if (l_Hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        m_CurveMenuAt = a_At(ImGui::GetMousePos().x);
    }

    const std::vector<CurvePoint> l_Chosen = GetChosenPoints(data, container.ID, l_Parameter->ID);
    if (ImGui::BeginPopupContextItem("##CurveMenu"))
    {
        DrawCurveMenu(data, container, *l_Parameter, l_Chosen);
        ImGui::EndPopup();
    }

    // The parameter's range under the editor, a discrete one's labels each at its place
    const float l_Line = ImGui::GetTextLineHeight();
    const ImVec2 l_Axis = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(l_Width, l_Line));
    const ImU32 l_Faint = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    l_DrawList.PushClipRect(l_Axis, ImVec2(l_Axis.x + l_Width, l_Axis.y + l_Line), true);
    if (l_Parameter->Kind == Trinity::SoundParameterKind::Discrete && l_Parameter->Labels.size() > 1)
    {
        for (std::size_t it_Label = 0; it_Label < l_Parameter->Labels.size(); ++it_Label)
        {
            const std::string l_Label(std::string_view(l_Parameter->Labels[it_Label]));
            const float l_Size = ImGui::CalcTextSize(l_Label.c_str()).x;
            const float l_X = l_Axis.x + l_View.ToFrame(static_cast<float>(it_Label)) / c_CurveFrames * l_Width;
            l_DrawList.AddText(ImVec2(std::clamp(l_X - l_Size * 0.5f, l_Axis.x, l_Axis.x + l_Width - l_Size), l_Axis.y), l_Faint, l_Label.c_str());
        }
    }
    else
    {
        const std::string l_Lowest = FormatParameter(*l_Parameter, l_Parameter->GetMinimum());
        const std::string l_Highest = FormatParameter(*l_Parameter, l_Parameter->GetMaximum());
        l_DrawList.AddText(l_Axis, l_Faint, l_Lowest.c_str());
        l_DrawList.AddText(ImVec2(l_Axis.x + (l_Width - ImGui::CalcTextSize(std::string(l_ParameterName).c_str()).x) * 0.5f, l_Axis.y), l_Faint, std::string(l_ParameterName).c_str());
        l_DrawList.AddText(ImVec2(l_Axis.x + l_Width - ImGui::CalcTextSize(l_Highest.c_str()).x, l_Axis.y), l_Faint, l_Highest.c_str());
    }

    l_DrawList.PopClipRect();
    if (l_View.IsChanged())
    {
        Trinity::SoundEventData l_After = data;
        l_After.FindContainer(container.ID)->Curves = l_View.GetCurves();
        Edit(std::move(l_After), std::format("Edit curves of {} by {}", std::string_view(container.Name), l_ParameterName), std::format("Curves {} {}", container.ID, l_Parameter->ID));
    }

    DrawPointFields(data, container, *l_Parameter, l_Chosen);
}

// Points removed, added where it was right-clicked at the curve's value there, and curves set level again
void SoundEventPanel::DrawCurveMenu(const Trinity::SoundEventData& data, const Trinity::SoundContainer& container, const Trinity::SoundParameter& parameter, const std::vector<CurvePoint>& chosen)
{
    const std::vector<Trinity::SoundCurveTarget> l_Shown = GetShownTargets(container, parameter.ID);
    if (ImGui::MenuItem(std::format("{} Remove Chosen Points", Trinity::Icons::c_Trash).c_str(), "Delete", false, !chosen.empty()))
    {
        RemovePoints(data, container.ID, parameter.ID, chosen);
    }

    ImGui::BeginDisabled(l_Shown.empty());
    if (ImGui::BeginMenu(std::format("Add Point at {}", FormatParameter(parameter, m_CurveMenuAt)).c_str()))
    {
        for (const Trinity::SoundCurveTarget it_Target : l_Shown)
        {
            if (ImGui::MenuItem(GetTargetInfo(it_Target).Name))
            {
                AddPoint(data, container.ID, parameter.ID, it_Target, m_CurveMenuAt);
            }
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Reset"))
    {
        for (const Trinity::SoundCurveTarget it_Target : l_Shown)
        {
            if (ImGui::MenuItem(GetTargetInfo(it_Target).Name))
            {
                ResetCurve(data, container.ID, parameter.ID, it_Target);
            }
        }

        ImGui::EndMenu();
    }

    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem("Fit View"))
    {
        m_ViewBottom = -c_CurveMargin;
        m_ViewTop = 1.0f + c_CurveMargin;
    }
}

// The one point chosen, typed: where it is along the parameter, between its neighbours, and its value there, in its curve's units
void SoundEventPanel::DrawPointFields(const Trinity::SoundEventData& data, const Trinity::SoundContainer& container, const Trinity::SoundParameter& parameter, const std::vector<CurvePoint>& chosen)
{
    if (chosen.size() > 1)
    {
        ImGui::TextDisabled("%s", std::format("{} points chosen. Drag one to move them all, or Delete removes them", chosen.size()).c_str());

        return;
    }

    if (chosen.empty())
    {
        ImGui::TextDisabled("Double-click a curve to add a point, and drag points to move them. Drag around points to choose several, and Delete removes those chosen. The wheel zooms and the middle button pans up and down");

        return;
    }

    const std::vector<Trinity::SoundCurveTarget> l_Shown = GetShownTargets(container, parameter.ID);
    const TargetInfo& l_Target = GetTargetInfo(l_Shown[static_cast<std::size_t>(chosen.front().Curve)]);
    const Trinity::SoundCurve& l_Curve = *data.FindCurve(container.ID, parameter.ID, l_Target.Target);
    const std::size_t l_Index = static_cast<std::size_t>(chosen.front().Point);
    const glm::vec2 l_Point = l_Curve.Points[l_Index];
    const bool l_Discrete = parameter.Kind == Trinity::SoundParameterKind::Discrete;
    const float l_Step = l_Discrete ? 1.0f : 0.0f;
    const float l_Lowest = l_Index > 0 ? (l_Discrete ? l_Curve.Points[l_Index - 1].x + l_Step : std::nextafter(l_Curve.Points[l_Index - 1].x, FLT_MAX)) : parameter.GetMinimum();
    const float l_Highest = l_Index + 1 < l_Curve.Points.size() ? (l_Discrete ? l_Curve.Points[l_Index + 1].x - l_Step : std::nextafter(l_Curve.Points[l_Index + 1].x, -FLT_MAX)) : parameter.GetMaximum();
    const auto a_Set = [&](glm::vec2 point, std::string_view what)
    {
        Trinity::SoundEventData l_After = data;
        l_After.FindCurve(container.ID, parameter.ID, l_Target.Target)->Points[l_Index] = point;
        Edit(std::move(l_After), std::format("Move point {} of the {} curve of {} {}", l_Index + 1, l_Target.Name, std::string_view(container.Name), what), std::format("Point {} {} {} {}", container.ID, parameter.ID, l_Target.Name, l_Index));
    };

    ImGui::AlignTextToFramePadding();
    TextLine(std::format("{} point {}", l_Target.Name, l_Index + 1));
    ImGui::SameLine();
    const float l_FieldWidth = ImGui::GetFontSize() * c_SliderWidthInFonts * 0.8f;
    ImGui::SetNextItemWidth(l_FieldWidth);
    if (l_Discrete)
    {
        if (ImGui::BeginCombo("##At", std::string(GetLabel(parameter, l_Point.x)).c_str()))
        {
            for (long it_Label = std::lround(l_Lowest); it_Label <= std::lround(l_Highest); ++it_Label)
            {
                ImGui::PushID(static_cast<int>(it_Label));
                if (ImGui::Selectable(std::string(GetLabel(parameter, static_cast<float>(it_Label))).c_str(), std::lround(l_Point.x) == it_Label))
                {
                    a_Set(glm::vec2(static_cast<float>(it_Label), l_Point.y), "along");
                }

                ImGui::PopID();
            }

            ImGui::EndCombo();
        }
    }
    else
    {
        float l_At = l_Point.x;
        if (ImGui::DragFloat("##At", &l_At, (parameter.GetMaximum() - parameter.GetMinimum()) * 0.001f, l_Lowest, std::max(l_Lowest, l_Highest), std::format("{} %.3f", EscapeFormat(std::string_view(parameter.Name))).c_str(), ImGuiSliderFlags_AlwaysClamp))
        {
            a_Set(glm::vec2(std::clamp(l_At, l_Lowest, std::max(l_Lowest, l_Highest)), l_Point.y), "along");
        }
    }

    ImGui::SetItemTooltip("Where it is along the parameter, between the points beside it. Ctrl+click to type");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(l_FieldWidth);
    float l_Value = l_Point.y;
    if (ImGui::DragFloat("##Value", &l_Value, (l_Target.Maximum - l_Target.Minimum) * 0.001f, l_Target.Minimum, l_Target.Maximum, l_Target.Format, ImGuiSliderFlags_AlwaysClamp | (l_Target.Target == Trinity::SoundCurveTarget::Cutoff ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None)))
    {
        a_Set(glm::vec2(l_Point.x, std::clamp(l_Value, l_Target.Minimum, l_Target.Maximum)), "up or down");
    }

    ImGui::SetItemTooltip("%s", std::format("Its {} there. Ctrl+click to type", l_Target.Name).c_str());
}

// Delete, Ctrl+D and F2 act on what was chosen last while the panel has the focus and nothing is typed into. While the curve editor has it, Delete and Backspace take its chosen points
void SoundEventPanel::ReadShortcuts(const Trinity::SoundEventData& data)
{
    if (!m_Focused || !m_TextKey.empty() || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        return;
    }

    const EditorSession::SoundEventSelection& l_Selection = m_Session.GetSoundEventSelection();
    const bool l_Delete = ImGui::IsKeyChordPressed(ImGuiKey_Delete);
    if (m_CurveFocused)
    {
        const Trinity::SoundContainer* l_Container = data.FindContainer(l_Selection.Container);
        const std::vector<CurvePoint> l_Chosen = l_Container != nullptr ? GetChosenPoints(data, l_Container->ID, l_Selection.Parameter) : std::vector<CurvePoint>();
        if ((l_Delete || ImGui::IsKeyChordPressed(ImGuiKey_Backspace)) && !l_Chosen.empty())
        {
            RemovePoints(data, l_Container->ID, l_Selection.Parameter, l_Chosen);
        }

        return;
    }

    const bool l_Duplicate = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D);
    const bool l_Rename = ImGui::IsKeyChordPressed(ImGuiKey_F2);
    if (m_Chosen == Chosen::Parameter)
    {
        if (data.FindParameter(l_Selection.Parameter) == nullptr)
        {
            return;
        }

        if (l_Delete)
        {
            RemoveParameter(data, l_Selection.Parameter);
        }
        else if (l_Rename)
        {
            BeginRename(Chosen::Parameter, l_Selection.Parameter);
        }

        return;
    }

    const Trinity::SoundContainer* l_Container = data.FindContainer(l_Selection.Container);
    if (l_Container == nullptr)
    {
        return;
    }

    if (l_Rename)
    {
        BeginRename(Chosen::Container, l_Container->ID);
    }
    else if (l_Container->Parent && l_Delete)
    {
        RemoveContainer(data, l_Container->ID);
    }
    else if (l_Container->Parent && l_Duplicate)
    {
        DuplicateContainer(data, l_Container->ID);
    }
}

// Named for its clip, or for its type among the others, and renamed at once unless a clip named it
void SoundEventPanel::AddContainer(const Trinity::SoundEventData& data, Trinity::UUID parent, Trinity::SoundContainerType type, Trinity::UUID clip)
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = clip && l_Registry != nullptr ? l_Registry->Find(clip) : nullptr;
    const Trinity::SoundContainer* l_Parent = data.FindContainer(parent);
    const std::string l_Name = l_Record != nullptr ? GetStem(l_Record->Path) : FreeName(data.Containers, GetTypeInfo(type).Name);
    Trinity::SoundEventData l_After = data;
    const Trinity::SoundContainer* l_Added = l_Parent != nullptr ? l_After.AddContainer(parent, type, l_Name, clip) : nullptr;
    if (l_Added == nullptr)
    {
        return;
    }

    EditorSession::SoundEventSelection l_Select = m_Session.GetSoundEventSelection();
    l_Select.Container = l_Added->ID;
    if (!clip)
    {
        m_FocusKey = std::format("Name {}", l_Added->ID);
    }

    Edit(std::move(l_After), std::format("Add {} {} to {}", GetTypeInfo(type).Name, l_Name, std::string_view(l_Parent->Name)), {}, l_Select, Chosen::Container);
}

void SoundEventPanel::DuplicateContainer(const Trinity::SoundEventData& data, Trinity::UUID container)
{
    const Trinity::SoundContainer* l_Original = data.FindContainer(container);
    if (l_Original == nullptr || !l_Original->Parent)
    {
        return;
    }

    const std::string l_Name = FreeName(data.Containers, std::string_view(l_Original->Name));
    Trinity::SoundEventData l_After = data;
    const Trinity::SoundContainer* l_Copy = l_After.DuplicateContainer(container, l_Name);
    if (l_Copy == nullptr)
    {
        return;
    }

    EditorSession::SoundEventSelection l_Select = m_Session.GetSoundEventSelection();
    l_Select.Container = l_Copy->ID;
    Edit(std::move(l_After), std::format("Duplicate {} as {}", std::string_view(l_Original->Name), l_Name), {}, l_Select, Chosen::Container);
}

// What was chosen in the branch moves up to its parent
void SoundEventPanel::RemoveContainer(const Trinity::SoundEventData& data, Trinity::UUID container)
{
    const Trinity::SoundContainer* l_Container = data.FindContainer(container);
    Trinity::SoundEventData l_After = data;
    if (l_Container == nullptr || !l_After.RemoveContainer(container))
    {
        return;
    }

    EditorSession::SoundEventSelection l_Select = m_Session.GetSoundEventSelection();
    l_Select.Container = l_Container->Parent;
    Edit(std::move(l_After), std::format("Remove {}", std::string_view(l_Container->Name)), {}, l_Select, Chosen::Container);
}

void SoundEventPanel::MoveContainer(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parent, Trinity::UUID before, std::string_view way)
{
    const Trinity::SoundContainer* l_Container = data.FindContainer(container);
    Trinity::SoundEventData l_After = data;
    if (l_Container == nullptr || !l_After.MoveContainer(container, parent, before) || l_After == data)
    {
        return;
    }

    Edit(std::move(l_After), std::format("Move {} {}", std::string_view(l_Container->Name), way));
}

void SoundEventPanel::SetContainerType(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::SoundContainerType type)
{
    const Trinity::SoundContainer* l_Container = data.FindContainer(container);
    Trinity::SoundEventData l_After = data;
    if (l_Container != nullptr && l_After.SetContainerType(container, type))
    {
        Edit(std::move(l_After), std::format("Make {} a {}", std::string_view(l_Container->Name), GetTypeInfo(type).Name));
    }
}

void SoundEventPanel::SetClip(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID clip)
{
    const Trinity::SoundContainer* l_Container = data.FindContainer(container);
    if (l_Container == nullptr || l_Container->Type != Trinity::SoundContainerType::Clip || l_Container->Clip == clip)
    {
        return;
    }

    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = clip && l_Registry != nullptr ? l_Registry->Find(clip) : nullptr;
    Trinity::SoundEventData l_After = data;
    l_After.FindContainer(container)->Clip = clip;
    Edit(std::move(l_After), l_Record != nullptr ? std::format("Play {} in {}", GetStem(l_Record->Path), std::string_view(l_Container->Name)) : std::format("Play no clip in {}", std::string_view(l_Container->Name)));
}

void SoundEventPanel::RenameContainer(const Trinity::SoundEventData& data, Trinity::UUID container, std::string_view name)
{
    const Trinity::SoundContainer* l_Container = data.FindContainer(container);
    if (l_Container == nullptr || std::string_view(l_Container->Name) == name)
    {
        return;
    }

    Trinity::SoundEventData l_After = data;
    l_After.FindContainer(container)->Name = Trinity::TaggedString<Trinity::MemoryTag::Audio>(name);
    Edit(std::move(l_After), std::format("Rename {} to {}", std::string_view(l_Container->Name), name));
    m_Error.clear();
}

// Chosen, and renamed at once
void SoundEventPanel::AddParameter(const Trinity::SoundEventData& data, Trinity::SoundParameterKind kind)
{
    const std::string l_Name = FreeName(data.Parameters, "New Parameter");
    Trinity::SoundEventData l_After = data;
    const Trinity::SoundParameter* l_Added = l_After.AddParameter(l_Name, kind);
    if (l_Added == nullptr)
    {
        return;
    }

    EditorSession::SoundEventSelection l_Select = m_Session.GetSoundEventSelection();
    l_Select.Parameter = l_Added->ID;
    m_FocusKey = std::format("Name {}", l_Added->ID);
    Edit(std::move(l_After), std::format("Add {} parameter {}", kind == Trinity::SoundParameterKind::Continuous ? "continuous" : "discrete", l_Name), {}, l_Select, Chosen::Parameter);
}

// With its curves, and the Switches by it then pick by none
void SoundEventPanel::RemoveParameter(const Trinity::SoundEventData& data, Trinity::UUID parameter)
{
    const Trinity::SoundParameter* l_Parameter = data.FindParameter(parameter);
    Trinity::SoundEventData l_After = data;
    if (l_Parameter == nullptr || !l_After.RemoveParameter(parameter))
    {
        return;
    }

    EditorSession::SoundEventSelection l_Select = m_Session.GetSoundEventSelection();
    l_Select.Parameter = l_After.Parameters.empty() ? Trinity::UUID() : l_After.Parameters.front().ID;
    Edit(std::move(l_After), std::format("Remove parameter {}", std::string_view(l_Parameter->Name)), {}, l_Select, Chosen::Container);
}

// A name another parameter has, or none, which the game would not find it by, is not made, and why shows under the field
void SoundEventPanel::RenameParameter(const Trinity::SoundEventData& data, Trinity::UUID parameter, std::string_view name)
{
    const Trinity::SoundParameter* l_Parameter = data.FindParameter(parameter);
    if (l_Parameter == nullptr || std::string_view(l_Parameter->Name) == name)
    {
        return;
    }

    Trinity::SoundEventData l_After = data;
    if (!l_After.RenameParameter(parameter, name))
    {
        m_Error = name.empty() ? std::string("A parameter needs a name") : std::format("Another parameter is called {}", name);
        m_ErrorKey = std::format("Name {}", parameter);

        return;
    }

    Edit(std::move(l_After), std::format("Rename parameter {} to {}", std::string_view(l_Parameter->Name), name));
    m_Error.clear();
}

void SoundEventPanel::AddCurve(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter, Trinity::SoundCurveTarget target)
{
    const Trinity::SoundContainer* l_Container = data.FindContainer(container);
    const Trinity::SoundParameter* l_Parameter = data.FindParameter(parameter);
    Trinity::SoundEventData l_After = data;
    if (l_Container != nullptr && l_Parameter != nullptr && l_After.AddCurve(container, parameter, target) != nullptr)
    {
        Edit(std::move(l_After), std::format("Add {} curve of {} by {}", GetTargetInfo(target).Name, std::string_view(l_Container->Name), std::string_view(l_Parameter->Name)));
    }
}

void SoundEventPanel::RemoveCurve(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter, Trinity::SoundCurveTarget target)
{
    const Trinity::SoundContainer* l_Container = data.FindContainer(container);
    const Trinity::SoundParameter* l_Parameter = data.FindParameter(parameter);
    Trinity::SoundEventData l_After = data;
    if (l_Container != nullptr && l_Parameter != nullptr && l_After.RemoveCurve(container, parameter, target))
    {
        Edit(std::move(l_After), std::format("Remove {} curve of {} by {}", GetTargetInfo(target).Name, std::string_view(l_Container->Name), std::string_view(l_Parameter->Name)));
    }
}

// Level where it changes nothing again, as it is first added
void SoundEventPanel::ResetCurve(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter, Trinity::SoundCurveTarget target)
{
    const Trinity::SoundContainer* l_Container = data.FindContainer(container);
    const Trinity::SoundParameter* l_Parameter = data.FindParameter(parameter);
    Trinity::SoundEventData l_After = data;
    if (l_Container == nullptr || l_Parameter == nullptr || !l_After.RemoveCurve(container, parameter, target) || l_After.AddCurve(container, parameter, target) == nullptr)
    {
        return;
    }

    // Back in its place among the container's curves
    const auto& l_Before = l_Container->Curves;
    auto& l_Curves = l_After.FindContainer(container)->Curves;
    const std::size_t l_Place = static_cast<std::size_t>(std::ranges::find_if(l_Before, [&](const Trinity::SoundCurve& curve) { return curve.Parameter == parameter && curve.Target == target; }) - l_Before.begin());
    std::rotate(l_Curves.begin() + static_cast<std::ptrdiff_t>(std::min(l_Place, l_Curves.size() - 1)), l_Curves.end() - 1, l_Curves.end());
    if (l_After != data)
    {
        Edit(std::move(l_After), std::format("Reset {} curve of {} by {}", GetTargetInfo(target).Name, std::string_view(l_Container->Name), std::string_view(l_Parameter->Name)));
    }
}

// At the curve's value there, so the curve is the same until the point is moved, where no point already is
void SoundEventPanel::AddPoint(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter, Trinity::SoundCurveTarget target, float value)
{
    const Trinity::SoundParameter* l_Parameter = data.FindParameter(parameter);
    const Trinity::SoundCurve* l_Curve = data.FindCurve(container, parameter, target);
    if (l_Parameter == nullptr || l_Curve == nullptr || l_Curve->Points.size() >= c_MostPoints)
    {
        return;
    }

    const float l_At = l_Parameter->Clamp(value);
    const glm::vec2 l_Point(l_At, l_Curve->Evaluate(l_At));
    Trinity::SoundEventData l_After = data;
    auto& l_Points = l_After.FindCurve(container, parameter, target)->Points;
    const auto a_After = std::ranges::upper_bound(l_Points, l_Point.x, {}, [](const glm::vec2& point) { return point.x; });
    if ((a_After != l_Points.begin() && (a_After - 1)->x >= l_Point.x) || (a_After != l_Points.end() && a_After->x <= l_Point.x))
    {
        return;
    }

    l_Points.insert(a_After, l_Point);
    Edit(std::move(l_After), std::format("Add a point to the {} curve of {}", GetTargetInfo(target).Name, std::string_view(data.FindContainer(container)->Name)));
}

// A curve keeps one point at least, its first chosen
void SoundEventPanel::RemovePoints(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter, const std::vector<CurvePoint>& chosen)
{
    const Trinity::SoundContainer* l_Container = data.FindContainer(container);
    if (l_Container == nullptr || chosen.empty())
    {
        return;
    }

    const std::vector<Trinity::SoundCurveTarget> l_Shown = GetShownTargets(*l_Container, parameter);
    Trinity::SoundEventData l_After = data;
    std::size_t l_Removed = 0;
    for (std::size_t it_Curve = 0; it_Curve < l_Shown.size(); ++it_Curve)
    {
        auto& l_Points = l_After.FindCurve(container, parameter, l_Shown[it_Curve])->Points;
        std::vector<std::size_t> l_Places;
        for (const CurvePoint& it_Point : chosen)
        {
            if (static_cast<std::size_t>(it_Point.Curve) == it_Curve)
            {
                l_Places.push_back(static_cast<std::size_t>(it_Point.Point));
            }
        }

        std::ranges::sort(l_Places);
        if (l_Places.size() == l_Points.size())
        {
            l_Places.erase(l_Places.begin());
        }

        for (auto it_Place = l_Places.rbegin(); it_Place != l_Places.rend(); ++it_Place)
        {
            l_Points.erase(l_Points.begin() + static_cast<std::ptrdiff_t>(*it_Place));
            ++l_Removed;
        }
    }

    if (l_Removed > 0)
    {
        Edit(std::move(l_After), std::format("Remove {} point{} from the curves of {}", l_Removed, l_Removed == 1 ? "" : "s", std::string_view(l_Container->Name)));
    }
}

void SoundEventPanel::BeginRename(Chosen chosen, Trinity::UUID id)
{
    Choose(chosen, id);
    m_FocusKey = std::format("Name {}", id);
}

std::vector<SoundEventPanel::CurvePoint> SoundEventPanel::GetChosenPoints(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter) const
{
    const Trinity::SoundContainer* l_Container = data.FindContainer(container);
    if (l_Container == nullptr)
    {
        return {};
    }

    const std::vector<Trinity::SoundCurveTarget> l_Shown = GetShownTargets(*l_Container, parameter);
    std::vector<CurvePoint> l_Chosen;
    for (const CurvePoint& it_Point : m_LastPoints)
    {
        const Trinity::SoundCurve* l_Curve = it_Point.Curve >= 0 && static_cast<std::size_t>(it_Point.Curve) < l_Shown.size() ? data.FindCurve(container, parameter, l_Shown[static_cast<std::size_t>(it_Point.Curve)]) : nullptr;
        if (l_Curve != nullptr && it_Point.Point >= 0 && static_cast<std::size_t>(it_Point.Point) < l_Curve->Points.size())
        {
            l_Chosen.push_back(it_Point);
        }
    }

    return l_Chosen;
}

std::string SoundEventPanel::GetCurveKey(Trinity::UUID container, Trinity::UUID parameter) const
{
    return std::format("{} {} {}", m_Asset, container, parameter);
}