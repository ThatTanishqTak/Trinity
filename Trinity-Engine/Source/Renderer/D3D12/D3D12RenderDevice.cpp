#include "Trinity/Renderer/D3D12/D3D12RenderDevice.hpp"

#include "Trinity/Core/Log.hpp"

namespace Trinity
{
	D3D12RenderDevice::~D3D12RenderDevice()
	{
		Shutdown();
	}

	bool D3D12RenderDevice::Initialize(const NativeWindowHandle& window)
	{
		(void)window;

		TR_CORE_ERROR("DirectX 12 renderer is not implemented yet");

		return false;
	}

	void D3D12RenderDevice::Shutdown()
	{
	}
}