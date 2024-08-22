#include <Trinity.h>

#include "ImGui/imgui.h"

class MenuLayer : public Trinity::Layer
{
public:
	MenuLayer() : Layer("MenuLayer")
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

	}
};

class Sandbox : public Trinity::Application
{
public:
	Sandbox()
	{
		PushLayer(new MenuLayer);
	}

	~Sandbox()
	{

	}
};

Trinity::Application* Trinity::CreateApplication()
{
	return new Sandbox();
}