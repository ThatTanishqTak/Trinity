#include "Panels/HierarchyPanel.hpp"

#include "EditorCommands.hpp"
#include "EditorPayloads.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <format>
#include <string_view>
#include <utility>

namespace
{
    // The top and bottom quarters of a row drop before and after it, and the middle drops inside it
    constexpr float c_EdgeFraction = 0.25f;
    constexpr float c_DropLineThickness = 2.0f;
    constexpr const char* c_DefaultName = "Entity";
    // How long a second click on a selected entity waits before it renames, at least, so the second click of a double-click never does
    constexpr double c_SlowClickSeconds = 0.5;

    Trinity::UUID GetID(Trinity::Entity entity)
    {
        return entity ? entity.GetUUID() : Trinity::UUID();
    }

    // An entity's or an asset's, which both payloads carry
    Trinity::UUID ReadPayloadUUID(const ImGuiPayload& payload)
    {
        std::uint64_t l_Value = 0;
        std::memcpy(&l_Value, payload.Data, sizeof(l_Value));

        return Trinity::UUID(l_Value);
    }

    bool IsModelPayload(const ImGuiPayload* payload, const Trinity::AssetRegistry* registry)
    {
        const Trinity::AssetRecord* l_Record = payload != nullptr && payload->IsDataType(c_AssetPayload) && registry != nullptr ? registry->Find(ReadPayloadUUID(*payload)) : nullptr;

        return l_Record != nullptr && l_Record->Importer == ModelImporter::c_Importer;
    }

    const void* ToImGuiID(Trinity::UUID uuid)
    {
        return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(uuid.GetValue()));
    }

    // Whether the text holds the part, whatever the case of either
    bool ContainsIgnoringCase(std::string_view text, std::string_view part)
    {
        return part.empty() || !std::ranges::search(text, part, [](char left, char right) { return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right)); }).empty();
    }
}

HierarchyPanel::HierarchyPanel(EditorSession& session) : Panel("Hierarchy", Trinity::Icons::c_Sitemap, DockSlot::Left), m_Session(session)
{

}

// The scene's tree in hierarchy order, under a search field that stays as the tree scrolls. A selection made elsewhere, such as in the Viewport, opens its ancestors and scrolls into view. Edits wait until the tree is drawn, since they can destroy the entities it is walking. While a copy plays, the tree is the copy's, to select in and look at but not to change. A search draws its tree under IDs of its own, so the tree opened and closed without one is as it was once the search is cleared
void HierarchyPanel::OnImGuiRender()
{
    if (!m_Session.HasProject())
    {
        ImGui::TextDisabled("Open or create a project to edit its scenes");

        return;
    }

    m_ReadOnly = m_Session.IsPlaying();
    if (m_ReadOnly)
    {
        m_Renaming = {};
        ImGui::TextDisabled("Playing a copy, which cannot be changed");
    }

    Trinity::Scene& l_Scene = m_Session.GetScene();
    const Trinity::UUID l_Selection = m_Session.GetSelection();
    if (l_Selection != m_Revealed)
    {
        m_Revealed = l_Selection;
        m_RevealPath.clear();
        for (Trinity::Entity it_Parent = l_Scene.FindEntityByUUID(l_Selection) ? l_Scene.FindEntityByUUID(l_Selection).GetParent() : Trinity::Entity(); it_Parent; it_Parent = it_Parent.GetParent())
        {
            m_RevealPath.push_back(it_Parent.GetUUID());
        }

        m_ScrollToSelection = l_Selection.IsValid();
    }

    if (m_Renaming.IsValid() && !l_Scene.FindEntityByUUID(m_Renaming))
    {
        m_Renaming = {};
    }

    ReadShortcuts();
    DrawSearch();

    const bool l_Searching = !m_Search.empty();
    if (l_Searching)
    {
        UpdateSearch(l_Scene);
    }

    if (ImGui::BeginChild("##Tree"))
    {
        if (l_Searching)
        {
            ImGui::PushID("##Search");
        }

        for (Trinity::Entity it_Root = l_Scene.GetFirstRoot(); it_Root; it_Root = it_Root.GetNextSibling())
        {
            DrawEntity(it_Root);
        }

        if (l_Searching)
        {
            ImGui::PopID();
            if (m_Shown.empty())
            {
                ImGui::TextDisabled("No entity matches");
            }
        }

        DrawBackground();
    }

    ImGui::EndChild();

    UpdateSlowClick(l_Scene);
    m_SearchChanged = false;
    m_RevealPath.clear();
    for (std::move_only_function<void()>& it_Action : std::exchange(m_Deferred, {}))
    {
        it_Action();
    }
}

// Unity's search field. Ctrl+F comes to it and Escape clears it, and clearing it opens the tree to the selection
void HierarchyPanel::DrawSearch()
{
    if (std::exchange(m_FocusSearch, false))
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##Search", std::format("{} Search, or t:Light for a type", Trinity::Icons::c_Search).c_str(), &m_Search, ImGuiInputTextFlags_EscapeClearsAll))
    {
        m_SearchChanged = true;
        if (m_Search.empty())
        {
            m_Revealed = {};
        }
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Matches names, whatever their case. t: matches a component or a script class an entity has, as t:Light, t:Collider or t:Player do, and both can be given, as in door t:Joint. The parents of what matches show dimmed");
    }
}

// Each word must be in an entity's name, whatever its case, and with t: terms the entity must have a component or a script class one of them names, so t:Renderer finds MeshRenderer and SpriteRenderer. Each match shows with every parent above it, so the tree keeps its shape
void HierarchyPanel::UpdateSearch(Trinity::Scene& scene)
{
    std::vector<std::string_view> l_Words;
    std::vector<std::string_view> l_Types;
    const std::string_view l_Search(m_Search);
    for (std::size_t l_Start = l_Search.find_first_not_of(' '); l_Start != std::string_view::npos; l_Start = l_Search.find_first_not_of(' ', l_Start))
    {
        const std::size_t l_End = std::min(l_Search.find(' ', l_Start), l_Search.size());
        const std::string_view l_Term = l_Search.substr(l_Start, l_End - l_Start);
        if (l_Term.size() >= 2 && (l_Term[0] == 't' || l_Term[0] == 'T') && l_Term[1] == ':')
        {
            if (l_Term.size() > 2)
            {
                l_Types.push_back(l_Term.substr(2));
            }
        }
        else
        {
            l_Words.push_back(l_Term);
        }

        l_Start = l_End;
    }

    // The components the t: terms name, by their labels, as Trinity.MeshRenderer reads MeshRenderer
    std::vector<std::string_view> l_Components;
    for (const std::string_view it_Name : Trinity::SceneSerializer::GetComponentNames())
    {
        if (std::ranges::any_of(l_Types, [it_Name](std::string_view type) { return ContainsIgnoringCase(GetComponentLabel(it_Name), type); }))
        {
            l_Components.push_back(it_Name);
        }
    }

    const auto a_HasType = [&l_Types, &l_Components](Trinity::Entity entity)
    {
        if (std::ranges::any_of(l_Components, [entity](std::string_view name) { return Trinity::SceneSerializer::HasComponent(entity, name); }))
        {
            return true;
        }

        return entity.Has<Trinity::ScriptComponent>() && std::ranges::any_of(entity.Get<Trinity::ScriptComponent>().Scripts, [&l_Types](const Trinity::ScriptEntry& script)
        {
            return std::ranges::any_of(l_Types, [&script](std::string_view type) { return ContainsIgnoringCase(std::string_view(script.ClassName), type); });
        });
    };

    m_Matched.clear();
    m_Shown.clear();
    for (Trinity::Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
    {
        const std::string l_Name = GetEntityLabel(it_Entity);
        if (!std::ranges::all_of(l_Words, [&l_Name](std::string_view word) { return ContainsIgnoringCase(l_Name, word); }) || (!l_Types.empty() && !a_HasType(it_Entity)))
        {
            continue;
        }

        // Up to the first parent already shown, whose own parents are shown too
        m_Matched.insert(it_Entity.GetUUID());
        Trinity::Entity l_Shown = it_Entity;
        while (l_Shown && m_Shown.insert(l_Shown.GetUUID()).second)
        {
            l_Shown = l_Shown.GetParent();
        }
    }
}

// While searching, only what the search shows: a branch with no match below it is a leaf, every branch opens as the search changes, and a parent shown only for what is below it is dimmed
void HierarchyPanel::DrawEntity(Trinity::Entity entity)
{
    const Trinity::UUID l_ID = entity.GetUUID();
    const bool l_Searching = !m_Search.empty();
    if (l_Searching && !m_Shown.contains(l_ID))
    {
        return;
    }

    const bool l_Selected = l_ID == m_Session.GetSelection();
    const bool l_Renaming = l_ID == m_Renaming;

    bool l_HasChildren = entity.GetChildCount() > 0;
    if (l_Searching)
    {
        l_HasChildren = false;
        for (Trinity::Entity it_Child = entity.GetFirstChild(); it_Child && !l_HasChildren; it_Child = it_Child.GetNextSibling())
        {
            l_HasChildren = m_Shown.contains(it_Child.GetUUID());
        }
    }

    ImGuiTreeNodeFlags l_Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
    if (!l_HasChildren)
    {
        l_Flags |= ImGuiTreeNodeFlags_Leaf;
    }
    else if (l_Searching)
    {
        l_Flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    if (l_Selected)
    {
        l_Flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (std::ranges::find(m_RevealPath, l_ID) != m_RevealPath.end() || (l_Searching && m_SearchChanged && l_HasChildren))
    {
        ImGui::SetNextItemOpen(true);
    }

    const bool l_Dimmed = l_Searching && !m_Matched.contains(l_ID);
    if (l_Dimmed)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }

    const std::string l_Label = l_Renaming ? std::string(Trinity::Icons::c_CubeOutline) : std::format("{} {}", Trinity::Icons::c_CubeOutline, GetEntityLabel(entity));
    const bool l_Open = ImGui::TreeNodeEx(ToImGuiID(l_ID), l_Flags, "%s", l_Label.c_str());
    if (l_Dimmed)
    {
        ImGui::PopStyleColor();
    }

    if (l_Selected && m_ScrollToSelection)
    {
        ImGui::SetScrollHereY();
        m_ScrollToSelection = false;
    }

    // Selecting on press, so a drag starts from the entity it drags, and the arrow opens and closes. As in Unity, a double-click frames the entity in the Scene panel, and a second, slower click on an entity already selected renames it
    const bool l_LeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen();
    if (l_LeftClicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        m_SlowClick = {};
        Select(l_ID);
        if (m_FrameSelection)
        {
            m_FrameSelection(false);
        }
    }
    else if (l_LeftClicked)
    {
        const ImGuiIO& l_IO = ImGui::GetIO();
        m_SlowClick = l_Selected && !l_Renaming && !l_IO.KeyCtrl && !l_IO.KeyShift && !l_IO.KeyAlt ? l_ID : Trinity::UUID();
        m_SlowClickTime = ImGui::GetTime();
        Select(l_ID);
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !ImGui::IsItemToggledOpen())
    {
        Select(l_ID);
    }

    if (!m_ReadOnly && ImGui::BeginPopupContextItem())
    {
        DrawContextMenu(entity);
        ImGui::EndPopup();
    }

    if (!m_ReadOnly && ImGui::BeginDragDropSource())
    {
        const std::uint64_t l_Value = l_ID.GetValue();
        ImGui::SetDragDropPayload(c_EntityPayload, &l_Value, sizeof(l_Value));
        ImGui::TextUnformatted(GetEntityLabel(entity).c_str());
        ImGui::EndDragDropSource();
    }

    if (!m_ReadOnly)
    {
        AcceptDrop(entity);
    }

    if (l_Renaming)
    {
        DrawRenameField(entity);
    }

    if (l_Open)
    {
        for (Trinity::Entity it_Child = entity.GetFirstChild(); it_Child; it_Child = it_Child.GetNextSibling())
        {
            DrawEntity(it_Child);
        }

        ImGui::TreePop();
    }
}

// Enter or clicking away keeps the name as one command. Escape puts the text back as it was, so nothing changes
void HierarchyPanel::DrawRenameField(Trinity::Entity entity)
{
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (std::exchange(m_FocusRename, false))
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::InputText("##Rename", &m_RenameText, ImGuiInputTextFlags_AutoSelectAll);
    if (!ImGui::IsItemDeactivated())
    {
        return;
    }

    m_Renaming = {};
    Trinity::TagComponent l_Tag = entity.Get<Trinity::TagComponent>();
    if (std::string_view(l_Tag.Tag) != m_RenameText)
    {
        // Leaving the field by clicking another entity selects that one, which the command would undo by selecting what it renamed
        const Trinity::UUID l_Selection = m_Session.GetSelection();
        l_Tag.Tag = m_RenameText;
        CommandStack& l_History = m_Session.GetHistory();
        l_History.Execute(Trinity::CreateScope<SetComponentCommand<Trinity::TagComponent>>(entity.GetUUID(), std::move(l_Tag), "Name"));
        l_History.EndMerge();
        m_Session.SetSelection(l_Selection);
    }
}

void HierarchyPanel::DrawContextMenu(Trinity::Entity entity)
{
    const Trinity::UUID l_ID = entity.GetUUID();
    if (ImGui::MenuItem(std::format("{} Create Child Entity", Trinity::Icons::c_CubeOutline).c_str()))
    {
        m_Deferred.push_back([this, l_ID] { Create(l_ID); });
    }

    DrawPrimitiveMenu(l_ID);

    if (ImGui::MenuItem("Rename", "F2", false, entity.Has<Trinity::TagComponent>()))
    {
        BeginRename(entity);
    }

    if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
    {
        m_Deferred.push_back([this, l_ID] { m_Session.GetHistory().Execute(Trinity::CreateScope<DuplicateEntityCommand>(l_ID)); });
    }

    ImGui::Separator();

    if (ImGui::MenuItem(std::format("{} Delete", Trinity::Icons::c_Trash).c_str(), "Delete"))
    {
        m_Deferred.push_back([this, l_ID] { m_Session.GetHistory().Execute(Trinity::CreateScope<DeleteEntityCommand>(l_ID)); });
    }
}

// The space below the tree: a click clears the selection, a drop makes the entity the last root, and its menu creates a root, the last two not while a copy plays
void HierarchyPanel::DrawBackground()
{
    const ImVec2 l_Available = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##Background", ImVec2(std::max(l_Available.x, 1.0f), std::max(l_Available.y, ImGui::GetFrameHeight())));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        Select({});
    }

    if (m_ReadOnly)
    {
        return;
    }

    if (ImGui::BeginPopupContextItem("##BackgroundMenu"))
    {
        if (ImGui::MenuItem(std::format("{} Create Entity", Trinity::Icons::c_CubeOutline).c_str()))
        {
            m_Deferred.push_back([this] { Create({}); });
        }

        DrawPrimitiveMenu({});

        ImGui::EndPopup();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_EntityPayload))
        {
            const Trinity::UUID l_Dragged = ReadPayloadUUID(*l_Payload);
            m_Deferred.push_back([this, l_Dragged] { Move(l_Dragged, {}, {}); });
        }
        else if (const ImGuiPayload* l_Model = IsModelPayload(ImGui::GetDragDropPayload(), m_Session.GetRegistry()) ? ImGui::AcceptDragDropPayload(c_AssetPayload) : nullptr)
        {
            const Trinity::UUID l_Dropped = ReadPayloadUUID(*l_Model);
            m_Deferred.push_back([this, l_Dropped] { static_cast<void>(m_Session.CreateModel(l_Dropped, {}, {}, glm::vec3(0.0f))); });
        }

        ImGui::EndDragDropTarget();
    }
}

// Where the mouse is over the row picks before, inside or after, and a line or an outline shows which. An entity moves there, and a model is created there
void HierarchyPanel::AcceptDrop(Trinity::Entity target)
{
    if (!ImGui::BeginDragDropTarget())
    {
        return;
    }

    const ImVec2 l_Min = ImGui::GetItemRectMin();
    const ImVec2 l_Max = ImGui::GetItemRectMax();
    const float l_Edge = (l_Max.y - l_Min.y) * c_EdgeFraction;
    const float l_MouseY = ImGui::GetMousePos().y;
    const bool l_Before = l_MouseY < l_Min.y + l_Edge;
    const bool l_After = !l_Before && l_MouseY > l_Max.y - l_Edge;

    const ImGuiDragDropFlags l_Flags = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
    const bool l_Model = IsModelPayload(ImGui::GetDragDropPayload(), m_Session.GetRegistry());
    if (const ImGuiPayload* l_Payload = l_Model ? ImGui::AcceptDragDropPayload(c_AssetPayload, l_Flags) : ImGui::AcceptDragDropPayload(c_EntityPayload, l_Flags))
    {
        const ImU32 l_Color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
        ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
        if (l_Before || l_After)
        {
            const float l_Y = l_Before ? l_Min.y : l_Max.y;
            l_DrawList.AddLine(ImVec2(l_Min.x, l_Y), ImVec2(l_Max.x, l_Y), l_Color, c_DropLineThickness);
        }
        else
        {
            l_DrawList.AddRect(l_Min, l_Max, l_Color, 0.0f, c_DropLineThickness);
        }

        const Trinity::UUID l_Dragged = ReadPayloadUUID(*l_Payload);
        if (l_Payload->IsDelivery() && l_Dragged != target.GetUUID())
        {
            const Trinity::UUID l_Target = target.GetUUID();
            const Trinity::UUID l_Parent = l_Before || l_After ? GetID(target.GetParent()) : l_Target;
            const Trinity::UUID l_Next = l_Before ? l_Target : (l_After ? GetID(target.GetNextSibling()) : Trinity::UUID());
            if (l_Model)
            {
                m_Deferred.push_back([this, l_Dragged, l_Parent, l_Next] { static_cast<void>(m_Session.CreateModel(l_Dragged, l_Parent, l_Next, glm::vec3(0.0f))); });
            }
            else
            {
                m_Deferred.push_back([this, l_Dragged, l_Parent, l_Next] { Move(l_Dragged, l_Parent, l_Next); });
            }
        }
    }

    ImGui::EndDragDropTarget();
}

// Rename, Ctrl+F to the search, and F to frame the selection in the Scene panel, or Shift+F to follow it there, while the Hierarchy has focus, as in Unity. Duplicate and Delete are Forge's own shortcuts, so they work from any panel
void HierarchyPanel::ReadShortcuts()
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || ImGui::GetIO().WantTextInput)
    {
        return;
    }

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
    {
        m_FocusSearch = true;

        return;
    }

    if (m_FrameSelection && (ImGui::IsKeyChordPressed(ImGuiKey_F) || ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_F)))
    {
        m_FrameSelection(ImGui::GetIO().KeyShift);

        return;
    }

    const Trinity::Entity l_Selected = m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection());
    if (l_Selected && l_Selected.Has<Trinity::TagComponent>() && ImGui::IsKeyPressed(ImGuiKey_F2, false))
    {
        BeginRename(l_Selected);
    }
}

// The slow click's rename waits out the double-click time with the button up, and is dropped by a drag, by the selection moving on, or by the Hierarchy losing focus to another panel
void HierarchyPanel::UpdateSlowClick(Trinity::Scene& scene)
{
    if (!m_SlowClick)
    {
        return;
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || m_Session.GetSelection() != m_SlowClick || !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
    {
        m_SlowClick = {};

        return;
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::GetTime() - m_SlowClickTime < std::max(static_cast<double>(ImGui::GetIO().MouseDoubleClickTime), c_SlowClickSeconds))
    {
        return;
    }

    if (const Trinity::Entity l_Entity = scene.FindEntityByUUID(std::exchange(m_SlowClick, {})))
    {
        BeginRename(l_Entity);
    }
}

void HierarchyPanel::Select(Trinity::UUID entity)
{
    m_Session.SetSelection(entity);
    m_Revealed = entity;
}

void HierarchyPanel::BeginRename(Trinity::Entity entity)
{
    if (m_ReadOnly || !entity.Has<Trinity::TagComponent>())
    {
        return;
    }

    Select(entity.GetUUID());
    m_Renaming = entity.GetUUID();
    m_RenameText = std::string(std::string_view(entity.Get<Trinity::TagComponent>().Tag));
    m_FocusRename = true;
}

// A built-in shape with its collider, under the parent or as a root, made once the tree is drawn and shown selected
void HierarchyPanel::DrawPrimitiveMenu(Trinity::UUID parent)
{
    if (!ImGui::BeginMenu(std::format("{} Create 3D Object", Trinity::Icons::c_Cube).c_str()))
    {
        return;
    }

    for (const Trinity::BuiltInMesh it_Mesh : Trinity::c_BuiltInMeshes)
    {
        if (ImGui::MenuItem(std::string(Trinity::ToString(it_Mesh)).c_str()))
        {
            m_Deferred.push_back([this, it_Mesh, parent]
            {
                if (m_Session.CreatePrimitive(it_Mesh, parent))
                {
                    m_Revealed = {};
                }
            });
        }
    }

    ImGui::EndMenu();
}

// A new entity is selected and named at once
void HierarchyPanel::Create(Trinity::UUID parent)
{
    if (m_Session.GetHistory().Execute(Trinity::CreateScope<CreateEntityCommand>(c_DefaultName, parent, Trinity::UUID())))
    {
        BeginRename(m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection()));
        m_Revealed = {};
    }
}

// Keeping the world transform, so a dragged entity stays where it is on screen
void HierarchyPanel::Move(Trinity::UUID entity, Trinity::UUID parent, Trinity::UUID before)
{
    m_Session.GetHistory().Execute(Trinity::CreateScope<MoveEntityCommand>(entity, parent, before, true));
}