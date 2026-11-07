#pragma once

#include "EditorSession.hpp"
#include "Export/ExportCommand.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <optional>
#include <string>
#include <vector>

// Unity's Build Profiles window: the platforms, of which those this version of Trinity builds for can be picked and the others are greyed, naming the milestone that brings them, then for the platform picked the scenes in the build, dragged in from the Content Browser or added as the open scene, ticked to ship and dragged into their order, the first ticked one starting the game, and a scene the project no longer has shown as Missing. Then the player: its product, company and version, its icon, the window it first opens with and the graphics APIs it tries in order, of those the platform has, which each platform keeps its own order of. Then Development Build, the output folder, and Build, which exports the game there or, when something stops it, says what, and Build And Run, which then starts it, with Stop Game while it runs. Each edit is a command in the history, so it undoes in one step, and is saved with the project as it is made. Text is one edit once its field is left
class BuildPanel final : public Panel
{
public:
    BuildPanel(EditorSession& session, ExportCommand& exporter);

    // What Build and Build And Run do, from the panel, the File menu or Ctrl+B: the settings checked, then the export started, or what stops it said in the panel and the Console. False when it is stopped, so the caller can bring the panel forward
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