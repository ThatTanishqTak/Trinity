#pragma once

#include "Trinity/Renderer/GraphicsAPI.hpp"
#include "Trinity/Window/NativeWindowHandle.hpp"

#include <cstdint>
#include <memory>

namespace Trinity
{
	class Renderer
	{
	public:
		Renderer() = default;
		virtual ~Renderer() = default;

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;
		Renderer(Renderer&&) = delete;
		Renderer& operator=(Renderer&&) = delete;

		static std::unique_ptr<Renderer> Create(GraphicsAPI api, const NativeWindowHandle& window);

		virtual void Shutdown() = 0;

		virtual GraphicsAPI GetAPI() const = 0;

		virtual bool BeginFrame() = 0;
		virtual void EndFrame() = 0;
		virtual void WaitIdle() = 0;

		void RequestResize(uint32_t width, uint32_t height);

	protected:
		virtual bool Initialize(const NativeWindowHandle& window) = 0;

		bool ConsumeResizeRequest(uint32_t& width, uint32_t& height);

	private:
		uint32_t m_RequestedWidth = 0;
		uint32_t m_RequestedHeight = 0;
		bool m_ResizeRequested = false;
	};
}