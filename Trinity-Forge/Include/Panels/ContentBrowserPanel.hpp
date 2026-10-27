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

// The project's Assets, and beside them its Scripts and Source folders once it has C# scripts or a C++ module, where New C# Script and New C++ Script make scripts from templates and a script opens in the script editor. Each root keeps what is in it: nothing moves from one to another. As in Unity's Project window, the slider at its foot sizes the thumbnails and shows a list at its leftmost notch, the search finds by name in the open folder or everywhere, and items, the space around them and the folder tree share one Assets menu
class ContentBrowserPanel final : public Panel
{
public:
    explicit ContentBrowserPanel(EditorSession& session);
    ~ContentBrowserPanel() override;

    void EndFrame();
    // In OnPrepareRender: the prefabs' and models' thumbnails readied this frame submitted, each to its own view
    void PrepareThumbnails() { m_PrefabThumbnails.Prepare(); }

    [[nodiscard]] bool IsFocused() const { return m_Focused; }
    // Shows what in the open scene uses an asset or a script file, as Find References In Scene asks, through the Hierarchy's search with a ref: term
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

    // An instance's root dropped to become a prefab in the folder, until it is answered whether as an original or a variant
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
    std::string m_Renaming;
    std::string m_RenameText;
    std::string m_Search;
    // Whether the search looks through every root rather than the open folder alone
    bool m_SearchAll = false;
    std::optional<Confirmation> m_Confirmation;
    // The language of the script New Script asks a name for, the name typed so far and the template it starts from
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
    bool m_OpenConfirmPopup = false;
    bool m_OpenNewScriptPopup = false;
    bool m_OpenNewPrefabPopup = false;
    bool m_OpenPrefabKindPopup = false;
    bool m_Focused = false;
};