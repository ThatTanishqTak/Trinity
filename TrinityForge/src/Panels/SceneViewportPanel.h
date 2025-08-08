#pragma once

#include "Trinity/UI/Panel.h"

#include <imgui.h>
#include <entt/entt.hpp>

namespace Trinity
{
    class Renderer;
    class Scene;
}

class SceneViewportPanel : public Trinity::Panel
{
public:
    SceneViewportPanel(Trinity::Renderer* renderer, Trinity::Scene* context, entt::entity* selectionContext);
    void OnUIRender() override;

private:
    Trinity::Renderer* m_Renderer = nullptr;
    Trinity::Scene* m_Context = nullptr;
    entt::entity* m_SelectionContext = nullptr;
    ImVec2 m_ViewportSize{ 0.0f, 0.0f };
};