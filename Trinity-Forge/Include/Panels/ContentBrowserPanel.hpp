#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"
#include "PrefabThumbnails.hpp"

#include <Trinity.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ImGuiTextBuffer;
struct ImVec2;

// The project's Assets, Scripts and Source folders, as Unity's Project window. Nothing moves across
class ContentBrowserPanel final : public Panel
{
public:
    explicit ContentBrowserPanel(EditorSession& session);
    ~ContentBrowserPanel() override;

    void EndFrame();
    // In OnPrepareRender: the thumbnails readied this frame submitted, each to its own view
    void PrepareThumbnails() { m_PrefabThumbnails.Prepare(); }

    [[nodiscard]] bool IsFocused() const { return m_Focused; }
    // Opens the asset's folder with its file selected and scrolled to, and leaves Properties showing what it shows. A sub-asset's file is the one it was imported from. False for an asset in none of the browser's folders
    bool Reveal(Trinity::UUID asset);
    // Shows what in the open scene uses an asset or script, through the Hierarchy's ref: search
    void SetFindReferences(std::function<void(std::string search)> find) { m_FindReferences = std::move(find); }
    // Where an input actions asset opens, as a double-click or Open asks
    void SetOpenInputActions(std::function<void(Trinity::UUID asset)> open) { m_OpenInputActions = std::move(open); }
    // Where an audio mixer opens, as a double-click or Open asks
    void SetOpenAudioMixer(std::function<void(Trinity::UUID asset)> open) { m_OpenAudioMixer = std::move(open); }
    // Where a sound event opens, as a double-click or Open asks
    void SetOpenSoundEvent(std::function<void(Trinity::UUID asset)> open) { m_OpenSoundEvent = std::move(open); }


    // The thumbnail size, or the list, which imgui.ini keeps for each person
    [[nodiscard]] bool ReadSetting(std::string_view key, std::string_view value);
    void WriteSettings(ImGuiTextBuffer& buffer) const;

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

    // What Delete and Reimport All ask before they act, since neither can be undone
    struct Confirmation
    {
        std::string Title;
        std::string Question;
        std::string Detail;
        std::string Button;
        std::move_only_function<void()> Action;
    };

    // An instance's root dropped to become a prefab, until original or variant is answered
    struct PrefabDrop
    {
        Trinity::UUID Entity;
        std::string Folder;
    };

    void DrawFolderTree(const std::string& path, const std::string& label);
    void DrawToolbar();
    void DrawItems();
    void DrawItem(const Item& item, ImVec2 size);
    void DrawPicture(const Item& item, ImVec2 position, float size);
    void DrawAssetMenu(const Item* item);
    void DrawCreateMenu(const Item* item);
    void DrawRenameField(const Item& item, ImVec2 position, float width);
    void DrawBackground();
    void DrawFooter();
    void DrawConfirmPopup();
    void DrawNewScriptPopup();
    void DrawNewPrefabPopup();
    void DrawPrefabKindPopup();
    void AcceptMove(const std::string& folder);
    void ReadShortcuts();

    void Select(const Item& item);
    void Open(const Item& item);
    void OpenFolder(const std::string& folder);
    void BeginRename(const Item& item);
    void RenameCreated(const std::optional<std::string>& path);
    void Duplicate(const Item& item);
    void AskDelete(const Item& item);
    void AskDeleteSelected();
    void ToggleSelected(const Item& item);
    void SelectRange(const std::vector<Item>& items, const std::string& to);
    void SetSelectedPaths(std::vector<std::string> paths);
    void MoveDragged(const std::string& path, const std::string& folder);
    [[nodiscard]] bool IsSelected(const std::string& path) const;
    [[nodiscard]] std::vector<std::string> GetSelectedPaths() const;
    void AskReimportAll();
    void ShowInFileManager(const Item* item);
    void Move(const std::string& path, const std::string& folder, const std::string& name);
    void SetSize(float size);
    void ReleaseAll();

    [[nodiscard]] bool IsListView() const;
    [[nodiscard]] bool IsSearching() const { return !m_Search.empty(); }
    [[nodiscard]] const std::vector<Trinity::DirectoryEntry>& List(const std::string& folder);
    [[nodiscard]] std::vector<Item> GetItems(const std::string& folder);
    [[nodiscard]] std::vector<Item> Search();
    void FindMatches(const std::string& folder, std::vector<Item>& matches);
    [[nodiscard]] std::optional<Item> FindItem(const std::string& path);
    [[nodiscard]] const Trinity::TextureAsset* GetThumbnail(Trinity::UUID id);

    EditorSession& m_Session;
    std::function<void(std::string search)> m_FindReferences;
    std::function<void(Trinity::UUID asset)> m_OpenInputActions;
    std::function<void(Trinity::UUID asset)> m_OpenAudioMixer;
    std::function<void(Trinity::UUID asset)> m_OpenSoundEvent;
    std::uint64_t m_CloseListener = 0;
    std::string m_Folder;
    std::string m_Selected;
    // The items selected along with it by Ctrl and Shift clicks, and what it was when they were: anything else that selects an item lets go of them
    std::vector<std::string> m_AlsoSelected;
    std::string m_AlsoSelectedWith;
    // The item last selected without Shift, which a Shift-click selects from, and the item one landed on until the frame's items are known
    std::string m_SelectionAnchor;
    std::optional<std::string> m_RangeTo;
    std::string m_Renaming;
    std::string m_RenameText;
    std::string m_Search;
    // Whether the search looks through every root rather than the open folder alone
    bool m_SearchAll = false;
    std::optional<Confirmation> m_Confirmation;
    // The language of the script New Script asks a name for, the name typed, and its template
    std::optional<Trinity::ScriptLanguage> m_NewScript;
    std::string m_NewScriptName;
    EditorSession::ScriptTemplate m_NewScriptTemplate = EditorSession::ScriptTemplate::Script;
    // The name typed so far for the prefab Create > Prefab makes
    std::string m_NewPrefabName;
    std::optional<PrefabDrop> m_PrefabDrop;
    std::unordered_map<std::string, std::vector<Trinity::DirectoryEntry>> m_Listings;
    std::uint64_t m_ListedScan = 0;
    std::unordered_map<Trinity::UUID, Thumbnail> m_Thumbnails;
    PrefabThumbnails m_PrefabThumbnails{ m_Session };
    std::vector<std::move_only_function<void()>> m_Deferred;
    float m_ThumbnailSize = 6.0f;
    int m_DrawnFrame = -1;
    bool m_FocusRename = false;
    bool m_FocusSearch = false;
    // Reveal's, until the selected item is next drawn
    bool m_ScrollToSelected = false;
    bool m_OpenConfirmPopup = false;
    bool m_OpenNewScriptPopup = false;
    bool m_OpenNewPrefabPopup = false;
    bool m_OpenPrefabKindPopup = false;
    bool m_Focused = false;
};