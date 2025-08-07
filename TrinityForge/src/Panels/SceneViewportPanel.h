#pragma once

#include "Trinity/UI/Panel.h"
#include <imgui.h>
#include <vector>

namespace Trinity
{
    class Renderer;
}

class SceneViewportPanel : public Trinity::Panel
{
public:
    SceneViewportPanel(Trinity::Renderer* renderer);
    void OnUIRender() override;

private:
    enum class PrimitiveType { Rectangle, Circle };

    struct Primitive
    {
        PrimitiveType m_Type;
        ImVec2 m_Position;
        ImVec2 m_Size;
    };

    Trinity::Renderer* m_Renderer = nullptr;
    std::vector<Primitive> m_Primitives;
};