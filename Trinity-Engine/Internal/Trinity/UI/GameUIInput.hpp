#pragma once

#include "Trinity/Input/KeyCodes.hpp"

#include <RmlUi/Core/Input.h>

namespace Rml
{
    class Element;
}

// What the game UI hands RmlUi of the keyboard: keys by RmlUi's names, the modifiers held, and which elements take the keyboard while they have the focus
namespace Trinity::GameUIInput
{
    // KI_UNKNOWN for a key RmlUi has no name for
    [[nodiscard]] Rml::Input::KeyIdentifier ToKeyIdentifier(KeyCode key);
    // Ctrl, Shift, Alt and Super as Input reads them now, as RmlUi's KeyModifier bits
    [[nodiscard]] int GetKeyModifiers();
    // A text input, of type text, password or none given, or a textarea
    [[nodiscard]] bool IsTextField(Rml::Element& element);
}