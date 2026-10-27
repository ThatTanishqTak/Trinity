#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Unity's Audio Mixer window, for one .trmixer asset: its snapshots, its groups as a tree and its exposed values in a column on the left, and a strip for each group on the right, with its volume on a fader beside its meter, its Mute, Solo and Bypass, and its chain of effects, Attenuation among them, which Add... puts more after. The faders show and edit the selected snapshot, which is heard while no scene plays, and while one does with Edit in Play Mode on; with it off they follow the mix the scene plays and nothing is edited. Every edit is a command in the history, so it undoes in one step, and is saved to the file as it is made, so the project's mixer is heard as edited at once and keeps the edit once a scene stops. Until an asset is opened from the Content Browser, it shows the project's mixer
class AudioMixerPanel final : public Panel
{
public:
    explicit AudioMixerPanel(EditorSession& session);

    void SetAsset(Trinity::UUID asset);

    // Delete, Duplicate and Rename are the panel's own while it has the focus
    [[nodiscard]] bool IsFocused() const { return m_Focused; }

protected:
    void OnImGuiRender() override;

private:
    // What was chosen last, which Delete, Ctrl+D and F2 act on, and what a rename is typed into: a snapshot, a group or an effect, which the session's selection names, or an exposed value, by its index
    enum class Chosen : std::uint8_t
    {
        None,
        Snapshot,
        Item,
        Exposed
    };

    // An edit made as the panel draws, made once it has drawn, so nothing drawn after it reads what it replaced, with what it selects once made
    struct Pending
    {
        Trinity::AudioMixerData After;
        std::string What;
        std::string Field;
        std::optional<EditorSession::MixerSelection> Select;
    };

    [[nodiscard]] Trinity::UUID GetShownAsset() const;
    [[nodiscard]] bool IsProjectMixer(Trinity::UUID asset) const;
    [[nodiscard]] bool IsEditable(Trinity::UUID asset) const;
    void Clamp(Trinity::UUID asset, const Trinity::AudioMixerData& data);
    void Edit(Trinity::AudioMixerData after, std::string what, std::string field = {}, std::optional<EditorSession::MixerSelection> select = {});
    void Choose(Chosen chosen, Trinity::UUID snapshot, Trinity::UUID item);

    void DrawHeader(Trinity::UUID asset, const Trinity::AssetRecord& record, const Trinity::AudioMixerData* data);
    void DrawSnapshots(const Trinity::AudioMixerData& data, bool editable);
    void DrawGroups(const Trinity::AudioMixerData& data, bool editable);
    void DrawGroupNode(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group, bool editable);
    void DrawGroupMenu(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group);
    void DrawExposed(const Trinity::AudioMixerData& data, bool editable);
    void DrawStrips(Trinity::UUID asset, const Trinity::AudioMixerData& data, bool editable);
    void DrawStrip(Trinity::UUID asset, const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group, bool editable);
    void DrawToggles(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group);
    void DrawChain(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group, bool editable);
    void DrawEffectMenu(const Trinity::AudioMixerData& data, const Trinity::AudioMixerGroup& group, const Trinity::AudioEffectList& chain, std::size_t place);
    void DrawRenameError(Chosen chosen);
    void DrawRenameField(const Trinity::AudioMixerData& data);
    void ReadShortcuts(const Trinity::AudioMixerData& data, bool editable);

    void BeginRename(Chosen chosen, Trinity::UUID id, std::size_t index, std::string_view current);
    void CommitRename(const Trinity::AudioMixerData& data);
    void AddSnapshot(const Trinity::AudioMixerData& data, Trinity::UUID copyOf);
    void RemoveSnapshot(const Trinity::AudioMixerData& data, Trinity::UUID snapshot);
    void SetStartSnapshot(const Trinity::AudioMixerData& data, Trinity::UUID snapshot);
    void AddGroup(const Trinity::AudioMixerData& data, Trinity::UUID parent);
    void DuplicateGroup(const Trinity::AudioMixerData& data, Trinity::UUID group);
    void RemoveGroup(const Trinity::AudioMixerData& data, Trinity::UUID group);
    void MoveGroup(const Trinity::AudioMixerData& data, Trinity::UUID group, Trinity::UUID parent, Trinity::UUID before, std::string_view way);
    void InsertEffect(const Trinity::AudioMixerData& data, Trinity::UUID group, std::size_t place, Trinity::AudioEffectType type);
    void RemoveEffect(const Trinity::AudioMixerData& data, Trinity::UUID effect);
    void MoveEffect(const Trinity::AudioMixerData& data, Trinity::UUID group, std::size_t from, std::size_t to);
    void RemoveExposed(const Trinity::AudioMixerData& data, std::size_t index);

    EditorSession& m_Session;
    Trinity::UUID m_Asset;
    std::optional<Pending> m_Pending;
    Chosen m_Chosen = Chosen::None;
    std::size_t m_ChosenExposed = 0;
    // A rename being typed: what it renames, a snapshot or a group by its ID or an exposed value by its index, its text, whether its field takes the keyboard when next drawn, and whether it was drawn this frame, as it is not under a group folded away
    Chosen m_Renaming = Chosen::None;
    Trinity::UUID m_RenameID;
    std::size_t m_RenameIndex = 0;
    std::string m_RenameText;
    bool m_FocusRename = false;
    bool m_RenameDrawn = false;
    // Why the last rename was refused, shown under the list it was typed in until something else is chosen
    std::string m_RenameError;
    Chosen m_RenameErrorIn = Chosen::None;
    bool m_Focused = false;
};