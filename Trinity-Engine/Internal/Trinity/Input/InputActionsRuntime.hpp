#pragma once

#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"

#include <functional>

// Main registers its loader, a SceneManager starts and ends it, and the scene runtime updates it
namespace Trinity::InputActionsRuntime
{
    void Initialize();
    void Shutdown();

    // The actions asset, or none, and player 0 alone. Handles found before are left reading nothing
    void Start(UUID asset);
    void Stop();

    // Before the scene's scripts. Player 0's UI map reads first, then the UI handler takes what it uses
    void Update(Timestep timestep);
    // The game UI's turn in each frame's update, or none. Input reads as the UI's while it runs
    void SetUIHandler(std::function<void(Timestep)> handler);
    // Around each fixed step, so OnFixedUpdate reads the presses since the step before
    void BeginFixedStep();
    void EndFixedStep();
}