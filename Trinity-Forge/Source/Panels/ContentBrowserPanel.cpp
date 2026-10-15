#include "Panels/ContentBrowserPanel.hpp"

#include "EditorPayloads.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <format>
#include <tuple>
#include <utility>

namespace
{
    constexpr const char* c_DeletePopup = "Delete###ForgeDeleteAsset";
    constexpr const char* c_NewScriptPopup = "New Script###ForgeNewScript";
    constexpr float c_MinimumThumbnailSize = 3.0f;
    constexpr float c_MaximumThumbnailSize = 12.0f;
    constexpr float c_IconScale = 0.55f;
    constexpr float c_TreeWidthInFonts = 14.0f;

    std::string GetParent(std::string_view path)
    {
        return std::string(path.substr(0, path.find_last_of('/')));
    }

    std::string_view GetName(std::string_view path)
    {
        return path.substr(path.find_last_of('/') + 1);
    }

    bool IsSameOrInside(std::string_view path, std::string_view folder)
    {
        return path == folder || (path.starts_with(folder) && path.size() > folder.size() && path[folder.size()] == '/');
    }

    struct Root
    {
        std::string_view Path;
        std::string_view Label;
    };

    constexpr std::array<Root, 3> c_Roots{ { { Trinity::Project::c_AssetsMount, "Assets" },{ EditorSession::c_ScriptsFolder, "Scripts" },{ EditorSession::c_SourceFolder, "Source" } } };

    // The root the path is in, or none
    const Root* FindRoot(std::string_view path)
    {
        const auto a_Found = std::ranges::find_if(c_Roots, [path](const Root& root) { return IsSameOrInside(path, root.Path); });

        return a_Found != c_Roots.end() ? &*a_Found : nullptr;
    }

    // A file a script editor opens, by its extension, or a module's CMakeLists.txt
    bool IsScriptFile(std::string_view name)
    {
        constexpr std::array<std::string_view, 9> c_Extensions{ ".cs", ".cpp", ".hpp", ".h", ".inl", ".csproj", ".props", ".targets", ".cmake" };

        return name == "CMakeLists.txt" || std::ranges::any_of(c_Extensions, [name](std::string_view extension) { return name.ends_with(extension); });
    }

    bool LessIgnoringCase(std::string_view left, std::string_view right)
    {
        return std::ranges::lexicographical_compare(left, right, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) < std::tolower(static_cast<unsigned char>(b)); });
    }

    // A file is renamed without its extension, so a texture stays a texture
    std::string_view GetStem(const std::string& name)
    {
        const std::size_t l_Dot = name.find_last_of('.');

        return l_Dot == std::string::npos || l_Dot == 0 ? std::string_view(name) : std::string_view(name).substr(0, l_Dot);
    }

    const char* GetIcon(const Trinity::AssetRecord* record, bool folder, std::string_view name)
    {
        if (folder)
        {
            return Trinity::Icons::c_Folder;
        }

        if (record == nullptr && IsScriptFile(name))
        {
            return Trinity::Icons::c_FileCode;
        }

        if (record != nullptr && record->Importer == Trinity::TextureAsset::c_AssetType)
        {
            return Trinity::Icons::c_FileImage;
        }

        if (record != nullptr && record->Importer == ModelImporter::c_Importer)
        {
            return Trinity::Icons::c_Cube;
        }

        if (record != nullptr && record->Importer == Trinity::MaterialAsset::c_AssetType)
        {
            return Trinity::Icons::c_PaintBrush;
        }

        if (record != nullptr && record->Importer == Trinity::EnvironmentAsset::c_AssetType)
        {
            return Trinity::Icons::c_Globe;
        }

        return record != nullptr && record->Importer == "Scene" ? Trinity::Icons::c_CubeOutline : Trinity::Icons::c_File;
    }
}

ContentBrowserPanel::ContentBrowserPanel(EditorSession& session) : Panel("Content Browser", Trinity::Icons::c_FolderOpen, DockSlot::Bottom), m_Session(session), m_Folder(Trinity::Project::c_AssetsMount)
{
    m_CloseListener = m_Session.AddCloseListener([this] { ReleaseAll(); });
}

ContentBrowserPanel::~ContentBrowserPanel()
{
    m_Session.RemoveCloseListener(m_CloseListener);
}

// After every panel is drawn. Thumbnails not drawn this frame, scrolled away, in another folder or in a hidden panel, let go of their textures
void ContentBrowserPanel::EndFrame()
{
    const int l_Frame = ImGui::GetFrameCount();
    std::erase_if(m_Thumbnails, [l_Frame](const auto& thumbnail) { return thumbnail.second.LastDrawn != l_Frame; });
    if (m_Thumbnails.empty())
    {
        decltype(m_Thumbnails)().swap(m_Thumbnails);
    }

    if (m_DrawnFrame != l_Frame)
    {
        m_Focused = false;
    }
}

// The folder tree on the left and the open folder's contents on the right. Moves and deletes wait until both are drawn, since they change what is being listed
void ContentBrowserPanel::OnImGuiRender()
{
    m_DrawnFrame = ImGui::GetFrameCount();
    m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!m_Session.HasProject())
    {
        ImGui::TextDisabled("Open or create a project to see its assets");

        return;
    }

    if (m_ListedScan != m_Session.GetScanCount())
    {
        m_ListedScan = m_Session.GetScanCount();
        m_Listings.clear();
    }

    if (!m_Session.GetProject() || !std::filesystem::is_directory(m_Session.GetProject()->ToNativePath(m_Folder)))
    {
        m_Folder = std::string(Trinity::Project::c_AssetsMount);
    }

    ReadShortcuts();

    if (ImGui::BeginTable("##ContentBrowser", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail()))
    {
        ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * c_TreeWidthInFonts);
        ImGui::TableSetupColumn("Contents", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        // Scripts and Source once the project has them
        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("##Folders"))
        {
            for (const Root& it_Root : c_Roots)
            {
                if (it_Root.Path == Trinity::Project::c_AssetsMount || std::filesystem::is_directory(m_Session.GetProject()->ToNativePath(it_Root.Path)))
                {
                    DrawFolderTree(std::string(it_Root.Path), std::string(it_Root.Label));
                }
            }
        }

        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        DrawToolbar();
        if (ImGui::BeginChild("##Contents"))
        {
            DrawGrid();
        }

        ImGui::EndChild();
        ImGui::EndTable();
    }

    DrawDeletePopup();
    DrawNewScriptPopup();

    for (std::move_only_function<void()>& it_Action : std::exchange(m_Deferred, {}))
    {
        it_Action();
    }
}

void ContentBrowserPanel::DrawFolderTree(const std::string& path, const std::string& label)
{
    std::vector<std::string> l_Children;
    for (const Trinity::DirectoryEntry& it_Entry : List(path))
    {
        if (it_Entry.Type == Trinity::FileType::Directory)
        {
            l_Children.push_back(it_Entry.Name);
        }
    }

    ImGuiTreeNodeFlags l_Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (l_Children.empty())
    {
        l_Flags |= ImGuiTreeNodeFlags_Leaf;
    }

    const Root* l_Root = FindRoot(path);
    if (l_Root != nullptr && l_Root->Path == path)
    {
        l_Flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    if (path == m_Folder)
    {
        l_Flags |= ImGuiTreeNodeFlags_Selected;
    }

    // The open folder's ancestors open with it, wherever it was opened from
    if (m_Folder.starts_with(path + "/"))
    {
        ImGui::SetNextItemOpen(true);
    }

    const bool l_Open = ImGui::TreeNodeEx(path.c_str(), l_Flags, "%s", std::format("{} {}", Trinity::Icons::c_Folder, label).c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        m_Folder = path;
    }

    AcceptMove(path);

    if (l_Open)
    {
        for (const std::string& it_Child : l_Children)
        {
            DrawFolderTree(std::format("{}/{}", path, it_Child), it_Child);
        }

        ImGui::TreePop();
    }
}

// Up, then the open folder's path from its root, each part of which opens it and takes a drop
void ContentBrowserPanel::DrawToolbar()
{
    const Root* l_Root = FindRoot(m_Folder);
    ImGui::BeginDisabled(l_Root == nullptr || m_Folder == l_Root->Path);
    if (ImGui::Button(Trinity::Icons::c_ArrowUp))
    {
        m_Folder = GetParent(m_Folder);
    }

    ImGui::EndDisabled();

    std::string l_Path;
    std::size_t l_Start = l_Root != nullptr ? l_Root->Path.size() : 1;
    while (l_Start <= m_Folder.size())
    {
        const std::size_t l_End = std::min(m_Folder.find('/', l_Start), m_Folder.size());
        l_Path = m_Folder.substr(0, l_End);
        const std::string l_Name = l_Root != nullptr && l_Path == l_Root->Path ? std::string(l_Root->Label) : std::string(GetName(l_Path));

        ImGui::SameLine();
        if (ImGui::Button(std::format("{}###Crumb{}", l_Name, l_Path).c_str()))
        {
            m_Folder = l_Path;
        }

        AcceptMove(l_Path);
        l_Start = l_End + 1;
    }

    // The thumbnail size at the right end, when there is room for it
    const float l_SliderWidth = ImGui::GetFontSize() * 8.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(ImGui::GetContentRegionAvail().x - l_SliderWidth, 0.0f));
    ImGui::SetNextItemWidth(l_SliderWidth);
    ImGui::SliderFloat("##ThumbnailSize", &m_ThumbnailSize, c_MinimumThumbnailSize, c_MaximumThumbnailSize, "", ImGuiSliderFlags_AlwaysClamp);
}

// Folders first, then files, each by name, in as many columns as fit
void ContentBrowserPanel::DrawGrid()
{
    const float l_Size = std::floor(ImGui::GetFontSize() * m_ThumbnailSize);
    const float l_Spacing = ImGui::GetStyle().ItemSpacing.x;
    const int l_Columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + l_Spacing) / (l_Size + l_Spacing)));

    const std::vector<Item> l_Items = GetItems(m_Folder);
    for (std::size_t it_Index = 0; it_Index < l_Items.size(); ++it_Index)
    {
        if (it_Index % static_cast<std::size_t>(l_Columns) != 0)
        {
            ImGui::SameLine();
        }

        DrawItem(l_Items[it_Index], l_Size);
    }

    if (l_Items.empty())
    {
        ImGui::TextDisabled("This folder is empty");
    }

    DrawBackground();
}

// A thumbnail or an icon over the name. Textures are only loaded for items on screen
void ContentBrowserPanel::DrawItem(const Item& item, float size)
{
    ImGui::PushID(item.Path.c_str());

    const float l_LabelHeight = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 l_Min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##Item", ImVec2(size, size + l_LabelHeight));
    const bool l_Visible = ImGui::IsItemVisible();
    const bool l_Hovered = ImGui::IsItemHovered();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        Select(item);
    }

    if (l_Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        Open(item);
    }

    if (ImGui::BeginPopupContextItem())
    {
        DrawItemMenu(item);
        ImGui::EndPopup();
    }

    if (ImGui::BeginDragDropSource())
    {
        if (item.Folder)
        {
            ImGui::SetDragDropPayload(c_FolderPayload, item.Path.c_str(), item.Path.size() + 1);
        }
        else if (item.Record != nullptr)
        {
            const std::uint64_t l_Value = item.Record->ID.GetValue();
            ImGui::SetDragDropPayload(c_AssetPayload, &l_Value, sizeof(l_Value));
        }

        ImGui::TextUnformatted(std::format("{} {}", GetIcon(item.Record, item.Folder, item.Name), item.Name).c_str());
        ImGui::EndDragDropSource();
    }

    if (item.Folder)
    {
        AcceptMove(item.Path);
    }

    if (l_Visible)
    {
        ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
        const ImVec2 l_Max(l_Min.x + size, l_Min.y + size + l_LabelHeight);
        if (item.Path == m_Selected || l_Hovered)
        {
            l_DrawList.AddRectFilled(l_Min, l_Max, ImGui::GetColorU32(item.Path == m_Selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), ImGui::GetStyle().FrameRounding);
        }

        const float l_Padding = ImGui::GetStyle().FramePadding.x;
        const ImVec2 l_ImageMin(l_Min.x + l_Padding, l_Min.y + l_Padding);
        const float l_ImageSize = size - l_Padding * 2.0f;
        const Trinity::TextureAsset* l_Texture = item.Record != nullptr && item.Record->Importer == Trinity::TextureAsset::c_AssetType ? GetThumbnail(item.Record->ID) : nullptr;
        if (l_Texture != nullptr)
        {
            // Fitted inside the square, keeping the texture's shape
            const float l_Aspect = static_cast<float>(l_Texture->GetWidth()) / static_cast<float>(std::max(l_Texture->GetHeight(), 1u));
            const ImVec2 l_Fit = l_Aspect >= 1.0f ? ImVec2(l_ImageSize, l_ImageSize / l_Aspect) : ImVec2(l_ImageSize * l_Aspect, l_ImageSize);
            const ImVec2 l_FitMin(l_ImageMin.x + (l_ImageSize - l_Fit.x) * 0.5f, l_ImageMin.y + (l_ImageSize - l_Fit.y) * 0.5f);
            l_DrawList.AddImage(ImTextureRef(static_cast<ImTextureID>(Trinity::GetImGuiTextureID(*l_Texture))), l_FitMin, ImVec2(l_FitMin.x + l_Fit.x, l_FitMin.y + l_Fit.y));
        }
        else
        {
            const char* l_Icon = GetIcon(item.Record, item.Folder, item.Name);
            const float l_IconSize = l_ImageSize * c_IconScale;
            const ImVec2 l_IconExtent = ImGui::GetFont()->CalcTextSizeA(l_IconSize, FLT_MAX, 0.0f, l_Icon);
            l_DrawList.AddText(ImGui::GetFont(), l_IconSize, ImVec2(l_ImageMin.x + (l_ImageSize - l_IconExtent.x) * 0.5f, l_ImageMin.y + (l_ImageSize - l_IconExtent.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), l_Icon);
        }

        const ImVec2 l_LabelMin(l_Min.x + l_Padding, l_Min.y + size);
        if (item.Path == m_Renaming)
        {
            DrawRenameField(item, l_LabelMin, size - l_Padding * 2.0f);
        }
        else
        {
            const ImVec2 l_TextSize = ImGui::CalcTextSize(item.Name.c_str());
            const float l_TextX = l_TextSize.x < size - l_Padding * 2.0f ? l_Min.x + (size - l_TextSize.x) * 0.5f : l_LabelMin.x;
            ImGui::RenderTextEllipsis(&l_DrawList, ImVec2(l_TextX, l_LabelMin.y), ImVec2(l_Min.x + size - l_Padding, l_Max.y), l_Min.x + size - l_Padding, item.Name.c_str(), nullptr, &l_TextSize);
        }

        if (l_Hovered && !ImGui::IsDragDropActive())
        {
            ImGui::SetItemTooltip("%s", item.Path.c_str());
        }
    }

    ImGui::PopID();
}

void ContentBrowserPanel::DrawItemMenu(const Item& item)
{
    if ((item.Folder || (item.Record != nullptr && item.Record->Importer == "Scene")) && ImGui::MenuItem("Open"))
    {
        Open(item);
    }

    if (!item.Folder && item.Record == nullptr && IsScriptFile(item.Name) && ImGui::MenuItem(std::format("{} Open in {}", Trinity::Icons::c_FileCode, IdeLauncher::ToString(m_Session.GetScriptEditor())).c_str()))
    {
        Open(item);
    }

    if (ImGui::MenuItem("Rename", "F2"))
    {
        BeginRename(item);
    }

    ImGui::Separator();

    if (ImGui::MenuItem(std::format("{} Delete", Trinity::Icons::c_Trash).c_str(), "Delete"))
    {
        m_PendingDelete = item.Path;
        m_OpenDeletePopup = true;
    }
}

// Enter or clicking away renames. Escape puts the text back, so nothing changes
void ContentBrowserPanel::DrawRenameField(const Item& item, ImVec2 position, float width)
{
    const ImVec2 l_Cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(position);
    ImGui::SetNextItemWidth(width);
    if (std::exchange(m_FocusRename, false))
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::InputText("##Rename", &m_RenameText, ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemDeactivated())
    {
        m_Renaming.clear();
        const std::string l_Name = item.Folder ? m_RenameText : m_RenameText + item.Name.substr(GetStem(item.Name).size());
        if (l_Name != item.Name)
        {
            m_Deferred.push_back([this, l_Path = item.Path, l_Name]
            {
                if (m_Session.MoveAsset(l_Path, GetParent(l_Path), l_Name))
                {
                    m_Selected = std::format("{}/{}", GetParent(l_Path), l_Name);
                }

                m_Listings.clear();
            });
        }
    }

    ImGui::SetCursorScreenPos(l_Cursor);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// The space after the items: a click clears the selection, its menu makes a folder, a material among the assets and a script in Scripts or Source, and a drop moves into the open folder
void ContentBrowserPanel::DrawBackground()
{
    const ImVec2 l_Available = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##Background", ImVec2(std::max(l_Available.x, 1.0f), std::max(l_Available.y, ImGui::GetFrameHeight())));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        m_Selected.clear();
    }

    if (ImGui::BeginPopupContextItem("##BackgroundMenu"))
    {
        if (ImGui::MenuItem(std::format("{} New Folder", Trinity::Icons::c_Folder).c_str()))
        {
            m_Deferred.push_back([this]
            {
                if (const std::optional<std::string> l_Folder = m_Session.CreateFolder(m_Folder))
                {
                    m_Listings.clear();
                    BeginRename({ std::string(GetName(*l_Folder)), *l_Folder, true, nullptr });
                }
            });
        }

        if (IsSameOrInside(m_Folder, Trinity::Project::c_AssetsMount) && ImGui::MenuItem(std::format("{} New Material", Trinity::Icons::c_PaintBrush).c_str()))
        {
            m_Deferred.push_back([this]
            {
                if (const std::optional<std::string> l_Material = m_Session.CreateMaterial(m_Folder))
                {
                    m_Listings.clear();
                    BeginRename({ std::string(GetName(*l_Material)), *l_Material, false, m_Session.GetRegistry()->FindByPath(*l_Material) });
                }
            });
        }

        // The name is asked for first, since it names the class as well as the file
        for (const auto& [it_Language, it_Folder, it_Label] : { std::tuple(Trinity::ScriptLanguage::CSharp, EditorSession::c_ScriptsFolder, "New C# Script..."), std::tuple(Trinity::ScriptLanguage::Cpp, EditorSession::c_SourceFolder, "New C++ Script...") })
        {
            if (IsSameOrInside(m_Folder, it_Folder) && ImGui::MenuItem(std::format("{} {}", Trinity::Icons::c_FileCode, it_Label).c_str()))
            {
                m_NewScript = it_Language;
                m_NewScriptName = "NewScript";
                m_OpenNewScriptPopup = true;
            }
        }

        if (ImGui::MenuItem(std::format("{} Refresh", Trinity::Icons::c_Refresh).c_str(), "F5"))
        {
            m_Session.Request(EditorSession::Command::Refresh);
        }

        ImGui::EndPopup();
    }

    AcceptMove(m_Folder);
}

// Asks first, since a deleted file is gone for good
void ContentBrowserPanel::DrawDeletePopup()
{
    if (std::exchange(m_OpenDeletePopup, false))
    {
        ImGui::OpenPopup(c_DeletePopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(c_DeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    const bool l_Folder = m_Session.GetProject() != nullptr && std::filesystem::is_directory(m_Session.GetProject()->ToNativePath(m_PendingDelete));
    ImGui::TextUnformatted(std::format("{} Delete {}{}?", Trinity::Icons::c_Warning, GetName(m_PendingDelete), l_Folder ? " and everything in it" : "").c_str());
    ImGui::TextDisabled("This cannot be undone, and anything using it will find it missing");
    ImGui::Spacing();

    if (ImGui::Button("Delete"))
    {
        m_Deferred.push_back([this, l_Path = std::exchange(m_PendingDelete, {})]
        {
            static_cast<void>(m_Session.DeleteAsset(l_Path));
            m_Selected.clear();
            m_Listings.clear();
        });
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        m_PendingDelete.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// The class's name, which names its file too. Enter makes it, and Escape or Cancel makes nothing
void ContentBrowserPanel::DrawNewScriptPopup()
{
    if (std::exchange(m_OpenNewScriptPopup, false))
    {
        ImGui::OpenPopup(c_NewScriptPopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(c_NewScriptPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    const bool l_CSharp = m_NewScript == Trinity::ScriptLanguage::CSharp;
    ImGui::TextUnformatted(std::format("{} A new {} script in {}", Trinity::Icons::c_FileCode, l_CSharp ? "C#" : "C++", m_Folder).c_str());
    ImGui::TextDisabled("Its class and its file take this name");
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
    const bool l_Entered = ImGui::InputText("##ScriptName", &m_NewScriptName, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::Spacing();

    if (ImGui::Button("Create") || l_Entered)
    {
        m_Deferred.push_back([this, l_Language = m_NewScript.value_or(Trinity::ScriptLanguage::CSharp), l_Name = m_NewScriptName]
        {
            if (const std::optional<std::string> l_Script = m_Session.CreateScript(m_Folder, l_Language, l_Name))
            {
                m_Selected = *l_Script;
            }

            m_Listings.clear();
        });
        m_NewScript.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        m_NewScript.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// Files arrive as asset UUIDs, which the Viewport and the texture slot take too, and folders as paths
void ContentBrowserPanel::AcceptMove(const std::string& folder)
{
    if (!ImGui::BeginDragDropTarget())
    {
        return;
    }

    if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_AssetPayload))
    {
        std::uint64_t l_Value = 0;
        std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));
        const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
        if (const Trinity::AssetRecord* l_Record = l_Registry != nullptr ? l_Registry->Find(Trinity::UUID(l_Value)) : nullptr)
        {
            m_Deferred.push_back([this, l_Path = l_Record->Path, folder] { Move(l_Path, folder); });
        }
    }

    if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_FolderPayload))
    {
        m_Deferred.push_back([this, l_Path = std::string(static_cast<const char*>(l_Payload->Data)), folder] { Move(l_Path, folder); });
    }

    ImGui::EndDragDropTarget();
}

// While the browser has focus, Delete and F2 act on its selection rather than on the scene's
void ContentBrowserPanel::ReadShortcuts()
{
    if (!m_Focused || ImGui::GetIO().WantTextInput || m_Selected.empty())
    {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        m_PendingDelete = m_Selected;
        m_OpenDeletePopup = true;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
    {
        for (const Item& it_Item : GetItems(GetParent(m_Selected)))
        {
            if (it_Item.Path == m_Selected)
            {
                BeginRename(it_Item);
            }
        }
    }
}

// A file is shown in Properties, where a texture's import settings are
void ContentBrowserPanel::Select(const Item& item)
{
    m_Selected = item.Path;
    m_Session.SetInspectedAsset(item.Record != nullptr ? item.Record->ID : Trinity::UUID());
}

void ContentBrowserPanel::Open(const Item& item)
{
    if (item.Folder)
    {
        m_Deferred.push_back([this, l_Path = item.Path] { m_Folder = l_Path; });
    }
    else if (item.Record != nullptr && item.Record->Importer == "Scene")
    {
        m_Session.RequestOpenScene(item.Path);
    }
    else if (item.Record == nullptr && IsScriptFile(item.Name))
    {
        m_Deferred.push_back([this, l_Path = item.Path] { static_cast<void>(m_Session.OpenScript(l_Path)); });
    }
}

void ContentBrowserPanel::BeginRename(const Item& item)
{
    m_Selected = item.Path;
    m_Renaming = item.Path;
    m_RenameText = item.Folder ? item.Name : std::string(GetStem(item.Name));
    m_FocusRename = true;
}

// Within its root, since a script among the assets, or an asset among the scripts, would no longer be what it is
void ContentBrowserPanel::Move(const std::string& path, const std::string& folder)
{
    if (GetParent(path) == folder)
    {
        return;
    }

    if (FindRoot(path) != FindRoot(folder))
    {
        TR_ERROR("Forge: {} stays in {}, so it was not moved to {}", path, FindRoot(path) != nullptr ? FindRoot(path)->Label : std::string_view("its folder"), folder);

        return;
    }

    const std::string l_Name(GetName(path));
    if (m_Session.MoveAsset(path, folder, l_Name))
    {
        if (m_Folder == path || m_Folder.starts_with(path + "/"))
        {
            m_Folder = std::format("{}/{}{}", folder, l_Name, m_Folder.substr(path.size()));
        }

        m_Selected = std::format("{}/{}", folder, l_Name);
    }

    m_Listings.clear();
}

void ContentBrowserPanel::ReleaseAll()
{
    m_Thumbnails.clear();
    m_Listings.clear();
    m_Folder = std::string(Trinity::Project::c_AssetsMount);
    m_Selected.clear();
    m_Renaming.clear();
}

// Read from disk once, and again after a scan, so drawing never touches the disk
const std::vector<Trinity::DirectoryEntry>& ContentBrowserPanel::List(const std::string& folder)
{
    const auto [a_Found, a_Inserted] = m_Listings.try_emplace(folder);
    if (a_Inserted)
    {
        if (const Trinity::Expected<std::vector<Trinity::DirectoryEntry>, Trinity::FileError> l_Entries = Trinity::FileSystem::List(folder))
        {
            for (const Trinity::DirectoryEntry& it_Entry : *l_Entries)
            {
                if (!it_Entry.Name.starts_with('.') && !it_Entry.Name.ends_with(Trinity::AssetRegistry::c_MetaExtension))
                {
                    a_Found->second.push_back(it_Entry);
                }
            }
        }

        std::ranges::sort(a_Found->second, [](const Trinity::DirectoryEntry& left, const Trinity::DirectoryEntry& right)
        {
            const bool l_LeftFolder = left.Type == Trinity::FileType::Directory;
            const bool l_RightFolder = right.Type == Trinity::FileType::Directory;

            return l_LeftFolder != l_RightFolder ? l_LeftFolder : LessIgnoringCase(left.Name, right.Name);
        });
    }

    return a_Found->second;
}

std::vector<ContentBrowserPanel::Item> ContentBrowserPanel::GetItems(const std::string& folder)
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    std::vector<Item> l_Items;
    for (const Trinity::DirectoryEntry& it_Entry : List(folder))
    {
        Item l_Item;
        l_Item.Name = it_Entry.Name;
        l_Item.Path = std::format("{}/{}", folder, it_Entry.Name);
        l_Item.Folder = it_Entry.Type == Trinity::FileType::Directory;
        l_Item.Record = !l_Item.Folder && l_Registry != nullptr ? l_Registry->FindByPath(l_Item.Path) : nullptr;
        l_Items.push_back(std::move(l_Item));
    }

    return l_Items;
}

// The texture itself, once it has loaded and been uploaded. Until then the item shows its icon
const Trinity::TextureAsset* ContentBrowserPanel::GetThumbnail(Trinity::UUID id)
{
    auto a_Found = m_Thumbnails.find(id);
    if (a_Found == m_Thumbnails.end())
    {
        a_Found = m_Thumbnails.emplace(id, Thumbnail{ Trinity::AssetRef<Trinity::TextureAsset>(id), 0 }).first;
    }

    a_Found->second.LastDrawn = ImGui::GetFrameCount();
    const Trinity::TextureAsset* l_Texture = a_Found->second.Texture.IsReady() ? a_Found->second.Texture.Get() : nullptr;

    return l_Texture != nullptr && l_Texture->GetTexture() && l_Texture->GetShaderResourceIndex() != Trinity::RHI::c_NoBindlessIndex ? l_Texture : nullptr;
}