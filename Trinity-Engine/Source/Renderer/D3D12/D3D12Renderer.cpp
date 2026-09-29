#include "Trinity/Renderer/D3D12/D3D12Renderer.hpp"

#include "Trinity/Core/Log.hpp"

namespace Trinity
{
	D3D12Renderer::D3D12Renderer() = default;
	D3D12Renderer::~D3D12Renderer() = default;

	bool D3D12Renderer::Initialize(const NativeWindowHandle& window)
	{
		(void)window;

		TR_CORE_ERROR("DirectX 12 renderer is not implemented yet");

		return false;
	}

	void D3D12Renderer::Shutdown()
	{
	}

	bool D3D12Renderer::BeginFrame()
	{
		return false;
	}

	void D3D12Renderer::EndFrame()
	{
	}

	void D3D12Renderer::WaitIdle()
	{
	}
}