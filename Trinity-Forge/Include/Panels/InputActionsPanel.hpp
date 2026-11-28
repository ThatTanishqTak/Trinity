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

// Unity's Input Actions editor, for one .trinput asset. Every edit is a command, saved as made
class InputActionsPanel final : public Panel
{
public:
    explicit InputActionsPanel(EditorSession& session);

    void SetAsset(Trinity::UUID asset);

    // While Listen waits, Forge's shortcuts are left alone, so the key pressed is only bound
    [[nodiscard]] bool IsListening() const { return m_Listening.has_value(); }

protected:
    void OnImGuiRender() override;

private:
    // What the details show: a map, an action of it, or a binding of that action, by index, -1 for none
    struct Selection
    {
        int Map = -1;
        int Action = -1;
        int Binding = -1;

        [[nodiscard]] bool operator==(const Selection&) const = default;
    };

    // A source Listen fills, the frame it began in, and where each pad's axes rested then
    struct Listening
    {
        Selection Binding;
        std::size_t Part = 0;
        int Frame = 0;
        std::array<std::array<float, std::to_underlying(Trinity::GamepadAxis::Count)>, Trinity::Input::c_MaxGamepads> Rest{};
    };

    // An edit made as the panel draws, made once it has drawn. A rename takes the scene's fields along
    struct Pending
    {
        Trinity::InputActionsData After;
        std::string What;
        std::string Field;
        std::optional<Selection> Select;
        bool Rename = false;
    };

    [[nodiscard]] Trinity::UUID GetShownAsset() const;
    void Clamp(const Trinity::InputActionsData& data);
    void Edit(Trinity::InputActionsData after, std::string what, std::string field = {}, std::optional<Selection> select = {});
    void Rename(Trinity::InputActionsData after, std::string what);
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
    // A name being typed, kept apart from the asset's until the field is left, and why one was refused
    std::string m_NameText;
    std::string m_NameError;
    bool m_NameEdited = false;
    int m_FocusNameFrame = -1;
    bool m_Focused = false;
};