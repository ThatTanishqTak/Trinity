#pragma once

#include "Trinity/UI/Panel.h"
#include "Trinity/Renderer/Renderer.h"

#include <imgui.h>

class SceneViewportPanel : public Trinity::Panel
{
public:
    explicit SceneViewportPanel(Trinity::Renderer* renderer);
    void OnUIRender() override;

private:
    Trinity::Renderer* m_Renderer = nullptr;
    ImTextureID m_Image;
};