#pragma once

#include "Panels/Panel.hpp"

class ViewportPanel final : public Panel
{
public:
    ViewportPanel();

protected:
    void OnImGuiRender() override;
};