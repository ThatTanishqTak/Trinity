#pragma once

#include "Trinity/UI/Panel.h"
#include "Trinity/ECS/Scene.h"
#include "Trinity/Core/AssetManager.h"

#include <memory>
#include <vector>
#include <entt/entt.hpp>

class EditorLayer : public Trinity::Panel
{
public:
    EditorLayer(Trinity::Scene* scene, Trinity::AssetManager* assetManager);

    void OnUIRender() override;
    void RegisterPanel(std::unique_ptr<Trinity::Panel> panel);

    entt::entity* GetSelectionContext() { return &m_SelectedEntity; }

private:
    Trinity::Scene* m_Scene = nullptr;
    Trinity::AssetManager* m_AssetManager = nullptr;
    std::vector<std::unique_ptr<Trinity::Panel>> m_Panels;
    entt::entity m_SelectedEntity{ entt::null };
};