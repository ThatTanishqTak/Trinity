#pragma once

#include "Trinity/Core/Core.h"
#include "Trinity/Core/Application.h"

#include <Windows.h>

#ifdef TR_PLATFORM_WINDOWS

extern Trinity::Application* Trinity::CreateApplication(ApplicationCommandLineArgs args);

int main(int argc, char** argv)
{
	Trinity::Log::Init();

	auto app = Trinity::CreateApplication({ argc, argv });

	app->Run();
	
	delete app;
}

#endif