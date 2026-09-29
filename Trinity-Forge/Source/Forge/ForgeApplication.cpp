#include "Trinity/Core/EntryPoint.hpp"

#include "Forge/ForgeLayer.hpp"

#include <memory>

namespace Forge
{
	class ForgeApplication : public Trinity::Application
	{
	public:
		ForgeApplication(const Trinity::ApplicationSpecification& specification) : Trinity::Application(specification)
		{

		}

	protected:
		void OnInitialize() override
		{
			PushLayer(std::make_unique<ForgeLayer>());
		}
	};
}

Trinity::Application* Trinity::CreateApplication(Trinity::ApplicationCommandLineArgs args)
{
	const Trinity::ApplicationSpecification l_ApplicationSpecification
	{
		.Title = "Forge",
		.Width = 1080,
		.Height = 720,
		.Args = args
	};

	return new Forge::ForgeApplication(l_ApplicationSpecification);
}