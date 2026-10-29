#include "RuntimeLayer.hpp"

#include <Trinity/Core/EntryPoint.hpp>

// The player. Its main, with the Agility SDK's exports, is in this file, so every player that links the runtime's objects has them, the prebuilt one and a game's alike
class RuntimeApplication final : public Trinity::Application
{
public:
    explicit RuntimeApplication(Trinity::ApplicationSpecification specification) : Application(std::move(specification))
    {
        PushLayer<RuntimeLayer>();
    }
};

// C# scripts run in a player once their ahead-of-time library does, so .NET is not started
Trinity::Application* Trinity::CreateApplication(ApplicationCommandLineArgs args)
{
    ApplicationSpecification l_Specification;
    l_Specification.Name = "Trinity Runtime";
    l_Specification.CommandLineArgs = args;
    l_Specification.StartDotNet = false;

    return new RuntimeApplication(std::move(l_Specification));
}