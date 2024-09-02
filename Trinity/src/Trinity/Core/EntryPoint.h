#pragma once

#ifdef TR_PLATFORM_WINDOWS

extern Trinity::Application* Trinity::CreateApplication();

int main(int argc, char** argv)
{
	Trinity::Log::Init();

	TR_PROFILE_BEGIN_SESSION("Startup", "TrinityProfile-Startup.json");
	auto app = Trinity::CreateApplication();
	TR_PROFILE_END_SESSION();

	TR_PROFILE_BEGIN_SESSION("Runtime", "TrinityProfile-Runtime.json");
	app->Run();
	TR_PROFILE_END_SESSION();
	
	TR_PROFILE_BEGIN_SESSION("Shutdown", "TrinityProfile-Shutdown.json");
	delete app;
	TR_PROFILE_END_SESSION();
}

#endif