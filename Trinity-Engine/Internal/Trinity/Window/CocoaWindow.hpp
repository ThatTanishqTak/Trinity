#pragma once

#include "Trinity/Window/Window.hpp"

#include <string>

namespace Trinity
{
	class CocoaWindow : public Window
	{
	public:
		CocoaWindow();
		~CocoaWindow() override;

		bool Initialize(const WindowSpecification& specification) override;
		void Shutdown() override;

		bool PollEvents() override;

		uint32_t GetWidth() const override { return m_Width; }
		uint32_t GetHeight() const override { return m_Height; }
		bool IsMinimized() const override { return false; }
		bool IsMaximized() const override { return false; }

		std::string_view GetTitle() const override { return m_Title; }

		float GetContentScale() const override { return 1.0f; }

		NativeWindowHandle GetNativeHandle() const override { return { NativeWindowHandle::Platform::Cocoa, nullptr, nullptr, 0 }; }

	private:
		std::string m_Title;

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};
}