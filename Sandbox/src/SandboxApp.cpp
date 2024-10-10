#include <Trinity.h>
#include <Trinity/Core/EntryPoint.h>

#include "Platform/OpenGL/OpenGLShader.h"
#include "ImGui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Sandbox2D.h"

class Sandbox : public Trinity::Application
{
public:
	Sandbox() : Trinity::Application("Forge")
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
	return new Sandbox();
}