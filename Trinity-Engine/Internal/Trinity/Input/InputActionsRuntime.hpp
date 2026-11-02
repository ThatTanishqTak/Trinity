#pragma once

#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"

#include <functional>

// What drives InputActions: Main registers its loader, a SceneManager starts and ends it, and the scene runtime evaluates it each frame and marks each fixed step
namespace Trinity::InputActionsRuntime
{
    void Initialize();
    void Shutdown();

    // The actions asset, or none, and player 0 alone, as a SceneManager begins. Handles found before are left reading nothing
    void Start(UUID asset);
    void Stop();

    // Each frame of the running scene, before its scripts: every player's enabled maps, from Input as it is now, with their interactions moved on by the frame's time. Player 0's UI map goes first, from Input as the UI reads it, then the UI handler has its turn and takes what it uses, and the rest read what it left. The handler has its turn even before the asset is ready, but not while a rebind listens
    void Update(Timestep timestep);
    // The game UI's turn in each frame's update, or none. Input reads as the UI's while it runs
    void SetUIHandler(std::function<void(Timestep)> handler);
    // Around each fixed step, so OnFixedUpdate reads the presses since the step before, then those are cleared
    void BeginFixedStep();
    void EndFixedStep();
}