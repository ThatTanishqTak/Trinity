#pragma once

#include "EditorSession.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>


// Edit > Preferences..., Forge's settings for the person using it, as Unity's Preferences window shows them: its sections listed down the left under a search that filters them by their settings' names, and the chosen section's settings on the right. Each applies as it changes and is saved with the preferences, and a section's Reset to Defaults puts it back as Forge first had it
class PreferencesWindow
{
public:
    explicit PreferencesWindow(EditorSession& session);

    // Opens it, or brings it to the front where it is open already
    void Open();
    void Draw();

private:
    enum class Section : std::uint8_t
    {
        General,
        Audio,
        ExternalTools,
        Colors,
        SceneView
    };

    // Whether the search leaves the section listed, and the setting in it shown
    [[nodiscard]] bool Lists(Section section) const;
    [[nodiscard]] bool Shows(Section section, std::string_view setting) const;
    void DrawGeneral();
    void DrawAudio();
    void DrawExternalTools();
    void DrawColors();
    void DrawSceneView();

    EditorSession& m_Session;
    bool m_Open = false;
    bool m_FocusRequested = false;
    Section m_Section = Section::General;
    std::string m_Search;
    // The outputs as the system listed them when the Output Device menu last opened
    std::vector<Trinity::AudioDevice> m_Devices;
};