#include "Trinity/Renderer/Renderer.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Renderer/Vulkan/VulkanRenderer.hpp"

#if defined(_WIN32)
#include "Trinity/Renderer/D3D12/D3D12Renderer.hpp"
#endif

namespace Trinity
{
	std::unique_ptr<Renderer> Renderer::Create(GraphicsAPI api, const NativeWindowHandle& window, bool vsync)
	{
		if (!IsGraphicsAPISupported(api))
		{
			TR_CORE_ERROR("{} is not supported on this platform", GraphicsAPIToString(api));

			return nullptr;
		}

		std::unique_ptr<Renderer> l_Device;

		switch (api)
		{
			case GraphicsAPI::Vulkan:
			{
				l_Device = std::make_unique<VulkanRenderer>();

				break;
			}
			case GraphicsAPI::DirectX12:
			{
#if defined(_WIN32)
				l_Device = std::make_unique<D3D12Renderer>();
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

		l_Device->m_VSync = vsync;

		if (!l_Device->Initialize(window))
		{
			l_Device->Shutdown();

			return nullptr;
		}

		return l_Device;
	}

	void Renderer::RequestResize(uint32_t width, uint32_t height)
	{
		m_RequestedWidth = width;
		m_RequestedHeight = height;
		m_ResizeRequested = true;
	}

	void Renderer::SetVSync(bool enabled)
	{
		if (enabled != m_VSync)
		{
			m_VSync = enabled;
			m_VSyncChanged = true;
		}
	}

	bool Renderer::ConsumeVSyncChange()
	{
		const bool l_Changed = m_VSyncChanged;
		m_VSyncChanged = false;

		return l_Changed;
	}

	bool Renderer::ConsumeResizeRequest(uint32_t& width, uint32_t& height)
	{
		if (!m_ResizeRequested)
		{
			return false;
		}

		width = m_RequestedWidth;
		height = m_RequestedHeight;
		m_ResizeRequested = false;

		return true;
	}
}