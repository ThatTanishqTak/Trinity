#pragma once

#include "Panels/Panel.hpp"

class PropertiesPanel final : public Panel
{
public:
    PropertiesPanel();

protected:
    void OnImGuiRender() override;
};