#pragma once

#include "Trinity/UI/Panel.h"
#include "Trinity/Renderer/Renderer.h"

#include <glm/vec2.hpp>

class SceneViewportPanel : public Trinity::Panel
{
public:
    SceneViewportPanel(Trinity::Renderer* renderer);

    void OnUIRender() override;

private:
    Trinity::Renderer* m_Renderer = nullptr;
    glm::vec2 m_ViewportSize{ 0.0f, 0.0f };
};