#pragma once

#include "EditorSession.hpp"

#include <Trinity.hpp>

#include <optional>
#include <string>
#include <string_view>

// Unity's Inspector of an Audio Mixer Group, shown by Properties. Each edit is a command
class AudioMixerInspector
{
public:
    explicit AudioMixerInspector(EditorSession& session);

    void Draw();

private:
    // An edit made as the inspector draws, made once it has drawn, so nothing drawn reads stale data
    struct Pending
    {
        Trinity::AudioMixerData After;
        std::string What;
        std::string Field;
    };

    // The mixer, the snapshot edited, the group shown, the mix playing, and whether rows edit
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