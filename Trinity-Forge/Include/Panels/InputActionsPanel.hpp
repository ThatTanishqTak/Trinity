#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// Unity's Input Actions editor, for one .trinput asset: its maps on the left, the chosen map's actions with their bindings in the middle, and what is chosen on the right, an action's type and interaction, or a binding's sources and processors. Every edit is a command in the history, so it undoes in one step, and is saved to the file and loaded again as it is made, so a scene playing reads it at once and keeps it once it stops. Listen takes the next key, mouse button other than the left, gamepad button, or stick or trigger pushed past half as a source, and Escape or a click anywhere stops it. Until an asset is opened from the Content Browser, it shows the project's default
class InputActionsPanel final : public Panel
{
public:
    explicit InputActionsPanel(EditorSession& session);

    void SetAsset(Trinity::UUID asset);

    // While Listen waits, Forge's shortcuts are left alone, so the key pressed is only bound
    [[nodiscard]] bool IsListening() const { return m_Listening.has_value(); }
    // Delete and Duplicate are the panel's own while it has the focus
    [[nodiscard]] bool IsFocused() const { return m_Focused; }

protected:
    void OnImGuiRender() override;

private:
    // What the details show: a map, an action of it, or a binding of that action, each by its index, -1 for none
    struct Selection
    {
        int Map = -1;
        int Action = -1;
        int Binding = -1;

        [[nodiscard]] bool operator==(const Selection&) const = default;
    };

    // A source Listen fills, by the binding and the part of it, the frame it began in, whose input was the click that began it, and where each pad's axes rested then, so a trigger already held is not taken
    struct Listening
    {
        Selection Binding;
        std::size_t Part = 0;
        int Frame = 0;
        std::array<std::array<float, std::to_underlying(Trinity::GamepadAxis::Count)>, Trinity::Input::c_MaxGamepads> Rest{};
    };

    // An edit made as the panel draws, made once it has drawn, so nothing drawn after it reads what it replaced
    struct Pending
    {
        Trinity::InputActionsData After;
        std::string What;
        std::string Field;
        std::optional<Selection> Select;
    };

    [[nodiscard]] Trinity::UUID GetShownAsset() const;
    void Clamp(const Trinity::InputActionsData& data);
    void Edit(Trinity::InputActionsData after, std::string what, std::string field = {}, std::optional<Selection> select = {});
    void Select(const Trinity::InputActionsData& data, Selection selection);

    void DrawHeader(Trinity::UUID asset, const Trinity::AssetRecord& record);
    void DrawMaps(const Trinity::InputActionsData& data);
    void DrawActions(const Trinity::InputActionsData& data);
    void DrawDetails(const Trinity::InputActionsData& data);
    void DrawMapDetails(const Trinity::InputActionsData& data);
    void DrawActionDetails(const Trinity::InputActionsData& data);
    void DrawInteraction(const Trinity::InputActionsData& data);
    void DrawBindingDetails(const Trinity::InputActionsData& data);
    void DrawSource(const Trinity::InputActionsData& data, std::size_t part, const char* label);
    void DrawProcessors(const Trinity::InputActionsData& data);
    void DrawName(const Trinity::InputActionsData& data, std::string_view current);
    void CommitName(const Trinity::InputActionsData& data);
    void ReadShortcuts(const Trinity::InputActionsData& data);

    void AddMap(const Trinity::InputActionsData& data);
    void AddAction(const Trinity::InputActionsData& data, int map);
    void AddBinding(const Trinity::InputActionsData& data, Selection action, Trinity::InputCompositeKind composite);
    void Remove(const Trinity::InputActionsData& data, Selection what);
    void Duplicate(const Trinity::InputActionsData& data, Selection what);
    void Move(const Trinity::InputActionsData& data, Selection what, int by);
    void Bind(const Trinity::InputActionsData& data, Selection binding, std::size_t part, Trinity::InputSource source);

    void BeginListen(Selection binding, std::size_t part);
    void UpdateListen(const Trinity::InputActionsData& data);

    EditorSession& m_Session;
    Trinity::UUID m_Asset;
    // The asset drawn last, so a new one starts with its first map chosen
    Trinity::UUID m_Shown;
    Selection m_Selected;
    std::optional<Listening> m_Listening;
    std::optional<Pending> m_Pending;
    std::string m_Search;
    std::string m_PickerFilter;
    // A name being typed, kept apart from the asset's until the field is left, whether it has been typed into, why the last one was refused, and the frame its field takes the keyboard in, as after Rename
    std::string m_NameText;
    std::string m_NameError;
    bool m_NameEdited = false;
    int m_FocusNameFrame = -1;
    bool m_Focused = false;
};