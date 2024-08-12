#pragma once

#ifdef TR_PLATFORM_WINDOWS

extern Trinity::Application* Trinity::CreateApplication();

int main(int argc, char** argv)
{
	Trinity::Log::Init();

	auto app = Trinity::CreateApplication();
	app->Run();
	
	delete app;
}


#endif