#pragma once

#include "Trinity/Renderer/Renderer.hpp"

namespace Trinity
{
	class VulkanRenderer : public Renderer
	{
	public:
		VulkanRenderer();
		~VulkanRenderer() override;

		void Shutdown() override;

		GraphicsAPI GetAPI() const override { return GraphicsAPI::Vulkan; }

		bool BeginFrame() override;
		void EndFrame() override;
		void WaitIdle() override;

	protected:
		bool Initialize(const NativeWindowHandle& window) override;
	};
}