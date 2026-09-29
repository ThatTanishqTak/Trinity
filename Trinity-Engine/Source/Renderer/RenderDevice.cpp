#include "Trinity/Renderer/RenderDevice.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Renderer/Vulkan/VulkanRenderDevice.hpp"

#if defined(_WIN32)
#include "Trinity/Renderer/D3D12/D3D12RenderDevice.hpp"
#endif

namespace Trinity
{
	std::unique_ptr<RenderDevice> RenderDevice::Create(GraphicsAPI api, const NativeWindowHandle& window)
	{
		if (!IsGraphicsAPISupported(api))
		{
			TR_CORE_ERROR("{} is not supported on this platform", GraphicsAPIToString(api));

			return nullptr;
		}

		std::unique_ptr<RenderDevice> l_Device;

		switch (api)
		{
			case GraphicsAPI::Vulkan:
			{
				l_Device = std::make_unique<VulkanRenderDevice>();

				break;
			}
			case GraphicsAPI::DirectX12:
			{
#if defined(_WIN32)
				l_Device = std::make_unique<D3D12RenderDevice>();
#endif

				break;
			}
			case GraphicsAPI::Metal:
			{
				TR_CORE_ERROR("Metal renderer is not implemented yet");

				break;
			}
		}

		if (!l_Device)
		{
			return nullptr;
		}

		TR_CORE_INFO("Initializing {} renderer", GraphicsAPIToString(api));

		if (!l_Device->Initialize(window))
		{
			l_Device->Shutdown();

			return nullptr;
		}

		return l_Device;
	}
}