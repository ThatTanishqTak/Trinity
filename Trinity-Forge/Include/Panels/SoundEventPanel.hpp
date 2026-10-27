#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Unity's Audio Random Container window, for one .trsound asset, with what FMOD's event editor adds to it: the audition along the top, Play and Stop beside a slider for each of the event's parameters, heard through the project's mixer, its containers as a tree and its parameters in a column on the left, and on the right what was chosen last there, a parameter's range and labels, or a container's values, the event's own with the root's, and its curves by a parameter in ImCurveEdit, Volume, Pitch and Cutoff in one view, each over its own range. Clips dragged in from the Content Browser become Clip containers. Every edit is a command in the history, so it undoes in one step, and is saved to the file as it is made, so the event playing, the audition's and a scene's, moves to it as each of its containers next triggers
class SoundEventPanel final : public Panel
{
public:
    explicit SoundEventPanel(EditorSession& session);

    void SetAsset(Trinity::UUID asset);

    // Delete, Duplicate and Rename are the panel's own while it has the focus
    [[nodiscard]] bool IsFocused() const { return m_Focused; }

protected:
    void OnImGuiRender() override;

private:
    // What was chosen last in the column on the left, which the right shows and Delete, Ctrl+D and F2 act on: a container, or a parameter, which the session's selection names
    enum class Chosen : std::uint8_t
    {
        Container,
        Parameter
    };

    // An edit made as the panel draws, made once it has drawn, so nothing drawn after it reads what it replaced, with what it selects and chooses once made
    struct Pending
    {
        Trinity::SoundEventData After;
        std::string What;
        std::string Field;
        std::optional<EditorSession::SoundEventSelection> Select;
        std::optional<Chosen> Choose;
    };

    // A point of a curve as the curve editor counts them: the curve among the container's by the parameter shown, and the point along it
    struct CurvePoint
    {
        int Curve = 0;
        int Point = 0;

        [[nodiscard]] bool operator==(const CurvePoint&) const = default;
    };

    [[nodiscard]] Trinity::UUID GetShownAsset() const;
    void Clamp(Trinity::UUID asset, const Trinity::SoundEventData& data);
    void Edit(Trinity::SoundEventData after, std::string what, std::string field = {}, std::optional<EditorSession::SoundEventSelection> select = {}, std::optional<Chosen> choose = {});
    void Choose(Chosen chosen, Trinity::UUID id);
    // The text a field shows, kept while it is typed into, and what was typed once it is left after an edit
    [[nodiscard]] std::optional<std::string> TextField(const char* label, const std::string& key, std::string_view value);

    void DrawAudition(Trinity::UUID asset, const Trinity::SoundEventData& data);
    void DrawTree(const Trinity::SoundEventData& data);
    void DrawContainerNode(const Trinity::SoundEventData& data, const Trinity::SoundContainer& container);
    void DrawContainerMenu(const Trinity::SoundEventData& data, const Trinity::SoundContainer& container);
    void AcceptDrop(const Trinity::SoundEventData& data, const Trinity::SoundContainer& target, bool end);
    void DrawParameters(const Trinity::SoundEventData& data);
    void DrawContainerDetails(Trinity::UUID asset, const Trinity::SoundEventData& data, const Trinity::SoundContainer& container);
    void DrawEventSettings(const Trinity::SoundEventData& data);
    void DrawClipSlot(const Trinity::SoundEventData& data, const Trinity::SoundContainer& container);
    void DrawParameterDetails(const Trinity::SoundEventData& data, const Trinity::SoundParameter& parameter);
    void DrawCurves(Trinity::UUID asset, const Trinity::SoundEventData& data, const Trinity::SoundContainer& container);
    void DrawCurveMenu(const Trinity::SoundEventData& data, const Trinity::SoundContainer& container, const Trinity::SoundParameter& parameter, const std::vector<CurvePoint>& chosen);
    void DrawPointFields(const Trinity::SoundEventData& data, const Trinity::SoundContainer& container, const Trinity::SoundParameter& parameter, const std::vector<CurvePoint>& chosen);
    void ReadShortcuts(const Trinity::SoundEventData& data);

    void AddContainer(const Trinity::SoundEventData& data, Trinity::UUID parent, Trinity::SoundContainerType type, Trinity::UUID clip = {});
    void DuplicateContainer(const Trinity::SoundEventData& data, Trinity::UUID container);
    void RemoveContainer(const Trinity::SoundEventData& data, Trinity::UUID container);
    void MoveContainer(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parent, Trinity::UUID before, std::string_view way);
    void SetContainerType(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::SoundContainerType type);
    void SetClip(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID clip);
    void RenameContainer(const Trinity::SoundEventData& data, Trinity::UUID container, std::string_view name);
    void AddParameter(const Trinity::SoundEventData& data, Trinity::SoundParameterKind kind);
    void RemoveParameter(const Trinity::SoundEventData& data, Trinity::UUID parameter);
    void RenameParameter(const Trinity::SoundEventData& data, Trinity::UUID parameter, std::string_view name);
    void AddCurve(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter, Trinity::SoundCurveTarget target);
    void RemoveCurve(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter, Trinity::SoundCurveTarget target);
    void ResetCurve(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter, Trinity::SoundCurveTarget target);
    void AddPoint(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter, Trinity::SoundCurveTarget target, float value);
    void RemovePoints(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter, const std::vector<CurvePoint>& chosen);
    // The rename of what is chosen typed into its Name field
    void BeginRename(Chosen chosen, Trinity::UUID id);

    // The points ImCurveEdit had chosen as it last drew, those of the curves shown now
    [[nodiscard]] std::vector<CurvePoint> GetChosenPoints(const Trinity::SoundEventData& data, Trinity::UUID container, Trinity::UUID parameter) const;
    [[nodiscard]] std::string GetCurveKey(Trinity::UUID container, Trinity::UUID parameter) const;

    EditorSession& m_Session;
    Trinity::UUID m_Asset;
    // The event the audition last had values for, which it forgets as another is shown
    Trinity::UUID m_Auditioned;
    std::optional<Pending> m_Pending;
    Chosen m_Chosen = Chosen::Container;
    // A field being typed into, by a key of what it edits, and its text, the field to take the keyboard when next drawn, and why the last name typed was refused, under the field it was typed in
    std::string m_TextKey;
    std::string m_Text;
    std::string m_FocusKey;
    std::string m_ErrorKey;
    std::string m_Error;
    std::string m_ClipFilter;
    // The curve editor's view, for the curves it was last fitted to: the parameter's range across it, and from the bottom of each curve's range to its top, with a little room, up it, which the wheel zooms and the middle button drags
    std::string m_ViewKey;
    float m_ViewBottom = -0.05f;
    float m_ViewTop = 1.05f;
    // Whether the editor is between the start and the end of a drag of its points, which stays one command however long it lasts, whether it was clicked last of the panel, so Delete takes its points, and where on its parameter it was last right-clicked
    bool m_CurveEditing = false;
    bool m_CurveFocused = false;
    float m_CurveMenuAt = 0.0f;
    // ImCurveEdit keeps the points chosen by their places, whatever curves it is given, and highlights those the curves it draws have, so those are the ones Delete and the point's fields take
    std::vector<CurvePoint> m_LastPoints;
    bool m_Focused = false;
};