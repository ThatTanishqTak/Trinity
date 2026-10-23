#include "Panels/HierarchyPanel.hpp"

#include "EditorCommands.hpp"
#include "EditorPayloads.hpp"
#include "Panels/ConsolePanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <format>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
    // The top and bottom quarters of a row drop before and after it, and the middle drops inside it
    constexpr float c_EdgeFraction = 0.25f;
    constexpr float c_DropLineThickness = 2.0f;
    // How long a second click on a selected entity waits before it renames, at least, so the second click of a double-click never does
    constexpr double c_SlowClickSeconds = 0.5;
    // The keys the tree takes from ImGui's navigation while the Hierarchy has focus, so its focus box stays where it is as they move the selection
    constexpr std::array<ImGuiKey, 10> c_TreeKeys{ ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Home, ImGuiKey_End, ImGuiKey_PageUp, ImGuiKey_PageDown, ImGuiKey_Enter, ImGuiKey_KeypadEnter };

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

    bool IsAssetPayload(const ImGuiPayload* payload, const Trinity::AssetRegistry* registry, std::string_view importer)
    {
        const Trinity::AssetRecord* l_Record = payload != nullptr && payload->IsDataType(c_AssetPayload) && registry != nullptr ? registry->Find(ReadPayloadUUID(*payload)) : nullptr;

        return l_Record != nullptr && l_Record->Importer == importer;
    }

    // Unity's blue for a prefab's instance, darker on a light theme so it reads there, told by how bright the windows' background is, as the Console tells it
    ImVec4 GetPrefabColor()
    {
        const ImVec4 l_Background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        const bool l_Light = l_Background.x * 0.2126f + l_Background.y * 0.7152f + l_Background.z * 0.0722f > 0.5f;

        return l_Light ? ImVec4(0.1f, 0.35f, 0.8f, 1.0f) : ImVec4(0.45f, 0.7f, 1.0f, 1.0f);
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

    // Whether the text starts with the prefix, whatever the case of either
    bool StartsWithIgnoringCase(std::string_view text, std::string_view prefix)
    {
        return text.size() >= prefix.size() && std::ranges::equal(text.substr(0, prefix.size()), prefix, [](char left, char right) { return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right)); });
    }

    // A record named by its path, the end of its path after a /, as Models/Rock.fbx names /assets/Models/Rock.fbx, or its UUID
    bool NamesRecord(const Trinity::AssetRecord& record, std::string_view term)
    {
        const std::string_view l_Path(record.Path);

        return l_Path == term || (l_Path.size() > term.size() && l_Path.ends_with(term) && l_Path[l_Path.size() - term.size() - 1] == '/') || record.ID.ToString() == term;
    }

    // The class name after its namespace, as Game.Player and Game::Player both read Player
    std::string_view GetShortClassName(std::string_view name)
    {
        const std::size_t l_Separator = name.find_last_of(".:");

        return l_Separator == std::string_view::npos ? name : name.substr(l_Separator + 1);
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

    m_Rows.clear();
    if (ImGui::BeginChild("##Tree"))
    {
        if (std::exchange(m_FocusTree, false))
        {
            ImGui::SetWindowFocus();
        }

        m_PageRows = std::max(static_cast<std::size_t>(ImGui::GetContentRegionAvail().y / ImGui::GetFrameHeightWithSpacing()), std::size_t(1));
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

void HierarchyPanel::SetSearch(std::string search)
{
    m_Search = std::move(search);
    m_SearchChanged = true;
}

// Unity's search field. Ctrl+F comes to it, Escape clears it, and Down goes into the tree to the first match. Clearing it opens the tree to the selection
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

    // The rows are the last frame's, which show the matches for what the field held then
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
    {
        const auto a_First = std::ranges::find_if(m_Rows, [this](const Row& row) { return m_Search.empty() || m_Matched.contains(row.Entity); });
        if (a_First != m_Rows.end())
        {
            Select(a_First->Entity);
            m_KeepSelectionVisible = true;
        }

        ImGui::ClearActiveID();
        m_FocusTree = true;
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Matches names, whatever their case. t: matches a component or a script class an entity has, as t:Light, t:Collider or t:Player do, and ref: an asset or a script file it uses, as Find References In Scene in the Content Browser gives. They can be mixed, as in door t:Joint, and a term in quotes keeps its spaces. The parents of what matches show dimmed, and the Scene panel outlines the matches");
    }
}

// Each word must be in an entity's name, whatever its case, with t: terms the entity must have a component or a script class one of them names, so t:Renderer finds MeshRenderer and SpriteRenderer, and with ref: terms it must use an asset or a script one of them names. Each match shows with every parent above it, so the tree keeps its shape
void HierarchyPanel::UpdateSearch(Trinity::Scene& scene)
{
    // Terms are split by spaces, and a term's value in quotes keeps its spaces, as in ref:"Models/Old Rock.fbx"
    std::vector<std::string_view> l_Words;
    std::vector<std::string_view> l_Types;
    std::vector<std::string_view> l_References;
    const std::string_view l_Search(m_Search);
    for (std::size_t l_Start = l_Search.find_first_not_of(' '); l_Start != std::string_view::npos; l_Start = l_Search.find_first_not_of(' ', l_Start))
    {
        std::vector<std::string_view>* l_Terms = &l_Words;
        std::size_t l_ValueStart = l_Start;
        if (StartsWithIgnoringCase(l_Search.substr(l_Start), "t:"))
        {
            l_Terms = &l_Types;
            l_ValueStart += 2;
        }
        else if (StartsWithIgnoringCase(l_Search.substr(l_Start), "ref:"))
        {
            l_Terms = &l_References;
            l_ValueStart += 4;
        }

        std::string_view l_Value;
        std::size_t l_End = 0;
        if (l_ValueStart < l_Search.size() && l_Search[l_ValueStart] == '"')
        {
            const std::size_t l_Close = std::min(l_Search.find('"', l_ValueStart + 1), l_Search.size());
            l_Value = l_Search.substr(l_ValueStart + 1, l_Close - l_ValueStart - 1);
            l_End = std::min(l_Close + 1, l_Search.size());
        }
        else
        {
            l_End = std::min(l_Search.find(' ', l_ValueStart), l_Search.size());
            l_Value = l_Search.substr(l_ValueStart, l_End - l_ValueStart);
        }

        if (!l_Value.empty())
        {
            l_Terms->push_back(l_Value);
        }

        l_Start = l_End;
    }

    if (!l_References.empty())
    {
        std::string l_Key = std::format("{} {} {}", m_Session.GetScanCount(), m_Session.GetModelImportCount(), m_Session.GetHistory().GetPosition());
        for (const std::string_view it_Term : l_References)
        {
            l_Key += '\n';
            l_Key += it_Term;
        }

        if (l_Key != m_ReferencesKey)
        {
            ResolveReferences(l_References);
            m_ReferencesKey = std::move(l_Key);
        }
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
        if (!std::ranges::all_of(l_Words, [&l_Name](std::string_view word) { return ContainsIgnoringCase(l_Name, word); }) || (!l_Types.empty() && !a_HasType(it_Entity)) || (!l_References.empty() && !HasReference(it_Entity)))
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

// A term names an asset by its path, the end of its path, as Models/Rock.fbx does, or its UUID, and every asset of that name counts. A model brings what was imported from it, its meshes, materials and collision shapes, and a texture brings each material that samples it, read from its file. A script file, which is no asset, names the class it is named after, as Edit Script finds it
void HierarchyPanel::ResolveReferences(const std::vector<std::string_view>& terms)
{
    m_References = {};
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();

    std::unordered_map<Trinity::UUID, std::vector<Trinity::UUID>> l_Children;
    if (l_Registry != nullptr)
    {
        for (const Trinity::AssetRecord* it_Record : l_Registry->GetRecords())
        {
            if (it_Record->Parent.IsValid())
            {
                l_Children[it_Record->Parent].push_back(it_Record->ID);
            }
        }
    }

    std::vector<Trinity::UUID> l_Pending;
    for (const std::string_view it_Term : terms)
    {
        const std::size_t l_Slash = it_Term.find_last_of('/');
        const std::string_view l_File = l_Slash == std::string_view::npos ? it_Term : it_Term.substr(l_Slash + 1);
        const std::size_t l_Dot = l_File.rfind('.');
        std::string l_Extension(l_Dot == std::string_view::npos ? std::string_view() : l_File.substr(l_Dot));
        std::ranges::transform(l_Extension, l_Extension.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
        if (l_Extension == ".cs" || l_Extension == ".cpp" || l_Extension == ".hpp" || l_Extension == ".h")
        {
            m_References.Scripts.emplace_back(l_Extension == ".cs" ? Trinity::ScriptLanguage::CSharp : Trinity::ScriptLanguage::Cpp, std::string(l_File.substr(0, l_Dot)));

            continue;
        }

        if (l_Registry != nullptr)
        {
            for (const Trinity::AssetRecord* it_Record : l_Registry->GetRecords())
            {
                if (NamesRecord(*it_Record, it_Term))
                {
                    l_Pending.push_back(it_Record->ID);
                }
            }
        }
    }

    while (!l_Pending.empty())
    {
        const Trinity::UUID l_Asset = l_Pending.back();
        l_Pending.pop_back();
        if (!m_References.Assets.insert(l_Asset).second)
        {
            continue;
        }

        if (const auto a_Found = l_Children.find(l_Asset); a_Found != l_Children.end())
        {
            l_Pending.insert(l_Pending.end(), a_Found->second.begin(), a_Found->second.end());
        }
    }

    std::unordered_set<Trinity::UUID> l_Textures;
    for (const Trinity::UUID it_Asset : m_References.Assets)
    {
        const Trinity::AssetRecord* l_Record = l_Registry != nullptr ? l_Registry->Find(it_Asset) : nullptr;
        if (l_Record != nullptr && l_Record->Importer == Trinity::TextureAsset::c_AssetType)
        {
            l_Textures.insert(it_Asset);
        }
    }

    if (l_Textures.empty())
    {
        return;
    }

    for (const Trinity::AssetRecord* it_Record : l_Registry->GetRecords())
    {
        if (it_Record->Importer != ModelImporter::c_MaterialImporter)
        {
            continue;
        }

        const std::optional<Trinity::MaterialData> l_Material = m_Session.ReadMaterial(it_Record->ID);
        if (!l_Material)
        {
            continue;
        }

        for (const Trinity::MaterialTexture* it_Slot : { &l_Material->BaseColorTexture, &l_Material->MetallicRoughnessTexture, &l_Material->NormalTexture, &l_Material->OcclusionTexture, &l_Material->EmissiveTexture })
        {
            if (l_Textures.contains(it_Slot->Texture))
            {
                m_References.Assets.insert(it_Record->ID);

                break;
            }
        }
    }
}

// Whether a slot of the entity holds an asset the ref: terms name, or one of its scripts is of a class named after a script file they name
bool HierarchyPanel::HasReference(Trinity::Entity entity) const
{
    bool l_Found = false;
    ForEachSlotReference(entity, [this, &l_Found](const SlotReference& reference)
    {
        l_Found = l_Found || (!reference.IsEntity && reference.Value.IsValid() && m_References.Assets.contains(reference.Value));
    });

    if (l_Found || m_References.Scripts.empty() || !entity.Has<Trinity::ScriptComponent>())
    {
        return l_Found;
    }

    return std::ranges::any_of(entity.Get<Trinity::ScriptComponent>().Scripts, [this](const Trinity::ScriptEntry& script)
    {
        const std::string_view l_Name = GetShortClassName(std::string_view(script.ClassName));

        return std::ranges::any_of(m_References.Scripts, [&script, l_Name](const std::pair<Trinity::ScriptLanguage, std::string>& reference) { return reference.first == script.Language && reference.second == l_Name; });
    });
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

    if (const auto a_Request = m_OpenRequests.find(l_ID); a_Request != m_OpenRequests.end())
    {
        ImGui::SetNextItemOpen(a_Request->second);
        m_OpenRequests.erase(a_Request);
    }

    // As Unity colours them: a prefab's instance blue, its root and the prefab's entities in it alike, and an entity standing in for a missing prefab red. What is added to an instance is as any entity is
    const bool l_Dimmed = l_Searching && !m_Matched.contains(l_ID);
    const Trinity::Entity l_Instance = Trinity::Prefab::FindInstanceRoot(entity);
    const bool l_Colored = l_Dimmed || l_Instance;
    if (l_Colored)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, l_Dimmed ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : IsMissingPrefab(l_Instance) ? ConsolePanel::GetLevelColor(Trinity::LogLevel::Error) : GetPrefabColor());
    }

    const char* l_Icon = l_Instance == entity ? Trinity::Icons::c_PackageVariantClosed : Trinity::Icons::c_CubeOutline;
    const std::string l_Label = l_Renaming ? std::string(l_Icon) : std::format("{} {}", l_Icon, GetEntityLabel(entity));
    const bool l_Open = ImGui::TreeNodeEx(ToImGuiID(l_ID), l_Flags, "%s", l_Label.c_str());
    m_Rows.push_back({ .Entity = l_ID, .HasChildren = l_HasChildren, .Open = l_HasChildren && l_Open });
    if (l_Colored)
    {
        ImGui::PopStyleColor();
    }

    if (l_Selected && m_ScrollToSelection)
    {
        ImGui::SetScrollHereY();
        m_ScrollToSelection = false;
    }
    else if (l_Selected && std::exchange(m_KeepSelectionVisible, false))
    {
        ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeY);
    }

    // As in Unity, a press selects once it is released without a drag, so dragging an entity onto a field in Properties leaves Properties on the selection, while the arrow opens and closes and a right-click selects at once. A double-click frames the entity in the Scene panel, and a second, slower click on an entity already selected renames it
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
        m_Pressed = l_ID;
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !ImGui::IsItemToggledOpen())
    {
        Select(l_ID);
    }

    if (m_Pressed == l_ID && ImGui::IsItemDeactivated())
    {
        if (ImGui::IsItemHovered() && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left))
        {
            Select(l_ID);
        }

        m_Pressed = {};
    }

    if (ImGui::BeginPopupContextItem())
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

// While a copy plays, Copy alone, which takes the playing entity to paste once it stops
void HierarchyPanel::DrawContextMenu(Trinity::Entity entity)
{
    const Trinity::UUID l_ID = entity.GetUUID();
    ImGui::BeginDisabled(m_ReadOnly);
    if (ImGui::MenuItem(std::format("{} Create Empty Child", Trinity::Icons::c_CubeOutline).c_str(), "Alt+Shift+N"))
    {
        m_Deferred.push_back([this, l_ID] { Create(l_ID); });
    }

    DrawCreateMenus(l_ID);
    ImGui::EndDisabled();

    ImGui::Separator();

    if (ImGui::MenuItem(std::format("{} Copy", Trinity::Icons::c_Copy).c_str(), "Ctrl+C"))
    {
        m_Session.CopyEntity(l_ID);
    }

    ImGui::BeginDisabled(m_ReadOnly);
    if (ImGui::MenuItem(std::format("{} Paste", Trinity::Icons::c_Paste).c_str(), "Ctrl+V", false, m_Session.HasCopiedEntity()))
    {
        m_Deferred.push_back([this]
        {
            if (m_Session.PasteEntity())
            {
                m_Revealed = {};
            }
        });
    }

    if (ImGui::MenuItem(std::format("{} Paste As Child", Trinity::Icons::c_Paste).c_str(), "Ctrl+Shift+V", false, m_Session.HasCopiedEntity()))
    {
        m_Deferred.push_back([this, l_ID]
        {
            if (m_Session.PasteEntityAsChild(l_ID))
            {
                m_Revealed = {};
            }
        });
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Rename", "F2", false, entity.Has<Trinity::TagComponent>() && !IsMissingPrefab(entity)))
    {
        BeginRename(entity);
    }

    if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
    {
        m_Deferred.push_back([this, l_ID] { static_cast<void>(m_Session.DuplicateEntity(l_ID)); });
    }

    ImGui::Separator();

    if (ImGui::MenuItem(std::format("{} Delete", Trinity::Icons::c_Trash).c_str(), "Delete"))
    {
        m_Deferred.push_back([this, l_ID] { m_Session.GetHistory().Execute(Trinity::CreateScope<DeleteEntityCommand>(l_ID)); });
    }

    ImGui::EndDisabled();
}

// The space below the tree: a click clears the selection, a drop makes the entity the last root, or a model's hierarchy or a prefab's instance one, and its menu creates a root or pastes, the last two not while a copy plays
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
        if (ImGui::MenuItem(std::format("{} Create Empty", Trinity::Icons::c_CubeOutline).c_str(), "Ctrl+Shift+N"))
        {
            m_Deferred.push_back([this] { Create({}); });
        }

        DrawCreateMenus({});

        ImGui::Separator();

        if (ImGui::MenuItem(std::format("{} Paste", Trinity::Icons::c_Paste).c_str(), "Ctrl+V", false, m_Session.HasCopiedEntity()))
        {
            m_Deferred.push_back([this]
            {
                if (m_Session.PasteEntity())
                {
                    m_Revealed = {};
                }
            });
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
        else if (const ImGuiPayload* l_Model = IsAssetPayload(ImGui::GetDragDropPayload(), m_Session.GetRegistry(), ModelImporter::c_Importer) ? ImGui::AcceptDragDropPayload(c_AssetPayload) : nullptr)
        {
            const Trinity::UUID l_Dropped = ReadPayloadUUID(*l_Model);
            m_Deferred.push_back([this, l_Dropped] { static_cast<void>(m_Session.CreateModel(l_Dropped, {}, {}, glm::vec3(0.0f))); });
        }
        else if (const ImGuiPayload* l_Prefab = IsAssetPayload(ImGui::GetDragDropPayload(), m_Session.GetRegistry(), Trinity::PrefabAsset::c_AssetType) ? ImGui::AcceptDragDropPayload(c_AssetPayload) : nullptr)
        {
            const Trinity::UUID l_Dropped = ReadPayloadUUID(*l_Prefab);
            m_Deferred.push_back([this, l_Dropped] { static_cast<void>(m_Session.InstantiatePrefab(l_Dropped, {}, {}, std::nullopt)); });
        }

        ImGui::EndDragDropTarget();
    }
}

// Where the mouse is over the row picks before, inside or after, and a line or an outline shows which. An entity moves there, and a model's hierarchy or a prefab's instance is created there. A script goes onto the entity, as in Unity, so the whole row lights up for it
void HierarchyPanel::AcceptDrop(Trinity::Entity target)
{
    if (!ImGui::BeginDragDropTarget())
    {
        return;
    }

    if (const ImGuiPayload* l_Script = ImGui::AcceptDragDropPayload(c_ScriptPayload))
    {
        m_Deferred.push_back([this, l_Target = target.GetUUID(), l_Path = std::string(static_cast<const char*>(l_Script->Data))] { static_cast<void>(m_Session.AddScript(l_Target, l_Path)); });
    }

    const ImVec2 l_Min = ImGui::GetItemRectMin();
    const ImVec2 l_Max = ImGui::GetItemRectMax();
    const float l_Edge = (l_Max.y - l_Min.y) * c_EdgeFraction;
    const float l_MouseY = ImGui::GetMousePos().y;
    const bool l_Before = l_MouseY < l_Min.y + l_Edge;
    const bool l_After = !l_Before && l_MouseY > l_Max.y - l_Edge;

    const ImGuiDragDropFlags l_Flags = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
    const bool l_Model = IsAssetPayload(ImGui::GetDragDropPayload(), m_Session.GetRegistry(), ModelImporter::c_Importer);
    const bool l_Prefab = IsAssetPayload(ImGui::GetDragDropPayload(), m_Session.GetRegistry(), Trinity::PrefabAsset::c_AssetType);
    if (const ImGuiPayload* l_Payload = l_Model || l_Prefab ? ImGui::AcceptDragDropPayload(c_AssetPayload, l_Flags) : ImGui::AcceptDragDropPayload(c_EntityPayload, l_Flags))
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
            else if (l_Prefab)
            {
                m_Deferred.push_back([this, l_Dragged, l_Parent, l_Next] { static_cast<void>(m_Session.InstantiatePrefab(l_Dragged, l_Parent, l_Next, std::nullopt)); });
            }
            else
            {
                m_Deferred.push_back([this, l_Dragged, l_Parent, l_Next] { Move(l_Dragged, l_Parent, l_Next); });
            }
        }
    }

    ImGui::EndDragDropTarget();
}

// Rename, Ctrl+F to the search, F to frame the selection in the Scene panel, or Shift+F to follow it there, and the tree's keys, while the Hierarchy has focus, as in Unity, and not one of its menus, which keeps its own keys. Copy, Paste, Duplicate and Delete are Forge's own shortcuts, so they work from any panel
void HierarchyPanel::ReadShortcuts()
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_NoPopupHierarchy) || ImGui::GetIO().WantTextInput)
    {
        return;
    }

    ReadTreeKeys();
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

// Unity's tree keys, through the rows the last frame drew: Up and Down step through them, Home and End go to the first and the last, and PageUp and PageDown a page at a time. Left closes a branch or goes to its parent, Right opens it or goes to its first child, and with Alt either does every branch below it too. Enter renames, as on a Mac, and Ctrl+Up and Ctrl+Down move the entity among its siblings, as in Godot, the last two not while a copy plays
void HierarchyPanel::ReadTreeKeys()
{
    const ImGuiID l_Owner = ImGui::GetID("##TreeKeys");
    for (const ImGuiKey it_Key : c_TreeKeys)
    {
        ImGui::SetKeyOwner(it_Key, l_Owner);
    }

    if (m_Rows.empty())
    {
        return;
    }

    const auto a_Pressed = [l_Owner](ImGuiKeyChord chord) { return ImGui::IsKeyChordPressed(chord, ImGuiInputFlags_Repeat, l_Owner); };
    Trinity::Scene& l_Scene = m_Session.GetScene();
    const Trinity::Entity l_Selected = l_Scene.FindEntityByUUID(m_Session.GetSelection());
    const auto a_Row = std::ranges::find(m_Rows, m_Session.GetSelection(), &Row::Entity);
    const std::size_t l_Last = m_Rows.size() - 1;
    const std::size_t l_Index = static_cast<std::size_t>(std::distance(m_Rows.begin(), a_Row));
    const bool l_Shown = a_Row != m_Rows.end();

    std::optional<std::size_t> l_Target;
    if (a_Pressed(ImGuiKey_UpArrow))
    {
        l_Target = l_Shown ? (l_Index > 0 ? l_Index - 1 : 0) : l_Last;
    }
    else if (a_Pressed(ImGuiKey_DownArrow))
    {
        l_Target = l_Shown ? std::min(l_Index + 1, l_Last) : 0;
    }
    else if (a_Pressed(ImGuiKey_PageUp))
    {
        l_Target = l_Shown && l_Index > m_PageRows ? l_Index - m_PageRows : 0;
    }
    else if (a_Pressed(ImGuiKey_PageDown))
    {
        l_Target = l_Shown ? std::min(l_Index + m_PageRows, l_Last) : 0;
    }
    else if (a_Pressed(ImGuiKey_Home))
    {
        l_Target = 0;
    }
    else if (a_Pressed(ImGuiKey_End))
    {
        l_Target = l_Last;
    }
    else if (!l_Shown)
    {
        return;
    }
    else if (a_Pressed(ImGuiMod_Alt | ImGuiKey_LeftArrow))
    {
        SetBranchOpen(l_Selected, false, true);
    }
    else if (a_Pressed(ImGuiMod_Alt | ImGuiKey_RightArrow))
    {
        SetBranchOpen(l_Selected, true, true);
    }
    else if (a_Pressed(ImGuiKey_LeftArrow))
    {
        const Trinity::Entity l_Parent = l_Selected.GetParent();
        if (a_Row->Open)
        {
            SetBranchOpen(l_Selected, false, false);
        }
        else if (const auto a_ParentRow = std::ranges::find(m_Rows, GetID(l_Parent), &Row::Entity); l_Parent && a_ParentRow != m_Rows.end())
        {
            l_Target = static_cast<std::size_t>(std::distance(m_Rows.begin(), a_ParentRow));
        }
    }
    else if (a_Pressed(ImGuiKey_RightArrow))
    {
        if (a_Row->HasChildren && !a_Row->Open)
        {
            SetBranchOpen(l_Selected, true, false);
        }
        else if (a_Row->HasChildren)
        {
            l_Target = std::min(l_Index + 1, l_Last);
        }
    }
    else if (!m_ReadOnly && a_Pressed(ImGuiMod_Ctrl | ImGuiKey_UpArrow))
    {
        Reorder(l_Selected, true);
    }
    else if (!m_ReadOnly && a_Pressed(ImGuiMod_Ctrl | ImGuiKey_DownArrow))
    {
        Reorder(l_Selected, false);
    }
    else if (ImGui::IsKeyChordPressed(ImGuiKey_Enter, ImGuiInputFlags_None, l_Owner) || ImGui::IsKeyChordPressed(ImGuiKey_KeypadEnter, ImGuiInputFlags_None, l_Owner))
    {
        BeginRename(l_Selected);
    }

    if (l_Target)
    {
        Select(m_Rows[*l_Target].Entity);
        m_KeepSelectionVisible = true;
    }
}

// Below asks the same of every branch under it, which applies as each is next drawn, so a branch closed below one that is closed too is found closed once that one opens
void HierarchyPanel::SetBranchOpen(Trinity::Entity entity, bool open, bool below)
{
    m_OpenRequests[entity.GetUUID()] = open;
    for (Trinity::Entity it_Entity = below ? entity.GetFirstChild() : Trinity::Entity(); it_Entity; it_Entity = m_Session.GetScene().GetNextInSubtree(it_Entity, entity))
    {
        if (it_Entity.GetChildCount() > 0)
        {
            m_OpenRequests[it_Entity.GetUUID()] = open;
        }
    }

    m_KeepSelectionVisible = true;
}

// Before the sibling above, or after the one below, one command each. Its place in the world stays, since its parent does
void HierarchyPanel::Reorder(Trinity::Entity entity, bool up)
{
    const Trinity::Entity l_Previous = entity.GetPreviousSibling();
    const Trinity::Entity l_Next = entity.GetNextSibling();
    if (up ? !l_Previous : !l_Next)
    {
        return;
    }

    Move(entity.GetUUID(), GetID(entity.GetParent()), up ? l_Previous.GetUUID() : GetID(l_Next.GetNextSibling()));
    m_KeepSelectionVisible = true;
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
    if (m_ReadOnly || !entity.Has<Trinity::TagComponent>() || IsMissingPrefab(entity))
    {
        return;
    }

    Select(entity.GetUUID());
    m_Renaming = entity.GetUUID();
    m_RenameText = std::string(std::string_view(entity.Get<Trinity::TagComponent>().Tag));
    m_FocusRename = true;
}

void HierarchyPanel::Rename(Trinity::UUID entity)
{
    if (const Trinity::Entity l_Entity = m_Session.GetScene().FindEntityByUUID(entity))
    {
        BeginRename(l_Entity);
        m_Revealed = {};
    }
}

// As the Entity menu has them: a built-in shape with its collider, a light or a camera, under the parent or as a root, made once the tree is drawn and shown selected
void HierarchyPanel::DrawCreateMenus(Trinity::UUID parent)
{
    const auto a_Create = [this](std::move_only_function<bool()> create)
    {
        m_Deferred.push_back([this, l_Create = std::move(create)]() mutable
        {
            if (l_Create())
            {
                m_Revealed = {};
            }
        });
    };

    if (ImGui::BeginMenu(std::format("{} 3D Object", Trinity::Icons::c_Cube).c_str()))
    {
        for (const Trinity::BuiltInMesh it_Mesh : Trinity::c_BuiltInMeshes)
        {
            if (ImGui::MenuItem(std::string(Trinity::ToString(it_Mesh)).c_str()))
            {
                a_Create([this, it_Mesh, parent] { return m_Session.CreatePrimitive(it_Mesh, parent); });
            }
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(std::format("{} Light", Trinity::Icons::c_Globe).c_str()))
    {
        for (const Trinity::LightType it_Type : c_LightTypes)
        {
            if (ImGui::MenuItem(std::string(GetLightLabel(it_Type)).c_str()))
            {
                a_Create([this, it_Type, parent] { return m_Session.CreateLight(it_Type, parent); });
            }
        }

        ImGui::EndMenu();
    }

    if (ImGui::MenuItem(std::format("{} Camera", Trinity::Icons::c_Monitor).c_str()))
    {
        a_Create([this, parent] { return m_Session.CreateCamera(parent); });
    }
}

// A new entity is selected and named at once
void HierarchyPanel::Create(Trinity::UUID parent)
{
    if (m_Session.CreateEmpty(parent))
    {
        Rename(m_Session.GetSelection());
    }
}

// Keeping the world transform, so a dragged entity stays where it is on screen
void HierarchyPanel::Move(Trinity::UUID entity, Trinity::UUID parent, Trinity::UUID before)
{
    static_cast<void>(m_Session.MoveEntity(entity, parent, before));
}