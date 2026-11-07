#pragma once

#include "HubSettings.hpp"
#include "Pages/HubPage.hpp"

#include <Trinity.hpp>

#include <cstddef>
#include <filesystem>
#include <vector>

// Hub's one window, as Unity Hub's is: a sidebar of its pages, Projects, Installs and Settings, beside the chosen page, filling the window with no docking and no window of its own outside it
class HubLayer final : public Trinity::Layer
{
public:
    explicit HubLayer(std::filesystem::path directory);

    void OnAttach() override;
    void OnImGuiRender() override;

private:
    void DrawSidebar();

    std::filesystem::path m_Directory;
    HubSettings m_Settings;
    std::vector<Trinity::Scope<HubPage>> m_Pages;
    std::size_t m_Page = 0;
};