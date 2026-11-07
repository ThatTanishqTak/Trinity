#pragma once

#include "HubInstalls.hpp"
#include "HubPathPrompt.hpp"
#include "HubReleases.hpp"
#include "HubSettings.hpp"
#include "Pages/HubPage.hpp"

#include <Trinity.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// Hub's one window, as Unity Hub's is: a sidebar of its pages, Projects, Installs and Settings, beside the chosen page, filling the window with no docking and no window of its own outside it
class HubLayer final : public Trinity::Layer
{
public:
    // directory is the folder Hub keeps its lists in, installsDirectory where it installs versions unless its settings name another, and releasesUrl where it asks for Trinity's releases
    HubLayer(std::filesystem::path directory, std::filesystem::path installsDirectory, std::string releasesUrl);

    void OnAttach() override;
    void OnUpdate(Trinity::Timestep timestep) override;
    void OnImGuiRender() override;

private:
    void DrawSidebar();

    std::filesystem::path m_Directory;
    std::filesystem::path m_DefaultInstallsDirectory;
    HubSettings m_Settings;
    HubInstalls m_Installs;
    HubReleases m_Releases;
    HubPathPrompt m_PathPrompt;
    // After what they are given, so they go first
    std::vector<Trinity::Scope<HubPage>> m_Pages;
    std::size_t m_Page = 0;
};