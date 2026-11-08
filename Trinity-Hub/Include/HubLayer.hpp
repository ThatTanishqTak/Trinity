#pragma once

#include "HubInstalls.hpp"
#include "HubLauncher.hpp"
#include "HubPathPrompt.hpp"
#include "HubProjects.hpp"
#include "HubReleases.hpp"
#include "HubSettings.hpp"
#include "Pages/HubPage.hpp"

#include <Trinity.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// Hub's one window, as Unity Hub's is: a sidebar of its pages, Projects, Installs and Settings, beside the chosen page, filling the window with no docking and no window of its own outside it. New Project, opened from the Projects page, takes that page's place until it is left
class HubLayer final : public Trinity::Layer
{
public:
    // directory is the folder Hub keeps its lists in, installsDirectory where it installs versions unless its settings name another, and releasesUrl where it asks for Trinity's releases. New Project makes projects in Trinity Projects in the person's Documents unless the settings name another folder
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
    // After what they are given, so they go first
    std::vector<Trinity::Scope<HubPage>> m_Pages;
    Trinity::Scope<HubPage> m_NewProject;
    std::size_t m_Page = 0;
    bool m_NewProjectShown = false;
};