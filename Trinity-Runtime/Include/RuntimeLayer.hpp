#pragma once

#include <Trinity.hpp>

#include <filesystem>
#include <string>

// Plays a project's scenes in the window, as Forge's Game panel shows them while playing: through the project's settings, its mixer, its input actions and its C++ module, from the start scene, or the one --scene names, through the scene's primary camera
class RuntimeLayer final : public Trinity::Layer
{
public:
    RuntimeLayer();

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(Trinity::Timestep timestep) override;
    void OnPrepareRender(Trinity::RHI::CommandList& commands) override;

private:
    [[nodiscard]] bool OpenProject(const std::filesystem::path& path);
    void LoadModule();
    void UnloadModule();
    [[nodiscard]] bool LoadStartScene(const Trinity::ApplicationCommandLineArgs& args);

    Trinity::Scope<Trinity::Project> m_Project;
    Trinity::Scope<Trinity::AssetRegistry> m_Registry;
    Trinity::SharedLibrary m_Module;
    Trinity::GameModule::UnloadFunction m_UnloadModule = nullptr;
    Trinity::AssetRef<Trinity::AudioMixerAsset> m_Mixer;
    bool m_MixerApplied = false;
    Trinity::Scope<Trinity::SceneManager> m_Scenes;
    bool m_WarnedNoCamera = false;
};