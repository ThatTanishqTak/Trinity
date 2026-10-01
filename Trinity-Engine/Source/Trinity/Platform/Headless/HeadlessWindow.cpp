#include "Trinity/Platform/Headless/HeadlessWindow.hpp"

#include <chrono>
#include <thread>

namespace Trinity
{
    HeadlessWindow::HeadlessWindow(const WindowSpecification& specification) : m_Width(specification.Width), m_Height(specification.Height)
    {

    }

    void HeadlessWindow::PollEvents()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}