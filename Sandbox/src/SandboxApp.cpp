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

	}

	void OnEvent(Trinity::Event& event) override
	{

	}

	virtual void OnImGuiRender() override
	{
		static bool show = true;
		ImGui::ShowDemoWindow(&show);
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