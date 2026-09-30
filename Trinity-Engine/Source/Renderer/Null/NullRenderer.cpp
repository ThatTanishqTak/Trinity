#include "Trinity/Renderer/Null/NullRenderer.hpp"

#include <cstdint>
#include <thread>

namespace Trinity
{
	namespace
	{
		constexpr std::chrono::nanoseconds s_VSyncFrameTime{ 16666667 };
	}

	NullRenderer::NullRenderer() = default;
	NullRenderer::~NullRenderer() = default;

	bool NullRenderer::Initialize(const NativeWindowHandle& window)
	{
		(void)window;

		return true;
	}

	void NullRenderer::Shutdown()
	{

	}

	bool NullRenderer::BeginFrame()
	{
		uint32_t l_Width = 0;
		uint32_t l_Height = 0;
		ConsumeResizeRequest(l_Width, l_Height);
		ConsumeVSyncChange();

		return true;
	}

	void NullRenderer::EndFrame()
	{
		if (!IsVSync())
		{
			return;
		}

		m_NextFrameTime += s_VSyncFrameTime;

		const std::chrono::steady_clock::time_point l_Now = std::chrono::steady_clock::now();
		if (m_NextFrameTime < l_Now)
		{
			m_NextFrameTime = l_Now;
		}
		else
		{
			std::this_thread::sleep_until(m_NextFrameTime);
		}
	}

	void NullRenderer::WaitIdle()
	{

	}
}