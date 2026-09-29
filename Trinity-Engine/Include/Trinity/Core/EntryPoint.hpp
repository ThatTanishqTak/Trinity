#include "Trinity/Core/Application.hpp"
#include "Trinity/Core/Log.hpp"

#include <cstdlib>
#include <memory>

int main(int argc, char** argv)
{
	Trinity::Log::Initialize();

	int l_ExitCode = EXIT_FAILURE;

	{
		std::unique_ptr<Trinity::Application> l_Application(Trinity::CreateApplication({ argc, argv }));
		if (l_Application && l_Application->IsInitialized())
		{
			l_Application->Run();
			l_ExitCode = EXIT_SUCCESS;
		}
	}

	Trinity::Log::Shutdown();

	return l_ExitCode;
}