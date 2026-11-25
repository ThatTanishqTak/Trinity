#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// As Preferences is laid out: sections on the left under a search, and the chosen one's settings. The project's player, window, graphics, audio and physics settings. Each edit is a command, saved with the project
class ProjectSettingsPanel final : public Panel
{
public:
    enum class Section : std::uint8_t
    {
        Player,
        Window,
        Graphics,
        Audio,
        Physics,
        CollisionLayers
    };

    explicit ProjectSettingsPanel(EditorSession& session);

    // Opens the panel on the section and brings it to the front, as the Build panel's Player Settings does
    void ShowSection(Section section);

protected:
    void OnImGuiRender() override;

private:
    // What a field shows while it is typed into, which the settings take once it is left
    struct TextBuffer
    {
        std::string Text;
        bool Editing = false;
    };

    // Whether the search leaves the section listed, and the setting in it shown
    [[nodiscard]] bool Lists(Section section) const;
    [[nodiscard]] bool Shows(Section section, std::string_view setting) const;
    void DrawPlayer(const Trinity::ExportSettings& settings);
    void DrawIcon(const Trinity::ExportSettings& settings);
    void DrawWindow(const Trinity::ExportSettings& settings);
    void DrawGraphics(const Trinity::ExportSettings& settings);
    void DrawAudio(Trinity::AudioSettings settings, const Trinity::CollisionLayers& layers, bool playing);
    void DrawMixer(Trinity::AudioSettings settings);
    void DrawPhysics(Trinity::PhysicsSettings settings);
    void DrawCollisionLayers(Trinity::PhysicsSettings settings);
    void DrawLayerNames(Trinity::PhysicsSettings settings);
    void DrawCollisionMatrix(Trinity::PhysicsSettings settings);

    // What the project exports as the project has it now, changed as given, as one command
    template<typename F>
    void Edit(std::string field, F&& change);
    // True once a field that was typed into is left with text other than the value
    [[nodiscard]] static bool TextField(const char* id, TextBuffer& buffer, const std::string& value);

    EditorSession& m_Session;
    Section m_Section = Section::Player;
    std::string m_Search;
    TextBuffer m_Product;
    TextBuffer m_Company;
    TextBuffer m_Version;
    std::string m_IconFilter;
    // A name being typed, kept apart from the project's until the field is left
    std::array<std::string, Trinity::c_MaxCollisionLayers> m_Names;
};