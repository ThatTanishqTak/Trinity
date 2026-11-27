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

// Unity's Audio Random Container window with FMOD's curves, for one .trsound. Edits are commands
class SoundEventPanel final : public Panel
{
public:
    explicit SoundEventPanel(EditorSession& session);

    void SetAsset(Trinity::UUID asset);

    // Delete, Duplicate and Rename are the panel's own while it has the focus
    // Not from a frame it was last drawn in: as a tab behind another it is not drawn, and would stay focused for good
    [[nodiscard]] bool IsFocused() const { return m_Focused && IsWindowFocused(); }

protected:
    void OnImGuiRender() override;

private:
    // What was chosen last on the left, which the right shows and Delete, Ctrl+D and F2 act on
    enum class Chosen : std::uint8_t
    {
        Container,
        Parameter
    };

    // An edit made as the panel draws, made once it has drawn, with what it selects once made
    struct Pending
    {
        Trinity::SoundEventData After;
        std::string What;
        std::string Field;
        std::optional<EditorSession::SoundEventSelection> Select;
        std::optional<Chosen> Choose;
    };

    // A point as the curve editor counts them: the curve by the parameter shown, and the point along it
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
    // The text a field shows, kept while it is typed into, and what was typed once it is left
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
    // A field being typed into, by a key of what it edits, its text, and why a name was refused
    std::string m_TextKey;
    std::string m_Text;
    std::string m_FocusKey;
    std::string m_ErrorKey;
    std::string m_Error;
    std::string m_ClipFilter;
    // The curve editor's view, for the curves it was last fitted to. The wheel zooms, middle drags
    std::string m_ViewKey;
    float m_ViewBottom = -0.05f;
    float m_ViewTop = 1.05f;
    // Whether a drag of points is under way, which stays one command, and where it was right-clicked
    bool m_CurveEditing = false;
    bool m_CurveFocused = false;
    float m_CurveMenuAt = 0.0f;
    // ImCurveEdit keeps chosen points by their places, so these are the ones Delete and the fields take
    std::vector<CurvePoint> m_LastPoints;
    bool m_Focused = false;
};