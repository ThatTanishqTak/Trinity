#pragma once

#include "Trinity/Renderer/Renderer.hpp"

namespace Trinity
{
	class D3D12Renderer : public Renderer
	{
	public:
		D3D12Renderer();
		~D3D12Renderer() override;

		void Shutdown() override;

		GraphicsAPI GetAPI() const override { return GraphicsAPI::DirectX12; }

		bool BeginFrame() override;
		void EndFrame() override;
		void WaitIdle() override;

	protected:
		bool Initialize(const NativeWindowHandle& window) override;
	};
}