#include <Trinity.h>

class Sandbox : public Trinity::Application
{
public:
	Sandbox()
	{

	}

	~Sandbox()
	{

	}
};

Trinity::Application* Trinity::CreateApplication()
{
	return new Sandbox();
}