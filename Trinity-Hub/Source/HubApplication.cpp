#include "HubLayer.hpp"

#include <Trinity/Core/EntryPoint.hpp>

class HubApplication final : public Trinity::Application
{
public:
    explicit HubApplication(Trinity::ApplicationSpecification specification) : Application(std::move(specification))
    {
        PushLayer<HubLayer>();
    }
};

Trinity::Application* Trinity::CreateApplication(ApplicationCommandLineArgs args)
{
    ApplicationSpecification l_Specification;
    l_Specification.Name = "Trinity Hub";
    l_Specification.Window.Width = 1100;
    l_Specification.Window.Height = 700;
    l_Specification.CommandLineArgs = args;

    return new HubApplication(std::move(l_Specification));
}