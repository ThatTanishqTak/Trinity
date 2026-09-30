#pragma once

#include "Trinity/Renderer/GraphicsAPI.hpp"
#include "Trinity/Window/NativeWindowHandle.hpp"

#include <cstdint>
#include <memory>

namespace Trinity
{
	struct RenderDeviceSpecification
	{
		bool VSync = true;
		bool Validation = false;
	};

	class Renderer
	{
	public:
		Renderer() = default;
		virtual ~Renderer() = default;

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;
		Renderer(Renderer&&) = delete;
		Renderer& operator=(Renderer&&) = delete;

		static std::unique_ptr<Renderer> Create(GraphicsAPI api, const NativeWindowHandle& window, const RenderDeviceSpecification& specification);

		virtual void Shutdown() = 0;

		virtual GraphicsAPI GetAPI() const = 0;

		virtual bool BeginFrame() = 0;
		virtual void EndFrame() = 0;
		virtual void WaitIdle() = 0;

		void RequestResize(uint32_t width, uint32_t height);

		void SetVSync(bool enabled);
		bool IsVSync() const { return m_VSync; }
		bool IsValidationEnabled() const { return m_ValidationEnabled; }

	protected:
		virtual bool Initialize(const NativeWindowHandle& window) = 0;

		bool ConsumeResizeRequest(uint32_t& width, uint32_t& height);
		bool ConsumeVSyncChange();

	private:
		uint32_t m_RequestedWidth = 0;
		uint32_t m_RequestedHeight = 0;
		bool m_ResizeRequested = false;

		bool m_VSync = true;
		bool m_VSyncChanged = false;
		bool m_ValidationEnabled = false;
	};
}