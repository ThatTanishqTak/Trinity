#include "Forge/ForgeLayer.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Events/KeyEvent.hpp"

namespace Forge
{
	ForgeLayer::ForgeLayer() : Trinity::Layer("ForgeLayer")
	{

	}

	void ForgeLayer::OnAttach()
	{
		TR_INFO("{} attached", GetName());
	}

	void ForgeLayer::OnDetach()
	{
		TR_INFO("{} detached", GetName());
	}

	void ForgeLayer::OnUpdate(Trinity::Timestep deltaTime)
	{
		(void)deltaTime;
	}

	void ForgeLayer::OnFixedUpdate(Trinity::Timestep fixedDeltaTime)
	{
		(void)fixedDeltaTime;
	}

	void ForgeLayer::OnEvent(Trinity::Event& event)
	{
		Trinity::EventDispatcher l_Dispatcher(event);
		l_Dispatcher.Dispatch<Trinity::KeyPressedEvent>([](Trinity::KeyPressedEvent& keyEvent)
		{
			TR_TRACE("{}", keyEvent.ToString());

			return false;
		});
	}
}