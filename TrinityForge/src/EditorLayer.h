#pragma once

#include "Trinity/UI/Panel.h"

#include <memory>
#include <vector>
#include <entt/entt.hpp>

class EditorLayer : public Trinity::Panel
{
public:
    EditorLayer() = default;

    void OnUIRender() override;
    void RegisterPanel(std::unique_ptr<Trinity::Panel> panel);

    entt::entity* GetSelectionContext() { return &m_SelectedEntity; }

private:
    std::vector<std::unique_ptr<Trinity::Panel>> m_Panels;
    entt::entity m_SelectedEntity{ entt::null };
};