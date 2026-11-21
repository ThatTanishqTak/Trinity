#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Scripting/ScriptField.hpp"
#include "Trinity/UI/UIDataModel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Trinity
{
    class ScriptRuntime;

    // As RmlUi raises it. Numbered as C#'s UIEventType
    enum class UIEventType : std::uint8_t
    {
        Click,
        Change,
        Submit,
        PointerEnter,
        PointerLeave,
        Focus,
        Blur
    };

    inline constexpr std::uint32_t c_UIEventTypeCount = 7;

    [[nodiscard]] TRINITY_API std::string_view ToString(UIEventType type);

    // Safe to keep after the element has gone: calls then do nothing, logged once. Main thread only
    class TRINITY_API UIElement
    {
    public:
        UIElement() = default;

        [[nodiscard]] bool IsValid() const;
        explicit operator bool() const { return IsValid(); }

        [[nodiscard]] std::string GetID() const;
        // Every text inside it, joined. Setting it replaces everything it holds, shown as written, not RML
        [[nodiscard]] std::string GetText() const;
        void SetText(std::string_view text) const;
        [[nodiscard]] std::string GetInnerRML() const;
        void SetInnerRML(std::string_view rml) const;
        // A form control's value, such as a text field's text. Empty, and unsettable, for any other element
        [[nodiscard]] std::string GetValue() const;
        void SetValue(std::string_view value) const;
        // A checkbox's or a radio button's, which its checked attribute is
        [[nodiscard]] bool IsChecked() const;
        void SetChecked(bool checked) const;

        [[nodiscard]] bool HasClass(std::string_view name) const;
        void SetClass(std::string_view name, bool set = true) const;
        [[nodiscard]] std::optional<std::string> GetAttribute(std::string_view name) const;
        void SetAttribute(std::string_view name, std::string_view value) const;
        void RemoveAttribute(std::string_view name) const;
        // Set on the element itself, over its stylesheets'. False for a property or value RmlUi cannot read
        bool SetStyle(std::string_view property, std::string_view value) const;
        void RemoveStyle(std::string_view property) const;
        // As the element has it, from its own style or its stylesheets. Empty for one RmlUi does not know
        [[nodiscard]] std::string GetStyle(std::string_view property) const;

        // Hidden sets display: none on the element. Visible is whether it and all around it are displayed
        [[nodiscard]] bool IsVisible() const;
        void SetVisible(bool visible) const;
        [[nodiscard]] bool IsFocused() const;
        // False when it cannot take the focus, such as when it is hidden
        bool Focus() const;
        // The focus back on its document, when the element has it
        void Blur() const;

        // As C# holds it, 0 for none
        [[nodiscard]] std::uint64_t GetHandle() const { return m_Handle; }
        [[nodiscard]] static UIElement FromHandle(std::uint64_t handle)
        {
            UIElement l_Element;
            l_Element.m_Handle = handle;

            return l_Element;
        }

        [[nodiscard]] bool operator==(const UIElement&) const = default;

    private:
        std::uint64_t m_Handle = 0;
    };

    // A handler of UI events, which stays valid to remove after it has gone
    struct UIEventHandle
    {
        std::uint64_t ID = 0;

        explicit operator bool() const { return ID != 0; }
        [[nodiscard]] bool operator==(const UIEventHandle&) const = default;
    };

    struct UIEvent;

    // Invoke is false when the handler failed, which removes it. Free lets it go
    struct UIHandlerCalls
    {
        bool (*Invoke)(void* handler, const UIEvent& event) = nullptr;
        void (*Free)(void* handler) = nullptr;
        ScriptLanguage Language = ScriptLanguage::Cpp;
    };

    // Loaded as it is first reached, so OnCreate finds its elements. Calls do nothing if it did not
    class TRINITY_API UIDocument
    {
    public:
        UIDocument() = default;
        UIDocument(ScriptRuntime& runtime, UUID entity) : m_Runtime(&runtime), m_Entity(entity) {}

        // Whether its entity has a UIDocument whose document loaded
        [[nodiscard]] bool IsValid() const;
        [[nodiscard]] UUID GetEntity() const { return m_Entity; }

        [[nodiscard]] UIElement GetElement(std::string_view id) const;
        // The first element the selector matches, such as "#menu button.primary"
        [[nodiscard]] UIElement Query(std::string_view selector) const;
        // The element with the focus, or none
        [[nodiscard]] UIElement GetFocus() const;
        // As data-model names it, made on first ask. Invalid for a name RmlUi cannot reach
        [[nodiscard]] UIDataModel GetModel(std::string_view name) const;

        // By element ID, so it holds for an element made again. Click, Change and Submit bubble out to it
        template<typename F>
            requires std::is_invocable_v<std::decay_t<F>&, const UIEvent&>
        UIEventHandle On(std::string_view element, UIEventType type, F&& handler) const;
        // False when it had already gone
        bool Off(UIEventHandle handler) const;

    private:
        UIEventHandle AddHandler(std::string_view element, UIEventType type, const UIHandlerCalls& calls, void* handler) const;

        ScriptRuntime* m_Runtime = nullptr;
        UUID m_Entity;
    };

    // The element it was added for, the target, on or inside it, and the value, valid for the call
    struct UIEvent
    {
        UIEventType Type = UIEventType::Click;
        UIDocument Document;
        UIElement Element;
        UIElement Target;
        std::string_view Value;
    };

    // Numbered from 1 in order. Path holds the IDs from the target outwards, each followed by a zero
    struct UIQueuedEvent
    {
        std::uint64_t Sequence = 0;
        UUID Entity;
        UIEventType Type = UIEventType::Click;
        UIElement Target;
        std::string Path;
        std::string Value;
    };

    namespace Detail
    {
        // A C++ handler, freed under the tag it was made under
        template<typename F>
        struct UIHandlerCallback
        {
            F Callback;

            static bool Invoke(void* handler, const UIEvent& event)
            {
                static_cast<UIHandlerCallback*>(handler)->Callback(event);

                return true;
            }

            static void Free(void* handler)
            {
                Memory::Delete(static_cast<UIHandlerCallback*>(handler));
            }

            static constexpr UIHandlerCalls c_Calls{ &Invoke, &Free, ScriptLanguage::Cpp };
        };
    }

    template<typename F>
        requires std::is_invocable_v<std::decay_t<F>&, const UIEvent&>
    UIEventHandle UIDocument::On(std::string_view element, UIEventType type, F&& handler) const
    {
        using Callback = Detail::UIHandlerCallback<std::decay_t<F>>;

        return AddHandler(element, type, Callback::c_Calls, Memory::New<Callback>(MemoryTag::Scripting, std::forward<F>(handler)));
    }
}