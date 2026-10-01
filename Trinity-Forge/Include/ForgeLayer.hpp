#pragma once

#include <Trinity.hpp>

class ForgeLayer final : public Trinity::Layer
{
public:
    ForgeLayer();

    void OnAttach() override;
};