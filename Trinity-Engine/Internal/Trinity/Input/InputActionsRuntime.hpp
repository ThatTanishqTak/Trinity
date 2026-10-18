#pragma once

#include "Trinity/Core/UUID.hpp"

// What drives InputActions: Main registers its loader, a SceneManager starts and ends it, and the scene runtime evaluates it each frame and marks each fixed step
namespace Trinity::InputActionsRuntime
{
    void Initialize();
    void Shutdown();

    // The actions asset, or none, and player 0 alone, as a SceneManager begins. Handles found before are left reading nothing
    void Start(UUID asset);
    void Stop();

    // Each frame of the running scene, before its scripts: every player's enabled maps, from Input as it is now
    void Update();
    // Around each fixed step, so OnFixedUpdate reads the presses since the step before, then those are cleared
    void BeginFixedStep();
    void EndFixedStep();
}