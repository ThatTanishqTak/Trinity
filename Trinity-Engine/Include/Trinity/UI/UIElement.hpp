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

    // What happened to an element, as RmlUi raises it: a click, a control's value changing, a form submitted, the pointer coming onto and leaving it, and the focus coming to and leaving it. Numbered as C#'s UIEventType
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

    // An element of a UI document, safe to keep after the element has gone, such as when SetInnerRML replaced what held it or its document was read again: every call on it then does nothing, or reads as nothing, logged once a handle. Main thread only
    class TRINITY_API UIElement
    {
    public:
        UIElement() = default;

        [[nodiscard]] bool IsValid() const;
        explicit operator bool() const { return IsValid(); }

        [[nodiscard]] std::string GetID() const;
        // Every text inside it, joined, and text put in place of everything it holds, shown as written rather than read as RML
        [[nodiscard]] std::string GetText() const;
        void SetText(std::string_view text) const;
        [[nodiscard]] std::string GetInnerRML() const;
        void SetInnerRML(std::string_view rml) const;
        // A form control's value, such as a text field's text or a range's number, and empty for any other element, on which setting it does nothing
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
        // A property set on the element itself, as its style attribute sets one, over its stylesheets'. False for a property or a value RmlUi cannot read
        bool SetStyle(std::string_view property, std::string_view value) const;
        void RemoveStyle(std::string_view property) const;
        // The property as the element has it, from its own style or its stylesheets, empty for one RmlUi does not know
        [[nodiscard]] std::string GetStyle(std::string_view property) const;

        // Hidden is display: none set on the element, so it takes no space and neither the pointer nor the focus reaches it. Shown again, that goes, and its stylesheets' display holds. Visible is whether it and every element around it are displayed
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

    // How the runtime calls a handler, which is opaque to it. Invoke is false when the handler failed, logged by its language, which removes it, and Free lets it go. The language is the one its time goes to
    struct UIHandlerCalls
    {
        bool (*Invoke)(void* handler, const UIEvent& event) = nullptr;
        void (*Free)(void* handler) = nullptr;
        ScriptLanguage Language = ScriptLanguage::Cpp;
    };

    // A UIDocument of the running scene, by its entity, as its scripts reach it. Its document is loaded as it is first reached, when its scene has not been shown yet, so OnCreate finds its elements. Every call does nothing, or finds nothing, for an entity without a UIDocument or one whose document did not load. Main thread only
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
        // The data model of that name, as data-model="name" names it in the document, made the first time it is asked for, and kept while the scene's scripts run, its document loaded or not. Invalid for a name RmlUi cannot reach
        [[nodiscard]] UIDataModel GetModel(std::string_view name) const;

        // The handler, for each event of that type on the element with that ID, and for Click, Change and Submit on any element inside it too, as they bubble out, the innermost element's handlers first. It is found by its ID as each event comes, so it holds for an element made again. Events are queued as they happen and given to handlers once a frame, before the fixed steps and OnUpdate, after the input action handlers. The handler is the script's that was running as it was added, and goes with it, and every handler goes with the running scene. Kept under the Scripting tag until then or until removed
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

    // What a handler is told: its document, the element it was added for, where the event happened, which is that element or one inside it, and for Change and Submit the value it changed to or the form sent, valid for the call
    struct UIEvent
    {
        UIEventType Type = UIEventType::Click;
        UIDocument Document;
        UIElement Element;
        UIElement Target;
        std::string_view Value;
    };

    // A UI event GameUI queued for the scripts of the scene whose UIDocument's document it happened in, numbered from 1 in the order it happened. Path holds the IDs of the target and of each element around it that has one, the target's first, each followed by a zero, and only the target's for PointerEnter, PointerLeave, Focus and Blur, which each element has for itself
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