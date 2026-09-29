#pragma once

#include "Trinity/Renderer/RenderDevice.hpp"

namespace Trinity
{
	class D3D12RenderDevice : public RenderDevice
	{
	public:
		D3D12RenderDevice() = default;
		~D3D12RenderDevice() override;

		void Shutdown() override;

		GraphicsAPI GetAPI() const override { return GraphicsAPI::DirectX12; }

	protected:
		bool Initialize(const NativeWindowHandle& window) override;
	};
}