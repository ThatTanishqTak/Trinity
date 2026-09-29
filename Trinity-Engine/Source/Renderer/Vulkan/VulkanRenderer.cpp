#include "Trinity/Renderer/Vulkan/VulkanRenderer.hpp"

#include "Trinity/Core/Log.hpp"

namespace Trinity
{
	VulkanRenderer::VulkanRenderer() = default;
	VulkanRenderer::~VulkanRenderer() = default;

	bool VulkanRenderer::Initialize(const NativeWindowHandle& window)
	{
		(void)window;

		TR_CORE_ERROR("Vulkan renderer is not implemented yet");

		return false;
	}

	void VulkanRenderer::Shutdown()
	{

	}

	bool VulkanRenderer::BeginFrame()
	{
		return false;
	}

	void VulkanRenderer::EndFrame()
	{

	}

	void VulkanRenderer::WaitIdle()
	{

	}
}