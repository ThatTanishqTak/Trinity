#pragma once

#include "Trinity/UI/Panel.h"

#include <filesystem>

class ContentBrowserPanel : public Trinity::Panel
{
public:
    ContentBrowserPanel() = default;
    explicit ContentBrowserPanel(const std::filesystem::path& baseDirectory);

    void OnUIRender() override;

private:
    void DrawDirectory(const std::filesystem::path& path);

    std::filesystem::path m_BaseDirectory;
    std::filesystem::path m_CurrentDirectory;
    float m_ThumbnailSize = 64.0f;
    float m_Padding = 16.0f;
};