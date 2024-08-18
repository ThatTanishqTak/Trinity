#include <Trinity.h>

#include "ImGui/imgui.h"

class ExampleLayer : public Trinity::Layer
{
public:
	ExampleLayer() : Layer("Example")
	{

	}

	void OnUpdate() override
	{
		//if (Trinity::Input::IsKeyPressed(TR_KEY_TAB))
		//{
		//	TR_CLIENT_TRACE("Tab key is pressed!");
		//}
	}

	void OnEvent(Trinity::Event& event) override
	{
		//if (event.GetEventType() == Trinity::EventType::KeyPressed)
		//{
		//	Trinity::KeyPressedEvent& e = (Trinity::KeyPressedEvent&)event;
		//	TR_CLIENT_TRACE("{0}", event);
		//}
	}

	virtual void OnImGuiRender() override
	{
		ImGui::Begin("Test");
		ImGui::Text("Hello World");
		ImGui::End();
	}
};

class Sandbox : public Trinity::Application
{
public:
	Sandbox()
	{
		PushLayer(new ExampleLayer);
	}

	~Sandbox()
	{

	}
};

Trinity::Application* Trinity::CreateApplication()
{
	return new Sandbox();
}