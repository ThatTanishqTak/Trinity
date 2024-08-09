#pragma once

#ifdef TR_PLATFORM_WINDOWS

extern Trinity::Application* Trinity::CreateApplication();

int main(int argc, char** argv)
{
	Trinity::Log::Init();
	TR_CORE_WARN("Initialized Trinity Log");
	TR_CLIENT_INFO("Hello From App");

	auto app = Trinity::CreateApplication();
	app->Run();
	
	delete app;
}

#endif