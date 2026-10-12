#pragma once

// What a game module exports for Forge to call, each optional: TrinityModuleLoad once Forge has loaded the module, which it does after each good build, and TrinityModuleUnload before it unloads it, which it does before each build and as the project closes. A module's symbols are hidden but those it marks with TR_GAME_MODULE_EXPORT
#if defined(_WIN32)
#define TR_GAME_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define TR_GAME_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace Trinity::GameModule
{
    using LoadFunction = void (*)();
    using UnloadFunction = void (*)();

    inline constexpr const char* c_LoadFunction = "TrinityModuleLoad";
    inline constexpr const char* c_UnloadFunction = "TrinityModuleUnload";
}