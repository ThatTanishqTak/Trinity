#pragma once

#include "Trinity/UI/Panel.h"

#include <memory>
#include <vector>

class EditorLayer : public Trinity::Panel
{
public:
    EditorLayer() = default;

    void OnUIRender() override;
    void RegisterPanel(std::unique_ptr<Trinity::Panel> p_Panel);

private:
    std::vector<std::unique_ptr<Trinity::Panel>> m_Panels;
};