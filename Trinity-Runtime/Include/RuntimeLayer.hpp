#pragma once

#include "RuntimeDisplay.hpp"

#include <Trinity.hpp>

#include <filesystem>
#include <optional>
#include <string>

// Plays a game from its pack, as its Game.trgame describes it. It never reads a project
class RuntimeLayer final : public Trinity::Layer
{
public:
    // The game, or none for one that could not be opened, which was reported, so the player closes
    RuntimeLayer(std::optional<Trinity::GameSettings> game, std::filesystem::path gameDirectory, Trinity::DisplaySettings display);

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(Trinity::Timestep timestep) override;
    void OnEvent(Trinity::Event& event) override;
    void OnPrepareRender(Trinity::RHI::CommandList& commands) override;

private:
    [[nodiscard]] bool OpenAssets();
    void LoadModule();
    void LoadScripts();
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