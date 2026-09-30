#pragma once

#include "Trinity/Renderer/Renderer.hpp"

#include <chrono>

namespace Trinity
{
	class NullRenderer : public Renderer
	{
	public:
		NullRenderer();
		~NullRenderer() override;

		void Shutdown() override;

		GraphicsAPI GetAPI() const override { return GraphicsAPI::Null; }

		bool BeginFrame() override;
		void EndFrame() override;
		void WaitIdle() override;

	protected:
		bool Initialize(const NativeWindowHandle& window) override;

	private:
		std::chrono::steady_clock::time_point m_NextFrameTime{};
	};
}