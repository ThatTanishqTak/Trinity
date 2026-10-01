#include "SandboxLayer.hpp"

SandboxLayer::SandboxLayer() : Layer("Sandbox")
{

}

void SandboxLayer::OnAttach()
{
    TR_INFO("Sandbox attached. Escape closes the window.");
}

void SandboxLayer::OnUpdate(Trinity::Timestep timestep)
{
    m_SecondsSinceReport += timestep;
    ++m_FramesSinceReport;

    if (m_SecondsSinceReport >= 1.0f)
    {
        TR_TRACE("{:.1f} fps", static_cast<float>(m_FramesSinceReport) / m_SecondsSinceReport);
        m_SecondsSinceReport = 0.0f;
        m_FramesSinceReport = 0;
    }
}

void SandboxLayer::OnEvent(Trinity::Event& event)
{
    if (event.GetEventType() != Trinity::EventType::MouseMoved)
    {
        TR_TRACE("{}", event);
    }

    Trinity::EventDispatcher l_Dispatcher(event);
    l_Dispatcher.Dispatch<Trinity::KeyPressedEvent>(TR_BIND_EVENT_FN(OnKeyPressed));
}

bool SandboxLayer::OnKeyPressed(Trinity::KeyPressedEvent& event)
{
    if (event.GetKeyCode() == Trinity::KeyCode::TR_ESCAPE)
    {
        Trinity::Application::Get().Close();

        return true;
    }

    return false;
}