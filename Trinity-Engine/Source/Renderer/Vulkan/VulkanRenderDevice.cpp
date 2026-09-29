#include "Trinity/Renderer/Vulkan/VulkanRenderDevice.hpp"

#include "Trinity/Core/Log.hpp"

namespace Trinity
{
	VulkanRenderDevice::VulkanRenderDevice() = default;
	VulkanRenderDevice::~VulkanRenderDevice() = default;

	bool VulkanRenderDevice::Initialize(const NativeWindowHandle& window)
	{
		(void)window;

		TR_CORE_ERROR("Vulkan renderer is not implemented yet");

		return false;
	}

	void VulkanRenderDevice::Shutdown()
	{

	}
}