#include "Panels/ContentBrowserPanel.hpp"

#include "EditorPayloads.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <utility>

namespace
{
    // Shared by every question the browser asks, each of which titles it
    constexpr const char* c_ConfirmPopup = "###ForgeConfirmAsset";
    constexpr const char* c_NewScriptPopup = "New Script###ForgeNewScript";
    // The slider's leftmost notch is the list, and the smallest grid is the next, with nothing between them
    constexpr float c_ListSize = 2.0f;
    constexpr float c_MinimumThumbnailSize = 3.0f;
    constexpr float c_MaximumThumbnailSize = 12.0f;
    constexpr float c_WheelStep = 0.5f;
    constexpr float c_IconScale = 0.55f;
    constexpr float c_TreeWidthInFonts = 14.0f;
    constexpr float c_SearchWidthInFonts = 14.0f;
    constexpr float c_SliderWidthInFonts = 6.0f;
    constexpr std::string_view c_SizeSetting = "ContentBrowserSize";

#if defined(TR_PLATFORM_WINDOWS)
    constexpr const char* c_ShowInFileManager = "Show in Explorer";
#elif defined(TR_PLATFORM_MACOS)
    constexpr const char* c_ShowInFileManager = "Reveal in Finder";
#else
    constexpr const char* c_ShowInFileManager = "Show in File Manager";
#endif

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

    // Clamped to the slider, and between the list and the smallest grid, whichever is nearer
    float SnapSize(float size)
    {
        const float l_Size = std::clamp(size, c_ListSize, c_MaximumThumbnailSize);
        if (l_Size >= c_MinimumThumbnailSize)
        {
            return l_Size;
        }

        return l_Size - c_ListSize < c_MinimumThumbnailSize - l_Size ? c_ListSize : c_MinimumThumbnailSize;
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

    // A root itself, which is never renamed, duplicated, moved or deleted
    bool IsRoot(std::string_view path)
    {
        const Root* l_Root = FindRoot(path);

        return l_Root != nullptr && l_Root->Path == path;
    }

    // Assets always, and Scripts and Source once the project has them
    bool IsShown(const Root& root, const Trinity::Project& project)
    {
        return root.Path == Trinity::Project::c_AssetsMount || std::filesystem::is_directory(project.ToNativePath(root.Path));
    }

    // A file a script editor opens, by its extension, or a module's CMakeLists.txt
    bool IsScriptFile(std::string_view name)
    {
        constexpr std::array<std::string_view, 9> c_Extensions{ ".cs", ".cpp", ".hpp", ".h", ".inl", ".csproj", ".props", ".targets", ".cmake" };

        return name == "CMakeLists.txt" || std::ranges::any_of(c_Extensions, [name](std::string_view extension) { return name.ends_with(extension); });
    }

    // A script file that holds a class, which Find References In Scene finds by the class it is named after
    bool IsClassFile(std::string_view name)
    {
        constexpr std::array<std::string_view, 4> c_Extensions{ ".cs", ".cpp", ".hpp", ".h" };

        return std::ranges::any_of(c_Extensions, [name](std::string_view extension) { return name.ends_with(extension); });
    }

    // The ref: term that names a file, by its path inside its root, as Models/Rock.fbx names /assets/Models/Rock.fbx, in quotes when the path has a space
    std::string GetReferenceSearch(std::string_view path)
    {
        const std::size_t l_Root = path.find('/', 1);
        const std::string_view l_Inside = l_Root == std::string_view::npos ? path : path.substr(l_Root + 1);

        return l_Inside.find(' ') == std::string_view::npos ? std::format("ref:{}", l_Inside) : std::format("ref:\"{}\"", l_Inside);
    }

    // What an importer cooks, and so what Reimport cooks again
    bool IsImported(const Trinity::AssetRecord* record)
    {
        return record != nullptr && (record->Importer == TextureImporter::c_Importer || record->Importer == ModelImporter::c_Importer || record->Importer == EnvironmentImporter::c_Importer);
    }

    // Properties edits a material, so the system has no program to open it in
    bool IsMaterial(const Trinity::AssetRecord* record)
    {
        return record != nullptr && record->Importer == Trinity::MaterialAsset::c_AssetType;
    }

    bool IsScene(const Trinity::AssetRecord* record)
    {
        return record != nullptr && record->Importer == "Scene";
    }

    bool EqualIgnoringCase(char left, char right)
    {
        return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
    }

    bool LessIgnoringCase(std::string_view left, std::string_view right)
    {
        return std::ranges::lexicographical_compare(left, right, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) < std::tolower(static_cast<unsigned char>(b)); });
    }

    bool ContainsIgnoringCase(std::string_view text, std::string_view part)
    {
        return !std::ranges::search(text, part, EqualIgnoringCase).empty();
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

        if (IsMaterial(record))
        {
            return Trinity::Icons::c_PaintBrush;
        }

        if (record != nullptr && record->Importer == Trinity::EnvironmentAsset::c_AssetType)
        {
            return Trinity::Icons::c_Globe;
        }

        return IsScene(record) ? Trinity::Icons::c_CubeOutline : Trinity::Icons::c_File;
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

bool ContentBrowserPanel::ReadSetting(std::string_view key, std::string_view value)
{
    if (key != c_SizeSetting)
    {
        return false;
    }

    float l_Value = 0.0f;
    const std::from_chars_result l_Parsed = std::from_chars(value.data(), value.data() + value.size(), l_Value);
    if (l_Parsed.ec == std::errc() && std::isfinite(l_Value))
    {
        m_ThumbnailSize = SnapSize(l_Value);
    }

    return true;
}

void ContentBrowserPanel::WriteSettings(ImGuiTextBuffer& buffer) const
{
    const std::string l_Line = std::format("{}={}\n", c_SizeSetting, m_ThumbnailSize);
    buffer.append(l_Line.c_str(), l_Line.c_str() + l_Line.size());
}

// The folder tree on the left, and on the right the toolbar, the open folder's contents or what a search finds, and the foot. Moves and deletes wait until everything is drawn, since they change what is being listed
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

    // A folder deleted or moved away leaves the nearest one still there open
    const Trinity::Project& l_Project = *m_Session.GetProject();
    while (FindRoot(m_Folder) != nullptr && !IsRoot(m_Folder) && !std::filesystem::is_directory(l_Project.ToNativePath(m_Folder)))
    {
        m_Folder = GetParent(m_Folder);
    }

    if (FindRoot(m_Folder) == nullptr || !std::filesystem::is_directory(l_Project.ToNativePath(m_Folder)))
    {
        m_Folder = std::string(Trinity::Project::c_AssetsMount);
    }

    ReadShortcuts();

    if (ImGui::BeginTable("##ContentBrowser", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail()))
    {
        ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * c_TreeWidthInFonts);
        ImGui::TableSetupColumn("Contents", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("##Folders"))
        {
            for (const Root& it_Root : c_Roots)
            {
                if (IsShown(it_Root, l_Project))
                {
                    DrawFolderTree(std::string(it_Root.Path), std::string(it_Root.Label));
                }
            }
        }

        ImGui::EndChild();

        // The wheel sizes the items instead of scrolling them while Ctrl is held
        ImGui::TableSetColumnIndex(1);
        DrawToolbar();
        if (ImGui::BeginChild("##Contents", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_None, ImGui::GetIO().KeyCtrl ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None))
        {
            DrawItems();
        }

        ImGui::EndChild();
        DrawFooter();
        ImGui::EndTable();
    }

    DrawConfirmPopup();
    DrawNewScriptPopup();

    for (std::move_only_function<void()>& it_Action : std::exchange(m_Deferred, {}))
    {
        it_Action();
    }
}

// A click or a right-click opens the folder and selects it, as in Unity, and the open folder is renamed here rather than among the items
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

    if (IsRoot(path))
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

    const Item l_Item{ label, path, true, nullptr };
    const bool l_Renaming = path == m_Renaming && path == m_Folder;
    const std::string l_Label = l_Renaming ? std::string(Trinity::Icons::c_Folder) : std::format("{} {}", Trinity::Icons::c_Folder, label);
    const bool l_Open = ImGui::TreeNodeEx(path.c_str(), l_Flags, "%s", l_Label.c_str());
    const ImVec2 l_NodeMin = ImGui::GetItemRectMin();
    const ImVec2 l_NodeMax = ImGui::GetItemRectMax();
    if ((ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        OpenFolder(path);
        Select(l_Item);
    }

    if (ImGui::BeginPopupContextItem())
    {
        DrawAssetMenu(&l_Item);
        ImGui::EndPopup();
    }

    AcceptMove(path);

    // Over the name, after the icon
    if (l_Renaming)
    {
        const float l_X = l_NodeMin.x + ImGui::GetTreeNodeToLabelSpacing() + ImGui::CalcTextSize(std::format("{} ", Trinity::Icons::c_Folder).c_str()).x;
        DrawRenameField(l_Item, ImVec2(l_X, l_NodeMin.y + (l_NodeMax.y - l_NodeMin.y - ImGui::GetFrameHeight()) * 0.5f), std::max(l_NodeMax.x - l_X, 1.0f));
    }

    if (l_Open)
    {
        for (const std::string& it_Child : l_Children)
        {
            DrawFolderTree(std::format("{}/{}", path, it_Child), it_Child);
        }

        ImGui::TreePop();
    }
}

// Up, then the open folder's path from its root, each part of which opens it and takes a drop, or while searching, where to search as Unity's search bar has it. The search box sits at the right end
void ContentBrowserPanel::DrawToolbar()
{
    const Root* l_Root = FindRoot(m_Folder);
    ImGui::BeginDisabled(l_Root == nullptr || m_Folder == l_Root->Path);
    if (ImGui::Button(Trinity::Icons::c_ArrowUp))
    {
        OpenFolder(GetParent(m_Folder));
    }

    ImGui::EndDisabled();

    if (IsSearching())
    {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Search:");
        ImGui::SameLine();
        if (ImGui::RadioButton("All", m_SearchAll))
        {
            m_SearchAll = true;
        }

        ImGui::SameLine();
        const std::string_view l_FolderName = l_Root != nullptr && m_Folder == l_Root->Path ? l_Root->Label : GetName(m_Folder);
        if (ImGui::RadioButton(std::format("'{}'###SearchFolder", l_FolderName).c_str(), !m_SearchAll))
        {
            m_SearchAll = false;
        }
    }
    else
    {
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
    }

    // At the right end, when there is room for it. Escape clears it
    const float l_SearchWidth = ImGui::GetFontSize() * c_SearchWidthInFonts;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(ImGui::GetContentRegionAvail().x - l_SearchWidth, 0.0f));
    ImGui::SetNextItemWidth(l_SearchWidth);
    if (std::exchange(m_FocusSearch, false))
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::InputTextWithHint("##Search", std::format("{} Search", Trinity::Icons::c_Search).c_str(), &m_Search, ImGuiInputTextFlags_EscapeClearsAll);
}

// Folders first, then files, each by name, in as many columns as fit, or a row each in the list. A search shows what it finds instead, from wherever it is. Ctrl and the wheel size them as the slider does
void ContentBrowserPanel::DrawItems()
{
    const ImGuiIO& l_IO = ImGui::GetIO();
    if (l_IO.KeyCtrl && l_IO.MouseWheel != 0.0f && ImGui::IsWindowHovered())
    {
        const float l_Step = l_IO.MouseWheel * c_WheelStep;
        if (IsListView())
        {
            SetSize(l_Step > 0.0f ? c_MinimumThumbnailSize : c_ListSize);
        }
        else if (m_ThumbnailSize <= c_MinimumThumbnailSize && l_Step < 0.0f)
        {
            SetSize(c_ListSize);
        }
        else
        {
            SetSize(std::max(m_ThumbnailSize + l_Step, c_MinimumThumbnailSize));
        }
    }

    const std::vector<Item> l_Items = IsSearching() ? Search() : GetItems(m_Folder);
    if (IsListView())
    {
        const ImVec2 l_Row(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
        for (const Item& it_Item : l_Items)
        {
            DrawItem(it_Item, l_Row);
        }
    }
    else
    {
        const float l_Size = std::floor(ImGui::GetFontSize() * m_ThumbnailSize);
        const float l_Spacing = ImGui::GetStyle().ItemSpacing.x;
        const int l_Columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + l_Spacing) / (l_Size + l_Spacing)));
        const ImVec2 l_Cell(l_Size, l_Size + ImGui::GetTextLineHeightWithSpacing());
        for (std::size_t it_Index = 0; it_Index < l_Items.size(); ++it_Index)
        {
            if (it_Index % static_cast<std::size_t>(l_Columns) != 0)
            {
                ImGui::SameLine();
            }

            DrawItem(l_Items[it_Index], l_Cell);
        }
    }

    if (l_Items.empty())
    {
        ImGui::TextDisabled(IsSearching() ? "Nothing matches the search" : "This folder is empty");
    }

    DrawBackground();
}

// A thumbnail or an icon, over the name in the grid and before it in the list. Textures are only loaded for items on screen
void ContentBrowserPanel::DrawItem(const Item& item, ImVec2 size)
{
    ImGui::PushID(item.Path.c_str());

    const bool l_List = IsListView();
    const ImVec2 l_Min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##Item", size);
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
        DrawAssetMenu(&item);
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
        const ImGuiStyle& l_Style = ImGui::GetStyle();
        const ImVec2 l_Max(l_Min.x + size.x, l_Min.y + size.y);
        if (item.Path == m_Selected || l_Hovered)
        {
            l_DrawList.AddRectFilled(l_Min, l_Max, ImGui::GetColorU32(item.Path == m_Selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), l_Style.FrameRounding);
        }

        // The picture fills the top of a grid cell, or a square at the start of a list row
        const float l_Padding = l_List ? l_Style.FramePadding.y : l_Style.FramePadding.x;
        const float l_PictureSize = (l_List ? size.y : size.x) - l_Padding * 2.0f;
        DrawPicture(item, ImVec2(l_Min.x + l_Padding, l_Min.y + l_Padding), l_PictureSize);

        // The name, centred under the picture while it fits, or after it along the row
        const ImVec2 l_LabelMin = l_List ? ImVec2(l_Min.x + l_Padding + l_PictureSize + l_Style.ItemInnerSpacing.x, l_Min.y) : ImVec2(l_Min.x + l_Style.FramePadding.x, l_Min.y + size.x);
        const float l_LabelMaxX = l_Max.x - l_Style.FramePadding.x;
        if (item.Path == m_Renaming && item.Path != m_Folder)
        {
            DrawRenameField(item, l_LabelMin, l_LabelMaxX - l_LabelMin.x);
        }
        else
        {
            const ImVec2 l_TextSize = ImGui::CalcTextSize(item.Name.c_str());
            const float l_TextX = !l_List && l_TextSize.x < l_LabelMaxX - l_LabelMin.x ? l_Min.x + (size.x - l_TextSize.x) * 0.5f : l_LabelMin.x;
            const float l_TextY = l_List ? l_Min.y + (size.y - l_TextSize.y) * 0.5f : l_LabelMin.y;
            ImGui::RenderTextEllipsis(&l_DrawList, ImVec2(l_TextX, l_TextY), ImVec2(l_LabelMaxX, l_Max.y), l_LabelMaxX, item.Name.c_str(), nullptr, &l_TextSize);
        }

        if (l_Hovered && !ImGui::IsDragDropActive())
        {
            ImGui::SetItemTooltip("%s", item.Path.c_str());
        }
    }

    ImGui::PopID();
}

// A texture's own picture, fitted inside the square keeping its shape, or the icon for the item's kind, centred in it
void ContentBrowserPanel::DrawPicture(const Item& item, ImVec2 position, float size)
{
    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    const Trinity::TextureAsset* l_Texture = item.Record != nullptr && item.Record->Importer == Trinity::TextureAsset::c_AssetType ? GetThumbnail(item.Record->ID) : nullptr;
    if (l_Texture != nullptr)
    {
        const float l_Aspect = static_cast<float>(l_Texture->GetWidth()) / static_cast<float>(std::max(l_Texture->GetHeight(), 1u));
        const ImVec2 l_Fit = l_Aspect >= 1.0f ? ImVec2(size, size / l_Aspect) : ImVec2(size * l_Aspect, size);
        const ImVec2 l_FitMin(position.x + (size - l_Fit.x) * 0.5f, position.y + (size - l_Fit.y) * 0.5f);
        l_DrawList.AddImage(ImTextureRef(static_cast<ImTextureID>(Trinity::GetImGuiTextureID(*l_Texture))), l_FitMin, ImVec2(l_FitMin.x + l_Fit.x, l_FitMin.y + l_Fit.y));

        return;
    }

    // A list's icon is as tall as its text, and a thumbnail's leaves room around it
    const char* l_Icon = GetIcon(item.Record, item.Folder, item.Name);
    const float l_IconSize = IsListView() ? size : size * c_IconScale;
    const ImVec2 l_IconExtent = ImGui::GetFont()->CalcTextSizeA(l_IconSize, FLT_MAX, 0.0f, l_Icon);
    l_DrawList.AddText(ImGui::GetFont(), l_IconSize, ImVec2(position.x + (size - l_IconExtent.x) * 0.5f, position.y + (size - l_IconExtent.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), l_Icon);
}

// Unity's Assets menu, for an item or a folder in the tree, or with none for the open folder, from the space around the items. What cannot apply is greyed, with a tooltip where the reason is not plain. A root is never renamed, duplicated or deleted
void ContentBrowserPanel::DrawAssetMenu(const Item* item)
{
    const bool l_Editable = item != nullptr && !IsRoot(item->Path);
    const bool l_Importing = m_Session.IsImportingAssets();

    if (ImGui::BeginMenu("Create"))
    {
        DrawCreateMenu();
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem(std::format("{} {}", Trinity::Icons::c_FolderOpen, c_ShowInFileManager).c_str()))
    {
        ShowInFileManager(item);
    }

    if (ImGui::MenuItem("Open", nullptr, false, item != nullptr && !IsMaterial(item->Record)))
    {
        Open(*item);
    }

    if (ImGui::MenuItem(std::format("{} Delete", Trinity::Icons::c_Trash).c_str(), "Delete", false, l_Editable))
    {
        AskDelete(*item);
    }

    if (ImGui::MenuItem("Rename", "F2", false, l_Editable))
    {
        BeginRename(*item);
    }

    if (ImGui::MenuItem("Copy Path", "Ctrl+Alt+C", false, item != nullptr))
    {
        ImGui::SetClipboardText(item->Path.c_str());
    }

    if (ImGui::MenuItem(std::format("{} Duplicate", Trinity::Icons::c_Copy).c_str(), "Ctrl+D", false, l_Editable))
    {
        Duplicate(*item);
    }

    ImGui::Separator();
    if (ImGui::MenuItem(std::format("{} Import New Asset...", Trinity::Icons::c_Download).c_str()))
    {
        m_Session.RequestImportAsset(m_Folder);
    }

    ImGui::SetItemTooltip("Copies a file from anywhere into %s", m_Folder.c_str());

    // As in Unity: what in the open scene uses the asset, a model's meshes and materials, or a texture through its materials, or has the class a script file is named after, in the Hierarchy
    const bool l_Findable = item != nullptr && !item->Folder && (item->Record != nullptr || IsClassFile(item->Name));
    if (ImGui::MenuItem(std::format("{} Find References In Scene", Trinity::Icons::c_Search).c_str(), nullptr, false, l_Findable && m_FindReferences))
    {
        m_FindReferences(GetReferenceSearch(item->Path));
    }

    ImGui::Separator();
    if (ImGui::MenuItem(std::format("{} Refresh", Trinity::Icons::c_Refresh).c_str(), "F5"))
    {
        m_Session.Request(EditorSession::Command::Refresh);
    }

    const bool l_Reimportable = item != nullptr && (item->Folder ? IsSameOrInside(item->Path, Trinity::Project::c_AssetsMount) : IsImported(item->Record));
    if (ImGui::MenuItem("Reimport", nullptr, false, l_Reimportable && !l_Importing))
    {
        m_Deferred.push_back([this, l_Path = item->Path] { static_cast<void>(m_Session.ReimportAssets(l_Path)); });
    }

    if (l_Importing)
    {
        ImGui::SetItemTooltip("Not while assets are importing");
    }
    else if (item != nullptr && !l_Reimportable)
    {
        ImGui::SetItemTooltip("Only textures, models and environments are imported");
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Reimport All", nullptr, false, !l_Importing))
    {
        AskReimportAll();
    }

    if (l_Importing)
    {
        ImGui::SetItemTooltip("Not while assets are importing");
    }
}

// Unity's Create menu. Everything is made in the open folder and named there at once: scenes and materials among the assets, and each language's scripts in its own folder
void ContentBrowserPanel::DrawCreateMenu()
{
    if (ImGui::MenuItem(std::format("{} Folder", Trinity::Icons::c_Folder).c_str()))
    {
        m_Deferred.push_back([this] { RenameCreated(m_Session.CreateFolder(m_Folder)); });
    }

    ImGui::Separator();
    const bool l_InAssets = IsSameOrInside(m_Folder, Trinity::Project::c_AssetsMount);
    if (ImGui::MenuItem(std::format("{} Scene", Trinity::Icons::c_CubeOutline).c_str(), nullptr, false, l_InAssets))
    {
        m_Deferred.push_back([this] { RenameCreated(m_Session.CreateScene(m_Folder)); });
    }

    if (!l_InAssets)
    {
        ImGui::SetItemTooltip("Scenes go in Assets or a folder in it");
    }

    if (ImGui::MenuItem(std::format("{} Material", Trinity::Icons::c_PaintBrush).c_str(), nullptr, false, l_InAssets))
    {
        m_Deferred.push_back([this] { RenameCreated(m_Session.CreateMaterial(m_Folder)); });
    }

    if (!l_InAssets)
    {
        ImGui::SetItemTooltip("Materials go in Assets or a folder in it");
    }

    // The name is asked for first, since it names the class as well as the file
    ImGui::Separator();
    const Trinity::Project* l_Project = m_Session.GetProject();
    const auto a_ScriptItem = [this](Trinity::ScriptLanguage language, std::string_view folder, std::string_view name, bool exists, std::string_view create)
    {
        const bool l_Inside = IsSameOrInside(m_Folder, folder);
        if (ImGui::MenuItem(std::format("{} {} Script...", Trinity::Icons::c_FileCode, name).c_str(), nullptr, false, exists && l_Inside))
        {
            m_NewScript = language;
            m_NewScriptName = "NewScript";
            m_OpenNewScriptPopup = true;
        }

        if (!exists)
        {
            ImGui::SetItemTooltip("%s", std::format("The project has no {} scripts yet. Scripts > {} makes their home", name, create).c_str());
        }
        else if (!l_Inside)
        {
            ImGui::SetItemTooltip("%s", std::format("{} scripts go in {} or a folder in it", name, GetName(folder)).c_str());
        }
    };

    a_ScriptItem(Trinity::ScriptLanguage::CSharp, EditorSession::c_ScriptsFolder, "C#", l_Project != nullptr && l_Project->HasAssembly(), "Create C# Project");
    a_ScriptItem(Trinity::ScriptLanguage::Cpp, EditorSession::c_SourceFolder, "C++", l_Project != nullptr && l_Project->HasModule(), "Create C++ Module");
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
            m_Deferred.push_back([this, l_Path = item.Path, l_Name] { Move(l_Path, GetParent(l_Path), l_Name); });
        }
    }

    ImGui::SetCursorScreenPos(l_Cursor);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// The space after the items: a click clears the selection, its menu is the Assets menu for the open folder, and a drop moves into the open folder
void ContentBrowserPanel::DrawBackground()
{
    const ImVec2 l_Available = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##Background", ImVec2(std::max(l_Available.x, 1.0f), std::max(l_Available.y, ImGui::GetFrameHeight())));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        m_Selected.clear();
    }

    if (ImGui::BeginPopupContextItem("##BackgroundMenu"))
    {
        DrawAssetMenu(nullptr);
        ImGui::EndPopup();
    }

    AcceptMove(m_Folder);
}

// Unity's foot: the selection's icon and path, and the size slider at the right end, whose leftmost notch is the list
void ContentBrowserPanel::DrawFooter()
{
    const float l_SliderWidth = ImGui::GetFontSize() * c_SliderWidthInFonts;
    const float l_PathWidth = std::max(ImGui::GetContentRegionAvail().x - l_SliderWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f);
    const ImVec2 l_Min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(l_PathWidth, ImGui::GetFrameHeight()));
    if (const std::optional<Item> l_Selected = m_Selected.empty() ? std::nullopt : FindItem(m_Selected))
    {
        const std::string l_Text = std::format("{} {}", GetIcon(l_Selected->Record, l_Selected->Folder, l_Selected->Name), l_Selected->Path);
        const float l_TextY = l_Min.y + ImGui::GetStyle().FramePadding.y;
        const float l_MaxX = l_Min.x + l_PathWidth;
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(l_Min.x, l_TextY), ImVec2(l_MaxX, l_TextY + ImGui::GetFontSize()), l_MaxX, l_Text.c_str(), nullptr, nullptr);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(l_SliderWidth);
    float l_Size = m_ThumbnailSize;
    if (ImGui::SliderFloat("##ThumbnailSize", &l_Size, c_ListSize, c_MaximumThumbnailSize, "", ImGuiSliderFlags_AlwaysClamp))
    {
        SetSize(l_Size);
    }

    ImGui::SetItemTooltip("%s", IsListView() ? "A list. Drag right for thumbnails, or Ctrl and the wheel over the items" : "The thumbnails' size. Drag to the left end for a list, or Ctrl and the wheel over the items");
}

// The question, then its action once everything is drawn, or nothing at Cancel or Escape
void ContentBrowserPanel::DrawConfirmPopup()
{
    if (std::exchange(m_OpenConfirmPopup, false))
    {
        ImGui::OpenPopup(c_ConfirmPopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const std::string l_Name = std::format("{}{}", m_Confirmation ? m_Confirmation->Title : std::string(), c_ConfirmPopup);
    if (!ImGui::BeginPopupModal(l_Name.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    if (!m_Confirmation)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    ImGui::TextUnformatted(std::format("{} {}", Trinity::Icons::c_Warning, m_Confirmation->Question).c_str());
    ImGui::TextDisabled("%s", m_Confirmation->Detail.c_str());
    ImGui::Spacing();

    if (ImGui::Button(m_Confirmation->Button.c_str()))
    {
        m_Deferred.push_back(std::move(m_Confirmation->Action));
        m_Confirmation.reset();
        ImGui::CloseCurrentPopup();
    }
    else
    {
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            m_Confirmation.reset();
            ImGui::CloseCurrentPopup();
        }
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
            m_Deferred.push_back([this, l_Path = l_Record->Path, folder] { Move(l_Path, folder, std::string(GetName(l_Path))); });
        }
    }

    if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_FolderPayload))
    {
        m_Deferred.push_back([this, l_Path = std::string(static_cast<const char*>(l_Payload->Data)), folder] { Move(l_Path, folder, std::string(GetName(l_Path))); });
    }

    ImGui::EndDragDropTarget();
}

// While the browser has focus, Delete, F2, Ctrl+D and Ctrl+Alt+C act on its selection rather than on the scene's, and Ctrl+F goes to the search, as in Unity
void ContentBrowserPanel::ReadShortcuts()
{
    if (!m_Focused || ImGui::GetIO().WantTextInput)
    {
        return;
    }

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
    {
        m_FocusSearch = true;

        return;
    }

    const bool l_Delete = ImGui::IsKeyPressed(ImGuiKey_Delete, false);
    const bool l_Rename = ImGui::IsKeyPressed(ImGuiKey_F2, false);
    const bool l_Duplicate = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D);
    const bool l_CopyPath = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_C);
    if (m_Selected.empty() || !(l_Delete || l_Rename || l_Duplicate || l_CopyPath))
    {
        return;
    }

    const std::optional<Item> l_Item = FindItem(m_Selected);
    if (!l_Item)
    {
        return;
    }

    if (l_CopyPath)
    {
        ImGui::SetClipboardText(l_Item->Path.c_str());
    }
    else if (IsRoot(l_Item->Path))
    {
        // A root stays as it is
    }
    else if (l_Delete)
    {
        AskDelete(*l_Item);
    }
    else if (l_Rename)
    {
        BeginRename(*l_Item);
    }
    else
    {
        Duplicate(*l_Item);
    }
}

// A file is shown in Properties, where a texture's import settings are
void ContentBrowserPanel::Select(const Item& item)
{
    m_Selected = item.Path;
    m_Session.SetInspectedAsset(item.Record != nullptr ? item.Record->ID : Trinity::UUID());
}

// A folder opens here, a scene in Forge and a script in the script editor. Anything else but a material, which Properties edits, opens in the program the system has for it, as Unity opens a texture in an image editor
void ContentBrowserPanel::Open(const Item& item)
{
    if (item.Folder)
    {
        m_Deferred.push_back([this, l_Path = item.Path] { OpenFolder(l_Path); });
    }
    else if (IsScene(item.Record))
    {
        m_Session.RequestOpenScene(item.Path);
    }
    else if (item.Record == nullptr && IsScriptFile(item.Name))
    {
        m_Deferred.push_back([this, l_Path = item.Path] { static_cast<void>(m_Session.OpenScript(l_Path)); });
    }
    else if (!IsMaterial(item.Record))
    {
        m_Deferred.push_back([this, l_Path = item.Path]
        {
            const Trinity::Project* l_Project = m_Session.GetProject();
            if (l_Project == nullptr)
            {
                return;
            }

            if (const Trinity::Expected<void, std::string> l_Opened = Trinity::Process::OpenWithSystem(l_Project->ToNativePath(l_Path)); !l_Opened)
            {
                TR_ERROR("Forge: {} could not be opened: {}", l_Path, l_Opened.GetError());
            }
        });
    }
}

// Choosing a folder ends a search, as it does in Unity
void ContentBrowserPanel::OpenFolder(const std::string& folder)
{
    m_Folder = folder;
    m_Search.clear();
}

void ContentBrowserPanel::BeginRename(const Item& item)
{
    m_Selected = item.Path;
    m_Renaming = item.Path;
    m_RenameText = item.Folder ? item.Name : std::string(GetStem(item.Name));
    m_FocusRename = true;
}

// What Create made, named at once as Unity names what it creates
void ContentBrowserPanel::RenameCreated(const std::optional<std::string>& path)
{
    m_Listings.clear();
    if (const std::optional<Item> l_Item = path ? FindItem(*path) : std::nullopt)
    {
        BeginRename(*l_Item);
    }
}

// The copy is selected, as Unity selects it
void ContentBrowserPanel::Duplicate(const Item& item)
{
    m_Deferred.push_back([this, l_Path = item.Path]
    {
        if (const std::optional<std::string> l_Copy = m_Session.DuplicateAsset(l_Path))
        {
            m_Selected = *l_Copy;
        }

        m_Listings.clear();
    });
}

// Asks first, since a deleted file is gone for good
void ContentBrowserPanel::AskDelete(const Item& item)
{
    m_Confirmation = Confirmation{ "Delete", std::format("Delete {}{}?", item.Name, item.Folder ? " and everything in it" : ""), "This cannot be undone, and anything using it will find it missing", "Delete", [this, l_Path = item.Path]
    {
        static_cast<void>(m_Session.DeleteAsset(l_Path));
        m_Selected.clear();
        m_Listings.clear();
    } };
    m_OpenConfirmPopup = true;
}

// Asks first, as Unity does, since every texture, model and environment is cooked again
void ContentBrowserPanel::AskReimportAll()
{
    m_Confirmation = Confirmation{ "Reimport All", "Reimport every asset in the project?", "Every texture, model and environment is cooked again, which can take a while", "Reimport", [this] { static_cast<void>(m_Session.ReimportAssets(Trinity::Project::c_AssetsMount)); } };
    m_OpenConfirmPopup = true;
}

// The folder the item is in, or the open folder for none, as the desktop shows it
void ContentBrowserPanel::ShowInFileManager(const Item* item)
{
    const Trinity::Project* l_Project = m_Session.GetProject();
    if (l_Project == nullptr)
    {
        return;
    }

    const std::filesystem::path l_Folder = item != nullptr ? l_Project->ToNativePath(item->Path).parent_path() : l_Project->ToNativePath(m_Folder);
    if (const Trinity::Expected<void, std::string> l_Shown = Trinity::Process::OpenWithSystem(l_Folder); !l_Shown)
    {
        TR_ERROR("Forge: {} could not be shown: {}", item != nullptr ? item->Path : m_Folder, l_Shown.GetError());
    }
}

// Renames, or moves within its root, since a script among the assets, or an asset among the scripts, would no longer be what it is. The open folder follows a folder it is in
void ContentBrowserPanel::Move(const std::string& path, const std::string& folder, const std::string& name)
{
    if (GetParent(path) == folder && GetName(path) == name)
    {
        return;
    }

    if (FindRoot(path) != FindRoot(folder))
    {
        TR_ERROR("Forge: {} stays in {}, so it was not moved to {}", path, FindRoot(path) != nullptr ? FindRoot(path)->Label : std::string_view("its folder"), folder);

        return;
    }

    if (m_Session.MoveAsset(path, folder, name))
    {
        const std::string l_Path = std::format("{}/{}", folder, name);
        if (IsSameOrInside(m_Folder, path))
        {
            m_Folder = l_Path + m_Folder.substr(path.size());
        }

        m_Selected = l_Path;
    }

    m_Listings.clear();
}

// Saved in imgui.ini with the panels
void ContentBrowserPanel::SetSize(float size)
{
    const float l_Size = SnapSize(size);
    if (l_Size != m_ThumbnailSize)
    {
        m_ThumbnailSize = l_Size;
        ImGui::MarkIniSettingsDirty();
    }
}

void ContentBrowserPanel::ReleaseAll()
{
    m_Thumbnails.clear();
    m_Listings.clear();
    m_Folder = std::string(Trinity::Project::c_AssetsMount);
    m_Selected.clear();
    m_Renaming.clear();
    m_Search.clear();
}

bool ContentBrowserPanel::IsListView() const
{
    return m_ThumbnailSize < c_MinimumThumbnailSize;
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

// What the search finds in the open folder and everything in it, or in every root, folders first and then files, each by name
std::vector<ContentBrowserPanel::Item> ContentBrowserPanel::Search()
{
    std::vector<Item> l_Matches;
    if (m_SearchAll)
    {
        for (const Root& it_Root : c_Roots)
        {
            if (IsShown(it_Root, *m_Session.GetProject()))
            {
                FindMatches(std::string(it_Root.Path), l_Matches);
            }
        }
    }
    else
    {
        FindMatches(m_Folder, l_Matches);
    }

    std::ranges::sort(l_Matches, [](const Item& left, const Item& right) { return left.Folder != right.Folder ? left.Folder : LessIgnoringCase(left.Name, right.Name); });

    return l_Matches;
}

// From the listings, so a search reads the disk no more than browsing does
void ContentBrowserPanel::FindMatches(const std::string& folder, std::vector<Item>& matches)
{
    for (Item& it_Item : GetItems(folder))
    {
        if (it_Item.Folder)
        {
            FindMatches(it_Item.Path, matches);
        }

        if (ContainsIgnoringCase(it_Item.Name, m_Search))
        {
            matches.push_back(std::move(it_Item));
        }
    }
}

// From its folder's listing, or a root by its label. Nothing for a path no longer there
std::optional<ContentBrowserPanel::Item> ContentBrowserPanel::FindItem(const std::string& path)
{
    if (const Root* l_Root = FindRoot(path); l_Root != nullptr && l_Root->Path == path)
    {
        return Item{ std::string(l_Root->Label), path, true, nullptr };
    }

    for (Item& it_Item : GetItems(GetParent(path)))
    {
        if (it_Item.Path == path)
        {
            return std::move(it_Item);
        }
    }

    return std::nullopt;
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