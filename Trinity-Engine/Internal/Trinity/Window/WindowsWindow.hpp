#pragma once

#include "Trinity/Input/KeyCodes.hpp"
#include "Trinity/Window/Window.hpp"

#include <bitset>
#include <string>

struct HWND__;
struct HINSTANCE__;

namespace Trinity
{
	class WindowsWindow final : public Window
	{
	public:
		WindowsWindow();
		~WindowsWindow() override;

		bool Initialize(const WindowSpecification& specification) override;
		void Shutdown() override;

		bool PollEvents() override;

		uint32_t GetWidth() const override { return m_Width; }
		uint32_t GetHeight() const override { return m_Height; }
		bool IsMinimized() const override { return m_Minimized; }
		bool IsMaximized() const override { return m_Maximized; }

		float GetContentScale() const override { return m_ContentScale; }

		std::string_view GetTitle() const override { return m_Title; }

		NativeWindowHandle GetNativeHandle() const override { return { NativeWindowHandle::Platform::Win32, m_Instance, m_WindowHandle, 0 }; }

	private:
		struct Procedure;

		std::string m_Title;

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		float m_ContentScale = 1.0f;

		bool m_CloseRequested = false;
		bool m_Minimized = false;
		bool m_Maximized = false;
		bool m_InModalLoop = false;

		uint16_t m_HighSurrogate = 0;

		std::bitset<KeyCodeCount> m_KeysDown;

		HWND__* m_WindowHandle = nullptr;
		HINSTANCE__* m_Instance = nullptr;
	};
}