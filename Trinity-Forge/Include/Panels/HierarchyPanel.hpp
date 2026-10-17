#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

class HierarchyPanel final : public Panel
{
public:
    explicit HierarchyPanel(EditorSession& session);

    // Frames the selection in the Scene panel, as a double-click and F do here, and with follow keeps it framed as it moves, as Shift+F does
    void SetFrameSelection(std::function<void(bool follow)> frame) { m_FrameSelection = std::move(frame); }
    // Puts the text in the search field, as Find References In Scene does with a ref: term
    void SetSearch(std::string search);
    // The entities the search matches while it holds anything, which the Scene panel outlines, or null
    [[nodiscard]] const std::unordered_set<Trinity::UUID>* GetSearchMatches() const { return m_Search.empty() ? nullptr : &m_Matched; }
    // Starts typing a new name in the entity's row, as F2 does, which the Entity menu's Create Empty does for the entity it made
    void Rename(Trinity::UUID entity);

protected:
    void OnImGuiRender() override;

private:
    // What the search's ref: terms name: the assets a slot may hold, and by language the script classes named after a script file
    struct References
    {
        std::unordered_set<Trinity::UUID> Assets;
        std::vector<std::pair<Trinity::ScriptLanguage, std::string>> Scripts;
    };

    void DrawSearch();
    void ResolveReferences(const std::vector<std::string_view>& terms);
    [[nodiscard]] bool HasReference(Trinity::Entity entity) const;
    void UpdateSearch(Trinity::Scene& scene);
    void DrawEntity(Trinity::Entity entity);
    void DrawRenameField(Trinity::Entity entity);
    void DrawContextMenu(Trinity::Entity entity);
    void DrawBackground();
    void DrawCreateMenus(Trinity::UUID parent);
    void AcceptDrop(Trinity::Entity target);
    void ReadShortcuts();
    void UpdateSlowClick(Trinity::Scene& scene);

    void Select(Trinity::UUID entity);
    void BeginRename(Trinity::Entity entity);
    void Create(Trinity::UUID parent);
    void Move(Trinity::UUID entity, Trinity::UUID parent, Trinity::UUID before);

    EditorSession& m_Session;
    std::function<void(bool follow)> m_FrameSelection;
    std::vector<std::move_only_function<void()>> m_Deferred;
    std::vector<Trinity::UUID> m_RevealPath;
    Trinity::UUID m_Revealed;
    Trinity::UUID m_Renaming;
    std::string m_RenameText;
    bool m_FocusRename = false;
    // The entity a second click landed on while it was already selected, and when, which renames it as Unity does once no other click follows
    Trinity::UUID m_SlowClick;
    double m_SlowClickTime = 0.0;
    // What the search field holds, and while it holds anything, the entities it matches and those it shows: the matches and every parent above one
    std::string m_Search;
    std::unordered_set<Trinity::UUID> m_Matched;
    std::unordered_set<Trinity::UUID> m_Shown;
    bool m_FocusSearch = false;
    // As the search changes, so the tree opens to every match afresh and is then left to be opened and closed
    bool m_SearchChanged = false;
    // The ref: terms resolved, worked out again only as the terms, the project's assets or the history change, since a texture's materials are read from their files
    References m_References;
    std::string m_ReferencesKey;
    // While a copy of the scene plays, which nothing may change
    bool m_ReadOnly = false;
    bool m_ScrollToSelection = false;
};