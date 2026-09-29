#pragma once

#include "Trinity/Renderer/RenderDevice.hpp"

namespace Trinity
{
	class VulkanRenderDevice : public RenderDevice
	{
	public:
		VulkanRenderDevice() = default;
		~VulkanRenderDevice() override;

		void Shutdown() override;

		GraphicsAPI GetAPI() const override { return GraphicsAPI::Vulkan; }

	protected:
		bool Initialize(const NativeWindowHandle& window) override;
	};
}