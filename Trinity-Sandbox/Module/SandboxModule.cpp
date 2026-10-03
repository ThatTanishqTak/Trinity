// Loaded and unloaded by Sandbox: declares a console variable and allocates under Game, and both must be gone once it unloads
#include <Trinity.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace
{
    struct ModuleState
    {
        std::array<std::byte, 4096> Data{};
        std::vector<std::uint32_t> Values;
    };

    Trinity::ConsoleVariable<std::int32_t> s_ModuleValue("sandbox.module_value", 42, "Declared by the Sandbox test module, and gone again once it unloads");

    ModuleState* s_State = nullptr;
}

TRINITY_MODULE_EXPORT void SandboxModuleAttach()
{
    s_State = Trinity::Memory::New<ModuleState>(Trinity::MemoryTag::Game);
    s_State->Values.resize(1024);
}

// Fills a string owned by Sandbox, so memory allocated here is freed by the executable
TRINITY_MODULE_EXPORT void SandboxModuleDescribe(std::string& description)
{
    description = std::format("Sandbox module: sandbox.module_value = {}, {} values held under Game", s_ModuleValue.Get(), s_State != nullptr ? s_State->Values.size() : 0);
}

TRINITY_MODULE_EXPORT void SandboxModuleDetach()
{
    Trinity::Memory::Delete(s_State);
    s_State = nullptr;
}