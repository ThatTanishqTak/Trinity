#include "Panels/InputActionsPanel.hpp"

#include "EditorPayloads.hpp"
#include "Panels/ConsolePanel.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <format>
#include <span>
#include <string_view>

namespace
{
    constexpr float c_LabelWidthInFonts = 7.0f;
    // How far a stick or trigger has to be pushed from where it rested, and from its centre, for Listen to take it
    constexpr float c_ListenPush = 0.5f;

    constexpr std::array c_ActionTypes{ Trinity::InputActionType::Button, Trinity::InputActionType::Axis, Trinity::InputActionType::Vector2 };
    constexpr std::array c_InteractionKinds{ Trinity::InputInteractionKind::None, Trinity::InputInteractionKind::Hold, Trinity::InputInteractionKind::Tap, Trinity::InputInteractionKind::MultiTap };
    constexpr std::array c_ProcessorKinds{ Trinity::InputProcessorKind::RadialDeadzone, Trinity::InputProcessorKind::AxialDeadzone, Trinity::InputProcessorKind::Invert, Trinity::InputProcessorKind::Scale, Trinity::InputProcessorKind::Normalize };
    constexpr std::array c_CompositeKinds{ Trinity::InputCompositeKind::None, Trinity::InputCompositeKind::Axis, Trinity::InputCompositeKind::Vector2 };

    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    // A label on the left and the widget filling the rest of the row
    void Label(const char* label)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(ImGui::GetFontSize() * c_LabelWidthInFonts);
        ImGui::SetNextItemWidth(-FLT_MIN);
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

    bool ContainsIgnoringCase(std::string_view text, std::string_view part)
    {
        return part.empty() || !std::ranges::search(text, part, [](char a_Left, char a_Right) { return std::tolower(static_cast<unsigned char>(a_Left)) == std::tolower(static_cast<unsigned char>(a_Right)); }).empty();
    }

    // A column's title, with its add button at the right when it has one. Whether that was pressed
    bool ColumnHeader(const char* title, const char* addTooltip)
    {
        ImGui::PushFont(Trinity::ImGuiLayer::GetFont(Trinity::UIFont::Bold), 0.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(title);
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

    // A source without its device, as Space for Keyboard/Space, or a dash for none
    std::string ShortName(Trinity::InputSource source)
    {
        const std::string l_Name = Trinity::ToString(source);

        return l_Name.empty() ? std::string("-") : l_Name.substr(l_Name.find('/') + 1);
    }

    // As the actions column shows a binding: its source, or a composite's parts in order after its kind
    std::string Describe(const Trinity::InputBinding& binding)
    {
        const std::span<const std::string_view> l_Parts = Trinity::GetCompositeParts(binding.Composite);
        if (l_Parts.empty())
        {
            const std::string l_Source = Trinity::ToString(binding.Sources[0]);

            return l_Source.empty() ? std::string("No source") : l_Source;
        }

        std::string l_Text = std::format("{}:", Trinity::ToString(binding.Composite));
        for (std::size_t it_Part = 0; it_Part < l_Parts.size(); ++it_Part)
        {
            l_Text += std::format("{}{}", it_Part == 0 ? " " : " / ", ShortName(binding.Sources[it_Part]));
        }

        return l_Text;
    }

    std::string_view GetCompositeLabel(Trinity::InputCompositeKind kind)
    {
        switch (kind)
        {
            case Trinity::InputCompositeKind::None:
            {
                return "Source";
            }
            case Trinity::InputCompositeKind::Axis:
            {
                return "Axis composite";
            }
            case Trinity::InputCompositeKind::Vector2:
            {
                return "Vector2 composite";
            }
        }

        return "Unknown";
    }

    std::string_view GetInteractionTooltip(Trinity::InputInteractionKind kind)
    {
        switch (kind)
        {
            case Trinity::InputInteractionKind::None:
            {
                return "Performs as it is pressed, and never cancels";
            }
            case Trinity::InputInteractionKind::Hold:
            {
                return "Performs once held long enough, and cancels when let go before";
            }
            case Trinity::InputInteractionKind::Tap:
            {
                return "Performs as it is let go soon enough, and cancels once held longer";
            }
            case Trinity::InputInteractionKind::MultiTap:
            {
                return "Performs on the last of its taps, and cancels when a tap is too long or the next too late";
            }
        }

        return {};
    }

    // A name new among the items': the base, or the base with the first number free after it
    template<typename Item>
    std::string FreeName(const Trinity::InputVector<Item>& items, std::string_view base)
    {
        const auto a_Taken = [&items](std::string_view name) { return std::ranges::any_of(items, [name](const Item& item) { return std::string_view(item.Name) == name; }); };
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

    // Why the item at that index cannot be called this among the others, as a file refuses, or empty when it can
    template<typename Item>
    std::string CheckName(const Trinity::InputVector<Item>& items, int self, std::string_view name, std::string_view what)
    {
        if (name.empty())
        {
            return std::format("A {} needs a name", what);
        }

        if (name.find(Trinity::InputActionsData::c_PathSeparator) != std::string_view::npos)
        {
            return std::format("A {} name has no '/', which a path puts between a map and its action", what);
        }

        for (std::size_t it_Index = 0; it_Index < items.size(); ++it_Index)
        {
            if (static_cast<int>(it_Index) != self && std::string_view(items[it_Index].Name) == name)
            {
                return std::format("Another {} is called {}", what, name);
            }
        }

        return {};
    }

    template<typename Item>
    void MoveItem(Trinity::InputVector<Item>& items, int index, int by)
    {
        std::swap(items[static_cast<std::size_t>(index)], items[static_cast<std::size_t>(index + by)]);
    }

    // The first key but Escape, mouse button but the left, or gamepad button that went down this frame, or else stick or trigger pushed from where it rested. A stick is taken whole for a Vector2 action's own binding, and as the axis pushed otherwise
    std::optional<Trinity::InputSource> FindPushed(const std::array<std::array<float, std::to_underlying(Trinity::GamepadAxis::Count)>, Trinity::Input::c_MaxGamepads>& rest, bool wholeStick)
    {
        for (std::uint16_t it_Key = 0; it_Key < std::to_underlying(Trinity::KeyCode::Count); ++it_Key)
        {
            if (it_Key != std::to_underlying(Trinity::KeyCode::TR_ESCAPE) && Trinity::Input::WasKeyPressed(static_cast<Trinity::KeyCode>(it_Key)))
            {
                return Trinity::InputSource{ Trinity::InputSourceKind::Key, it_Key };
            }
        }

        for (std::uint8_t it_Button = std::to_underlying(Trinity::MouseCode::TR_RIGHT); it_Button < std::to_underlying(Trinity::MouseCode::Count); ++it_Button)
        {
            if (Trinity::Input::WasMouseButtonPressed(static_cast<Trinity::MouseCode>(it_Button)))
            {
                return Trinity::InputSource{ Trinity::InputSourceKind::MouseButton, it_Button };
            }
        }

        for (std::uint32_t it_Gamepad = 0; it_Gamepad < Trinity::Input::c_MaxGamepads; ++it_Gamepad)
        {
            if (!Trinity::Input::IsGamepadConnected(it_Gamepad))
            {
                continue;
            }

            for (std::uint8_t it_Button = 0; it_Button < std::to_underlying(Trinity::GamepadButton::Count); ++it_Button)
            {
                if (Trinity::Input::WasGamepadButtonPressed(it_Gamepad, static_cast<Trinity::GamepadButton>(it_Button)))
                {
                    return Trinity::InputSource{ Trinity::InputSourceKind::GamepadButton, it_Button };
                }
            }

            for (std::uint8_t it_Axis = 0; it_Axis < std::to_underlying(Trinity::GamepadAxis::Count); ++it_Axis)
            {
                const float l_Value = Trinity::Input::GetGamepadAxis(it_Gamepad, static_cast<Trinity::GamepadAxis>(it_Axis));
                if (std::abs(l_Value) < c_ListenPush || std::abs(l_Value - rest[it_Gamepad][it_Axis]) < c_ListenPush)
                {
                    continue;
                }

                const bool l_Stick = it_Axis <= std::to_underlying(Trinity::GamepadAxis::TR_RIGHT_STICK_Y);
                if (wholeStick && l_Stick)
                {
                    return Trinity::InputSource{ Trinity::InputSourceKind::GamepadStick, static_cast<std::uint16_t>(it_Axis < std::to_underlying(Trinity::GamepadAxis::TR_RIGHT_STICK_X) ? 0 : 1) };
                }

                return Trinity::InputSource{ Trinity::InputSourceKind::GamepadAxis, it_Axis };
            }
        }

        return std::nullopt;
    }
}

InputActionsPanel::InputActionsPanel(EditorSession& session) : Panel("Input Actions", Trinity::Icons::c_Gamepad, DockSlot::Bottom), m_Session(session)
{

}

void InputActionsPanel::SetAsset(Trinity::UUID asset)
{
    m_Asset = asset;
}

// The asset opened, while the project has it, and else the project's default
Trinity::UUID InputActionsPanel::GetShownAsset() const
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = l_Registry != nullptr && m_Asset ? l_Registry->Find(m_Asset) : nullptr;
    if (l_Record != nullptr && l_Record->Importer == Trinity::InputActionsAsset::c_AssetType)
    {
        return m_Asset;
    }

    const Trinity::Project* l_Project = m_Session.GetProject();

    return l_Project != nullptr ? l_Project->GetInputActions() : Trinity::UUID();
}

// Edits are made once the panel has drawn, so the data it drew from stays as it was until then. A selection follows the edit only once it has been made
void InputActionsPanel::OnImGuiRender()
{
    m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const Trinity::UUID l_Asset = m_Session.HasProject() ? GetShownAsset() : Trinity::UUID();
    const EditorSession::InputActionsFile* l_File = l_Asset ? m_Session.ReadInputActions(l_Asset) : nullptr;
    const Trinity::AssetRecord* l_Record = l_File != nullptr ? m_Session.GetRegistry()->Find(l_Asset) : nullptr;
    if (l_Asset != m_Shown)
    {
        m_Shown = l_Asset;
        m_Selected = {};
        m_Listening.reset();
        m_NameEdited = false;
    }

    if (l_File == nullptr || l_Record == nullptr)
    {
        m_Listening.reset();
        ImGui::TextDisabled("%s", m_Session.HasProject() ? "Double-click an input actions asset in the Content Browser to edit it here. Create > Input Actions there makes one" : "Open or create a project to edit its input actions");

        return;
    }

    DrawHeader(l_Asset, *l_Record);
    if (!l_File->Data)
    {
        m_Listening.reset();
        ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Error));
        ImGui::TextWrapped("%s", l_File->Error.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Once the file is fixed, it is read again as Forge gets the focus back, or at F5");

        return;
    }

    const Trinity::InputActionsData& l_Data = *l_File->Data;
    Clamp(l_Data);
    UpdateListen(l_Data);
    ReadShortcuts(l_Data);

    if (ImGui::BeginTable("##InputActions", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp, ImGui::GetContentRegionAvail()))
    {
        ImGui::TableSetupColumn("Maps", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##Maps"))
        {
            DrawMaps(l_Data);
        }

        ImGui::EndChild();

        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##Actions"))
        {
            DrawActions(l_Data);
        }

        ImGui::EndChild();

        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##Details"))
        {
            DrawDetails(l_Data);
        }

        ImGui::EndChild();
        ImGui::EndTable();
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
        const bool l_Made = l_Pending.Rename ? m_Session.RenameInputActions(l_Asset, l_Data, l_Pending.After, l_Pending.What) : m_Session.EditInputActions(l_Asset, l_Data, l_Pending.After, l_Pending.What, std::move(l_Pending.Field));
        if (l_Made && l_Pending.Select)
        {
            m_Selected = *l_Pending.Select;
            m_NameEdited = false;
        }
    }
}

// What is chosen stays inside what the asset has as an undo or a reload takes things away, and the first map is chosen once there is one
void InputActionsPanel::Clamp(const Trinity::InputActionsData& data)
{
    const int l_Maps = static_cast<int>(data.Maps.size());
    m_Selected.Map = l_Maps == 0 ? -1 : std::clamp(m_Selected.Map, 0, l_Maps - 1);
    if (m_Selected.Map < 0)
    {
        m_Selected = {};

        return;
    }

    const int l_Actions = static_cast<int>(data.Maps[static_cast<std::size_t>(m_Selected.Map)].Actions.size());
    m_Selected.Action = std::min(m_Selected.Action, l_Actions - 1);
    if (m_Selected.Action < 0)
    {
        m_Selected.Action = -1;
        m_Selected.Binding = -1;

        return;
    }

    const int l_Bindings = static_cast<int>(data.Maps[static_cast<std::size_t>(m_Selected.Map)].Actions[static_cast<std::size_t>(m_Selected.Action)].Bindings.size());
    m_Selected.Binding = std::max(-1, std::min(m_Selected.Binding, l_Bindings - 1));
}

void InputActionsPanel::Edit(Trinity::InputActionsData after, std::string what, std::string field, std::optional<Selection> select)
{
    m_Pending = Pending{ std::move(after), std::move(what), std::move(field), select, false };
}

void InputActionsPanel::Rename(Trinity::InputActionsData after, std::string what)
{
    m_Pending = Pending{ std::move(after), std::move(what),{}, std::nullopt, true };
}

// A rename typed for what was chosen is made before another is chosen, since its field is not drawn again to say it was left
void InputActionsPanel::Select(const Trinity::InputActionsData& data, Selection selection)
{
    if (selection == m_Selected)
    {
        return;
    }

    if (m_NameEdited)
    {
        CommitName(data);
    }

    m_Selected = selection;
    m_NameEdited = false;
    m_NameError.clear();
}

void InputActionsPanel::DrawHeader(Trinity::UUID asset, const Trinity::AssetRecord& record)
{
    TextLine(std::format("{} {}", Trinity::Icons::c_Gamepad, record.Path));
    ImGui::SameLine();
    const Trinity::Project* l_Project = m_Session.GetProject();
    if (l_Project != nullptr && l_Project->GetInputActions() == asset)
    {
        ImGui::TextDisabled("the project's default");
        ImGui::SetItemTooltip("Play reads it, and enables its first map for each player");
    }
    else if (ImGui::SmallButton("Set as Default"))
    {
        m_Session.SetDefaultInputActions(asset);
    }

    const float l_SearchWidth = ImGui::GetFontSize() * 14.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - l_SearchWidth));
    ImGui::SetNextItemWidth(l_SearchWidth);
    ImGui::InputTextWithHint("##Search", std::format("{} Search", Trinity::Icons::c_Search).c_str(), &m_Search, ImGuiInputTextFlags_EscapeClearsAll);
    ImGui::SetItemTooltip("Actions whose name or bindings have this");

    if (m_Listening)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Warn));
        ImGui::TextWrapped("Listening: press a key, a mouse button other than the left, or a gamepad button, or push a stick or trigger. Escape or a click stops it");
        ImGui::PopStyleColor();
    }
    else if (m_Session.IsPlaying())
    {
        ImGui::TextDisabled("The scene playing reads each edit at once, and the asset keeps it once the scene stops");
    }
}

void InputActionsPanel::DrawMaps(const Trinity::InputActionsData& data)
{
    if (ColumnHeader("Maps", "Add a map"))
    {
        AddMap(data);
    }

    if (data.Maps.empty())
    {
        ImGui::TextDisabled("No maps yet. + adds one");
    }

    for (std::size_t it_Map = 0; it_Map < data.Maps.size(); ++it_Map)
    {
        const int l_Map = static_cast<int>(it_Map);
        ImGui::PushID(l_Map);
        if (ImGui::Selectable(std::format("{}###Map", std::string_view(data.Maps[it_Map].Name)).c_str(), m_Selected.Map == l_Map, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap))
        {
            Select(data, { l_Map, -1, -1 });
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_FocusNameFrame = ImGui::GetFrameCount() + 1;
            }
        }

        if (ImGui::BeginPopupContextItem("##MapMenu"))
        {
            const Selection l_This{ l_Map, -1, -1 };
            if (ImGui::MenuItem("Add Action"))
            {
                AddAction(data, l_Map);
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Rename", "F2"))
            {
                Select(data, l_This);
                m_FocusNameFrame = ImGui::GetFrameCount() + 1;
            }

            if (ImGui::MenuItem(std::format("{} Duplicate", Trinity::Icons::c_Copy).c_str(), "Ctrl+D"))
            {
                Duplicate(data, l_This);
            }

            if (ImGui::MenuItem("Move Up", nullptr, false, l_Map > 0))
            {
                Move(data, l_This, -1);
            }

            if (ImGui::MenuItem("Move Down", nullptr, false, l_Map + 1 < static_cast<int>(data.Maps.size())))
            {
                Move(data, l_This, 1);
            }

            if (ImGui::MenuItem(std::format("{} Delete", Trinity::Icons::c_Trash).c_str(), "Delete"))
            {
                Remove(data, l_This);
            }

            ImGui::EndPopup();
        }

        if (it_Map == 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("first");
            ImGui::SetItemTooltip("The map Play enables for each player");
        }

        ImGui::PopID();
    }
}

void InputActionsPanel::DrawActions(const Trinity::InputActionsData& data)
{
    if (m_Selected.Map < 0)
    {
        ColumnHeader("Actions", nullptr);
        ImGui::TextDisabled("Add a map first");

        return;
    }

    if (ColumnHeader("Actions", "Add an action"))
    {
        AddAction(data, m_Selected.Map);
    }

    const Trinity::InputActionMap& l_Map = data.Maps[static_cast<std::size_t>(m_Selected.Map)];
    if (l_Map.Actions.empty())
    {
        ImGui::TextDisabled("No actions yet. + adds one");
    }

    bool l_Shown = false;
    for (std::size_t it_Action = 0; it_Action < l_Map.Actions.size(); ++it_Action)
    {
        const Trinity::InputActionDefinition& l_Action = l_Map.Actions[it_Action];
        const bool l_Matches = ContainsIgnoringCase(l_Action.Name, m_Search) || std::ranges::any_of(l_Action.Bindings, [this](const Trinity::InputBinding& binding) { return ContainsIgnoringCase(Describe(binding), m_Search); });
        if (!l_Matches)
        {
            continue;
        }

        l_Shown = true;
        const int l_Index = static_cast<int>(it_Action);
        const Selection l_This{ m_Selected.Map, l_Index, -1 };
        ImGui::PushID(l_Index);
        ImGuiTreeNodeFlags l_Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap;
        l_Flags |= l_Action.Bindings.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_None;
        l_Flags |= m_Selected == l_This ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None;
        if (!m_Search.empty())
        {
            ImGui::SetNextItemOpen(true);
        }

        const bool l_Open = ImGui::TreeNodeEx("##Action", l_Flags, "%s", l_Action.Name.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            Select(data, l_This);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_FocusNameFrame = ImGui::GetFrameCount() + 1;
            }
        }

        // Onto a script's input action field in Properties, which takes an action of the project's default asset
        if (ImGui::BeginDragDropSource())
        {
            const std::string l_Path = std::format("{}{}{}", std::string_view(l_Map.Name), Trinity::InputActionsData::c_PathSeparator, std::string_view(l_Action.Name));
            const std::string l_Payload = MakeInputActionPayload(GetShownAsset().GetValue(), l_Path);
            ImGui::SetDragDropPayload(c_InputActionPayload, l_Payload.data(), l_Payload.size());
            ImGui::TextUnformatted(std::format("{} {}", Trinity::Icons::c_Gamepad, l_Path).c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginPopupContextItem("##ActionMenu"))
        {
            if (ImGui::MenuItem("Add Binding"))
            {
                AddBinding(data, l_This, Trinity::InputCompositeKind::None);
            }

            if (ImGui::MenuItem("Add Axis Composite"))
            {
                AddBinding(data, l_This, Trinity::InputCompositeKind::Axis);
            }

            if (ImGui::MenuItem("Add Vector2 Composite"))
            {
                AddBinding(data, l_This, Trinity::InputCompositeKind::Vector2);
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Rename", "F2"))
            {
                Select(data, l_This);
                m_FocusNameFrame = ImGui::GetFrameCount() + 1;
            }

            if (ImGui::MenuItem("Copy Path"))
            {
                ImGui::SetClipboardText(std::format("{}/{}", std::string_view(l_Map.Name), std::string_view(l_Action.Name)).c_str());
            }

            if (ImGui::MenuItem(std::format("{} Duplicate", Trinity::Icons::c_Copy).c_str(), "Ctrl+D"))
            {
                Duplicate(data, l_This);
            }

            if (ImGui::MenuItem("Move Up", nullptr, false, l_Index > 0))
            {
                Move(data, l_This, -1);
            }

            if (ImGui::MenuItem("Move Down", nullptr, false, l_Index + 1 < static_cast<int>(l_Map.Actions.size())))
            {
                Move(data, l_This, 1);
            }

            if (ImGui::MenuItem(std::format("{} Delete", Trinity::Icons::c_Trash).c_str(), "Delete"))
            {
                Remove(data, l_This);
            }

            ImGui::EndPopup();
        }

        ImGui::SameLine();
        const std::string_view l_Kind = l_Action.Interaction.Kind == Trinity::InputInteractionKind::None ? std::string_view() : Trinity::ToString(l_Action.Interaction.Kind);
        ImGui::TextDisabled("%s", std::format("{}{}{}", Trinity::ToString(l_Action.Type), l_Kind.empty() ? "" : ", ", l_Kind).c_str());

        if (l_Open)
        {
            for (std::size_t it_Binding = 0; it_Binding < l_Action.Bindings.size(); ++it_Binding)
            {
                const Selection l_Binding{ m_Selected.Map, l_Index, static_cast<int>(it_Binding) };
                ImGui::PushID(static_cast<int>(it_Binding));
                const ImGuiTreeNodeFlags l_BindingFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth | (m_Selected == l_Binding ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);
                ImGui::TreeNodeEx("##Binding", l_BindingFlags, "%s", Describe(l_Action.Bindings[it_Binding]).c_str());
                if (ImGui::IsItemClicked())
                {
                    Select(data, l_Binding);
                }

                if (ImGui::BeginPopupContextItem("##BindingMenu"))
                {
                    const bool l_Single = l_Action.Bindings[it_Binding].Composite == Trinity::InputCompositeKind::None;
                    if (ImGui::MenuItem("Listen", nullptr, false, l_Single))
                    {
                        Select(data, l_Binding);
                        BeginListen(l_Binding, 0);
                    }

                    if (ImGui::MenuItem(std::format("{} Duplicate", Trinity::Icons::c_Copy).c_str(), "Ctrl+D"))
                    {
                        Duplicate(data, l_Binding);
                    }

                    if (ImGui::MenuItem("Move Up", nullptr, false, it_Binding > 0))
                    {
                        Move(data, l_Binding, -1);
                    }

                    if (ImGui::MenuItem("Move Down", nullptr, false, it_Binding + 1 < l_Action.Bindings.size()))
                    {
                        Move(data, l_Binding, 1);
                    }

                    if (ImGui::MenuItem(std::format("{} Delete", Trinity::Icons::c_Trash).c_str(), "Delete"))
                    {
                        Remove(data, l_Binding);
                    }

                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (!l_Shown && !l_Map.Actions.empty())
    {
        ImGui::TextDisabled("%s", std::format("No action of {} has \"{}\" in its name or bindings", std::string_view(l_Map.Name), m_Search).c_str());
    }
}

// Each kind of thing chosen in a scope of its own, so a field left half typed for one is never taken as typed for the next
void InputActionsPanel::DrawDetails(const Trinity::InputActionsData& data)
{
    ImGui::PushID(m_Selected.Map);
    ImGui::PushID(m_Selected.Action);
    ImGui::PushID(m_Selected.Binding);
    if (m_Selected.Binding >= 0)
    {
        DrawBindingDetails(data);
    }
    else if (m_Selected.Action >= 0)
    {
        DrawActionDetails(data);
    }
    else if (m_Selected.Map >= 0)
    {
        DrawMapDetails(data);
    }
    else
    {
        ColumnHeader("Details", nullptr);
        ImGui::TextDisabled("Add a map to give it actions");
    }

    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
}

void InputActionsPanel::DrawMapDetails(const Trinity::InputActionsData& data)
{
    const Trinity::InputActionMap& l_Map = data.Maps[static_cast<std::size_t>(m_Selected.Map)];
    ColumnHeader(std::format("Map {}", std::string_view(l_Map.Name)).c_str(), nullptr);
    DrawName(data, l_Map.Name);
    ImGui::TextDisabled("%s", std::format("{} action(s)", l_Map.Actions.size()).c_str());
    ImGui::TextWrapped("%s", m_Selected.Map == 0 ? "The first map, which Play enables for each player" : std::format("Enabled by a script with InputActions.EnableMap(\"{}\")", std::string_view(l_Map.Name)).c_str());
    if (ImGui::Button("Add Action"))
    {
        AddAction(data, m_Selected.Map);
    }
}

void InputActionsPanel::DrawActionDetails(const Trinity::InputActionsData& data)
{
    const Trinity::InputActionMap& l_Map = data.Maps[static_cast<std::size_t>(m_Selected.Map)];
    const Trinity::InputActionDefinition& l_Action = l_Map.Actions[static_cast<std::size_t>(m_Selected.Action)];
    const std::string l_Path = std::format("{}/{}", std::string_view(l_Map.Name), std::string_view(l_Action.Name));
    ColumnHeader(std::format("Action {}", l_Path).c_str(), nullptr);
    DrawName(data, l_Action.Name);

    Label("Type");
    if (ImGui::BeginCombo("##Type", Trinity::ToString(l_Action.Type).data()))
    {
        for (const Trinity::InputActionType it_Type : c_ActionTypes)
        {
            if (ImGui::Selectable(Trinity::ToString(it_Type).data(), it_Type == l_Action.Type) && it_Type != l_Action.Type)
            {
                Trinity::InputActionsData l_After = data;
                l_After.Maps[static_cast<std::size_t>(m_Selected.Map)].Actions[static_cast<std::size_t>(m_Selected.Action)].Type = it_Type;
                Edit(std::move(l_After), std::format("Make {} a {}", l_Path, Trinity::ToString(it_Type)));
            }

            ImGui::SetItemTooltip("%s", it_Type == Trinity::InputActionType::Button ? "Pressed or not, with how hard from 0 to 1" : it_Type == Trinity::InputActionType::Axis ? "From -1 to 1" : "An X and a Y, each from -1 to 1");
        }

        ImGui::EndCombo();
    }

    DrawInteraction(data);

    ImGui::SeparatorText("Bindings");
    ImGui::TextDisabled("%s", std::format("{} binding(s), the strongest of which wins", l_Action.Bindings.size()).c_str());
    const Selection l_This{ m_Selected.Map, m_Selected.Action, -1 };
    if (ImGui::Button("Add Binding"))
    {
        AddBinding(data, l_This, Trinity::InputCompositeKind::None);
    }

    ImGui::SameLine();
    if (ImGui::Button("Add Axis Composite"))
    {
        AddBinding(data, l_This, Trinity::InputCompositeKind::Axis);
    }

    ImGui::SetItemTooltip("An axis from a negative and a positive button, such as Q and E");
    ImGui::SameLine();
    if (ImGui::Button("Add Vector2 Composite"))
    {
        AddBinding(data, l_This, Trinity::InputCompositeKind::Vector2);
    }

    ImGui::SetItemTooltip("A Vector2 from four buttons, such as W, S, A and D");

    ImGui::Spacing();
    ImGui::TextDisabled("%s", std::format("Scripts find it as InputAction.Find(\"{}\")", l_Path).c_str());
}

void InputActionsPanel::DrawInteraction(const Trinity::InputActionsData& data)
{
    const Trinity::InputActionMap& l_Map = data.Maps[static_cast<std::size_t>(m_Selected.Map)];
    const Trinity::InputActionDefinition& l_Action = l_Map.Actions[static_cast<std::size_t>(m_Selected.Action)];
    const Trinity::InputInteraction& l_Interaction = l_Action.Interaction;
    const std::string l_Path = std::format("{}/{}", std::string_view(l_Map.Name), std::string_view(l_Action.Name));
    const std::string l_Field = std::format("{}/Interaction/", l_Path);
    const auto a_Edited = [&](const Trinity::InputInteraction& interaction, std::string what, std::string field)
    {
        Trinity::InputActionsData l_After = data;
        l_After.Maps[static_cast<std::size_t>(m_Selected.Map)].Actions[static_cast<std::size_t>(m_Selected.Action)].Interaction = interaction;
        Edit(std::move(l_After), std::move(what), std::move(field));
    };

    Label("Interaction");
    if (ImGui::BeginCombo("##Interaction", Trinity::ToString(l_Interaction.Kind).data()))
    {
        for (const Trinity::InputInteractionKind it_Kind : c_InteractionKinds)
        {
            if (ImGui::Selectable(Trinity::ToString(it_Kind).data(), it_Kind == l_Interaction.Kind) && it_Kind != l_Interaction.Kind)
            {
                a_Edited(Trinity::GetDefaultInteraction(it_Kind), std::format("Give {} {} interaction", l_Path, it_Kind == Trinity::InputInteractionKind::None ? std::string_view("no") : std::string_view(std::format("a {}", Trinity::ToString(it_Kind)))), {});
            }

            ImGui::SetItemTooltip("%s", GetInteractionTooltip(it_Kind).data());
        }

        ImGui::EndCombo();
    }

    Trinity::InputInteraction l_Edited = l_Interaction;
    switch (l_Interaction.Kind)
    {
        case Trinity::InputInteractionKind::None:
        {
            break;
        }
        case Trinity::InputInteractionKind::Hold:
        case Trinity::InputInteractionKind::Tap:
        {
            Label(l_Interaction.Kind == Trinity::InputInteractionKind::Hold ? "Hold for" : "Let go within");
            if (ImGui::DragFloat("##Duration", &l_Edited.Duration, 0.01f, 0.01f, 10.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp))
            {
                a_Edited(l_Edited, std::format("Set the {} time of {}", Trinity::ToString(l_Interaction.Kind), l_Path), l_Field + "Duration");
            }

            break;
        }
        case Trinity::InputInteractionKind::MultiTap:
        {
            int l_Count = static_cast<int>(l_Interaction.Count);
            Label("Taps");
            if (ImGui::DragInt("##Count", &l_Count, 0.05f, 2, 10, "%d", ImGuiSliderFlags_AlwaysClamp))
            {
                l_Edited.Count = static_cast<std::uint32_t>(l_Count);
                a_Edited(l_Edited, std::format("Set the taps of {}", l_Path), l_Field + "Count");
            }

            Label("Each within");
            if (ImGui::DragFloat("##Duration", &l_Edited.Duration, 0.01f, 0.01f, 10.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp))
            {
                a_Edited(l_Edited, std::format("Set the tap time of {}", l_Path), l_Field + "Duration");
            }

            Label("Apart by up to");
            if (ImGui::DragFloat("##Spacing", &l_Edited.Spacing, 0.01f, 0.01f, 10.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp))
            {
                a_Edited(l_Edited, std::format("Set the time between taps of {}", l_Path), l_Field + "Spacing");
            }

            break;
        }
    }

    if (l_Interaction.Kind != Trinity::InputInteractionKind::None)
    {
        ImGui::TextDisabled("WasPerformed and WasCanceled read it, and ReadProgress how far it has come");
    }
}

void InputActionsPanel::DrawBindingDetails(const Trinity::InputActionsData& data)
{
    const Trinity::InputActionMap& l_Map = data.Maps[static_cast<std::size_t>(m_Selected.Map)];
    const Trinity::InputActionDefinition& l_Action = l_Map.Actions[static_cast<std::size_t>(m_Selected.Action)];
    const Trinity::InputBinding& l_Binding = l_Action.Bindings[static_cast<std::size_t>(m_Selected.Binding)];
    const std::string l_Path = std::format("{}/{}", std::string_view(l_Map.Name), std::string_view(l_Action.Name));
    ColumnHeader(std::format("Binding of {}", l_Path).c_str(), nullptr);

    Label("Kind");
    if (ImGui::BeginCombo("##Kind", GetCompositeLabel(l_Binding.Composite).data()))
    {
        for (const Trinity::InputCompositeKind it_Kind : c_CompositeKinds)
        {
            if (ImGui::Selectable(GetCompositeLabel(it_Kind).data(), it_Kind == l_Binding.Composite) && it_Kind != l_Binding.Composite)
            {
                Trinity::InputActionsData l_After = data;
                Trinity::InputBinding& l_Edited = l_After.Maps[static_cast<std::size_t>(m_Selected.Map)].Actions[static_cast<std::size_t>(m_Selected.Action)].Bindings[static_cast<std::size_t>(m_Selected.Binding)];
                l_Edited.Composite = it_Kind;
                l_Edited.Sources = {};
                Edit(std::move(l_After), std::format("Make a binding of {} a {}", l_Path, GetCompositeLabel(it_Kind)));
            }
        }

        ImGui::EndCombo();
    }

    const std::span<const std::string_view> l_Parts = Trinity::GetCompositeParts(l_Binding.Composite);
    if (l_Parts.empty())
    {
        DrawSource(data, 0, "Source");
    }

    for (std::size_t it_Part = 0; it_Part < l_Parts.size(); ++it_Part)
    {
        DrawSource(data, it_Part, std::string(l_Parts[it_Part]).c_str());
    }

    DrawProcessors(data);
}

// The source's name opens a list to pick it from, and Listen takes the next input pressed
void InputActionsPanel::DrawSource(const Trinity::InputActionsData& data, std::size_t part, const char* label)
{
    const Trinity::InputBinding& l_Binding = data.Maps[static_cast<std::size_t>(m_Selected.Map)].Actions[static_cast<std::size_t>(m_Selected.Action)].Bindings[static_cast<std::size_t>(m_Selected.Binding)];
    const Trinity::InputSource l_Source = l_Binding.Sources[part];
    const std::string l_Name = Trinity::ToString(l_Source);
    const bool l_Listening = m_Listening && m_Listening->Binding == m_Selected && m_Listening->Part == part;
    ImGui::PushID(static_cast<int>(part));

    Label(label);
    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_ListenWidth = ImGui::CalcTextSize("Cancel").x + l_Style.FramePadding.x * 2.0f;
    const float l_PickerWidth = std::max(ImGui::GetFontSize() * 4.0f, ImGui::GetContentRegionAvail().x - l_ListenWidth - l_Style.ItemSpacing.x);
    if (ImGui::Button(std::format("{}###Source", l_Name.empty() ? std::string_view("None") : std::string_view(l_Name)).c_str(), ImVec2(l_PickerWidth, 0.0f)))
    {
        m_PickerFilter.clear();
        ImGui::OpenPopup("##SourcePicker");
    }

    // Kept off keyboard navigation, so the key Listen takes never presses it again
    ImGui::SameLine();
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    if (l_Listening)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }

    if (ImGui::Button(l_Listening ? "Cancel###Listen" : "Listen###Listen", ImVec2(l_ListenWidth, 0.0f)))
    {
        if (l_Listening)
        {
            m_Listening.reset();
        }
        else
        {
            BeginListen(m_Selected, part);
        }
    }

    if (l_Listening)
    {
        ImGui::PopStyleColor();
    }

    ImGui::PopItemFlag();
    ImGui::SetItemTooltip("Takes the next key, mouse button other than the left, or gamepad button, or a stick or trigger pushed past half");

    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 24.0f));
    if (ImGui::BeginPopup("##SourcePicker"))
    {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::InputTextWithHint("##Filter", std::format("{} Search", Trinity::Icons::c_Search).c_str(), &m_PickerFilter);
        if (ImGui::Selectable("None", l_Source.Kind == Trinity::InputSourceKind::None))
        {
            Bind(data, m_Selected, part, {});
        }

        std::string l_Device;
        for (const Trinity::InputSource it_Source : Trinity::GetInputSources())
        {
            const std::string l_Text = Trinity::ToString(it_Source);
            if (!ContainsIgnoringCase(l_Text, m_PickerFilter))
            {
                continue;
            }

            if (std::string_view l_This = std::string_view(l_Text).substr(0, l_Text.find('/')); l_This != l_Device)
            {
                l_Device = l_This;
                ImGui::SeparatorText(l_Device.c_str());
            }

            if (ImGui::Selectable(l_Text.c_str(), it_Source == l_Source))
            {
                Bind(data, m_Selected, part, it_Source);
            }
        }

        ImGui::EndPopup();
    }

    ImGui::PopID();
}

void InputActionsPanel::DrawProcessors(const Trinity::InputActionsData& data)
{
    const Trinity::InputActionMap& l_Map = data.Maps[static_cast<std::size_t>(m_Selected.Map)];
    const Trinity::InputActionDefinition& l_Action = l_Map.Actions[static_cast<std::size_t>(m_Selected.Action)];
    const Trinity::InputBinding& l_Binding = l_Action.Bindings[static_cast<std::size_t>(m_Selected.Binding)];
    const std::string l_Path = std::format("{}/{}", std::string_view(l_Map.Name), std::string_view(l_Action.Name));
    const auto a_Edited = [&](const auto& change, std::string what, std::string field)
    {
        Trinity::InputActionsData l_After = data;
        change(l_After.Maps[static_cast<std::size_t>(m_Selected.Map)].Actions[static_cast<std::size_t>(m_Selected.Action)].Bindings[static_cast<std::size_t>(m_Selected.Binding)].Processors);
        Edit(std::move(l_After), std::move(what), std::move(field));
    };

    ImGui::SeparatorText("Processors");
    if (l_Binding.Processors.empty())
    {
        ImGui::TextDisabled("None yet. Each is applied to the binding's value in turn");
    }

    for (std::size_t it_Processor = 0; it_Processor < l_Binding.Processors.size(); ++it_Processor)
    {
        const Trinity::InputProcessor& l_Processor = l_Binding.Processors[it_Processor];
        const std::string l_Field = std::format("{}/Binding {}/Processor {}/", l_Path, m_Selected.Binding, it_Processor);
        const std::string_view l_Kind = Trinity::ToString(l_Processor.Kind);
        const int l_Index = static_cast<int>(it_Processor);
        ImGui::PushID(l_Index);

        ImGui::AlignTextToFramePadding();
        TextLine(l_Kind);
        const float l_Button = ImGui::GetFrameHeight();
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - l_Button * 3.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f));
        ImGui::BeginDisabled(it_Processor == 0);
        if (ImGui::ArrowButton("##Up", ImGuiDir_Up))
        {
            a_Edited([l_Index](auto& processors) { MoveItem(processors, l_Index, -1); }, std::format("Move a {} processor of {} up", l_Kind, l_Path), {});
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(it_Processor + 1 == l_Binding.Processors.size());
        if (ImGui::ArrowButton("##Down", ImGuiDir_Down))
        {
            a_Edited([l_Index](auto& processors) { MoveItem(processors, l_Index, 1); }, std::format("Move a {} processor of {} down", l_Kind, l_Path), {});
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(Trinity::Icons::c_Trash, ImVec2(l_Button, l_Button)))
        {
            a_Edited([it_Processor](auto& processors) { processors.erase(processors.begin() + static_cast<std::ptrdiff_t>(it_Processor)); }, std::format("Remove a {} processor of {}", l_Kind, l_Path), {});
        }

        ImGui::SetItemTooltip("Remove");

        Trinity::InputProcessor l_Edited = l_Processor;
        const auto a_Set = [&](std::string_view field)
        {
            a_Edited([it_Processor, l_Edited](auto& processors) { processors[it_Processor] = l_Edited; }, std::format("Set the {} of a {} processor of {}", field, l_Kind, l_Path), l_Field + std::string(field));
        };

        switch (l_Processor.Kind)
        {
            case Trinity::InputProcessorKind::RadialDeadzone:
            case Trinity::InputProcessorKind::AxialDeadzone:
            {
                Label("Min");
                if (ImGui::DragFloat("##Min", &l_Edited.Min, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
                {
                    a_Set("Min");
                }

                ImGui::SetItemTooltip("Nothing below this");
                Label("Max");
                if (ImGui::DragFloat("##Max", &l_Edited.Max, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
                {
                    a_Set("Max");
                }

                ImGui::SetItemTooltip("All of it from this");

                break;
            }
            case Trinity::InputProcessorKind::Invert:
            {
                Label("Invert X");
                if (ImGui::Checkbox("##InvertX", &l_Edited.InvertX))
                {
                    a_Set("InvertX");
                }

                Label("Invert Y");
                if (ImGui::Checkbox("##InvertY", &l_Edited.InvertY))
                {
                    a_Set("InvertY");
                }

                break;
            }
            case Trinity::InputProcessorKind::Scale:
            {
                Label("Scale");
                if (ImGui::DragFloat2("##Scale", &l_Edited.Scale.x, 0.01f, -100.0f, 100.0f, "%.2f"))
                {
                    a_Set("Scale");
                }

                break;
            }
            case Trinity::InputProcessorKind::Normalize:
            {
                ImGui::TextDisabled("Brings a value longer than 1 back to 1");

                break;
            }
        }

        ImGui::PopID();
    }

    if (ImGui::Button("Add Processor"))
    {
        ImGui::OpenPopup("##AddProcessor");
    }

    if (ImGui::BeginPopup("##AddProcessor"))
    {
        for (const Trinity::InputProcessorKind it_Kind : c_ProcessorKinds)
        {
            if (ImGui::Selectable(Trinity::ToString(it_Kind).data()))
            {
                a_Edited([it_Kind](auto& processors) { processors.push_back(Trinity::InputProcessor{ .Kind = it_Kind }); }, std::format("Add a {} processor to {}", Trinity::ToString(it_Kind), l_Path), {});
            }
        }

        ImGui::EndPopup();
    }
}

// Typed into, and made once the field is left, or before another is chosen. A name the file would refuse is not made, and why shows under it. Rename takes the field with its text all selected, as ImGui would otherwise keep where the cursor was in a field taken again
void InputActionsPanel::DrawName(const Trinity::InputActionsData& data, std::string_view current)
{
    Label("Name");
    const int l_Frame = ImGui::GetFrameCount();
    if (m_FocusNameFrame == l_Frame)
    {
        ImGui::SetKeyboardFocusHere();
    }

    if (!m_NameEdited)
    {
        m_NameText = current;
    }

    const bool l_Renaming = l_Frame >= m_FocusNameFrame && l_Frame <= m_FocusNameFrame + 1;
    ImGui::InputText("##Name", &m_NameText, l_Renaming ? ImGuiInputTextFlags_AutoSelectAll : ImGuiInputTextFlags_None);
    if (ImGui::IsItemEdited())
    {
        m_NameEdited = true;
    }

    if (ImGui::IsItemDeactivated() && m_NameEdited)
    {
        CommitName(data);
    }

    if (!m_NameError.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Error));
        ImGui::TextWrapped("%s", m_NameError.c_str());
        ImGui::PopStyleColor();
    }
}

void InputActionsPanel::CommitName(const Trinity::InputActionsData& data)
{
    m_NameEdited = false;
    const std::string l_Name = Trim(m_NameText);
    Trinity::InputActionsData l_After = data;
    if (m_Selected.Action >= 0)
    {
        Trinity::InputVector<Trinity::InputActionDefinition>& l_Actions = l_After.Maps[static_cast<std::size_t>(m_Selected.Map)].Actions;
        Trinity::InputActionDefinition& l_Action = l_Actions[static_cast<std::size_t>(m_Selected.Action)];
        m_NameError = CheckName(l_Actions, m_Selected.Action, l_Name, "action");
        if (!m_NameError.empty() || std::string_view(l_Action.Name) == l_Name)
        {
            return;
        }

        const std::string l_What = std::format("Rename action {}/{} to {}", std::string_view(l_After.Maps[static_cast<std::size_t>(m_Selected.Map)].Name), std::string_view(l_Action.Name), l_Name);
        l_Action.Name = Trinity::InputName(std::string_view(l_Name));
        Rename(std::move(l_After), l_What);
    }
    else if (m_Selected.Map >= 0)
    {
        Trinity::InputActionMap& l_Map = l_After.Maps[static_cast<std::size_t>(m_Selected.Map)];
        m_NameError = CheckName(l_After.Maps, m_Selected.Map, l_Name, "map");
        if (!m_NameError.empty() || std::string_view(l_Map.Name) == l_Name)
        {
            return;
        }

        const std::string l_What = std::format("Rename map {} to {}", std::string_view(l_Map.Name), l_Name);
        l_Map.Name = Trinity::InputName(std::string_view(l_Name));
        Rename(std::move(l_After), l_What);
    }
}

// Delete, Ctrl+D and F2 act on what is chosen while the panel has the focus and nothing is typed into
void InputActionsPanel::ReadShortcuts(const Trinity::InputActionsData& data)
{
    if (!m_Focused || m_Selected.Map < 0 || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || m_Listening)
    {
        return;
    }

    if (ImGui::IsKeyChordPressed(ImGuiKey_Delete))
    {
        Remove(data, m_Selected);
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D))
    {
        Duplicate(data, m_Selected);
    }
    else if (ImGui::IsKeyChordPressed(ImGuiKey_F2) && m_Selected.Binding < 0)
    {
        m_FocusNameFrame = ImGui::GetFrameCount() + 1;
    }
}

void InputActionsPanel::AddMap(const Trinity::InputActionsData& data)
{
    Trinity::InputActionsData l_After = data;
    const std::string l_Name = FreeName(l_After.Maps, "New Map");
    l_After.Maps.emplace_back().Name = Trinity::InputName(std::string_view(l_Name));
    const int l_Index = static_cast<int>(l_After.Maps.size()) - 1;
    Edit(std::move(l_After), std::format("Add map {}", l_Name), {}, Selection{ l_Index, -1, -1 });
    m_FocusNameFrame = ImGui::GetFrameCount() + 1;
}

void InputActionsPanel::AddAction(const Trinity::InputActionsData& data, int map)
{
    Trinity::InputActionsData l_After = data;
    Trinity::InputActionMap& l_Map = l_After.Maps[static_cast<std::size_t>(map)];
    const std::string l_Name = FreeName(l_Map.Actions, "New Action");
    l_Map.Actions.emplace_back().Name = Trinity::InputName(std::string_view(l_Name));
    const int l_Index = static_cast<int>(l_Map.Actions.size()) - 1;
    const std::string l_What = std::format("Add action {}/{}", std::string_view(l_Map.Name), l_Name);
    Edit(std::move(l_After), l_What, {}, Selection{ map, l_Index, -1 });
    m_FocusNameFrame = ImGui::GetFrameCount() + 1;
}

void InputActionsPanel::AddBinding(const Trinity::InputActionsData& data, Selection action, Trinity::InputCompositeKind composite)
{
    Trinity::InputActionsData l_After = data;
    const Trinity::InputActionMap& l_Map = l_After.Maps[static_cast<std::size_t>(action.Map)];
    Trinity::InputActionDefinition& l_Action = l_After.Maps[static_cast<std::size_t>(action.Map)].Actions[static_cast<std::size_t>(action.Action)];
    l_Action.Bindings.emplace_back().Composite = composite;
    const int l_Index = static_cast<int>(l_Action.Bindings.size()) - 1;
    const std::string l_What = std::format("Add {} to {}/{}", composite == Trinity::InputCompositeKind::None ? std::string_view("a binding") : std::string_view(std::format("a {}", GetCompositeLabel(composite))), std::string_view(l_Map.Name), std::string_view(l_Action.Name));
    Edit(std::move(l_After), l_What, {}, Selection{ action.Map, action.Action, l_Index });
}

// The deepest thing named goes: the binding when one is, else the action, else the map. What is chosen moves to the one before it
void InputActionsPanel::Remove(const Trinity::InputActionsData& data, Selection what)
{
    Trinity::InputActionsData l_After = data;
    Trinity::InputActionMap& l_Map = l_After.Maps[static_cast<std::size_t>(what.Map)];
    if (what.Binding >= 0)
    {
        Trinity::InputActionDefinition& l_Action = l_Map.Actions[static_cast<std::size_t>(what.Action)];
        const std::string l_What = std::format("Remove binding {} of {}/{}", Describe(l_Action.Bindings[static_cast<std::size_t>(what.Binding)]), std::string_view(l_Map.Name), std::string_view(l_Action.Name));
        l_Action.Bindings.erase(l_Action.Bindings.begin() + what.Binding);
        Edit(std::move(l_After), l_What, {}, Selection{ what.Map, what.Action, what.Binding - 1 });
    }
    else if (what.Action >= 0)
    {
        const std::string l_What = std::format("Remove action {}/{}", std::string_view(l_Map.Name), std::string_view(l_Map.Actions[static_cast<std::size_t>(what.Action)].Name));
        l_Map.Actions.erase(l_Map.Actions.begin() + what.Action);
        Edit(std::move(l_After), l_What, {}, Selection{ what.Map, std::max(0, what.Action - 1), -1 });
    }
    else
    {
        const std::string l_What = std::format("Remove map {}", std::string_view(l_Map.Name));
        l_After.Maps.erase(l_After.Maps.begin() + what.Map);
        Edit(std::move(l_After), l_What, {}, Selection{ std::max(0, what.Map - 1), -1, -1 });
    }
}

// Right after the original, an action or a map named as Unity names a copy, with a number after its name
void InputActionsPanel::Duplicate(const Trinity::InputActionsData& data, Selection what)
{
    Trinity::InputActionsData l_After = data;
    Trinity::InputActionMap& l_Map = l_After.Maps[static_cast<std::size_t>(what.Map)];
    if (what.Binding >= 0)
    {
        Trinity::InputActionDefinition& l_Action = l_Map.Actions[static_cast<std::size_t>(what.Action)];
        const Trinity::InputBinding l_Copy = l_Action.Bindings[static_cast<std::size_t>(what.Binding)];
        l_Action.Bindings.insert(l_Action.Bindings.begin() + what.Binding + 1, l_Copy);
        const std::string l_What = std::format("Duplicate binding {} of {}/{}", Describe(l_Copy), std::string_view(l_Map.Name), std::string_view(l_Action.Name));
        Edit(std::move(l_After), l_What, {}, Selection{ what.Map, what.Action, what.Binding + 1 });
    }
    else if (what.Action >= 0)
    {
        Trinity::InputActionDefinition l_Copy = l_Map.Actions[static_cast<std::size_t>(what.Action)];
        const std::string l_Original(std::string_view(l_Copy.Name));
        l_Copy.Name = Trinity::InputName(std::string_view(FreeName(l_Map.Actions, l_Original)));
        const std::string l_What = std::format("Duplicate action {}/{} as {}", std::string_view(l_Map.Name), l_Original, std::string_view(l_Copy.Name));
        l_Map.Actions.insert(l_Map.Actions.begin() + what.Action + 1, std::move(l_Copy));
        Edit(std::move(l_After), l_What, {}, Selection{ what.Map, what.Action + 1, -1 });
    }
    else
    {
        Trinity::InputActionMap l_Copy = l_Map;
        const std::string l_Original(std::string_view(l_Copy.Name));
        l_Copy.Name = Trinity::InputName(std::string_view(FreeName(l_After.Maps, l_Original)));
        const std::string l_What = std::format("Duplicate map {} as {}", l_Original, std::string_view(l_Copy.Name));
        l_After.Maps.insert(l_After.Maps.begin() + what.Map + 1, std::move(l_Copy));
        Edit(std::move(l_After), l_What, {}, Selection{ what.Map + 1, -1, -1 });
    }
}

void InputActionsPanel::Move(const Trinity::InputActionsData& data, Selection what, int by)
{
    Trinity::InputActionsData l_After = data;
    Trinity::InputActionMap& l_Map = l_After.Maps[static_cast<std::size_t>(what.Map)];
    const std::string_view l_Way = by < 0 ? "up" : "down";
    if (what.Binding >= 0)
    {
        Trinity::InputActionDefinition& l_Action = l_Map.Actions[static_cast<std::size_t>(what.Action)];
        const std::string l_What = std::format("Move binding {} of {}/{} {}", Describe(l_Action.Bindings[static_cast<std::size_t>(what.Binding)]), std::string_view(l_Map.Name), std::string_view(l_Action.Name), l_Way);
        MoveItem(l_Action.Bindings, what.Binding, by);
        Edit(std::move(l_After), l_What, {}, Selection{ what.Map, what.Action, what.Binding + by });
    }
    else if (what.Action >= 0)
    {
        const std::string l_What = std::format("Move action {}/{} {}", std::string_view(l_Map.Name), std::string_view(l_Map.Actions[static_cast<std::size_t>(what.Action)].Name), l_Way);
        MoveItem(l_Map.Actions, what.Action, by);
        Edit(std::move(l_After), l_What, {}, Selection{ what.Map, what.Action + by, -1 });
    }
    else
    {
        const std::string l_What = std::format("Move map {} {}", std::string_view(l_Map.Name), l_Way);
        MoveItem(l_After.Maps, what.Map, by);
        Edit(std::move(l_After), l_What, {}, Selection{ what.Map + by, -1, -1 });
    }
}

void InputActionsPanel::Bind(const Trinity::InputActionsData& data, Selection binding, std::size_t part, Trinity::InputSource source)
{
    Trinity::InputActionsData l_After = data;
    const Trinity::InputActionMap& l_Map = l_After.Maps[static_cast<std::size_t>(binding.Map)];
    Trinity::InputActionDefinition& l_Action = l_After.Maps[static_cast<std::size_t>(binding.Map)].Actions[static_cast<std::size_t>(binding.Action)];
    Trinity::InputBinding& l_Binding = l_Action.Bindings[static_cast<std::size_t>(binding.Binding)];
    const std::span<const std::string_view> l_Parts = Trinity::GetCompositeParts(l_Binding.Composite);
    const std::string l_Name = Trinity::ToString(source);
    const std::string l_What = std::format("Bind {}{}{} of {}/{} to {}", l_Parts.empty() ? "" : "the ", l_Parts.empty() ? std::string_view() : l_Parts[part], l_Parts.empty() ? "a binding" : " part", std::string_view(l_Map.Name), std::string_view(l_Action.Name), l_Name.empty() ? std::string_view("nothing") : std::string_view(l_Name));
    l_Binding.Sources[part] = source;
    Edit(std::move(l_After), l_What);
}

// Where each pad's axes rest now, so only a push from there counts
void InputActionsPanel::BeginListen(Selection binding, std::size_t part)
{
    Listening l_Listening;
    l_Listening.Binding = binding;
    l_Listening.Part = part;
    l_Listening.Frame = ImGui::GetFrameCount();
    for (std::uint32_t it_Gamepad = 0; it_Gamepad < Trinity::Input::c_MaxGamepads; ++it_Gamepad)
    {
        for (std::uint8_t it_Axis = 0; it_Axis < std::to_underlying(Trinity::GamepadAxis::Count); ++it_Axis)
        {
            l_Listening.Rest[it_Gamepad][it_Axis] = Trinity::Input::GetGamepadAxis(it_Gamepad, static_cast<Trinity::GamepadAxis>(it_Axis));
        }
    }

    m_Listening = l_Listening;
}

// From the frame after it began, since that frame's input is the click that began it. It stops when what it fills is gone, as after an undo
void InputActionsPanel::UpdateListen(const Trinity::InputActionsData& data)
{
    if (!m_Listening)
    {
        return;
    }

    const Listening l_Listening = *m_Listening;
    const Selection& l_At = l_Listening.Binding;
    const bool l_Exists = l_At.Map >= 0 && l_At.Map < static_cast<int>(data.Maps.size()) && l_At.Action >= 0 && l_At.Action < static_cast<int>(data.Maps[static_cast<std::size_t>(l_At.Map)].Actions.size())
        && l_At.Binding >= 0 && l_At.Binding < static_cast<int>(data.Maps[static_cast<std::size_t>(l_At.Map)].Actions[static_cast<std::size_t>(l_At.Action)].Bindings.size());
    if (!l_Exists)
    {
        m_Listening.reset();

        return;
    }

    const Trinity::InputActionDefinition& l_Action = data.Maps[static_cast<std::size_t>(l_At.Map)].Actions[static_cast<std::size_t>(l_At.Action)];
    const Trinity::InputBinding& l_Binding = l_Action.Bindings[static_cast<std::size_t>(l_At.Binding)];
    if (l_Listening.Part >= std::max<std::size_t>(1, Trinity::GetCompositeParts(l_Binding.Composite).size()))
    {
        m_Listening.reset();

        return;
    }

    if (ImGui::GetFrameCount() <= l_Listening.Frame)
    {
        return;
    }

    if (Trinity::Input::WasKeyPressed(Trinity::KeyCode::TR_ESCAPE) || Trinity::Input::WasMouseButtonPressed(Trinity::MouseCode::TR_LEFT))
    {
        m_Listening.reset();

        return;
    }

    const bool l_WholeStick = l_Action.Type == Trinity::InputActionType::Vector2 && l_Binding.Composite == Trinity::InputCompositeKind::None;
    if (const std::optional<Trinity::InputSource> l_Source = FindPushed(l_Listening.Rest, l_WholeStick))
    {
        m_Listening.reset();
        Bind(data, l_At, l_Listening.Part, *l_Source);
    }
}