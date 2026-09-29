#pragma once

#include "Trinity/Renderer/GraphicsAPI.hpp"
#include "Trinity/Window/NativeWindowHandle.hpp"

#include <memory>

namespace Trinity
{
	class RenderDevice
	{
	public:
		RenderDevice() = default;
		virtual ~RenderDevice() = default;

		RenderDevice(const RenderDevice&) = delete;
		RenderDevice& operator=(const RenderDevice&) = delete;
		RenderDevice(RenderDevice&&) = delete;
		RenderDevice& operator=(RenderDevice&&) = delete;

		static std::unique_ptr<RenderDevice> Create(GraphicsAPI api, const NativeWindowHandle& window);

		virtual void Shutdown() = 0;

		virtual GraphicsAPI GetAPI() const = 0;

	protected:
		virtual bool Initialize(const NativeWindowHandle& window) = 0;
	};
}