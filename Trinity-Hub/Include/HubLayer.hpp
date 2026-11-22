#pragma once

#include "HubInstalls.hpp"
#include "HubLauncher.hpp"
#include "HubPathPrompt.hpp"
#include "HubProjects.hpp"
#include "HubReleases.hpp"
#include "HubRequirements.hpp"
#include "HubSettings.hpp"
#include "Pages/HubPage.hpp"

#include <Trinity.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// Hub's one window, as Unity Hub's: a sidebar of its pages beside the chosen page, with no docking
class HubLayer final : public Trinity::Layer
{
public:
    // directory holds Hub's lists, installsDirectory its versions, releasesUrl is where it asks
    HubLayer(std::filesystem::path directory, std::filesystem::path installsDirectory, std::string releasesUrl);

    void OnAttach() override;
    void OnUpdate(Trinity::Timestep timestep) override;
    void OnImGuiRender() override;

private:
    void DrawSidebar();
    void ShowPage(std::size_t page);

    std::filesystem::path m_Directory;
    std::filesystem::path m_DefaultInstallsDirectory;
    std::filesystem::path m_DefaultProjectsDirectory;
    HubSettings m_Settings;
    HubInstalls m_Installs;
    HubReleases m_Releases;
    HubProjects m_Projects;
    HubPathPrompt m_PathPrompt;
    // Each Forge Hub starts, from the Projects page or New Project, kept hold of until it ends
    HubLauncher m_Launcher;
    // What this machine has for each version, for the Installs page and New Project's languages
    HubRequirements m_Requirements;
    // After what they are given, so they go first
    std::vector<Trinity::Scope<HubPage>> m_Pages;
    Trinity::Scope<HubPage> m_NewProject;
    std::size_t m_Page = 0;
    bool m_NewProjectShown = false;
};