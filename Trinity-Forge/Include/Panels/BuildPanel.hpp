#pragma once

#include "EditorSession.hpp"
#include "Export/ExportCommand.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <optional>
#include <string>
#include <vector>

// Unity's Build Profiles window. Each edit is a command, saved with the project as it is made
class BuildPanel final : public Panel
{
public:
    BuildPanel(EditorSession& session, ExportCommand& exporter);

    // Checks the settings, then starts the export. False when stopped, so the caller can show the panel
    bool Build(bool run);

protected:
    void OnImGuiRender() override;

private:
    // What a field shows while it is typed into, which the settings take once it is left
    struct TextBuffer
    {
        std::string Text;
        bool Editing = false;
    };

    void DrawPlatforms(const Trinity::ExportSettings& settings);
    void DrawScenes(const Trinity::ExportSettings& settings);
    void DrawPlayer(const Trinity::ExportSettings& settings);
    void DrawIcon(const Trinity::ExportSettings& settings);
    void DrawWindow(const Trinity::ExportSettings& settings);
    void DrawGraphicsAPIs(const Trinity::ExportSettings& settings);
    void DrawBuild(const Trinity::ExportSettings& settings);

    // The settings as the project has them now, changed as given, as one command
    template<typename F>
    void Edit(std::string field, F&& change);
    // True once a field that was typed into is left with text other than the value
    [[nodiscard]] static bool TextField(const char* id, TextBuffer& buffer, const std::string& value);

    EditorSession& m_Session;
    ExportCommand& m_Export;
    TextBuffer m_Product;
    TextBuffer m_Company;
    TextBuffer m_Version;
    TextBuffer m_Output;
    std::string m_IconFilter;
    // As this version's manifest lists them, read once the panel is first drawn
    std::optional<std::vector<std::string>> m_Platforms;
    // Whether the last Build asked for was stopped, so what stops it shows until nothing does
    bool m_Refused = false;
};