#pragma once

#include <Trinity.hpp>

class HubLayer final : public Trinity::Layer
{
public:
    HubLayer();

    void OnAttach() override;
};