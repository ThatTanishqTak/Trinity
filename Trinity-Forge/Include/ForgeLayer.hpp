#pragma once

#include <Trinity.hpp>

class ForgeLayer final : public Trinity::Layer
{
public:
    ForgeLayer();

    void OnAttach() override;
    void OnEvent(Trinity::Event& event) override;
    void OnImGuiRender() override;

private:
    bool OnKeyPressed(Trinity::KeyPressedEvent& event);

    bool m_ShowDemoWindow = true;
};