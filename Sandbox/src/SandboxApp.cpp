#include <Trinity.h>

class ExampleLayer : public Trinity::Layer
{
public:
	ExampleLayer() : Layer("Example")
	{

	}

	void OnUpdate() override
	{
		if (Trinity::Input::IsKeyPressed(TR_KEY_TAB))
		{
			TR_CLIENT_TRACE("Tab key is pressed!");
		}
	}

	void OnEvent(Trinity::Event& event) override
	{
		if (event.GetEventType() == Trinity::EventType::KeyPressed)
		{
			Trinity::KeyPressedEvent& e = (Trinity::KeyPressedEvent&)event;
			TR_CLIENT_TRACE("{0}", e.GetKeyCode());
		}
	}
};

class Sandbox : public Trinity::Application
{
public:
	Sandbox()
	{
		PushLayer(new ExampleLayer);
		PushOverlay(new Trinity::ImGuiLayer());
	}

	~Sandbox()
	{

	}
};

Trinity::Application* Trinity::CreateApplication()
{
	return new Sandbox();
}