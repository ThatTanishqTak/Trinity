#include <Trinity.h>

#include "ImGui/imgui.h"

class DemoLayer : public Trinity::Layer
{
public:
	DemoLayer() : Layer("DemoLayer")
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
		bool show = true;
		//ImGui::ShowDemoWindow(&show);
	}
};

class Sandbox : public Trinity::Application
{
public:
	Sandbox()
	{
		PushLayer(new DemoLayer);
	}

	~Sandbox()
	{

	}
};

Trinity::Application* Trinity::CreateApplication()
{
	return new Sandbox();
}