#include <Trinity.h>
#include <Trinity/Core/EntryPoint.h>

#include "Sandbox2D.h"
#include "ExampleLayer.h"

class Sandbox : public Trinity::Application
{
public:
	Sandbox(const Trinity::ApplicationSpecification& specification) : Trinity::Application(specification)
	{
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
	spec.WorkingDirectory = "../Sandbox";
	spec.CommandLineArgs = args;

	return new Sandbox(spec);
}