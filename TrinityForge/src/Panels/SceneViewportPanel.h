#pragma once

#include "Trinity/UI/Panel.h"
#include <imgui.h>
#include <vector>

class SceneViewportPanel : public Trinity::Panel
{
public:
    SceneViewportPanel();
    void OnUIRender() override;

private:
    enum class PrimitiveType { Rectangle, Circle };

    struct Primitive
    {
        PrimitiveType m_Type;
        ImVec2 m_Position;
        ImVec2 m_Size;
    };

    std::vector<Primitive> m_Primitives;
};