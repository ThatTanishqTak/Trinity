#pragma once

#include "RuntimeDisplay.hpp"

#include <Trinity.hpp>

#include <filesystem>
#include <optional>
#include <string>

// Plays a game from its pack, as its Game.trgame describes it: through its physics, its mixer and its input actions, with its C++ module linked in or loaded from beside the pack, from the first scene of its build, or the one --scene names, through the scene's primary camera, in a window as its player last left it. It never reads a project
class RuntimeLayer final : public Trinity::Layer
{
public:
    // The game, or none for one that could not be opened, which was reported, so the player closes. Its folder holds its pack, and its module beside a shared engine. The display is the game's window with its player's choice over it, as the window was opened
    RuntimeLayer(std::optional<Trinity::GameSettings> game, std::filesystem::path gameDirectory, Trinity::DisplaySettings display);

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(Trinity::Timestep timestep) override;
    void OnEvent(Trinity::Event& event) override;
    void OnPrepareRender(Trinity::RHI::CommandList& commands) override;

private:
    [[nodiscard]] bool OpenAssets();
    void LoadModule();
    void UnloadModule();
    [[nodiscard]] bool LoadStartScene(const Trinity::ApplicationCommandLineArgs& args);

    std::optional<Trinity::GameSettings> m_Game;
    std::filesystem::path m_GameDirectory;
    Trinity::DisplaySettings m_DisplaySettings;
    Trinity::Scope<RuntimeDisplay> m_Display;
    Trinity::Scope<Trinity::AssetRegistry> m_Registry;
    Trinity::SharedLibrary m_Module;
    Trinity::GameModule::UnloadFunction m_UnloadModule = nullptr;
    bool m_ModuleLinked = false;
    Trinity::AssetRef<Trinity::AudioMixerAsset> m_Mixer;
    bool m_MixerApplied = false;
    Trinity::Scope<Trinity::SceneManager> m_Scenes;
    bool m_WarnedNoCamera = false;
};