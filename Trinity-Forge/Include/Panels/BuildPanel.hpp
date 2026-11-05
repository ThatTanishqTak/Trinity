#pragma once

#include "EditorSession.hpp"
#include "Export/PackCommand.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <string>

// Unity's Build Profiles window, for the one platform a build is made for: the scenes in the build, dragged in from the Content Browser or added as the open scene, ticked to ship and dragged into their order, the first ticked one starting the game, and a scene the project no longer has shown as Missing. Then the player: its product, company and version, its icon, the window it first opens with and the graphics APIs it tries in order. Then Development Build, the output folder, and Build, which writes the game there or, when something stops it, says what. Each edit is a command in the history, so it undoes in one step, and is saved with the project as it is made. Text is one edit once its field is left
class BuildPanel final : public Panel
{
public:
    BuildPanel(EditorSession& session, PackCommand& pack);

protected:
    void OnImGuiRender() override;

private:
    // What a field shows while it is typed into, which the settings take once it is left
    struct TextBuffer
    {
        std::string Text;
        bool Editing = false;
    };

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
    PackCommand& m_Pack;
    TextBuffer m_Product;
    TextBuffer m_Company;
    TextBuffer m_Version;
    TextBuffer m_Output;
    std::string m_IconFilter;
    // Whether the last Build asked for was stopped, so what stops it shows until nothing does
    bool m_Refused = false;
};