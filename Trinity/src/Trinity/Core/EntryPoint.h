#pragma once

#include "Trinity/Core/Application.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
	Application* CreateApplication(ApplicationCommandLineArgs args);
}


int main(int args, char** argv)
{
	Trinity::Utilities::Log::Init();

	auto app = Trinity::CreateApplication({ args, argv });

	app->Run();

	delete app;

	return 0;
}