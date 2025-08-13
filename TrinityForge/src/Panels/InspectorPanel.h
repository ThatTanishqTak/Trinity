#pragma once

#include "Trinity/UI/Panel.h"
#include "Trinity/ECS/Scene.h"
#include "Trinity/Core/AssetManager.h"

#include <entt/entt.hpp>
#include <string>

class InspectorPanel : public Trinity::Panel
{
public:
    InspectorPanel(Trinity::Scene* p_Context, entt::entity* p_SelectionContext, Trinity::AssetManager* p_AssetManager);

    void OnUIRender() override;

private:
    Trinity::Scene* m_Context = nullptr;
    entt::entity* m_SelectionContext = nullptr;
    Trinity::AssetManager* m_AssetManager = nullptr;
};