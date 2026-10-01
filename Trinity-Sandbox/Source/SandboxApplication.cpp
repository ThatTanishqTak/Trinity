#include "SandboxLayer.hpp"

#include <Trinity/Core/EntryPoint.hpp>

class SandboxApplication final : public Trinity::Application
{
public:
    explicit SandboxApplication(Trinity::ApplicationSpecification specification) : Application(std::move(specification))
    {
        PushLayer<SandboxLayer>();
    }
};

Trinity::Application* Trinity::CreateApplication(ApplicationCommandLineArgs args)
{
    ApplicationSpecification l_Specification;
    l_Specification.Name = "Trinity Sandbox";
    l_Specification.CommandLineArgs = args;

    return new SandboxApplication(std::move(l_Specification));
}