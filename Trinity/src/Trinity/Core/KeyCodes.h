#pragma once

namespace Trinity
{
    using KeyCode = uint16_t;

    namespace Key
    {
        enum : KeyCode
        {
            Space                    = 32,
            Apostrophe               = 39,  /* ' */
            Comma                    = 44,  /* , */
            Minus                    = 45,  /* - */
            Period                   = 46,  /* . */
            Slash                    = 47,  /* / */    
        
            D0                       = 48,
            D1                       = 49,
            D2                       = 50,
            D3                       = 51,
            D4                       = 52,
            D5                       = 53,
            D6                       = 54,
            D7                       = 55,
            D8                       = 56,
            D9                       = 57,
        
            Semicolon                = 59,  /* ; */
            Equal                    = 61,  /* = */
        
            A                        = 65,
            B                        = 66,
            C                        = 67,
            D                        = 68,
            E                        = 69,
            F                        = 70,
            G                        = 71,
            H                        = 72,
            I                        = 73,
            J                        = 74,
            K                        = 75,
            L                        = 76,
            M                        = 77,
            N                        = 78,
            O                        = 79,
            P                        = 80,
            Q                        = 81,
            R                        = 82,
            S                        = 83,
            T                        = 84,
            U                        = 85,
            V                        = 86,
            W                        = 87,
            X                        = 88,
            Y                        = 89,
            Z                        = 90,
        
            LeftBracket              = 91,  /* [ */
            Backslash                = 92,  /* \ */
            RightBracket             = 93,  /* ] */
            GraveAccent              = 96,  /* ` */

            World1                   = 161, /* non-US #1 */
            World2                   = 162, /* non-US #2 */

            Escape                   = 256,
            Enter                    = 257,
            Tab                      = 258,
            Backspace                = 259,
            Insert                   = 260,
            Delete                   = 261,
            Right                    = 262,
            Left                     = 263,
            Down                     = 264,
            Up                       = 265,
            PageUp                   = 266,
            PageDown                 = 267,
            Home                     = 268,
            End                      = 269,
            CapsLock                 = 280,
            ScrollLock               = 281,
            NumLock                  = 282,
            PrintScreen              = 283,
            Pause                    = 284,
        
            F1                       = 290,
            F2                       = 291,
            F3                       = 292,
            F4                       = 293,
            F5                       = 294,
            F6                       = 295,
            F7                       = 296,
            F8                       = 297,
            F9                       = 298,
            F10                      = 299,
            F11                      = 300,
            F12                      = 301,
            F13                      = 302,
            F14                      = 303,
            F15                      = 304,
            F16                      = 305,
            F17                      = 306,
            F18                      = 307,
            F19                      = 308,
            F20                      = 309,
            F21                      = 310,
            F22                      = 311,
            F23                      = 312,
            F24                      = 313,
            F25                      = 314,
        
            KP0                      = 320,
            KP1                      = 321,
            KP2                      = 322,
            KP3                      = 323,
            KP4                      = 324,
            KP5                      = 325,
            KP6                      = 326,
            KP7                      = 327,
            KP8                      = 328,
            KP9                      = 329,
            KPDecimal                = 330,
            KPDivide                 = 331,
            KPMultiply               = 332,
            KPSubtract               = 333,
            KPAdd                    = 334,
            KPEnter                  = 335,
            KPEqual                  = 336,
        
            LeftShift                = 340,
            LeftControl              = 341,
            LeftAlt                  = 342,
            LeftSuper                = 343,
            RightShift               = 344,
            RightControl             = 345,
            RightAlt                 = 346,
            RightSuper               = 347,
            Menu                     = 348,
        };
    }

    //inline std::ostream& operator<<(std::ostream& os, KeyCode keyCode)
    //{
    //    os << static_cast<int32_t>(keyCode);
    //    return os;
    //}
}

// ----------------------------- LEGACY CODE ----------------------------------
//#define TR_KEY_SPACE              ::Trinity::Key::Space
//#define TR_KEY_APOSTROPHE         ::Trinity::Key::Apostrophe  /* ' */
//#define TR_KEY_COMMA              ::Trinity::Key::Comma  /* , */
//#define TR_KEY_MINUS              ::Trinity::Key::Minus  /* - */
//#define TR_KEY_PERIOD             ::Trinity::Key::Period  /* . */
//#define TR_KEY_SLASH              ::Trinity::Key::Slash  /* / */
//
//#define TR_KEY_0                  ::Trinity::Key::D0
//#define TR_KEY_1                  ::Trinity::Key::D1
//#define TR_KEY_2                  ::Trinity::Key::D2
//#define TR_KEY_3                  ::Trinity::Key::D3
//#define TR_KEY_4                  ::Trinity::Key::D4
//#define TR_KEY_5                  ::Trinity::Key::D5
//#define TR_KEY_6                  ::Trinity::Key::D6
//#define TR_KEY_7                  ::Trinity::Key::D7
//#define TR_KEY_8                  ::Trinity::Key::D8
//#define TR_KEY_9                  ::Trinity::Key::D9
//
//#define TR_KEY_SEMICOLON          ::Trinity::Key::Semicolon  /* ; */
//#define TR_KEY_EQUAL              ::Trinity::Key::Equal  /* = */
//
//#define TR_KEY_A                  ::Trinity::Key::A
//#define TR_KEY_B                  ::Trinity::Key::B
//#define TR_KEY_C                  ::Trinity::Key::C
//#define TR_KEY_D                  ::Trinity::Key::D
//#define TR_KEY_E                  ::Trinity::Key::E
//#define TR_KEY_F                  ::Trinity::Key::F
//#define TR_KEY_G                  ::Trinity::Key::G
//#define TR_KEY_H                  ::Trinity::Key::H
//#define TR_KEY_I                  ::Trinity::Key::I
//#define TR_KEY_J                  ::Trinity::Key::J
//#define TR_KEY_K                  ::Trinity::Key::K
//#define TR_KEY_L                  ::Trinity::Key::L
//#define TR_KEY_M                  ::Trinity::Key::M
//#define TR_KEY_N                  ::Trinity::Key::N
//#define TR_KEY_O                  ::Trinity::Key::O
//#define TR_KEY_P                  ::Trinity::Key::P
//#define TR_KEY_Q                  ::Trinity::Key::Q
//#define TR_KEY_R                  ::Trinity::Key::R
//#define TR_KEY_S                  ::Trinity::Key::S
//#define TR_KEY_T                  ::Trinity::Key::T
//#define TR_KEY_U                  ::Trinity::Key::U
//#define TR_KEY_V                  ::Trinity::Key::V
//#define TR_KEY_W                  ::Trinity::Key::W
//#define TR_KEY_X                  ::Trinity::Key::X
//#define TR_KEY_Y                  ::Trinity::Key::Y
//#define TR_KEY_Z                  ::Trinity::Key::Z
//
//#define TR_KEY_LEFT_BRACKET       ::Trinity::Key::LeftBracket  /* [ */
//#define TR_KEY_BACKSLASH          ::Trinity::Key::Backslash  /* \ */
//#define TR_KEY_RIGHT_BRACKET      ::Trinity::Key::RightBracket  /* ] */
//#define TR_KEY_GRAVE_ACCENT       ::Trinity::Key::GraveAccent  /* ` */
//
//#define TR_KEY_WORLD_1            ::Trinity::Key::World1 /* non-US #1 */
//#define TR_KEY_WORLD_2            ::Trinity::Key::World2 /* non-US #2 */
//
//#define TR_KEY_ESCAPE             ::Trinity::Key::Escape
//#define TR_KEY_ENTER              ::Trinity::Key::Enter
//#define TR_KEY_TAB                ::Trinity::Key::Tab
//#define TR_KEY_BACKSPACE          ::Trinity::Key::Backspace
//#define TR_KEY_INSERT             ::Trinity::Key::Insert
//#define TR_KEY_DELETE             ::Trinity::Key::Delete
//#define TR_KEY_RIGHT              ::Trinity::Key::Right
//#define TR_KEY_LEFT               ::Trinity::Key::Left
//#define TR_KEY_DOWN               ::Trinity::Key::Down
//#define TR_KEY_UP                 ::Trinity::Key::Up
//#define TR_KEY_PAGE_UP            ::Trinity::Key::PageUp
//#define TR_KEY_PAGE_DOWN          ::Trinity::Key::PageDown
//#define TR_KEY_HOME               ::Trinity::Key::Home
//#define TR_KEY_END                ::Trinity::Key::End
//#define TR_KEY_CAPS_LOCK          ::Trinity::Key::CapsLock
//#define TR_KEY_SCROLL_LOCK        ::Trinity::Key::ScrollLock
//#define TR_KEY_NUM_LOCK           ::Trinity::Key::NumLock
//#define TR_KEY_PRINT_SCREEN       ::Trinity::Key::PrintScreen
//#define TR_KEY_PAUSE              ::Trinity::Key::Pause
//
//#define TR_KEY_F1                 ::Trinity::Key::F1
//#define TR_KEY_F2                 ::Trinity::Key::F2
//#define TR_KEY_F3                 ::Trinity::Key::F3
//#define TR_KEY_F4                 ::Trinity::Key::F4
//#define TR_KEY_F5                 ::Trinity::Key::F5
//#define TR_KEY_F6                 ::Trinity::Key::F6
//#define TR_KEY_F7                 ::Trinity::Key::F7
//#define TR_KEY_F8                 ::Trinity::Key::F8
//#define TR_KEY_F9                 ::Trinity::Key::F9
//#define TR_KEY_F10                ::Trinity::Key::F10
//#define TR_KEY_F11                ::Trinity::Key::F11
//#define TR_KEY_F12                ::Trinity::Key::F12
//#define TR_KEY_F13                ::Trinity::Key::F13
//#define TR_KEY_F14                ::Trinity::Key::F14
//#define TR_KEY_F15                ::Trinity::Key::F15
//#define TR_KEY_F16                ::Trinity::Key::F16
//#define TR_KEY_F17                ::Trinity::Key::F17
//#define TR_KEY_F18                ::Trinity::Key::F18
//#define TR_KEY_F19                ::Trinity::Key::F19
//#define TR_KEY_F20                ::Trinity::Key::F20
//#define TR_KEY_F21                ::Trinity::Key::F21
//#define TR_KEY_F22                ::Trinity::Key::F22
//#define TR_KEY_F23                ::Trinity::Key::F23
//#define TR_KEY_F24                ::Trinity::Key::F24
//#define TR_KEY_F25                ::Trinity::Key::F25
//
//#define TR_KEY_KP_0               ::Trinity::Key::KP0
//#define TR_KEY_KP_1               ::Trinity::Key::KP1
//#define TR_KEY_KP_2               ::Trinity::Key::KP2
//#define TR_KEY_KP_3               ::Trinity::Key::KP3
//#define TR_KEY_KP_4               ::Trinity::Key::KP4
//#define TR_KEY_KP_5               ::Trinity::Key::KP5
//#define TR_KEY_KP_6               ::Trinity::Key::KP6
//#define TR_KEY_KP_7               ::Trinity::Key::KP7
//#define TR_KEY_KP_8               ::Trinity::Key::KP8
//#define TR_KEY_KP_9               ::Trinity::Key::KP9
//#define TR_KEY_KP_DECIMAL         ::Trinity::Key::KPDecimal
//#define TR_KEY_KP_DIVIDE          ::Trinity::Key::KPDivide
//#define TR_KEY_KP_MULTIPLY        ::Trinity::Key::KPMultiply
//#define TR_KEY_KP_SUBTRACT        ::Trinity::Key::KPSubtract
//#define TR_KEY_KP_ADD             ::Trinity::Key::KPAdd
//#define TR_KEY_KP_ENTER           ::Trinity::Key::KPEnter
//#define TR_KEY_KP_EQUAL           ::Trinity::Key::KPEqual
//
//#define TR_KEY_LEFT_SHIFT         ::Trinity::Key::LeftShift
//#define TR_KEY_LEFT_CONTROL       ::Trinity::Key::LeftControl
//#define TR_KEY_LEFT_ALT           ::Trinity::Key::LeftAlt
//#define TR_KEY_LEFT_SUPER         ::Trinity::Key::LeftSuper
//#define TR_KEY_RIGHT_SHIFT        ::Trinity::Key::RightShift
//#define TR_KEY_RIGHT_CONTROL      ::Trinity::Key::RightControl
//#define TR_KEY_RIGHT_ALT          ::Trinity::Key::RightAlt
//#define TR_KEY_RIGHT_SUPER        ::Trinity::Key::RightSuper
//#define TR_KEY_MENU               ::Trinity::Key::Menu