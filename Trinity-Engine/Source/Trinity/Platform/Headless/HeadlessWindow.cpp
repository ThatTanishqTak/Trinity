#include "Trinity/Platform/Headless/HeadlessWindow.hpp"

#include "Trinity/Events/ApplicationEvent.hpp"

#include <chrono>
#include <thread>

namespace Trinity
{
    HeadlessWindow::HeadlessWindow(const WindowSpecification& specification) : m_Position(specification.Position.value_or(WindowPosition{})), m_Width(specification.Width), m_Height(specification.Height)
    {

    }

    void HeadlessWindow::PollEvents()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Moves and resizes report themselves at once, as they do on Windows
    void HeadlessWindow::SetPosition(WindowPosition position)
    {
        m_Position = position;

        WindowMovedEvent l_Event(m_Position.X, m_Position.Y);
        Dispatch(l_Event);
    }

    void HeadlessWindow::SetSize(std::uint32_t width, std::uint32_t height)
    {
        m_Width = width;
        m_Height = height;

        WindowResizeEvent l_Event(m_Width, m_Height);
        Dispatch(l_Event);
    }

    void HeadlessWindow::Dispatch(Event& event)
    {
        if (m_EventCallback)
        {
            m_EventCallback(event);
        }
    }
}