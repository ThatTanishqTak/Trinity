#include <Trinity.h>

class ExampleLayer : public Trinity::Layer
{
public:
	ExampleLayer() : Layer("Example")
	{

	}

	void OnUpdate() override
	{
		TR_CLIENT_INFO("Example::Update");
	}

	void OnEvent(Trinity::Event& event) override
	{
		TR_CLIENT_TRACE("{0}", event);
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