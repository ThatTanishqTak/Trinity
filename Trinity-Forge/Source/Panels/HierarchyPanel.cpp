#include "Panels/HierarchyPanel.hpp"

#include "EditorCommands.hpp"
#include "EditorPayloads.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <format>
#include <utility>

namespace
{
    // The top and bottom quarters of a row drop before and after it, and the middle drops inside it
    constexpr float c_EdgeFraction = 0.25f;
    constexpr float c_DropLineThickness = 2.0f;
    constexpr const char* c_DefaultName = "Entity";

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
}

HierarchyPanel::HierarchyPanel(EditorSession& session) : Panel("Hierarchy", Trinity::Icons::c_Sitemap, DockSlot::Left), m_Session(session)
{

}

// The scene's tree in hierarchy order. A selection made elsewhere, such as in the Viewport, opens its ancestors and scrolls into view. Edits wait until the tree is drawn, since they can destroy the entities it is walking
void HierarchyPanel::OnImGuiRender()
{
    if (!m_Session.HasProject())
    {
        ImGui::TextDisabled("Open or create a project to edit its scenes");

        return;
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

    for (Trinity::Entity it_Root = l_Scene.GetFirstRoot(); it_Root; it_Root = it_Root.GetNextSibling())
    {
        DrawEntity(it_Root);
    }

    DrawBackground();

    m_RevealPath.clear();
    for (std::move_only_function<void()>& it_Action : std::exchange(m_Deferred, {}))
    {
        it_Action();
    }
}

void HierarchyPanel::DrawEntity(Trinity::Entity entity)
{
    const Trinity::UUID l_ID = entity.GetUUID();
    const bool l_Selected = l_ID == m_Session.GetSelection();
    const bool l_Renaming = l_ID == m_Renaming;

    ImGuiTreeNodeFlags l_Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
    if (entity.GetChildCount() == 0)
    {
        l_Flags |= ImGuiTreeNodeFlags_Leaf;
    }

    if (l_Selected)
    {
        l_Flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (std::ranges::find(m_RevealPath, l_ID) != m_RevealPath.end())
    {
        ImGui::SetNextItemOpen(true);
    }

    const std::string l_Label = l_Renaming ? std::string(Trinity::Icons::c_CubeOutline) : std::format("{} {}", Trinity::Icons::c_CubeOutline, GetEntityLabel(entity));
    const bool l_Open = ImGui::TreeNodeEx(ToImGuiID(l_ID), l_Flags, "%s", l_Label.c_str());

    if (l_Selected && m_ScrollToSelection)
    {
        ImGui::SetScrollHereY();
        m_ScrollToSelection = false;
    }

    // Selecting on press, so a drag starts from the entity it drags. A double-click renames, and the arrow opens and closes
    if ((ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) && !ImGui::IsItemToggledOpen())
    {
        Select(l_ID);
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        BeginRename(entity);
    }

    if (ImGui::BeginPopupContextItem())
    {
        DrawContextMenu(entity);
        ImGui::EndPopup();
    }

    if (ImGui::BeginDragDropSource())
    {
        const std::uint64_t l_Value = l_ID.GetValue();
        ImGui::SetDragDropPayload(c_EntityPayload, &l_Value, sizeof(l_Value));
        ImGui::TextUnformatted(GetEntityLabel(entity).c_str());
        ImGui::EndDragDropSource();
    }

    AcceptDrop(entity);

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

// The space below the tree: a click clears the selection, a drop makes the entity the last root, and its menu creates a root
void HierarchyPanel::DrawBackground()
{
    const ImVec2 l_Available = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##Background", ImVec2(std::max(l_Available.x, 1.0f), std::max(l_Available.y, ImGui::GetFrameHeight())));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        Select({});
    }

    if (ImGui::BeginPopupContextItem("##BackgroundMenu"))
    {
        if (ImGui::MenuItem(std::format("{} Create Entity", Trinity::Icons::c_CubeOutline).c_str()))
        {
            m_Deferred.push_back([this] { Create({}); });
        }

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

// Rename while the Hierarchy has focus. Duplicate and Delete are Forge's own shortcuts, so they work from any panel
void HierarchyPanel::ReadShortcuts()
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || ImGui::GetIO().WantTextInput)
    {
        return;
    }

    const Trinity::Entity l_Selected = m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection());
    if (l_Selected && l_Selected.Has<Trinity::TagComponent>() && ImGui::IsKeyPressed(ImGuiKey_F2, false))
    {
        BeginRename(l_Selected);
    }
}

void HierarchyPanel::Select(Trinity::UUID entity)
{
    m_Session.SetSelection(entity);
    m_Revealed = entity;
}

void HierarchyPanel::BeginRename(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::TagComponent>())
    {
        return;
    }

    Select(entity.GetUUID());
    m_Renaming = entity.GetUUID();
    m_RenameText = std::string(std::string_view(entity.Get<Trinity::TagComponent>().Tag));
    m_FocusRename = true;
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