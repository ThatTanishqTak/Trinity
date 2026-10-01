#include "ForgeLayer.hpp"

#include <Trinity/Core/EntryPoint.hpp>

class ForgeApplication final : public Trinity::Application
{
public:
    explicit ForgeApplication(Trinity::ApplicationSpecification specification) : Application(std::move(specification))
    {
        PushLayer<ForgeLayer>();
    }
};

Trinity::Application* Trinity::CreateApplication(ApplicationCommandLineArgs args)
{
    ApplicationSpecification l_Specification;
    l_Specification.Name = "Trinity Forge";
    l_Specification.CommandLineArgs = args;

    return new ForgeApplication(std::move(l_Specification));
}