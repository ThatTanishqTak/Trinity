#include <Trinity.h>
#include <Trinity/Core/EntryPoint.h>

#include "Sandbox2D.h"
#include "EditorLayer.h"

class Sandbox : public Trinity::Application
{
public:
	Sandbox(const Trinity::ApplicationSpecification& specification) : Trinity::Application(specification)
	{
		//PushLayer(new DemoLayer());
		PushLayer(new Sandbox2D());
	}

	~Sandbox()
	{

	}
};

Trinity::Application* Trinity::CreateApplication(ApplicationCommandLineArgs args)
{
	ApplicationSpecification spec;
	spec.Name = "Sandbox";
	spec.WorkingDirectory = "../Hazelnut";
	spec.CommandLineArgs = args;

	return new Sandbox(spec);
}