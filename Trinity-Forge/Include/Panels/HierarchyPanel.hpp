#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class HierarchyPanel final : public Panel
{
public:
    explicit HierarchyPanel(EditorSession& session);

protected:
    void OnImGuiRender() override;

private:
    void DrawEntity(Trinity::Entity entity);
    void DrawRenameField(Trinity::Entity entity);
    void DrawContextMenu(Trinity::Entity entity);
    void DrawBackground();
    void AcceptDrop(Trinity::Entity target);
    void ReadShortcuts();

    void Select(Trinity::UUID entity);
    void BeginRename(Trinity::Entity entity);
    void Create(Trinity::UUID parent);
    void Move(Trinity::UUID entity, Trinity::UUID parent, Trinity::UUID before);

    EditorSession& m_Session;
    std::vector<std::move_only_function<void()>> m_Deferred;
    std::vector<Trinity::UUID> m_RevealPath;
    Trinity::UUID m_Revealed;
    Trinity::UUID m_Renaming;
    std::string m_RenameText;
    bool m_FocusRename = false;
    bool m_ScrollToSelection = false;
};