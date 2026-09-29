#pragma once

#include "Trinity/Events/Event.hpp"
#include "Trinity/Window/NativeWindowHandle.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace Trinity
{
	struct WindowSpecification
	{
		std::string Title = "Trinity-Window";
		uint32_t Width = 1080;
		uint32_t Height = 720;
		bool VSync = false;
	};

	class Window : public EventSource
	{
	public:
		using FrameCallbackFunction = std::function<void()>;

		Window() = default;
		virtual ~Window() = default;

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		static std::unique_ptr<Window> Create(const WindowSpecification& specification);

		virtual bool Initialize(const WindowSpecification& specification) = 0;
		virtual void Shutdown() = 0;

		virtual bool PollEvents() = 0;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual bool IsMinimized() const = 0;
		virtual float GetContentScale() const = 0;
		virtual std::string_view GetTitle() const = 0;
		virtual NativeWindowHandle GetNativeHandle() const = 0;

		void SetFrameCallback(FrameCallbackFunction callback) { m_FrameCallback = std::move(callback); }
		void SetRendererAttached(bool attached) { m_RendererAttached = attached; }

	protected:
		FrameCallbackFunction m_FrameCallback;
		bool m_RendererAttached = false;
	};
}