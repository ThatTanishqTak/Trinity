#include "Trinity/Scripting/Interop.hpp"

#include "Trinity/Scripting/ScriptRuntime.hpp"
#include "Trinity/UI/GameUIAccess.hpp"
#include "Trinity/UI/GameUIData.hpp"
#include "Trinity/UI/UIElement.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

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

            [[nodiscard]] GameUIData* GetData()
            {
                GameUI* l_UI = GameUIAccess::GetUI();

                return l_UI != nullptr ? &GameUIAccess::GetData(*l_UI) : nullptr;
            }

            // A type that is none of UIDataType's reads as none
            [[nodiscard]] std::optional<UIDataScalar> ToScalar(Int32 type, Int32 integer, Float real, Utf8 text)
            {
                if (type < 0 || type >= static_cast<Int32>(c_UIDataTypeCount))
                {
                    return std::nullopt;
                }

                UIDataScalar l_Scalar;
                l_Scalar.Type = static_cast<UIDataType>(type);
                l_Scalar.Int = integer;
                l_Scalar.Float = real;
                l_Scalar.Bool = integer != 0;
                l_Scalar.Text = ToStringView(text);

                return l_Scalar;
            }

            // Into C#'s fields when it is of the type asked for, the text kept until the next text is asked for
            Bool FromScalar(const std::optional<UIDataScalar>& scalar, Int32 type, Int32* integer, Float* real, Utf8* text)
            {
                if (!scalar || static_cast<Int32>(scalar->Type) != type)
                {
                    return false;
                }

                *integer = scalar->Type == UIDataType::Bool ? (scalar->Bool ? 1 : 0) : scalar->Int;
                *real = scalar->Float;
                *text = scalar->Type == UIDataType::String ? ToText(std::string(scalar->Text)) : Utf8{ nullptr, 0 };

                return true;
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

        UInt64 UIDocument_GetModel(EntityId entity, Utf8 name)
        {
            return GetDocument(entity).GetModel(ToStringView(name)).GetHandle();
        }

        Bool UIDataModel_IsValid(UInt64 model)
        {
            const GameUIData* l_Data = GetData();

            return l_Data != nullptr && l_Data->IsModel(model);
        }

        UInt64 UIDataModel_BindValue(UInt64 model, Utf8 name, Int32 type, Int32 integer, Float real, Utf8 text)
        {
            GameUIData* l_Data = GetData();
            const std::optional<UIDataScalar> l_Value = ToScalar(type, integer, real, text);

            return l_Data != nullptr && l_Value ? l_Data->BindValue(model, ToStringView(name), *l_Value) : 0;
        }

        // A field of a type that is none of UIDataType's refuses the list
        UInt64 UIDataModel_BindList(UInt64 model, Utf8 name, const Utf8 * fieldNames, const Int32 * fieldTypes, Int32 fieldCount)
        {
            GameUIData* l_Data = GetData();
            if (l_Data == nullptr || fieldCount < 0)
            {
                return 0;
            }

            std::vector<UIDataField> l_Fields;
            for (Int32 it_Field = 0; it_Field < fieldCount; ++it_Field)
            {
                if (fieldTypes[it_Field] < 0 || fieldTypes[it_Field] >= static_cast<Int32>(c_UIDataTypeCount))
                {
                    return 0;
                }

                l_Fields.push_back({ ToStringView(fieldNames[it_Field]), static_cast<UIDataType>(fieldTypes[it_Field]) });
            }

            return l_Data->BindList(model, ToStringView(name), l_Fields);
        }

        // -1 for none
        Int32 UIDataValue_GetType(UInt64 value)
        {
            const GameUIData* l_Data = GetData();
            const std::optional<UIDataType> l_Type = l_Data != nullptr ? l_Data->GetType(value) : std::nullopt;

            return l_Type ? static_cast<Int32>(*l_Type) : -1;
        }

        Void UIDataValue_Set(UInt64 value, Int32 type, Int32 integer, Float real, Utf8 text)
        {
            GameUIData* l_Data = GetData();
            if (const std::optional<UIDataScalar> l_Value = ToScalar(type, integer, real, text); l_Data != nullptr && l_Value)
            {
                l_Data->Set(value, *l_Value);
            }
        }

        Bool UIDataValue_Get(UInt64 value, Int32 type, Int32 * integer, Float * real, Utf8 * text)
        {
            const GameUIData* l_Data = GetData();

            return l_Data != nullptr && FromScalar(l_Data->Get(value), type, integer, real, text);
        }

        Bool UIDataList_IsValid(UInt64 list)
        {
            const GameUIData* l_Data = GetData();

            return l_Data != nullptr && l_Data->IsList(list);
        }

        Int32 UIDataList_GetSize(UInt64 list)
        {
            const GameUIData* l_Data = GetData();

            return l_Data != nullptr ? static_cast<Int32>(l_Data->GetSize(list)) : 0;
        }

        Void UIDataList_Resize(UInt64 list, Int32 rows)
        {
            if (GameUIData* l_Data = GetData(); l_Data != nullptr && rows >= 0)
            {
                l_Data->Resize(list, static_cast<std::uint32_t>(rows));
            }
        }

        Void UIDataList_Set(UInt64 list, Int32 row, Utf8 field, Int32 type, Int32 integer, Float real, Utf8 text)
        {
            GameUIData* l_Data = GetData();
            if (const std::optional<UIDataScalar> l_Value = ToScalar(type, integer, real, text); l_Data != nullptr && l_Value && row >= 0)
            {
                l_Data->Set(list, static_cast<std::uint32_t>(row), ToStringView(field), *l_Value);
            }
        }

        Bool UIDataList_Get(UInt64 list, Int32 row, Utf8 field, Int32 type, Int32 * integer, Float * real, Utf8 * text)
        {
            const GameUIData* l_Data = GetData();

            return l_Data != nullptr && row >= 0 && FromScalar(l_Data->Get(list, static_cast<std::uint32_t>(row), ToStringView(field)), type, integer, real, text);
        }
    }
}