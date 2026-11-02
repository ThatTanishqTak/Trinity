#include "Trinity/UI/GameUIInput.hpp"

#include "Trinity/Input/Input.hpp"

#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <array>
#include <utility>

namespace Trinity::GameUIInput
{
    namespace
    {
        // The keys outside the runs of letters, digits, the keypad's digits and the function keys
        constexpr std::array<std::pair<KeyCode, Rml::Input::KeyIdentifier>, 49> c_Keys{ {
            { KeyCode::TR_SPACE, Rml::Input::KI_SPACE },
            { KeyCode::TR_APOSTROPHE, Rml::Input::KI_OEM_7 },
            { KeyCode::TR_COMMA, Rml::Input::KI_OEM_COMMA },
            { KeyCode::TR_MINUS, Rml::Input::KI_OEM_MINUS },
            { KeyCode::TR_PERIOD, Rml::Input::KI_OEM_PERIOD },
            { KeyCode::TR_SLASH, Rml::Input::KI_OEM_2 },
            { KeyCode::TR_SEMICOLON, Rml::Input::KI_OEM_1 },
            { KeyCode::TR_EQUAL, Rml::Input::KI_OEM_PLUS },
            { KeyCode::TR_LEFT_BRACKET, Rml::Input::KI_OEM_4 },
            { KeyCode::TR_BACKSLASH, Rml::Input::KI_OEM_5 },
            { KeyCode::TR_RIGHT_BRACKET, Rml::Input::KI_OEM_6 },
            { KeyCode::TR_GRAVE_ACCENT, Rml::Input::KI_OEM_3 },
            { KeyCode::TR_WORLD_2, Rml::Input::KI_OEM_102 },
            { KeyCode::TR_ESCAPE, Rml::Input::KI_ESCAPE },
            { KeyCode::TR_ENTER, Rml::Input::KI_RETURN },
            { KeyCode::TR_TAB, Rml::Input::KI_TAB },
            { KeyCode::TR_BACKSPACE, Rml::Input::KI_BACK },
            { KeyCode::TR_INSERT, Rml::Input::KI_INSERT },
            { KeyCode::TR_DELETE, Rml::Input::KI_DELETE },
            { KeyCode::TR_RIGHT, Rml::Input::KI_RIGHT },
            { KeyCode::TR_LEFT, Rml::Input::KI_LEFT },
            { KeyCode::TR_DOWN, Rml::Input::KI_DOWN },
            { KeyCode::TR_UP, Rml::Input::KI_UP },
            { KeyCode::TR_PAGE_UP, Rml::Input::KI_PRIOR },
            { KeyCode::TR_PAGE_DOWN, Rml::Input::KI_NEXT },
            { KeyCode::TR_HOME, Rml::Input::KI_HOME },
            { KeyCode::TR_END, Rml::Input::KI_END },
            { KeyCode::TR_CAPS_LOCK, Rml::Input::KI_CAPITAL },
            { KeyCode::TR_SCROLL_LOCK, Rml::Input::KI_SCROLL },
            { KeyCode::TR_NUM_LOCK, Rml::Input::KI_NUMLOCK },
            { KeyCode::TR_PRINT_SCREEN, Rml::Input::KI_SNAPSHOT },
            { KeyCode::TR_PAUSE, Rml::Input::KI_PAUSE },
            { KeyCode::TR_KP_DECIMAL, Rml::Input::KI_DECIMAL },
            { KeyCode::TR_KP_DIVIDE, Rml::Input::KI_DIVIDE },
            { KeyCode::TR_KP_MULTIPLY, Rml::Input::KI_MULTIPLY },
            { KeyCode::TR_KP_SUBTRACT, Rml::Input::KI_SUBTRACT },
            { KeyCode::TR_KP_ADD, Rml::Input::KI_ADD },
            { KeyCode::TR_KP_ENTER, Rml::Input::KI_NUMPADENTER },
            { KeyCode::TR_LEFT_SHIFT, Rml::Input::KI_LSHIFT },
            { KeyCode::TR_RIGHT_SHIFT, Rml::Input::KI_RSHIFT },
            { KeyCode::TR_LEFT_CONTROL, Rml::Input::KI_LCONTROL },
            { KeyCode::TR_RIGHT_CONTROL, Rml::Input::KI_RCONTROL },
            { KeyCode::TR_LEFT_ALT, Rml::Input::KI_LMENU },
            { KeyCode::TR_RIGHT_ALT, Rml::Input::KI_RMENU },
            { KeyCode::TR_LEFT_SUPER, Rml::Input::KI_LMETA },
            { KeyCode::TR_RIGHT_SUPER, Rml::Input::KI_RMETA },
            { KeyCode::TR_MENU, Rml::Input::KI_APPS },
            { KeyCode::TR_APP_BACK, Rml::Input::KI_BROWSER_BACK },
            { KeyCode::TR_APP_FORWARD, Rml::Input::KI_BROWSER_FORWARD }
        } };

        // A run of keys whose codes and RmlUi's names go up together
        Rml::Input::KeyIdentifier FromRun(KeyCode key, KeyCode first, KeyCode last, Rml::Input::KeyIdentifier firstIdentifier)
        {
            const auto l_Code = std::to_underlying(key);
            if (l_Code < std::to_underlying(first) || l_Code > std::to_underlying(last))
            {
                return Rml::Input::KI_UNKNOWN;
            }

            return static_cast<Rml::Input::KeyIdentifier>(firstIdentifier + (l_Code - std::to_underlying(first)));
        }
    }

    Rml::Input::KeyIdentifier ToKeyIdentifier(KeyCode key)
    {
        using namespace Rml::Input;

        if (const KeyIdentifier l_Letter = FromRun(key, KeyCode::TR_A, KeyCode::TR_Z, KI_A); l_Letter != KI_UNKNOWN)
        {
            return l_Letter;
        }

        if (const KeyIdentifier l_Digit = FromRun(key, KeyCode::TR_D0, KeyCode::TR_D9, KI_0); l_Digit != KI_UNKNOWN)
        {
            return l_Digit;
        }

        if (const KeyIdentifier l_Pad = FromRun(key, KeyCode::TR_KP0, KeyCode::TR_KP9, KI_NUMPAD0); l_Pad != KI_UNKNOWN)
        {
            return l_Pad;
        }

        if (const KeyIdentifier l_Function = FromRun(key, KeyCode::TR_F1, KeyCode::TR_F24, KI_F1); l_Function != KI_UNKNOWN)
        {
            return l_Function;
        }

        const auto a_Found = std::ranges::find(c_Keys, key, &std::pair<KeyCode, Rml::Input::KeyIdentifier>::first);

        return a_Found != c_Keys.end() ? a_Found->second : KI_UNKNOWN;
    }

    int GetKeyModifiers()
    {
        using namespace Rml::Input;

        const auto a_Either = [](KeyCode left, KeyCode right) { return Input::IsKeyPressed(left) || Input::IsKeyPressed(right); };

        int l_Modifiers = 0;
        l_Modifiers |= a_Either(KeyCode::TR_LEFT_CONTROL, KeyCode::TR_RIGHT_CONTROL) ? KM_CTRL : 0;
        l_Modifiers |= a_Either(KeyCode::TR_LEFT_SHIFT, KeyCode::TR_RIGHT_SHIFT) ? KM_SHIFT : 0;
        l_Modifiers |= a_Either(KeyCode::TR_LEFT_ALT, KeyCode::TR_RIGHT_ALT) ? KM_ALT : 0;
        l_Modifiers |= a_Either(KeyCode::TR_LEFT_SUPER, KeyCode::TR_RIGHT_SUPER) ? KM_META : 0;

        return l_Modifiers;
    }

    bool IsTextField(Rml::Element& element)
    {
        const Rml::String& l_Tag = element.GetTagName();
        if (l_Tag == "textarea")
        {
            return true;
        }

        if (l_Tag != "input")
        {
            return false;
        }

        const Rml::String l_Type = element.GetAttribute<Rml::String>("type", "text");

        return l_Type == "text" || l_Type == "password";
    }
}