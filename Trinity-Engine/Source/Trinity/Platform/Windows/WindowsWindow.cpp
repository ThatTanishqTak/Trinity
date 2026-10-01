#include "Trinity/Platform/Windows/WindowsWindow.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Events/ApplicationEvent.hpp"
#include "Trinity/Events/KeyEvent.hpp"
#include "Trinity/Events/MouseEvent.hpp"

#include <string>

namespace Trinity
{
    namespace
    {
        constexpr const wchar_t* c_WindowClassName = L"TrinityWindow";

        std::uint32_t s_WindowCount = 0;

        std::wstring ToWide(std::string_view utf8)
        {
            if (utf8.empty())
            {
                return {};
            }

            const int l_SourceLength = static_cast<int>(utf8.size());
            const int l_Length = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), l_SourceLength, nullptr, 0);
            std::wstring l_Wide(static_cast<std::size_t>(l_Length), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), l_SourceLength, l_Wide.data(), l_Length);

            return l_Wide;
        }

        KeyCode TranslateKey(WPARAM virtualKey, LPARAM lParam)
        {
            const UINT l_ScanCode = static_cast<UINT>((lParam >> 16) & 0xFF);
            const bool l_Extended = (lParam & (LPARAM{ 1 } << 24)) != 0;

            if ((virtualKey >= '0' && virtualKey <= '9') || (virtualKey >= 'A' && virtualKey <= 'Z'))
            {
                return static_cast<KeyCode>(virtualKey);
            }

            if (virtualKey >= VK_F1 && virtualKey <= VK_F24)
            {
                return static_cast<KeyCode>(std::to_underlying(KeyCode::TR_F1) + (virtualKey - VK_F1));
            }

            if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9)
            {
                return static_cast<KeyCode>(std::to_underlying(KeyCode::TR_KP0) + (virtualKey - VK_NUMPAD0));
            }

            switch (virtualKey)
            {
                case VK_SHIFT:
                {
                    return ::MapVirtualKeyW(l_ScanCode, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? KeyCode::TR_RIGHT_SHIFT : KeyCode::TR_LEFT_SHIFT;
                }
                case VK_CONTROL:
                {
                    return l_Extended ? KeyCode::TR_RIGHT_CONTROL : KeyCode::TR_LEFT_CONTROL;
                }
                case VK_MENU:
                {
                    return l_Extended ? KeyCode::TR_RIGHT_ALT : KeyCode::TR_LEFT_ALT;
                }
                case VK_RETURN:
                {
                    return l_Extended ? KeyCode::TR_KP_ENTER : KeyCode::TR_ENTER;
                }
                case VK_LWIN:
                {
                    return KeyCode::TR_LEFT_SUPER;
                }
                case VK_RWIN:
                {
                    return KeyCode::TR_RIGHT_SUPER;
                }
                case VK_APPS:
                {
                    return KeyCode::TR_MENU;
                }
                case VK_SPACE:
                {
                    return KeyCode::TR_SPACE;
                }
                case VK_ESCAPE:
                {
                    return KeyCode::TR_ESCAPE;
                }
                case VK_TAB:
                {
                    return KeyCode::TR_TAB;
                }
                case VK_BACK:
                {
                    return KeyCode::TR_BACKSPACE;
                }
                case VK_INSERT:
                {
                    return KeyCode::TR_INSERT;
                }
                case VK_DELETE:
                {
                    return KeyCode::TR_DELETE;
                }
                case VK_RIGHT:
                {
                    return KeyCode::TR_RIGHT;
                }
                case VK_LEFT:
                {
                    return KeyCode::TR_LEFT;
                }
                case VK_DOWN:
                {
                    return KeyCode::TR_DOWN;
                }
                case VK_UP:
                {
                    return KeyCode::TR_UP;
                }
                case VK_PRIOR:
                {
                    return KeyCode::TR_PAGE_UP;
                }
                case VK_NEXT:
                {
                    return KeyCode::TR_PAGE_DOWN;
                }
                case VK_HOME:
                {
                    return KeyCode::TR_HOME;
                }
                case VK_END:
                {
                    return KeyCode::TR_END;
                }
                case VK_CAPITAL:
                {
                    return KeyCode::TR_CAPS_LOCK;
                }
                case VK_SCROLL:
                {
                    return KeyCode::TR_SCROLL_LOCK;
                }
                case VK_NUMLOCK:
                {
                    return KeyCode::TR_NUM_LOCK;
                }
                case VK_SNAPSHOT:
                {
                    return KeyCode::TR_PRINT_SCREEN;
                }
                case VK_PAUSE:
                {
                    return KeyCode::TR_PAUSE;
                }
                case VK_DECIMAL:
                {
                    return KeyCode::TR_KP_DECIMAL;
                }
                case VK_DIVIDE:
                {
                    return KeyCode::TR_KP_DIVIDE;
                }
                case VK_MULTIPLY:
                {
                    return KeyCode::TR_KP_MULTIPLY;
                }
                case VK_SUBTRACT:
                {
                    return KeyCode::TR_KP_SUBTRACT;
                }
                case VK_ADD:
                {
                    return KeyCode::TR_KP_ADD;
                }
                case VK_OEM_7:
                {
                    return KeyCode::TR_APOSTROPHE;
                }
                case VK_OEM_COMMA:
                {
                    return KeyCode::TR_COMMA;
                }
                case VK_OEM_MINUS:
                {
                    return KeyCode::TR_MINUS;
                }
                case VK_OEM_PERIOD:
                {
                    return KeyCode::TR_PERIOD;
                }
                case VK_OEM_2:
                {
                    return KeyCode::TR_SLASH;
                }
                case VK_OEM_1:
                {
                    return KeyCode::TR_SEMICOLON;
                }
                case VK_OEM_PLUS:
                {
                    return KeyCode::TR_EQUAL;
                }
                case VK_OEM_4:
                {
                    return KeyCode::TR_LEFT_BRACKET;
                }
                case VK_OEM_5:
                {
                    return KeyCode::TR_BACKSLASH;
                }
                case VK_OEM_6:
                {
                    return KeyCode::TR_RIGHT_BRACKET;
                }
                case VK_OEM_3:
                {
                    return KeyCode::TR_GRAVE_ACCENT;
                }
                default:
                {
                    return KeyCode::UNKNOWN;
                }
            }
        }
    }

    WindowsWindow::WindowsWindow(const WindowSpecification& specification) : m_Width(specification.Width), m_Height(specification.Height)
    {
        const HINSTANCE l_Instance = ::GetModuleHandleW(nullptr);

        if (s_WindowCount == 0)
        {
            WNDCLASSEXW l_WindowClass{};
            l_WindowClass.cbSize = sizeof(l_WindowClass);
            l_WindowClass.lpfnWndProc = &WindowsWindow::WindowProcedure;
            l_WindowClass.hInstance = l_Instance;
            l_WindowClass.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
            l_WindowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
            l_WindowClass.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
            l_WindowClass.lpszClassName = c_WindowClassName;

            [[maybe_unused]] const ATOM l_Atom = ::RegisterClassExW(&l_WindowClass);
            TR_CORE_ASSERT(l_Atom != 0, "RegisterClassExW failed with error {}", ::GetLastError());
        }

        DWORD l_Style = WS_OVERLAPPEDWINDOW;
        if (!specification.Resizable)
        {
            l_Style &= ~static_cast<DWORD>(WS_THICKFRAME | WS_MAXIMIZEBOX);
        }

        RECT l_Rectangle{ 0, 0, static_cast<LONG>(specification.Width), static_cast<LONG>(specification.Height) };
        ::AdjustWindowRectEx(&l_Rectangle, l_Style, FALSE, 0);

        m_Handle = ::CreateWindowExW(0, c_WindowClassName, ToWide(specification.Title).c_str(), l_Style, CW_USEDEFAULT, CW_USEDEFAULT, l_Rectangle.right - l_Rectangle.left, l_Rectangle.bottom - l_Rectangle.top, nullptr, nullptr, l_Instance, this);

        TR_CORE_ASSERT(m_Handle != nullptr, "CreateWindowExW failed with error {}", ::GetLastError());
        if (m_Handle == nullptr)
        {
            return;
        }

        ++s_WindowCount;
        ::ShowWindow(m_Handle, SW_SHOW);
    }

    WindowsWindow::~WindowsWindow()
    {
        if (m_Handle == nullptr)
        {
            return;
        }

        ::SetWindowLongPtrW(m_Handle, GWLP_USERDATA, 0);
        ::DestroyWindow(m_Handle);
        m_Handle = nullptr;

        if (--s_WindowCount == 0)
        {
            ::UnregisterClassW(c_WindowClassName, ::GetModuleHandleW(nullptr));
        }
    }

    void WindowsWindow::PollEvents()
    {
        MSG l_Message;
        while (::PeekMessageW(&l_Message, nullptr, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&l_Message);
            ::DispatchMessageW(&l_Message);
        }
    }

    void WindowsWindow::SetTitle(std::string_view title)
    {
        ::SetWindowTextW(m_Handle, ToWide(title).c_str());
    }

    void WindowsWindow::Dispatch(Event& event)
    {
        if (m_EventCallback)
        {
            m_EventCallback(event);
        }
    }

    void WindowsWindow::OnMouseButton(MouseCode button, bool pressed)
    {
        if (pressed)
        {
            if (m_MouseButtonsDown++ == 0)
            {
                ::SetCapture(m_Handle);
            }

            MouseButtonPressedEvent l_Event(button);
            Dispatch(l_Event);
        }
        else
        {
            if (m_MouseButtonsDown > 0 && --m_MouseButtonsDown == 0)
            {
                ::ReleaseCapture();
            }

            MouseButtonReleasedEvent l_Event(button);
            Dispatch(l_Event);
        }
    }

    LRESULT CALLBACK WindowsWindow::WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
    {
        WindowsWindow* window = nullptr;

        if (message == WM_NCCREATE)
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            window = static_cast<WindowsWindow*>(createStruct->lpCreateParams);
            window->m_Handle = handle;
            ::SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }
        else
        {
            window = reinterpret_cast<WindowsWindow*>(::GetWindowLongPtrW(handle, GWLP_USERDATA));
        }

        if (window != nullptr)
        {
            return window->HandleMessage(message, wParam, lParam);
        }

        return ::DefWindowProcW(handle, message, wParam, lParam);
    }

    LRESULT WindowsWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
            case WM_CLOSE:
            {
                WindowCloseEvent l_Event;
                Dispatch(l_Event);

                return 0;
            }
            case WM_SIZE:
            {
                m_Width = LOWORD(lParam);
                m_Height = HIWORD(lParam);
                WindowResizeEvent l_Event(m_Width, m_Height);
                Dispatch(l_Event);

                return 0;
            }
            case WM_DPICHANGED:
            {
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                ::SetWindowPos(m_Handle, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);

                return 0;
            }
            case WM_SETFOCUS:
            {
                WindowFocusEvent l_Event;
                Dispatch(l_Event);

                return 0;
            }
            case WM_KILLFOCUS:
            {
                m_MouseButtonsDown = 0;
                ::ReleaseCapture();

                WindowLostFocusEvent l_Event;
                Dispatch(l_Event);

                return 0;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
            {
                const KeyCode key = TranslateKey(wParam, lParam);
                if (key != KeyCode::UNKNOWN)
                {
                    const bool isRepeat = (lParam & (LPARAM{ 1 } << 30)) != 0;
                    KeyPressedEvent l_Event(key, isRepeat);
                    Dispatch(l_Event);
                }

                break;
            }
            case WM_KEYUP:
            case WM_SYSKEYUP:
            {
                const KeyCode key = TranslateKey(wParam, lParam);
                if (key != KeyCode::UNKNOWN)
                {
                    KeyReleasedEvent l_Event(key);
                    Dispatch(l_Event);
                }

                break;
            }
            case WM_CHAR:
            {
                const auto unit = static_cast<wchar_t>(wParam);
                if (IS_HIGH_SURROGATE(unit))
                {
                    m_HighSurrogate = unit;

                    return 0;
                }

                char32_t codepoint = unit;
                if (IS_LOW_SURROGATE(unit))
                {
                    if (m_HighSurrogate == 0)
                    {
                        return 0;
                    }

                    codepoint = 0x10000u + ((static_cast<char32_t>(m_HighSurrogate) - 0xD800u) << 10) + (static_cast<char32_t>(unit) - 0xDC00u);
                }
                m_HighSurrogate = 0;

                if (codepoint >= 32 && codepoint != 127)
                {
                    KeyTypedEvent l_Event(codepoint);
                    Dispatch(l_Event);
                }

                return 0;
            }
            case WM_MOUSEMOVE:
            {
                MouseMovedEvent l_Event(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
                Dispatch(l_Event);

                return 0;
            }
            case WM_MOUSEWHEEL:
            {
                MouseScrolledEvent l_Event(0.0f, static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA));
                Dispatch(l_Event);

                return 0;
            }
            case WM_MOUSEHWHEEL:
            {
                MouseScrolledEvent l_Event(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA), 0.0f);
                Dispatch(l_Event);

                return 0;
            }
            case WM_LBUTTONDOWN:
            {
                OnMouseButton(MouseCode::TR_LEFT, true);

                return 0;
            }
            case WM_LBUTTONUP:
            {
                OnMouseButton(MouseCode::TR_LEFT, false);

                return 0;
            }
            case WM_RBUTTONDOWN:
            {
                OnMouseButton(MouseCode::TR_RIGHT, true);

                return 0;
            }
            case WM_RBUTTONUP:
            {
                OnMouseButton(MouseCode::TR_RIGHT, false);

                return 0;
            }
            case WM_MBUTTONDOWN:
            {
                OnMouseButton(MouseCode::TR_MIDDLE, true);

                return 0;
            }
            case WM_MBUTTONUP:
            {
                OnMouseButton(MouseCode::TR_MIDDLE, false);

                return 0;
            }
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            {
                const MouseCode l_Button = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? MouseCode::TR_BUTTON_3 : MouseCode::TR_BUTTON_4;
                OnMouseButton(l_Button, message == WM_XBUTTONDOWN);

                return TRUE;
            }
            default:
            {
                break;
            }
        }

        return ::DefWindowProcW(m_Handle, message, wParam, lParam);
    }
}