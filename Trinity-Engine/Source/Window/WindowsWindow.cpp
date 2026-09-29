#include "Trinity/Window/WindowsWindow.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Events/ApplicationEvent.hpp"
#include "Trinity/Events/KeyEvent.hpp"
#include "Trinity/Events/MouseEvent.hpp"

#include <Windows.h>
#include <windowsx.h>

namespace Trinity
{
	namespace
	{
		constexpr const wchar_t* s_WindowClassName = L"TrinityWindowClass";

		constexpr DWORD s_WindowStyle = WS_OVERLAPPEDWINDOW;
		constexpr DWORD s_WindowExStyle = 0;
		constexpr int s_MinClientWidth = 320;
		constexpr int s_MinClientHeight = 240;
		constexpr UINT_PTR s_ModalFrameTimerId = 1;

		uint32_t s_WindowClassRefCount = 0;
		bool s_DpiAwarenessSet = false;

		void EnableDpiAwareness()
		{
			if (s_DpiAwarenessSet)
			{
				return;
			}

			s_DpiAwarenessSet = true;

			if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
			{
				TR_CORE_WARN("Failed to set per-monitor DPI awareness (error {}), a manifest may have set it already", GetLastError());
			}
		}

		std::wstring ToWide(std::string_view text)
		{
			if (text.empty())
			{
				return {};
			}

			const int l_Size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);

			std::wstring l_Result(static_cast<size_t>(l_Size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), l_Result.data(), l_Size);

			return l_Result;
		}

		KeyCode TranslateKey(WPARAM wParam, LPARAM lParam)
		{
			const bool l_Extended = (HIWORD(lParam) & KF_EXTENDED) != 0;
			const UINT l_ScanCode = LOBYTE(HIWORD(lParam));

			if ((wParam >= '0' && wParam <= '9') || (wParam >= 'A' && wParam <= 'Z'))
			{
				return static_cast<KeyCode>(wParam);
			}

			if (wParam >= VK_F1 && wParam <= VK_F24)
			{
				return static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::F1) + (wParam - VK_F1));
			}

			if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
			{
				return static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::KP0) + (wParam - VK_NUMPAD0));
			}

			switch (wParam)
			{
				case VK_SHIFT:
				{
					return MapVirtualKeyW(l_ScanCode, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? KeyCode::RightShift : KeyCode::LeftShift;
				}
				case VK_CONTROL:
				{
					return l_Extended ? KeyCode::RightControl : KeyCode::LeftControl;
				}
				case VK_MENU:
				{
					return l_Extended ? KeyCode::RightAlt : KeyCode::LeftAlt;
				}
				case VK_RETURN:
				{
					return l_Extended ? KeyCode::KPEnter : KeyCode::Enter;
				}
				case VK_LWIN:
				{
					return KeyCode::LeftSuper;
				}
				case VK_RWIN:
				{
					return KeyCode::RightSuper;
				}
				case VK_APPS:
				{
					return KeyCode::Menu;
				}
				case VK_SPACE:
				{
					return KeyCode::TR_SPACE;
				}
				case VK_ESCAPE:
				{
					return KeyCode::Escape;
				}
				case VK_TAB:
				{
					return KeyCode::Tab;
				}
				case VK_BACK:
				{
					return KeyCode::Backspace;
				}
				case VK_INSERT:
				{
					return KeyCode::Insert;
				}
				case VK_DELETE:
				{
					return KeyCode::Delete;
				}
				case VK_RIGHT:
				{
					return KeyCode::Right;
				}
				case VK_LEFT:
				{
					return KeyCode::Left;
				}
				case VK_DOWN:
				{
					return KeyCode::Down;
				}
				case VK_UP:
				{
					return KeyCode::Up;
				}
				case VK_PRIOR:
				{
					return KeyCode::PageUp;
				}
				case VK_NEXT:
				{
					return KeyCode::PageDown;
				}
				case VK_HOME:
				{
					return KeyCode::Home;
				}
				case VK_END:
				{
					return KeyCode::End;
				}
				case VK_CAPITAL:
				{
					return KeyCode::CapsLock;
				}
				case VK_SCROLL:
				{
					return KeyCode::ScrollLock;
				}
				case VK_NUMLOCK:
				{
					return KeyCode::NumLock;
				}
				case VK_SNAPSHOT:
				{
					return KeyCode::PrintScreen;
				}
				case VK_PAUSE:
				{
					return KeyCode::Pause;
				}
				case VK_DECIMAL:
				{
					return KeyCode::KPDecimal;
				}
				case VK_DIVIDE:
				{
					return KeyCode::KPDivide;
				}
				case VK_MULTIPLY:
				{
					return KeyCode::KPMultiply;
				}
				case VK_SUBTRACT:
				{
					return KeyCode::KPSubtract;
				}
				case VK_ADD:
				{
					return KeyCode::KPAdd;
				}
				case VK_OEM_1:
				{
					return KeyCode::Semicolon;
				}
				case VK_OEM_PLUS:
				{
					return KeyCode::Equal;
				}
				case VK_OEM_COMMA:
				{
					return KeyCode::Comma;
				}
				case VK_OEM_MINUS:
				{
					return KeyCode::Minus;
				}
				case VK_OEM_PERIOD:
				{
					return KeyCode::Period;
				}
				case VK_OEM_2:
				{
					return KeyCode::Slash;
				}
				case VK_OEM_3:
				{
					return KeyCode::GraveAccent;
				}
				case VK_OEM_4:
				{
					return KeyCode::LeftBracket;
				}
				case VK_OEM_5:
				{
					return KeyCode::Backslash;
				}
				case VK_OEM_6:
				{
					return KeyCode::RightBracket;
				}
				case VK_OEM_7:
				{
					return KeyCode::Apostrophe;
				}
				default:
				{
					return KeyCode::Unknown;
				}
			}
		}

		MouseCode TranslateMouseButton(UINT message, WPARAM wParam)
		{
			switch (message)
			{
				case WM_LBUTTONDOWN:
				case WM_LBUTTONUP:
				case WM_LBUTTONDBLCLK:
				{
					return MouseCode::Left;
				}
				case WM_RBUTTONDOWN:
				case WM_RBUTTONUP:
				case WM_RBUTTONDBLCLK:
				{
					return MouseCode::Right;
				}
				case WM_MBUTTONDOWN:
				case WM_MBUTTONUP:
				case WM_MBUTTONDBLCLK:
				{
					return MouseCode::Middle;
				}
				default:
				{
					return GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? MouseCode::Button3 : MouseCode::Button4;
				}
			}
		}
	}

	struct WindowsWindow::Procedure
	{
		static bool AcquireWindowClass(HINSTANCE instance)
		{
			if (s_WindowClassRefCount == 0)
			{
				WNDCLASSEXW l_WindowClass{};
				l_WindowClass.cbSize = sizeof(WNDCLASSEXW);
				l_WindowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
				l_WindowClass.lpfnWndProc = &Procedure::Handle;
				l_WindowClass.hInstance = instance;
				l_WindowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
				l_WindowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
				l_WindowClass.lpszClassName = s_WindowClassName;

				if (!RegisterClassExW(&l_WindowClass))
				{
					const DWORD l_Error = GetLastError();
					if (l_Error != ERROR_CLASS_ALREADY_EXISTS)
					{
						TR_CORE_CRITICAL("Failed to register window class (error {})", l_Error);

						return false;
					}
				}
			}

			++s_WindowClassRefCount;

			return true;
		}

		static void ReleaseWindowClass(HINSTANCE instance)
		{
			if (s_WindowClassRefCount == 0)
			{
				return;
			}

			if (--s_WindowClassRefCount == 0)
			{
				UnregisterClassW(s_WindowClassName, instance);
			}
		}

		static void RunModalFrame(WindowsWindow* window)
		{
			if (window->m_FrameCallback)
			{
				window->m_FrameCallback();
			}
		}

		static LRESULT CALLBACK Handle(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam)
		{
			if (message == WM_NCCREATE)
			{
				const CREATESTRUCTW* l_CreateStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
				SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(l_CreateStruct->lpCreateParams));

				return DefWindowProcW(windowHandle, message, wParam, lParam);
			}

			WindowsWindow* l_Window = reinterpret_cast<WindowsWindow*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
			if (!l_Window)
			{
				return DefWindowProcW(windowHandle, message, wParam, lParam);
			}

			switch (message)
			{
				case WM_CLOSE:
				{
					if (l_Window->m_EventCallback)
					{
						l_Window->Emit<WindowCloseEvent>();
					}
					else
					{
						l_Window->m_CloseRequested = true;
					}

					return 0;
				}
				case WM_SIZE:
				{
					if (wParam == SIZE_MINIMIZED)
					{
						l_Window->m_Minimized = true;

						return 0;
					}

					l_Window->m_Minimized = false;

					const uint32_t l_Width = LOWORD(lParam);
					const uint32_t l_Height = HIWORD(lParam);

					if (l_Width != l_Window->m_Width || l_Height != l_Window->m_Height)
					{
						l_Window->m_Width = l_Width;
						l_Window->m_Height = l_Height;

						l_Window->Emit<WindowResizeEvent>(l_Width, l_Height);
					}

					if (l_Window->m_InModalLoop)
					{
						RunModalFrame(l_Window);
					}

					return 0;
				}
				case WM_GETMINMAXINFO:
				{
					const UINT l_DPI = GetDpiForWindow(windowHandle);

					RECT l_Rect{ 0, 0, MulDiv(s_MinClientWidth, static_cast<int>(l_DPI), USER_DEFAULT_SCREEN_DPI), MulDiv(s_MinClientHeight, static_cast<int>(l_DPI), USER_DEFAULT_SCREEN_DPI) };
					AdjustWindowRectExForDpi(&l_Rect, s_WindowStyle, FALSE, s_WindowExStyle, l_DPI);

					MINMAXINFO* l_Info = reinterpret_cast<MINMAXINFO*>(lParam);
					l_Info->ptMinTrackSize.x = l_Rect.right - l_Rect.left;
					l_Info->ptMinTrackSize.y = l_Rect.bottom - l_Rect.top;

					return 0;
				}
				case WM_ENTERSIZEMOVE:
				case WM_ENTERMENULOOP:
				{
					l_Window->m_InModalLoop = true;
					SetTimer(windowHandle, s_ModalFrameTimerId, USER_TIMER_MINIMUM, nullptr);

					return 0;
				}
				case WM_EXITSIZEMOVE:
				case WM_EXITMENULOOP:
				{
					KillTimer(windowHandle, s_ModalFrameTimerId);
					l_Window->m_InModalLoop = false;

					return 0;
				}
				case WM_TIMER:
				{
					if (wParam == s_ModalFrameTimerId)
					{
						RunModalFrame(l_Window);

						return 0;
					}

					break;
				}
				case WM_MOVE:
				{
					l_Window->Emit<WindowMovedEvent>(static_cast<int32_t>(GET_X_LPARAM(lParam)), static_cast<int32_t>(GET_Y_LPARAM(lParam)));

					return 0;
				}
				case WM_SETFOCUS:
				{
					l_Window->Emit<WindowFocusEvent>();

					return 0;
				}
				case WM_KILLFOCUS:
				{
					l_Window->Emit<WindowLostFocusEvent>();

					return 0;
				}
				case WM_SYSCOMMAND:
				{
					if ((wParam & 0xFFF0) == SC_KEYMENU)
					{
						return 0;
					}

					break;
				}
				case WM_KEYDOWN:
				case WM_SYSKEYDOWN:
				{
					const KeyCode l_Key = TranslateKey(wParam, lParam);
					if (l_Key != KeyCode::Unknown)
					{
						l_Window->Emit<KeyPressedEvent>(l_Key, (HIWORD(lParam) & KF_REPEAT) != 0);
					}

					break;
				}
				case WM_KEYUP:
				case WM_SYSKEYUP:
				{
					const KeyCode l_Key = TranslateKey(wParam, lParam);
					if (l_Key != KeyCode::Unknown)
					{
						if (l_Key == KeyCode::PrintScreen)
						{
							l_Window->Emit<KeyPressedEvent>(l_Key, false);
						}

						l_Window->Emit<KeyReleasedEvent>(l_Key);
					}

					break;
				}
				case WM_CHAR:
				{
					const uint16_t l_Unit = static_cast<uint16_t>(wParam);

					if (l_Unit >= 0xD800 && l_Unit <= 0xDBFF)
					{
						l_Window->m_HighSurrogate = l_Unit;

						return 0;
					}

					uint32_t l_Codepoint = l_Unit;

					if (l_Unit >= 0xDC00 && l_Unit <= 0xDFFF)
					{
						if (l_Window->m_HighSurrogate == 0)
						{
							return 0;
						}

						l_Codepoint = 0x10000u + ((static_cast<uint32_t>(l_Window->m_HighSurrogate) - 0xD800u) << 10) + (static_cast<uint32_t>(l_Unit) - 0xDC00u);
					}

					l_Window->m_HighSurrogate = 0;

					if (l_Codepoint >= 32 && l_Codepoint != 127)
					{
						l_Window->Emit<KeyTypedEvent>(l_Codepoint);
					}

					return 0;
				}
				case WM_MOUSEMOVE:
				{
					l_Window->Emit<MouseMovedEvent>(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));

					return 0;
				}
				case WM_MOUSEWHEEL:
				{
					l_Window->Emit<MouseScrolledEvent>(0.0f, static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA));

					return 0;
				}
				case WM_MOUSEHWHEEL:
				{
					l_Window->Emit<MouseScrolledEvent>(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA), 0.0f);

					return 0;
				}
				case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
				case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
				case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
				case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
				{
					SetCapture(windowHandle);

					l_Window->Emit<MouseButtonPressedEvent>(TranslateMouseButton(message, wParam));

					return (message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK) ? TRUE : 0;
				}
				case WM_LBUTTONUP:
				case WM_RBUTTONUP:
				case WM_MBUTTONUP:
				case WM_XBUTTONUP:
				{
					l_Window->Emit<MouseButtonReleasedEvent>(TranslateMouseButton(message, wParam));

					if ((GET_KEYSTATE_WPARAM(wParam) & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2)) == 0)
					{
						ReleaseCapture();
					}

					return message == WM_XBUTTONUP ? TRUE : 0;
				}
				case WM_DPICHANGED:
				{
					const float l_Scale = static_cast<float>(LOWORD(wParam)) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
					if (l_Scale != l_Window->m_ContentScale)
					{
						l_Window->m_ContentScale = l_Scale;
						l_Window->Emit<WindowContentScaleEvent>(l_Scale);
					}

					const RECT* l_Suggested = reinterpret_cast<const RECT*>(lParam);
					SetWindowPos(windowHandle, nullptr, l_Suggested->left, l_Suggested->top, l_Suggested->right - l_Suggested->left, l_Suggested->bottom - l_Suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);

					return 0;
				}
				case WM_ERASEBKGND:
				{
					if (l_Window->m_RendererAttached)
					{
						return 1;
					}

					break;
				}
				default:
				{
					break;
				}
			}

			return DefWindowProcW(windowHandle, message, wParam, lParam);
		}
	};

	WindowsWindow::WindowsWindow() = default;
	WindowsWindow::~WindowsWindow() = default;

	bool WindowsWindow::Initialize(const WindowSpecification& specification)
	{
		TR_CORE_INFO("------- INITIALIZING WINDOW -------");

		EnableDpiAwareness();

		m_Instance = GetModuleHandleW(nullptr);
		m_Title = specification.Title;
		m_CloseRequested = false;
		m_Minimized = false;

		if (!Procedure::AcquireWindowClass(m_Instance))
		{
			return false;
		}

		const std::wstring l_Title = ToWide(m_Title);

		m_WindowHandle = CreateWindowExW(s_WindowExStyle, s_WindowClassName, l_Title.c_str(), s_WindowStyle, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, m_Instance, this);
		if (!m_WindowHandle)
		{
			TR_CORE_CRITICAL("Failed to create window (error {})", GetLastError());
			Procedure::ReleaseWindowClass(m_Instance);

			return false;
		}

		const UINT l_DPI = GetDpiForWindow(m_WindowHandle);
		m_ContentScale = static_cast<float>(l_DPI) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);

		RECT l_Rect{ 0, 0, MulDiv(static_cast<int>(specification.Width), static_cast<int>(l_DPI), USER_DEFAULT_SCREEN_DPI), MulDiv(static_cast<int>(specification.Height), static_cast<int>(l_DPI), USER_DEFAULT_SCREEN_DPI) };
		AdjustWindowRectExForDpi(&l_Rect, s_WindowStyle, FALSE, s_WindowExStyle, l_DPI);

		SetWindowPos(m_WindowHandle, nullptr, 0, 0, l_Rect.right - l_Rect.left, l_Rect.bottom - l_Rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		ShowWindow(m_WindowHandle, SW_SHOW);

		RECT l_ClientRect{};
		GetClientRect(m_WindowHandle, &l_ClientRect);

		m_Width = static_cast<uint32_t>(l_ClientRect.right - l_ClientRect.left);
		m_Height = static_cast<uint32_t>(l_ClientRect.bottom - l_ClientRect.top);

		TR_CORE_TRACE("Window Title: {}", m_Title);
		TR_CORE_TRACE("Window Resolution: {}x{}", m_Width, m_Height);
		TR_CORE_TRACE("Window Content Scale: {:.2f}", m_ContentScale);

		TR_CORE_INFO("------- WINDOW INITIALIZED -------");

		return true;
	}

	void WindowsWindow::Shutdown()
	{
		if (!m_WindowHandle)
		{
			return;
		}

		TR_CORE_INFO("------- SHUTTING DOWN WINDOW -------");

		DestroyWindow(m_WindowHandle);
		m_WindowHandle = nullptr;
		TR_CORE_TRACE("Destroyed window: {}", m_Title);

		Procedure::ReleaseWindowClass(m_Instance);

		TR_CORE_INFO("------- WINDOW SHUTDOWN COMPLETE -------");
	}

	bool WindowsWindow::PollEvents()
	{
		MSG l_Message{};
		while (PeekMessageW(&l_Message, nullptr, 0, 0, PM_REMOVE))
		{
			if (l_Message.message == WM_QUIT)
			{
				return false;
			}

			TranslateMessage(&l_Message);
			DispatchMessageW(&l_Message);
		}

		return !m_CloseRequested;
	}
}