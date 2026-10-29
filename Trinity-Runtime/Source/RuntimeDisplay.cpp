#include "RuntimeDisplay.hpp"

#include <string>
#include <utility>

namespace
{
    bool IsAltPressed()
    {
        return Trinity::Input::IsKeyPressed(Trinity::KeyCode::TR_LEFT_ALT) || Trinity::Input::IsKeyPressed(Trinity::KeyCode::TR_RIGHT_ALT);
    }

    bool IsHeadless()
    {
        return Trinity::Application::Get().GetSpecification().Window.Headless;
    }
}

// No Display.yaml is the first start, and one that cannot be read gives way to the game's window, reported
Trinity::DisplaySettings RuntimeDisplay::Load(const Trinity::WindowSettings& game)
{
    Trinity::DisplaySettings l_Game;
    l_Game.Window = game;

    const std::string l_Path = Trinity::GetDisplaySettingsPath();
    if (!Trinity::FileSystem::Exists(l_Path))
    {
        return l_Game;
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(l_Path);
    if (!l_Text)
    {
        TR_WARN("Runtime: {} could not be read, so the window opens as the game has it: {}", l_Path, Trinity::ToString(l_Text.GetError()));

        return l_Game;
    }

    Trinity::Expected<Trinity::DisplaySettings, std::string> l_Settings = Trinity::ParseDisplaySettings(*l_Text, game);
    if (!l_Settings)
    {
        TR_WARN("Runtime: {}, so the window opens as the game has it", l_Settings.GetError());

        return l_Game;
    }

    return std::move(*l_Settings);
}

// A windowed size is the window's, and a fullscreen window gets it back as it leaves fullscreen
void RuntimeDisplay::Configure(const Trinity::DisplaySettings& settings, Trinity::WindowSpecification& window)
{
    window.Width = settings.Window.Width;
    window.Height = settings.Window.Height;
    window.Fullscreen = settings.Window.Mode == Trinity::FullScreenMode::FullScreenWindow;
    window.Monitor = settings.Monitor;
    window.Position = settings.Position;
}

RuntimeDisplay::RuntimeDisplay(Trinity::DisplaySettings settings) : m_Settings(std::move(settings)), m_Saved(m_Settings)
{
    Trinity::Application::Get().GetRenderer().SetVSync(m_Settings.Window.VSync);
    Trinity::Screen::SetHost(this);
    TR_INFO("Runtime: the window is {} at {}x{}, with vsync {}", Trinity::ToString(m_Settings.Window.Mode), GetWidth(), GetHeight(), m_Settings.Window.VSync ? "on" : "off");
}

RuntimeDisplay::~RuntimeDisplay()
{
    Save();
    Trinity::Screen::SetHost(nullptr);
}

void RuntimeDisplay::OnEvent(Trinity::Event& event)
{
    Trinity::EventDispatcher l_Dispatcher(event);
    l_Dispatcher.Dispatch<Trinity::KeyPressedEvent>([this](Trinity::KeyPressedEvent& pressed)
    {
        const Trinity::KeyCode l_Key = pressed.GetKeyCode();
        if (pressed.IsRepeat() || (l_Key != Trinity::KeyCode::TR_ENTER && l_Key != Trinity::KeyCode::TR_KP_ENTER) || !IsAltPressed())
        {
            return false;
        }

        Trinity::ScreenRequest l_Switch;
        l_Switch.Mode = m_Request.Mode.value_or(m_Settings.Window.Mode) == Trinity::FullScreenMode::Windowed ? Trinity::FullScreenMode::FullScreenWindow : Trinity::FullScreenMode::Windowed;
        Request(l_Switch);

        return true;
    });
}

// Into fullscreen, the window first takes the windowed size asked for, so it comes back at it. Out of it, the window comes back where it was, then takes the size, which a fullscreen window was only given to keep
void RuntimeDisplay::Apply()
{
    if (!std::exchange(m_Requested, false))
    {
        return;
    }

    const Trinity::ScreenRequest l_Request = std::exchange(m_Request, {});
    Follow();

    Trinity::WindowSettings l_Wanted = m_Settings.Window;
    l_Wanted.Mode = l_Request.Mode.value_or(l_Wanted.Mode);
    l_Wanted.Width = l_Request.Width.value_or(l_Wanted.Width);
    l_Wanted.Height = l_Request.Height.value_or(l_Wanted.Height);
    l_Wanted.VSync = l_Request.VSync.value_or(l_Wanted.VSync);
    if (l_Wanted == m_Settings.Window)
    {
        return;
    }

    Trinity::Application& l_Application = Trinity::Application::Get();
    Trinity::Window& l_Window = l_Application.GetWindow();
    const auto a_Resize = [&l_Window, &l_Wanted]()
    {
        if (l_Window.GetWidth() != l_Wanted.Width || l_Window.GetHeight() != l_Wanted.Height)
        {
            l_Window.SetSize(l_Wanted.Width, l_Wanted.Height);
        }
    };

    if (l_Wanted.Mode == Trinity::FullScreenMode::Windowed)
    {
        l_Window.SetFullscreen(false);
        a_Resize();
    }
    else if (!l_Window.IsFullscreen())
    {
        a_Resize();
        l_Window.SetFullscreen(true);
    }

    if (l_Wanted.VSync != m_Settings.Window.VSync)
    {
        l_Application.GetRenderer().SetVSync(l_Wanted.VSync);
    }

    m_Settings.Window = l_Wanted;
    TR_INFO("Runtime: the window is {} at {}x{}, with vsync {}", Trinity::ToString(l_Wanted.Mode), GetWidth(), GetHeight(), l_Wanted.VSync ? "on" : "off");
    Save();
}

std::uint32_t RuntimeDisplay::GetWidth() const
{
    return Trinity::Application::Get().GetWindow().GetWidth();
}

std::uint32_t RuntimeDisplay::GetHeight() const
{
    return Trinity::Application::Get().GetWindow().GetHeight();
}

void RuntimeDisplay::Request(const Trinity::ScreenRequest& request)
{
    m_Request.Mode = request.Mode ? request.Mode : m_Request.Mode;
    m_Request.Width = request.Width ? request.Width : m_Request.Width;
    m_Request.Height = request.Height ? request.Height : m_Request.Height;
    m_Request.VSync = request.VSync ? request.VSync : m_Request.VSync;
    m_Requested = true;
}

// Nothing is taken from a minimized window, whose place and size Windows keeps off screen, or from a fullscreen one, whose are the monitor's
void RuntimeDisplay::Follow()
{
    const Trinity::Window& l_Window = Trinity::Application::Get().GetWindow();
    if (IsHeadless() || l_Window.IsMinimized())
    {
        return;
    }

    if (std::string l_Monitor = l_Window.GetMonitor(); !l_Monitor.empty())
    {
        m_Settings.Monitor = std::move(l_Monitor);
    }

    if (l_Window.IsFullscreen() || l_Window.GetWidth() == 0 || l_Window.GetHeight() == 0)
    {
        return;
    }

    m_Settings.Window.Width = l_Window.GetWidth();
    m_Settings.Window.Height = l_Window.GetHeight();
    m_Settings.Position = l_Window.GetPosition();
}

void RuntimeDisplay::Save()
{
    Follow();
    if (m_Settings == m_Saved)
    {
        return;
    }

    const std::string l_Path = Trinity::GetDisplaySettingsPath();
    if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(l_Path, Trinity::WriteDisplaySettings(m_Settings)); !l_Written)
    {
        TR_WARN("Runtime: the window's settings could not be kept in {}: {}", l_Path, Trinity::ToString(l_Written.GetError()));

        return;
    }

    m_Saved = m_Settings;
}