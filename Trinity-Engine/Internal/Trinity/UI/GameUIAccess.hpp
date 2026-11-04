#pragma once

#include "Trinity/UI/GameUI.hpp"
#include "Trinity/UI/UIElement.hpp"

namespace Rml
{
    class Element;
}

namespace Trinity
{
    // What UIElement and UIDocument reach inside GameUI, which no public header may show, as RmlUi's types are the engine's own. Main thread only
    struct GameUIAccess
    {
        // The application's UI, or null when it has none or RmlUi did not start
        [[nodiscard]] static GameUI* GetUI();
        // The element the handle names while it lives, and otherwise null, logged once a handle
        [[nodiscard]] static Rml::Element* Resolve(GameUI& ui, UIElement element);
        // A handle to the element, the same one for as long as it lives. None for null
        [[nodiscard]] static UIElement Wrap(GameUI& ui, Rml::Element* element);
        // The document's body, or null
        [[nodiscard]] static Rml::Element* GetBody(GameUI& ui, UIDocumentID document);
        // The element of the document with the focus, but neither its body nor its context's root, which stand for none
        [[nodiscard]] static Rml::Element* GetFocus(GameUI& ui, UIDocumentID document);
    };
}