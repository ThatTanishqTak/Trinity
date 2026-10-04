#pragma once

#include "Panels/Panel.hpp"

class ConsolePanel final : public Panel
{
public:
    ConsolePanel();

protected:
    void OnImGuiRender() override;
};