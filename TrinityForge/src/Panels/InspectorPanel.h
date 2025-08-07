#pragma once

#include "Trinity/UI/Panel.h"
#include "Trinity/ECS/Scene.h"
#include <entt/entt.hpp>

class InspectorPanel : public Trinity::Panel
{
public:
    InspectorPanel(Trinity::Scene* p_Context, entt::entity* p_SelectionContext);

    void OnUIRender() override;

private:
    Trinity::Scene* m_Context = nullptr;
    entt::entity* m_SelectionContext = nullptr;
};