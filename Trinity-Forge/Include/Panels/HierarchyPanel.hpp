#pragma once

#include "Panels/Panel.hpp"

class HierarchyPanel final : public Panel
{
public:
    HierarchyPanel();

protected:
    void OnImGuiRender() override;
};