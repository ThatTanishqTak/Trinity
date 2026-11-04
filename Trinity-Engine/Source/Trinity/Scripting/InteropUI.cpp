#include "Trinity/Scripting/Interop.hpp"

#include "Trinity/Scripting/ScriptRuntime.hpp"
#include "Trinity/UI/UIElement.hpp"

#include <optional>
#include <string>

namespace Trinity
{
    namespace Interop
    {
        namespace
        {
            // An element's text, numbered as C#'s UIElementText
            enum class ElementText : Int32
            {
                ID,
                Text,
                InnerRml,
                Value
            };

            // An element's flag, numbered as C#'s UIElementFlag
            enum class ElementFlag : Int32
            {
                Visible,
                Focused,
                Checked
            };

            [[nodiscard]] UIDocument GetDocument(EntityId entity)
            {
                ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();

                return l_Runtime != nullptr ? UIDocument(*l_Runtime, UUID(entity)) : UIDocument();
            }

            // Kept until the next text is asked for, as C# copies it at once
            [[nodiscard]] Utf8 ToText(std::string text)
            {
                static std::string s_Text;
                s_Text = std::move(text);

                return ToUtf8(s_Text);
            }
        }

        Bool UIDocument_IsValid(EntityId entity)
        {
            return GetDocument(entity).IsValid();
        }

        UInt64 UIDocument_GetElement(EntityId entity, Utf8 id)
        {
            return GetDocument(entity).GetElement(ToStringView(id)).GetHandle();
        }

        UInt64 UIDocument_Query(EntityId entity, Utf8 selector)
        {
            return GetDocument(entity).Query(ToStringView(selector)).GetHandle();
        }

        UInt64 UIDocument_GetFocus(EntityId entity)
        {
            return GetDocument(entity).GetFocus().GetHandle();
        }

        Bool UIElement_IsValid(UInt64 element)
        {
            return UIElement::FromHandle(element).IsValid();
        }

        Utf8 UIElement_GetText(UInt64 element, Int32 text)
        {
            const UIElement l_Element = UIElement::FromHandle(element);
            switch (static_cast<ElementText>(text))
            {
                case ElementText::ID:
                {
                    return ToText(l_Element.GetID());
                }
                case ElementText::Text:
                {
                    return ToText(l_Element.GetText());
                }
                case ElementText::InnerRml:
                {
                    return ToText(l_Element.GetInnerRML());
                }
                case ElementText::Value:
                {
                    return ToText(l_Element.GetValue());
                }
            }

            return Utf8{ nullptr, 0 };
        }

        // An element's ID is its document's to set, so setting it does nothing
        Void UIElement_SetText(UInt64 element, Int32 text, Utf8 value)
        {
            const UIElement l_Element = UIElement::FromHandle(element);
            switch (static_cast<ElementText>(text))
            {
                case ElementText::Text:
                {
                    l_Element.SetText(ToStringView(value));

                    break;
                }
                case ElementText::InnerRml:
                {
                    l_Element.SetInnerRML(ToStringView(value));

                    break;
                }
                case ElementText::Value:
                {
                    l_Element.SetValue(ToStringView(value));

                    break;
                }
                case ElementText::ID:
                {
                    break;
                }
            }
        }

        Bool UIElement_GetFlag(UInt64 element, Int32 flag)
        {
            const UIElement l_Element = UIElement::FromHandle(element);
            switch (static_cast<ElementFlag>(flag))
            {
                case ElementFlag::Visible:
                {
                    return l_Element.IsVisible();
                }
                case ElementFlag::Focused:
                {
                    return l_Element.IsFocused();
                }
                case ElementFlag::Checked:
                {
                    return l_Element.IsChecked();
                }
            }

            return false;
        }

        // Whether it took: focusing is false for an element that cannot take the focus, and the rest are true for an element that lives
        Bool UIElement_SetFlag(UInt64 element, Int32 flag, Bool set)
        {
            const UIElement l_Element = UIElement::FromHandle(element);
            switch (static_cast<ElementFlag>(flag))
            {
                case ElementFlag::Visible:
                {
                    l_Element.SetVisible(set);

                    return l_Element.IsValid();
                }
                case ElementFlag::Focused:
                {
                    if (set)
                    {
                        return l_Element.Focus();
                    }

                    l_Element.Blur();

                    return l_Element.IsValid();
                }
                case ElementFlag::Checked:
                {
                    l_Element.SetChecked(set);

                    return l_Element.IsValid();
                }
            }

            return false;
        }

        Bool UIElement_HasClass(UInt64 element, Utf8 name)
        {
            return UIElement::FromHandle(element).HasClass(ToStringView(name));
        }

        Void UIElement_SetClass(UInt64 element, Utf8 name, Bool set)
        {
            UIElement::FromHandle(element).SetClass(ToStringView(name), set);
        }

        Utf8 UIElement_GetAttribute(UInt64 element, Utf8 name, Bool* found)
        {
            std::optional<std::string> l_Value = UIElement::FromHandle(element).GetAttribute(ToStringView(name));
            *found = l_Value.has_value();

            return ToText(l_Value.value_or(std::string()));
        }

        Void UIElement_SetAttribute(UInt64 element, Utf8 name, Utf8 value)
        {
            UIElement::FromHandle(element).SetAttribute(ToStringView(name), ToStringView(value));
        }

        Void UIElement_RemoveAttribute(UInt64 element, Utf8 name)
        {
            UIElement::FromHandle(element).RemoveAttribute(ToStringView(name));
        }

        Utf8 UIElement_GetStyle(UInt64 element, Utf8 property)
        {
            return ToText(UIElement::FromHandle(element).GetStyle(ToStringView(property)));
        }

        Bool UIElement_SetStyle(UInt64 element, Utf8 property, Utf8 value)
        {
            return UIElement::FromHandle(element).SetStyle(ToStringView(property), ToStringView(value));
        }

        Void UIElement_RemoveStyle(UInt64 element, Utf8 property)
        {
            UIElement::FromHandle(element).RemoveStyle(ToStringView(property));
        }
    }
}