#pragma once

#include "Trinity/UI/Panel.h"
#include "Trinity/Renderer/Renderer.h"
#include "Trinity/ECS/Scene.h"

#include <entt/entt.hpp>
#include <glm/vec2.hpp>

class SceneViewportPanel : public Trinity::Panel
{
public:
    SceneViewportPanel(Trinity::Renderer* renderer, Trinity::Scene* context, entt::entity* selectionContext);

    void OnUIRender() override;

private:
    Trinity::Renderer* m_Renderer = nullptr;
    Trinity::Scene* m_Context = nullptr;
    entt::entity* m_SelectionContext = nullptr;
    glm::vec2 m_ViewportSize{ 0.0f, 0.0f };
};