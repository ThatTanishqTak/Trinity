#pragma once

#include "Trinity/Core/Application.h"

namespace Trinity
{
	Application* CreateApplication(ApplicationCommandLineArgs args);
}


int main(int args, char** argv)
{
	auto app = Trinity::CreateApplication({ args, argv });

	app->Run();

	delete app;

	return 0;
}