#include "Trinity/Window/WindowsWindow.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Events/ApplicationEvent.hpp"
#include "Trinity/Events/KeyEvent.hpp"
#include "Trinity/Events/MouseEvent.hpp"

#include <Windows.h>
#include <windowsx.h>

#include <array>
#include <utility>

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

		constexpr std::array<KeyCode, 512> CreateScanCodeTable()
		{
			std::array<KeyCode, 512> l_Table{};

			l_Table[0x00B] = KeyCode::D0;
			l_Table[0x002] = KeyCode::D1;
			l_Table[0x003] = KeyCode::D2;
			l_Table[0x004] = KeyCode::D3;
			l_Table[0x005] = KeyCode::D4;
			l_Table[0x006] = KeyCode::D5;
			l_Table[0x007] = KeyCode::D6;
			l_Table[0x008] = KeyCode::D7;
			l_Table[0x009] = KeyCode::D8;
			l_Table[0x00A] = KeyCode::D9;

			l_Table[0x01E] = KeyCode::A;
			l_Table[0x030] = KeyCode::B;
			l_Table[0x02E] = KeyCode::C;
			l_Table[0x020] = KeyCode::D;
			l_Table[0x012] = KeyCode::E;
			l_Table[0x021] = KeyCode::F;
			l_Table[0x022] = KeyCode::G;
			l_Table[0x023] = KeyCode::H;
			l_Table[0x017] = KeyCode::I;
			l_Table[0x024] = KeyCode::J;
			l_Table[0x025] = KeyCode::K;
			l_Table[0x026] = KeyCode::L;
			l_Table[0x032] = KeyCode::M;
			l_Table[0x031] = KeyCode::N;
			l_Table[0x018] = KeyCode::O;
			l_Table[0x019] = KeyCode::P;
			l_Table[0x010] = KeyCode::Q;
			l_Table[0x013] = KeyCode::R;
			l_Table[0x01F] = KeyCode::S;
			l_Table[0x014] = KeyCode::T;
			l_Table[0x016] = KeyCode::U;
			l_Table[0x02F] = KeyCode::V;
			l_Table[0x011] = KeyCode::W;
			l_Table[0x02D] = KeyCode::X;
			l_Table[0x015] = KeyCode::Y;
			l_Table[0x02C] = KeyCode::Z;

			l_Table[0x028] = KeyCode::Apostrophe;
			l_Table[0x02B] = KeyCode::Backslash;
			l_Table[0x033] = KeyCode::Comma;
			l_Table[0x00D] = KeyCode::Equal;
			l_Table[0x029] = KeyCode::GraveAccent;
			l_Table[0x01A] = KeyCode::LeftBracket;
			l_Table[0x00C] = KeyCode::Minus;
			l_Table[0x034] = KeyCode::Period;
			l_Table[0x01B] = KeyCode::RightBracket;
			l_Table[0x027] = KeyCode::Semicolon;
			l_Table[0x035] = KeyCode::Slash;
			l_Table[0x056] = KeyCode::World2;

			l_Table[0x00E] = KeyCode::Backspace;
			l_Table[0x153] = KeyCode::Delete;
			l_Table[0x14F] = KeyCode::End;
			l_Table[0x01C] = KeyCode::Enter;
			l_Table[0x001] = KeyCode::Escape;
			l_Table[0x147] = KeyCode::Home;
			l_Table[0x152] = KeyCode::Insert;
			l_Table[0x15D] = KeyCode::Menu;
			l_Table[0x151] = KeyCode::PageDown;
			l_Table[0x149] = KeyCode::PageUp;
			l_Table[0x045] = KeyCode::Pause;
			l_Table[0x039] = KeyCode::TR_SPACE;
			l_Table[0x00F] = KeyCode::Tab;
			l_Table[0x03A] = KeyCode::CapsLock;
			l_Table[0x145] = KeyCode::NumLock;
			l_Table[0x046] = KeyCode::ScrollLock;
			l_Table[0x137] = KeyCode::PrintScreen;

			l_Table[0x03B] = KeyCode::F1;
			l_Table[0x03C] = KeyCode::F2;
			l_Table[0x03D] = KeyCode::F3;
			l_Table[0x03E] = KeyCode::F4;
			l_Table[0x03F] = KeyCode::F5;
			l_Table[0x040] = KeyCode::F6;
			l_Table[0x041] = KeyCode::F7;
			l_Table[0x042] = KeyCode::F8;
			l_Table[0x043] = KeyCode::F9;
			l_Table[0x044] = KeyCode::F10;
			l_Table[0x057] = KeyCode::F11;
			l_Table[0x058] = KeyCode::F12;
			l_Table[0x064] = KeyCode::F13;
			l_Table[0x065] = KeyCode::F14;
			l_Table[0x066] = KeyCode::F15;
			l_Table[0x067] = KeyCode::F16;
			l_Table[0x068] = KeyCode::F17;
			l_Table[0x069] = KeyCode::F18;
			l_Table[0x06A] = KeyCode::F19;
			l_Table[0x06B] = KeyCode::F20;
			l_Table[0x06C] = KeyCode::F21;
			l_Table[0x06D] = KeyCode::F22;
			l_Table[0x06E] = KeyCode::F23;
			l_Table[0x076] = KeyCode::F24;

			l_Table[0x038] = KeyCode::LeftAlt;
			l_Table[0x01D] = KeyCode::LeftControl;
			l_Table[0x02A] = KeyCode::LeftShift;
			l_Table[0x15B] = KeyCode::LeftSuper;
			l_Table[0x138] = KeyCode::RightAlt;
			l_Table[0x11D] = KeyCode::RightControl;
			l_Table[0x036] = KeyCode::RightShift;
			l_Table[0x15C] = KeyCode::RightSuper;

			l_Table[0x150] = KeyCode::Down;
			l_Table[0x14B] = KeyCode::Left;
			l_Table[0x14D] = KeyCode::Right;
			l_Table[0x148] = KeyCode::Up;

			l_Table[0x052] = KeyCode::KP0;
			l_Table[0x04F] = KeyCode::KP1;
			l_Table[0x050] = KeyCode::KP2;
			l_Table[0x051] = KeyCode::KP3;
			l_Table[0x04B] = KeyCode::KP4;
			l_Table[0x04C] = KeyCode::KP5;
			l_Table[0x04D] = KeyCode::KP6;
			l_Table[0x047] = KeyCode::KP7;
			l_Table[0x048] = KeyCode::KP8;
			l_Table[0x049] = KeyCode::KP9;
			l_Table[0x04E] = KeyCode::KPAdd;
			l_Table[0x053] = KeyCode::KPDecimal;
			l_Table[0x135] = KeyCode::KPDivide;
			l_Table[0x11C] = KeyCode::KPEnter;
			l_Table[0x059] = KeyCode::KPEqual;
			l_Table[0x037] = KeyCode::KPMultiply;
			l_Table[0x04A] = KeyCode::KPSubtract;

			return l_Table;
		}

		constexpr std::array<KeyCode, 512> s_ScanCodeTable = CreateScanCodeTable();

		UINT GetScanCode(WPARAM wParam, LPARAM lParam)
		{
			UINT l_ScanCode = HIWORD(lParam) & (KF_EXTENDED | 0xFF);

			if (l_ScanCode == 0)
			{
				const UINT l_Mapped = MapVirtualKeyW(static_cast<UINT>(wParam), MAPVK_VK_TO_VSC_EX);
				if ((l_Mapped & 0xFF00) == 0xE100)
				{
					l_ScanCode = 0x045;
				}
				else
				{
					l_ScanCode = ((l_Mapped & 0xFF00) == 0xE000 ? 0x100 : 0) | (l_Mapped & 0xFF);
				}

				switch (wParam)
				{
					case VK_LEFT:
					case VK_RIGHT:
					case VK_UP:
					case VK_DOWN:
					case VK_HOME:
					case VK_END:
					case VK_PRIOR:
					case VK_NEXT:
					case VK_INSERT:
					case VK_DELETE:
					{
						l_ScanCode |= 0x100;

						break;
					}
					default:
					{
						break;
					}
				}
			}

			switch (l_ScanCode)
			{
				case 0x054:
				{
					return 0x137;
				}
				case 0x146:
				{
					return 0x045;
				}
				case 0x136:
				{
					return 0x036;
				}
				default:
				{
					return l_ScanCode;
				}
			}
		}

		KeyCode TranslateKeyLabel(WPARAM wParam, LPARAM lParam)
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
				case VK_LSHIFT:
				{
					return KeyCode::LeftShift;
				}
				case VK_RSHIFT:
				{
					return KeyCode::RightShift;
				}
				case VK_LCONTROL:
				{
					return KeyCode::LeftControl;
				}
				case VK_RCONTROL:
				{
					return KeyCode::RightControl;
				}
				case VK_LMENU:
				{
					return KeyCode::LeftAlt;
				}
				case VK_RMENU:
				{
					return KeyCode::RightAlt;
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
				case VK_CANCEL:
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
				case VK_OEM_102:
				{
					return KeyCode::World2;
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

		static void EmitKeyPressed(WindowsWindow* window, KeyCode key, KeyCode label, bool isRepeat)
		{
			window->m_KeysDown.set(static_cast<size_t>(key));
			window->Emit<KeyPressedEvent>(key, label, isRepeat);
		}

		static void EmitKeyReleased(WindowsWindow* window, KeyCode key, KeyCode label)
		{
			window->m_KeysDown.reset(static_cast<size_t>(key));
			window->Emit<KeyReleasedEvent>(key, label);
		}

		static bool IsAltGrFakeControl(WPARAM wParam, LPARAM lParam)
		{
			if (wParam != VK_CONTROL || (HIWORD(lParam) & KF_EXTENDED) != 0)
			{
				return false;
			}

			MSG l_Next{};
			if (!PeekMessageW(&l_Next, nullptr, 0, 0, PM_NOREMOVE))
			{
				return false;
			}

			const bool l_IsKeyMessage = l_Next.message == WM_KEYDOWN || l_Next.message == WM_SYSKEYDOWN || l_Next.message == WM_KEYUP || l_Next.message == WM_SYSKEYUP;

			return l_IsKeyMessage && l_Next.wParam == VK_MENU && (HIWORD(l_Next.lParam) & KF_EXTENDED) != 0 && l_Next.time == static_cast<DWORD>(GetMessageTime());
		}

		static void HandleKey(WindowsWindow* window, UINT message, WPARAM wParam, LPARAM lParam)
		{
			if (wParam == VK_PROCESSKEY || IsAltGrFakeControl(wParam, lParam))
			{
				return;
			}

			const KeyCode l_Key = s_ScanCodeTable[GetScanCode(wParam, lParam)];
			if (l_Key == KeyCode::Unknown)
			{
				return;
			}

			const KeyCode l_Label = TranslateKeyLabel(wParam, lParam);

			if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
			{
				EmitKeyPressed(window, l_Key, l_Label, (HIWORD(lParam) & KF_REPEAT) != 0);

				return;
			}

			// Windows sends only a key-up for Print Screen
			if (l_Key == KeyCode::PrintScreen)
			{
				EmitKeyPressed(window, l_Key, l_Label, false);
			}

			EmitKeyReleased(window, l_Key, l_Label);
		}

		static void ReleaseMissedKeyUps(WindowsWindow* window)
		{
			constexpr std::array<std::pair<int, KeyCode>, 4> l_Keys
			{ {
				{ VK_LSHIFT, KeyCode::LeftShift },
				{ VK_RSHIFT, KeyCode::RightShift },
				{ VK_LWIN, KeyCode::LeftSuper },
				{ VK_RWIN, KeyCode::RightSuper }
			} };

			for (const auto& [l_VirtualKey, l_Key] : l_Keys)
			{
				if (window->m_KeysDown.test(static_cast<size_t>(l_Key)) && (GetKeyState(l_VirtualKey) & 0x8000) == 0)
				{
					EmitKeyReleased(window, l_Key, l_Key);
				}
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
					const bool l_Minimized = wParam == SIZE_MINIMIZED;
					const bool l_Maximized = wParam == SIZE_MAXIMIZED || (l_Window->m_Maximized && wParam != SIZE_RESTORED);

					if (l_Minimized != l_Window->m_Minimized)
					{
						l_Window->m_Minimized = l_Minimized;

						if (l_Minimized)
						{
							l_Window->Emit<WindowMinimizeEvent>();
						}
						else
						{
							l_Window->Emit<WindowRestoreEvent>();
						}
					}

					if (l_Maximized != l_Window->m_Maximized)
					{
						l_Window->m_Maximized = l_Maximized;

						if (l_Maximized)
						{
							l_Window->Emit<WindowMaximizeEvent>();
						}
						else
						{
							l_Window->Emit<WindowUnmaximizeEvent>();
						}
					}

					if (l_Minimized)
					{
						return 0;
					}

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
					if (IsIconic(windowHandle))
					{
						return 0;
					}

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
					l_Window->m_KeysDown.reset();
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
				case WM_KEYUP:
				case WM_SYSKEYUP:
				{
					HandleKey(l_Window, message, wParam, lParam);

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
		m_Maximized = false;

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

		Procedure::ReleaseMissedKeyUps(this);

		return !m_CloseRequested;
	}
}