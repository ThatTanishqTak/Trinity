#include "Trinity/UI/UIElement.hpp"

#include "Trinity/Core/Memory.hpp"
#include "Trinity/Scripting/ScriptRuntime.hpp"
#include "Trinity/UI/GameUIAccess.hpp"
#include "Trinity/UI/GameUIData.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/Traits.h>
#include <RmlUi/Core/Variant.h>

namespace Trinity
{
    namespace
    {
        // The element the handle names while it lives, or null
        [[nodiscard]] Rml::Element* Reach(UIElement element)
        {
            GameUI* l_UI = element.GetHandle() != 0 ? GameUIAccess::GetUI() : nullptr;

            return l_UI != nullptr ? GameUIAccess::Resolve(*l_UI, element) : nullptr;
        }

        // What RmlUi keeps of a change is counted under GameUI, while text it gives back is the caller's
        template<typename F>
        decltype(auto) InUI(F&& work)
        {
            const MemoryTagScope l_Scope(MemoryTag::GameUI);

            return work();
        }

        void AppendText(const Rml::Element& element, std::string& text)
        {
            if (const Rml::ElementText* l_Text = rmlui_dynamic_cast<const Rml::ElementText*>(&element))
            {
                text.append(l_Text->GetText());
            }

            for (int it_Child = 0; it_Child < element.GetNumChildren(); ++it_Child)
            {
                AppendText(*element.GetChild(it_Child), text);
            }
        }
    }

    std::string_view ToString(UIEventType type)
    {
        switch (type)
        {
            case UIEventType::Click:
            {
                return "Click";
            }
            case UIEventType::Change:
            {
                return "Change";
            }
            case UIEventType::Submit:
            {
                return "Submit";
            }
            case UIEventType::PointerEnter:
            {
                return "PointerEnter";
            }
            case UIEventType::PointerLeave:
            {
                return "PointerLeave";
            }
            case UIEventType::Focus:
            {
                return "Focus";
            }
            case UIEventType::Blur:
            {
                return "Blur";
            }
        }

        return "Unknown";
    }

    bool UIElement::IsValid() const
    {
        return Reach(*this) != nullptr;
    }

    std::string UIElement::GetID() const
    {
        const Rml::Element* l_Element = Reach(*this);

        return l_Element != nullptr ? std::string(l_Element->GetId()) : std::string();
    }

    std::string UIElement::GetText() const
    {
        std::string l_Text;
        if (const Rml::Element* l_Element = Reach(*this))
        {
            AppendText(*l_Element, l_Text);
        }

        return l_Text;
    }

    void UIElement::SetText(std::string_view text) const
    {
        if (Rml::Element* l_Element = Reach(*this))
        {
            InUI([&] { l_Element->SetInnerRML(Rml::StringUtilities::EncodeRml(Rml::String(text))); });
        }
    }

    std::string UIElement::GetInnerRML() const
    {
        const Rml::Element* l_Element = Reach(*this);

        return l_Element != nullptr ? std::string(l_Element->GetInnerRML()) : std::string();
    }

    void UIElement::SetInnerRML(std::string_view rml) const
    {
        if (Rml::Element* l_Element = Reach(*this))
        {
            InUI([&] { l_Element->SetInnerRML(Rml::String(rml)); });
        }
    }

    std::string UIElement::GetValue() const
    {
        const Rml::ElementFormControl* l_Control = rmlui_dynamic_cast<const Rml::ElementFormControl*>(Reach(*this));

        return l_Control != nullptr ? std::string(l_Control->GetValue()) : std::string();
    }

    void UIElement::SetValue(std::string_view value) const
    {
        if (Rml::ElementFormControl* l_Control = rmlui_dynamic_cast<Rml::ElementFormControl*>(Reach(*this)))
        {
            InUI([&] { l_Control->SetValue(Rml::String(value)); });
        }
    }

    bool UIElement::IsChecked() const
    {
        const Rml::Element* l_Element = Reach(*this);

        return l_Element != nullptr && l_Element->HasAttribute("checked");
    }

    void UIElement::SetChecked(bool checked) const
    {
        if (Rml::Element* l_Element = Reach(*this); l_Element != nullptr && l_Element->HasAttribute("checked") != checked)
        {
            InUI([&] { checked ? l_Element->SetAttribute("checked", Rml::String()) : l_Element->RemoveAttribute("checked"); });
        }
    }

    bool UIElement::HasClass(std::string_view name) const
    {
        const Rml::Element* l_Element = Reach(*this);

        return l_Element != nullptr && l_Element->IsClassSet(Rml::String(name));
    }

    void UIElement::SetClass(std::string_view name, bool set) const
    {
        if (Rml::Element* l_Element = Reach(*this))
        {
            InUI([&] { l_Element->SetClass(Rml::String(name), set); });
        }
    }

    std::optional<std::string> UIElement::GetAttribute(std::string_view name) const
    {
        const Rml::Element* l_Element = Reach(*this);
        const Rml::Variant* l_Value = l_Element != nullptr ? l_Element->GetAttribute(Rml::String(name)) : nullptr;
        if (l_Value == nullptr)
        {
            return std::nullopt;
        }

        return std::string(l_Value->Get<Rml::String>());
    }

    void UIElement::SetAttribute(std::string_view name, std::string_view value) const
    {
        if (Rml::Element* l_Element = Reach(*this))
        {
            InUI([&] { l_Element->SetAttribute(Rml::String(name), Rml::String(value)); });
        }
    }

    void UIElement::RemoveAttribute(std::string_view name) const
    {
        if (Rml::Element* l_Element = Reach(*this))
        {
            InUI([&] { l_Element->RemoveAttribute(Rml::String(name)); });
        }
    }

    bool UIElement::SetStyle(std::string_view property, std::string_view value) const
    {
        Rml::Element* l_Element = Reach(*this);

        return l_Element != nullptr && InUI([&] { return l_Element->SetProperty(Rml::String(property), Rml::String(value)); });
    }

    void UIElement::RemoveStyle(std::string_view property) const
    {
        if (Rml::Element* l_Element = Reach(*this))
        {
            InUI([&] { l_Element->RemoveProperty(Rml::String(property)); });
        }
    }

    std::string UIElement::GetStyle(std::string_view property) const
    {
        Rml::Element* l_Element = Reach(*this);
        const Rml::Property* l_Property = l_Element != nullptr ? InUI([&] { return l_Element->GetProperty(Rml::String(property)); }) : nullptr;

        return l_Property != nullptr ? std::string(l_Property->ToString()) : std::string();
    }

    bool UIElement::IsVisible() const
    {
        const Rml::Element* l_Element = Reach(*this);

        return l_Element != nullptr && l_Element->IsVisible(true);
    }

    void UIElement::SetVisible(bool visible) const
    {
        Rml::Element* l_Element = Reach(*this);
        if (l_Element == nullptr)
        {
            return;
        }

        InUI([&]
        {
            if (visible)
            {
                l_Element->RemoveProperty("display");
            }
            else
            {
                l_Element->SetProperty("display", "none");
            }
        });
    }

    bool UIElement::IsFocused() const
    {
        Rml::Element* l_Element = Reach(*this);
        Rml::Context* l_Context = l_Element != nullptr ? l_Element->GetContext() : nullptr;

        return l_Context != nullptr && l_Context->GetFocusElement() == l_Element;
    }

    bool UIElement::Focus() const
    {
        Rml::Element* l_Element = Reach(*this);

        return l_Element != nullptr && InUI([&] { return l_Element->Focus(true); });
    }

    // RmlUi's own Blur gives the focus to the element around it, which then has it, so the document's body takes it, which stands for none
    void UIElement::Blur() const
    {
        Rml::Element* l_Element = Reach(*this);
        Rml::Context* l_Context = l_Element != nullptr ? l_Element->GetContext() : nullptr;
        Rml::Element* l_Body = l_Element != nullptr ? l_Element->GetOwnerDocument() : nullptr;
        if (l_Context != nullptr && l_Body != nullptr && l_Context->GetFocusElement() == l_Element)
        {
            InUI([&] { l_Body->Focus(); });
        }
    }

    bool UIDocument::IsValid() const
    {
        GameUI* l_UI = m_Runtime != nullptr ? GameUIAccess::GetUI() : nullptr;

        return l_UI != nullptr && l_UI->GetSceneDocument(m_Runtime->GetScene(), m_Entity) != GameUI::c_NoDocument;
    }

    UIElement UIDocument::GetElement(std::string_view id) const
    {
        GameUI* l_UI = m_Runtime != nullptr && !id.empty() ? GameUIAccess::GetUI() : nullptr;
        Rml::Element* l_Body = l_UI != nullptr ? GameUIAccess::GetBody(*l_UI, l_UI->GetSceneDocument(m_Runtime->GetScene(), m_Entity)) : nullptr;

        return l_Body != nullptr ? GameUIAccess::Wrap(*l_UI, InUI([&] { return l_Body->GetElementById(Rml::String(id)); })) : UIElement();
    }

    UIElement UIDocument::Query(std::string_view selector) const
    {
        GameUI* l_UI = m_Runtime != nullptr && !selector.empty() ? GameUIAccess::GetUI() : nullptr;
        Rml::Element* l_Body = l_UI != nullptr ? GameUIAccess::GetBody(*l_UI, l_UI->GetSceneDocument(m_Runtime->GetScene(), m_Entity)) : nullptr;

        return l_Body != nullptr ? GameUIAccess::Wrap(*l_UI, InUI([&] { return l_Body->QuerySelector(Rml::String(selector)); })) : UIElement();
    }

    UIElement UIDocument::GetFocus() const
    {
        GameUI* l_UI = m_Runtime != nullptr ? GameUIAccess::GetUI() : nullptr;

        return l_UI != nullptr ? GameUIAccess::Wrap(*l_UI, GameUIAccess::GetFocus(*l_UI, l_UI->GetSceneDocument(m_Runtime->GetScene(), m_Entity))) : UIElement();
    }

    UIDataModel UIDocument::GetModel(std::string_view name) const
    {
        GameUI* l_UI = m_Runtime != nullptr ? GameUIAccess::GetUI() : nullptr;

        return UIDataModel::FromHandle(l_UI != nullptr ? GameUIAccess::GetData(*l_UI).GetModel(m_Runtime->GetScene(), m_Entity, name) : 0);
    }

    UIEventHandle UIDocument::AddHandler(std::string_view element, UIEventType type, const UIHandlerCalls& calls, void* handler) const
    {
        if (m_Runtime == nullptr)
        {
            calls.Free(handler);

            return {};
        }

        return { m_Runtime->AddUIHandler(m_Entity, element, type, calls, handler) };
    }

    bool UIDocument::Off(UIEventHandle handler) const
    {
        return m_Runtime != nullptr && m_Runtime->RemoveUIHandler(handler.ID);
    }
}