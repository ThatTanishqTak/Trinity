#pragma once

#include "EditorSession.hpp"
#include "Export/ExportCommand.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Unity's Build Profiles window: the platform, the scenes and the build. What the game is called and how it opens is in Project Settings. Each edit is a command, saved with the project as it is made
class BuildPanel final : public Panel
{
public:
    BuildPanel(EditorSession& session, ExportCommand& exporter);

    // Checks the settings, then starts the export. False when stopped, so the caller can show the panel
    bool Build(bool run);
    // What Player Settings does, which is Project Settings opened on its Player section
    void SetShowPlayerSettings(std::function<void()> showPlayerSettings) { m_ShowPlayerSettings = std::move(showPlayerSettings); }

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
    void DrawBuild(const Trinity::ExportSettings& settings);

    // The settings as the project has them now, changed as given, as one command
    template<typename F>
    void Edit(std::string field, F&& change);
    // True once a field that was typed into is left with text other than the value
    [[nodiscard]] static bool TextField(const char* id, TextBuffer& buffer, const std::string& value);

    EditorSession& m_Session;
    ExportCommand& m_Export;
    TextBuffer m_Output;
    std::function<void()> m_ShowPlayerSettings;
    // As this version's manifest lists them, read once the panel is first drawn
    std::optional<std::vector<std::string>> m_Platforms;
    // Whether the last Build asked for was stopped, so what stops it shows until nothing does
    bool m_Refused = false;
};