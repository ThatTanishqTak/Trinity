#pragma once

#include "EditorSession.hpp"

#include <Trinity.hpp>

#include <optional>
#include <string>
#include <string_view>

// Unity's Inspector of an Audio Mixer Group, which Properties shows while a group or one of its effects is chosen in the Audio Mixer panel: the group's Pitch, then each unit of its chain in its order, Attenuation with the group's Volume, and each effect's Bypass, Wet and values, with a Send's target, the one chosen opened and scrolled to. The values are the snapshot's that the panel edits, and a right-click on one exposes it to scripts by a name made from it, or no longer, and picks the curve a transition to the snapshot moves it along. Each edit is a command, as the panel's are. While the project's mixer plays a scene with Edit in Play Mode off, the values follow the mix it plays and nothing is edited
class AudioMixerInspector
{
public:
    explicit AudioMixerInspector(EditorSession& session);

    void Draw();

private:
    // An edit made as the inspector draws, made once it has drawn, so nothing drawn after it reads what it replaced
    struct Pending
    {
        Trinity::AudioMixerData After;
        std::string What;
        std::string Field;
    };

    // What every row reads: the mixer, the snapshot edited, the group shown, the mix playing while the rows follow it, and whether they edit
    struct View
    {
        const Trinity::AudioMixerData* Data = nullptr;
        const Trinity::AudioMixerSnapshot* Snapshot = nullptr;
        const Trinity::AudioMixerGroup* Group = nullptr;
        const Trinity::AudioMixer* Live = nullptr;
        bool Editable = true;
    };

    void DrawValue(const View& view, Trinity::AudioMixerValue value, std::string_view owner);
    void DrawValueMenu(const View& view, Trinity::AudioMixerValue value, std::string_view label, std::string_view owner);
    void DrawSendTarget(const View& view, const Trinity::AudioEffect& send);
    void Edit(Trinity::AudioMixerData after, std::string what, std::string field = {});

    EditorSession& m_Session;
    std::optional<Pending> m_Pending;
    // The group or effect shown last, so one newly chosen has its section opened and scrolled to
    Trinity::UUID m_Shown;
};