#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct ImVec2;

class ContentBrowserPanel final : public Panel
{
public:
    explicit ContentBrowserPanel(EditorSession& session);
    ~ContentBrowserPanel() override;

    void EndFrame();

    [[nodiscard]] bool IsFocused() const { return m_Focused; }

protected:
    void OnImGuiRender() override;

private:
    struct Item
    {
        std::string Name;
        std::string Path;
        bool Folder = false;
        const Trinity::AssetRecord* Record = nullptr;
    };

    struct Thumbnail
    {
        Trinity::AssetRef<Trinity::TextureAsset> Texture;
        int LastDrawn = 0;
    };

    void DrawFolderTree(const std::string& path, const std::string& label);
    void DrawToolbar();
    void DrawGrid();
    void DrawItem(const Item& item, float size);
    void DrawItemMenu(const Item& item);
    void DrawRenameField(const Item& item, ImVec2 position, float width);
    void DrawBackground();
    void DrawDeletePopup();
    void AcceptMove(const std::string& folder);
    void ReadShortcuts();

    void Select(const Item& item);
    void Open(const Item& item);
    void BeginRename(const Item& item);
    void Move(const std::string& path, const std::string& folder);
    void ReleaseAll();

    [[nodiscard]] const std::vector<Trinity::DirectoryEntry>& List(const std::string& folder);
    [[nodiscard]] std::vector<Item> GetItems(const std::string& folder);
    [[nodiscard]] const Trinity::TextureAsset* GetThumbnail(Trinity::UUID id);

    EditorSession& m_Session;
    std::uint64_t m_CloseListener = 0;
    std::string m_Folder;
    std::string m_Selected;
    std::string m_Renaming;
    std::string m_RenameText;
    std::string m_PendingDelete;
    std::unordered_map<std::string, std::vector<Trinity::DirectoryEntry>> m_Listings;
    std::uint64_t m_ListedScan = 0;
    std::unordered_map<Trinity::UUID, Thumbnail> m_Thumbnails;
    std::vector<std::move_only_function<void()>> m_Deferred;
    float m_ThumbnailSize = 6.0f;
    int m_DrawnFrame = -1;
    bool m_FocusRename = false;
    bool m_OpenDeletePopup = false;
    bool m_Focused = false;
};