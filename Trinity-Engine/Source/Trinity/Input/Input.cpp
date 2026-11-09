#include "Trinity/Input/Input.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Platform.hpp"
#include "Trinity/Events/ApplicationEvent.hpp"
#include "Trinity/Events/GamepadEvent.hpp"
#include "Trinity/Events/KeyEvent.hpp"
#include "Trinity/Events/MouseEvent.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

namespace Trinity
{
    namespace
    {
        constexpr std::size_t c_KeyCount = std::to_underlying(KeyCode::Count);
        constexpr std::size_t c_MouseButtonCount = std::to_underlying(MouseCode::Count);
        constexpr std::size_t c_AxisCount = std::to_underlying(GamepadAxis::Count);
        // How near its rest an axis held as the scene gets the pads back has to come before the scene reads it, as far as Listen and an action's press point take a push to be
        constexpr float c_AxisHeldBack = 0.5f;

        // A pad's buttons held now and those that went down or up during this frame, each as bit 1 << GamepadButton, and its axes
        struct PadState
        {
            std::uint32_t Buttons = 0;
            std::uint32_t Pressed = 0;
            std::uint32_t Released = 0;
            std::array<float, c_AxisCount> Axes{};
        };

        // Each key, mouse button and pad held now, and those that went down or up during this frame. Where the mouse is, in the window's pixels, and where it was as the frame began, once it has moved at all, and how far the wheel has turned since
        struct DeviceState
        {
            std::array<bool, c_KeyCount> Keys{};
            std::array<bool, c_KeyCount> KeysPressed{};
            std::array<bool, c_KeyCount> KeysReleased{};
            std::array<bool, c_MouseButtonCount> MouseButtons{};
            std::array<bool, c_MouseButtonCount> MouseButtonsPressed{};
            std::array<bool, c_MouseButtonCount> MouseButtonsReleased{};
            MousePosition Mouse;
            MousePosition MouseAtFrameStart;
            glm::vec2 Scroll{ 0.0f };
            std::array<PadState, Input::c_MaxGamepads> Gamepads{};
        };

        // Whether a pad is in a slot, the rumble asked of it and its name
        struct GamepadState
        {
            bool Connected = false;
            float RumbleLow = 0.0f;
            float RumbleHigh = 0.0f;
            std::array<char, 128> Name{};
            std::size_t NameLength = 0;
        };

        // The devices as the window's events and the platform's readings left them, as the filter lets the scene have them, which the game UI reads, and as the scene has them after the UI took what it used. The filter the scene reads by and the one asked for its next frame, each pad's axes the UI and the scene are kept from, bit 1 << GamepadAxis, until they come back under c_AxisHeldBack, and those the UI took from the scene alone, until the same. This frame's key strokes and typed text the filter let in. Whether the mouse has moved at all, whether reads are the scene's or the UI's, and whether a window of the application had the focus at the last poll
        struct InputState
        {
            DeviceState Window;
            DeviceState UI;
            DeviceState Scene;
            std::array<GamepadState, Input::c_MaxGamepads> Gamepads{};
            SceneInputFilter Filter;
            SceneInputFilter NextFilter;
            std::array<std::uint32_t, Input::c_MaxGamepads> HeldBackAxes{};
            std::array<std::uint32_t, Input::c_MaxGamepads> TakenAxes{};
            std::array<KeyStroke, Input::c_MaxKeyStrokes> Strokes{};
            std::size_t StrokeCount = 0;
            std::array<char32_t, Input::c_MaxTypedCharacters> Typed{};
            std::size_t TypedCount = 0;
            bool MouseKnown = false;
            bool ReadingScene = false;
            bool ReadingUI = false;
            bool Foreground = false;
        };

        InputState s_State;

        [[nodiscard]] const DeviceState& GetDevices()
        {
            return s_State.ReadingUI ? s_State.UI : s_State.ReadingScene ? s_State.Scene : s_State.Window;
        }

        // The UI's devices and the scene's, which the filter changes alike
        [[nodiscard]] std::array<DeviceState*, 2> GetFiltered()
        {
            return { &s_State.UI, &s_State.Scene };
        }

        // A press taken in the frame it came is never seen, and a key or button seen held goes up
        void TakeHeld(bool& held, bool& pressed, bool& released)
        {
            if (pressed)
            {
                pressed = false;
                released = false;
                held = false;
            }
            else if (held)
            {
                held = false;
                released = true;
            }
        }

        template<std::size_t Count, typename Code>
        bool Read(const std::array<bool, Count>& states, Code code)
        {
            const std::size_t l_Index = std::to_underlying(code);

            return l_Index < states.size() && states[l_Index];
        }

        bool ReadButton(std::uint32_t buttons, GamepadButton button)
        {
            const std::size_t l_Index = std::to_underlying(button);

            return l_Index < std::to_underlying(GamepadButton::Count) && (buttons & (1u << l_Index)) != 0;
        }

        // NaN stops the motor too
        float ToMotorSpeed(float speed)
        {
            return speed > 0.0f ? std::min(speed, 1.0f) : 0.0f;
        }

        // Every key or button held goes up this frame
        template<std::size_t Count>
        void ReleaseAll(std::array<bool, Count>& held, std::array<bool, Count>& released)
        {
            for (std::size_t it_Index = 0; it_Index < Count; ++it_Index)
            {
                released[it_Index] = released[it_Index] || held[it_Index];
            }

            held.fill(false);
        }

        // A pad just connected holds nothing, though what went up as it last disconnected still did this frame
        void ClearGamepad(PadState& gamepad)
        {
            gamepad.Buttons = 0;
            gamepad.Axes.fill(0.0f);
        }

        // Every button held goes up this frame
        void ReleaseGamepad(PadState& gamepad)
        {
            gamepad.Released |= gamepad.Buttons;
            gamepad.Buttons = 0;
            gamepad.Axes.fill(0.0f);
        }

        // The buttons that went down, and the ones held that are up now
        void TakeButtons(PadState& gamepad, std::uint32_t down, std::uint32_t buttons)
        {
            gamepad.Pressed |= down;
            gamepad.Buttons |= down;
            gamepad.Released |= gamepad.Buttons & ~buttons;
            gamepad.Buttons &= buttons;
        }

        // The next frame's presses, releases, movement and scrolling count from here
        void BeginFrame(DeviceState& devices)
        {
            devices.KeysPressed.fill(false);
            devices.KeysReleased.fill(false);
            devices.MouseButtonsPressed.fill(false);
            devices.MouseButtonsReleased.fill(false);
            devices.MouseAtFrameStart = devices.Mouse;
            devices.Scroll = glm::vec2(0.0f);

            for (PadState& it_Gamepad : devices.Gamepads)
            {
                it_Gamepad.Pressed = 0;
                it_Gamepad.Released = 0;
            }
        }

        // As the scene gets the pads back, an axis held is kept from it, and from the UI, until it comes back under c_AxisHeldBack, and the rest read as they are, but for one the UI took from the scene
        void HoldBackAxes(std::uint32_t gamepad)
        {
            const PadState& l_Window = s_State.Window.Gamepads[gamepad];
            PadState& l_UI = s_State.UI.Gamepads[gamepad];
            PadState& l_Scene = s_State.Scene.Gamepads[gamepad];
            std::uint32_t& l_HeldBack = s_State.HeldBackAxes[gamepad];
            l_HeldBack = 0;
            for (std::size_t it_Axis = 0; it_Axis < c_AxisCount; ++it_Axis)
            {
                if (std::abs(l_Window.Axes[it_Axis]) >= c_AxisHeldBack)
                {
                    l_HeldBack |= 1u << it_Axis;
                }
                else
                {
                    l_UI.Axes[it_Axis] = l_Window.Axes[it_Axis];
                    l_Scene.Axes[it_Axis] = (s_State.TakenAxes[gamepad] & (1u << it_Axis)) != 0 ? 0.0f : l_Window.Axes[it_Axis];
                }
            }
        }

        // The filter asked for, as the frame ends. What the scene loses it lets go of, with releases it reads in its next frame, and the mouse it gets back starts where the pointer is, so it does not jump there
        void ApplySceneFilter()
        {
            const SceneInputFilter& l_Next = s_State.NextFilter;
            const SceneInputFilter& l_Filter = s_State.Filter;
            for (DeviceState* it_Devices : GetFiltered())
            {
                if (l_Filter.Keyboard && !l_Next.Keyboard)
                {
                    ReleaseAll(it_Devices->Keys, it_Devices->KeysReleased);
                }

                if (l_Filter.Mouse && !l_Next.Mouse)
                {
                    ReleaseAll(it_Devices->MouseButtons, it_Devices->MouseButtonsReleased);
                }
                else if (!l_Filter.Mouse && l_Next.Mouse)
                {
                    it_Devices->Mouse = s_State.Window.Mouse;
                    it_Devices->MouseAtFrameStart = s_State.Window.Mouse;
                }
            }

            for (std::uint32_t it_Gamepad = 0; it_Gamepad < Input::c_MaxGamepads; ++it_Gamepad)
            {
                if (l_Filter.Gamepads && !l_Next.Gamepads)
                {
                    ReleaseGamepad(s_State.UI.Gamepads[it_Gamepad]);
                    ReleaseGamepad(s_State.Scene.Gamepads[it_Gamepad]);
                }
                else if (!l_Filter.Gamepads && l_Next.Gamepads)
                {
                    HoldBackAxes(it_Gamepad);
                }
            }

            s_State.Filter = l_Next;
        }

        // Takes what the platform reports into the slots, and passes connections on to the layers as events. Readings are left out while no window of the application has the focus. The scene sees a button go down only while it has the pads, and sees every one it holds go up
        class GamepadSlots final : public Platform::GamepadListener
        {
        public:
            const std::function<void(Event&)>* OnEvent = nullptr;

            void OnGamepadConnected(std::uint32_t slot, std::string_view name) override
            {
                GamepadState& l_Gamepad = s_State.Gamepads[slot];
                l_Gamepad.Connected = true;
                l_Gamepad.RumbleLow = 0.0f;
                l_Gamepad.RumbleHigh = 0.0f;
                ClearGamepad(s_State.Window.Gamepads[slot]);
                ClearGamepad(s_State.UI.Gamepads[slot]);
                ClearGamepad(s_State.Scene.Gamepads[slot]);
                s_State.HeldBackAxes[slot] = 0;
                s_State.TakenAxes[slot] = 0;

                // Cut at a character's start, so the name stays UTF-8
                std::size_t l_Length = std::min(name.size(), l_Gamepad.Name.size());
                if (l_Length < name.size())
                {
                    while (l_Length > 0 && (static_cast<unsigned char>(name[l_Length]) & 0xC0) == 0x80)
                    {
                        --l_Length;
                    }
                }

                std::ranges::copy(name.substr(0, l_Length), l_Gamepad.Name.begin());
                l_Gamepad.NameLength = l_Length;

                const std::string_view l_Name = Input::GetGamepadName(slot);
                TR_CORE_INFO("Gamepad {} connected: {}", slot, l_Name);

                GamepadConnectedEvent l_Event(slot, l_Name);
                (*OnEvent)(l_Event);
            }

            void OnGamepadDisconnected(std::uint32_t slot) override
            {
                GamepadState& l_Gamepad = s_State.Gamepads[slot];
                ReleaseGamepad(s_State.Window.Gamepads[slot]);
                ReleaseGamepad(s_State.UI.Gamepads[slot]);
                ReleaseGamepad(s_State.Scene.Gamepads[slot]);
                l_Gamepad.Connected = false;
                l_Gamepad.RumbleLow = 0.0f;
                l_Gamepad.RumbleHigh = 0.0f;

                const std::string_view l_Name(l_Gamepad.Name.data(), l_Gamepad.NameLength);
                TR_CORE_INFO("Gamepad {} disconnected: {}", slot, l_Name);

                GamepadDisconnectedEvent l_Event(slot, l_Name);
                (*OnEvent)(l_Event);
                l_Gamepad.NameLength = 0;
            }

            void OnGamepadReading(std::uint32_t slot, const Platform::GamepadReading& reading) override
            {
                if (!s_State.Foreground)
                {
                    return;
                }

                PadState& l_Window = s_State.Window.Gamepads[slot];
                const std::uint32_t l_Down = reading.Buttons & ~l_Window.Buttons;
                TakeButtons(l_Window, l_Down, reading.Buttons);
                l_Window.Axes = reading.Axes;

                const bool l_SceneHasPads = s_State.Filter.Gamepads;
                PadState& l_UI = s_State.UI.Gamepads[slot];
                PadState& l_Scene = s_State.Scene.Gamepads[slot];
                TakeButtons(l_UI, l_SceneHasPads ? l_Down : 0, reading.Buttons);
                TakeButtons(l_Scene, l_SceneHasPads ? l_Down : 0, reading.Buttons);
                if (!l_SceneHasPads)
                {
                    return;
                }

                std::uint32_t& l_HeldBack = s_State.HeldBackAxes[slot];
                std::uint32_t& l_Taken = s_State.TakenAxes[slot];
                for (std::size_t it_Axis = 0; it_Axis < c_AxisCount; ++it_Axis)
                {
                    const std::uint32_t l_Bit = 1u << it_Axis;
                    if (std::abs(reading.Axes[it_Axis]) < c_AxisHeldBack)
                    {
                        l_HeldBack &= ~l_Bit;
                        l_Taken &= ~l_Bit;
                    }

                    l_UI.Axes[it_Axis] = (l_HeldBack & l_Bit) != 0 ? 0.0f : reading.Axes[it_Axis];
                    l_Scene.Axes[it_Axis] = ((l_HeldBack | l_Taken) & l_Bit) != 0 ? 0.0f : reading.Axes[it_Axis];
                }
            }
        };

        GamepadSlots s_GamepadSlots;
    }

    bool Input::IsKeyPressed(KeyCode key)
    {
        return Read(GetDevices().Keys, key);
    }

    bool Input::WasKeyPressed(KeyCode key)
    {
        return Read(GetDevices().KeysPressed, key);
    }

    bool Input::WasKeyReleased(KeyCode key)
    {
        return Read(GetDevices().KeysReleased, key);
    }

    bool Input::IsMouseButtonPressed(MouseCode button)
    {
        return Read(GetDevices().MouseButtons, button);
    }

    bool Input::WasMouseButtonPressed(MouseCode button)
    {
        return Read(GetDevices().MouseButtonsPressed, button);
    }

    bool Input::WasMouseButtonReleased(MouseCode button)
    {
        return Read(GetDevices().MouseButtonsReleased, button);
    }

    // The scene's, and the UI's, in its image's pixels
    MousePosition Input::GetMousePosition()
    {
        if (!s_State.ReadingScene && !s_State.ReadingUI)
        {
            return s_State.Window.Mouse;
        }

        const SceneInputFilter& l_Filter = s_State.Filter;
        const MousePosition& l_Mouse = GetDevices().Mouse;

        return { (l_Mouse.X - l_Filter.MouseOrigin.x) / l_Filter.MouseScale, (l_Mouse.Y - l_Filter.MouseOrigin.y) / l_Filter.MouseScale };
    }

    glm::vec2 Input::GetMouseDelta()
    {
        const DeviceState& l_Devices = GetDevices();
        const glm::vec2 l_Delta(l_Devices.Mouse.X - l_Devices.MouseAtFrameStart.X, l_Devices.Mouse.Y - l_Devices.MouseAtFrameStart.Y);

        return s_State.ReadingScene || s_State.ReadingUI ? l_Delta / s_State.Filter.MouseScale : l_Delta;
    }

    glm::vec2 Input::GetMouseScroll()
    {
        return GetDevices().Scroll;
    }

    bool Input::IsGamepadConnected(std::uint32_t gamepad)
    {
        return gamepad < c_MaxGamepads && s_State.Gamepads[gamepad].Connected;
    }

    std::string_view Input::GetGamepadName(std::uint32_t gamepad)
    {
        if (!IsGamepadConnected(gamepad))
        {
            return {};
        }

        const GamepadState& l_Gamepad = s_State.Gamepads[gamepad];

        return { l_Gamepad.Name.data(), l_Gamepad.NameLength };
    }

    bool Input::IsGamepadButtonPressed(std::uint32_t gamepad, GamepadButton button)
    {
        return gamepad < c_MaxGamepads && ReadButton(GetDevices().Gamepads[gamepad].Buttons, button);
    }

    bool Input::WasGamepadButtonPressed(std::uint32_t gamepad, GamepadButton button)
    {
        return gamepad < c_MaxGamepads && ReadButton(GetDevices().Gamepads[gamepad].Pressed, button);
    }

    bool Input::WasGamepadButtonReleased(std::uint32_t gamepad, GamepadButton button)
    {
        return gamepad < c_MaxGamepads && ReadButton(GetDevices().Gamepads[gamepad].Released, button);
    }

    float Input::GetGamepadAxis(std::uint32_t gamepad, GamepadAxis axis)
    {
        const std::size_t l_Index = std::to_underlying(axis);

        return gamepad < c_MaxGamepads && l_Index < c_AxisCount ? GetDevices().Gamepads[gamepad].Axes[l_Index] : 0.0f;
    }

    void Input::SetGamepadRumble(std::uint32_t gamepad, float lowFrequency, float highFrequency)
    {
        if (!IsGamepadConnected(gamepad))
        {
            return;
        }

        GamepadState& l_Gamepad = s_State.Gamepads[gamepad];
        l_Gamepad.RumbleLow = ToMotorSpeed(lowFrequency);
        l_Gamepad.RumbleHigh = ToMotorSpeed(highFrequency);
        if (s_State.Foreground)
        {
            Platform::SetGamepadRumble(gamepad, l_Gamepad.RumbleLow, l_Gamepad.RumbleHigh);
        }
    }

    void Input::StopGamepadRumble()
    {
        for (std::uint32_t it_Gamepad = 0; it_Gamepad < c_MaxGamepads; ++it_Gamepad)
        {
            SetGamepadRumble(it_Gamepad, 0.0f, 0.0f);
        }
    }

    // A scale that is not a positive number reads the mouse as the window has it
    void Input::SetSceneFilter(const SceneInputFilter& filter)
    {
        s_State.NextFilter = filter;
        if (!(filter.MouseScale > 0.0f))
        {
            s_State.NextFilter.MouseScale = 1.0f;
        }
    }

    // Losing the focus releases every pad and stops its rumble, and getting it back starts the rumble again and reads each pad as it is now, skipping what happened in between
    void Input::PollGamepads(const std::function<void(Event&)>& onEvent)
    {
        const bool l_Foreground = Platform::IsForeground();
        const bool l_Resync = l_Foreground && !s_State.Foreground;
        if (l_Foreground != s_State.Foreground)
        {
            s_State.Foreground = l_Foreground;
            for (std::uint32_t it_Gamepad = 0; it_Gamepad < c_MaxGamepads; ++it_Gamepad)
            {
                const GamepadState& l_Gamepad = s_State.Gamepads[it_Gamepad];
                if (!l_Gamepad.Connected)
                {
                    continue;
                }

                if (!l_Foreground)
                {
                    ReleaseGamepad(s_State.Window.Gamepads[it_Gamepad]);
                    ReleaseGamepad(s_State.UI.Gamepads[it_Gamepad]);
                    ReleaseGamepad(s_State.Scene.Gamepads[it_Gamepad]);
                }

                Platform::SetGamepadRumble(it_Gamepad, l_Foreground ? l_Gamepad.RumbleLow : 0.0f, l_Foreground ? l_Gamepad.RumbleHigh : 0.0f);
            }
        }

        s_GamepadSlots.OnEvent = &onEvent;
        Platform::PollGamepads(s_GamepadSlots, l_Resync);
        s_GamepadSlots.OnEvent = nullptr;
    }

    // The filter asked for this frame is taken after the edges are cleared, so what the scene lets go of by it is released in its next frame
    void Input::EndFrame()
    {
        BeginFrame(s_State.Window);
        BeginFrame(s_State.UI);
        BeginFrame(s_State.Scene);
        s_State.StrokeCount = 0;
        s_State.TypedCount = 0;
        ApplySceneFilter();
    }

    void Input::InitializeGamepads()
    {
        Platform::InitializeGamepads();
    }

    bool Input::HasGamepadRuntime()
    {
        return Platform::HasGamepadRuntime();
    }

    // Stops every pad's rumble, with no events, since the layers are gone
    void Input::ShutdownGamepads()
    {
        Platform::ShutdownGamepads();
        s_State.Gamepads = {};
        s_State.Window.Gamepads = {};
        s_State.UI.Gamepads = {};
        s_State.Scene.Gamepads = {};
        s_State.HeldBackAxes = {};
        s_State.TakenAxes = {};
        s_State.Foreground = false;
    }

    bool Input::SetReadingScene(bool reading)
    {
        return std::exchange(s_State.ReadingScene, reading);
    }

    bool Input::SetReadingUI(bool reading)
    {
        return std::exchange(s_State.ReadingUI, reading);
    }

    std::span<const KeyStroke> Input::GetKeyStrokes()
    {
        return std::span(s_State.Strokes.data(), s_State.StrokeCount);
    }

    std::u32string_view Input::GetTypedText()
    {
        return std::u32string_view(s_State.Typed.data(), s_State.TypedCount);
    }

    void Input::TakeKey(KeyCode key)
    {
        const std::size_t l_Index = std::to_underlying(key);
        if (l_Index < c_KeyCount)
        {
            DeviceState& l_Scene = s_State.Scene;
            bool l_Held = l_Scene.Keys[l_Index];
            bool l_Pressed = l_Scene.KeysPressed[l_Index];
            bool l_Released = l_Scene.KeysReleased[l_Index];
            TakeHeld(l_Held, l_Pressed, l_Released);
            l_Scene.Keys[l_Index] = l_Held;
            l_Scene.KeysPressed[l_Index] = l_Pressed;
            l_Scene.KeysReleased[l_Index] = l_Released;
        }
    }

    void Input::TakeMouseButton(MouseCode button)
    {
        const std::size_t l_Index = std::to_underlying(button);
        if (l_Index < c_MouseButtonCount)
        {
            DeviceState& l_Scene = s_State.Scene;
            bool l_Held = l_Scene.MouseButtons[l_Index];
            bool l_Pressed = l_Scene.MouseButtonsPressed[l_Index];
            bool l_Released = l_Scene.MouseButtonsReleased[l_Index];
            TakeHeld(l_Held, l_Pressed, l_Released);
            l_Scene.MouseButtons[l_Index] = l_Held;
            l_Scene.MouseButtonsPressed[l_Index] = l_Pressed;
            l_Scene.MouseButtonsReleased[l_Index] = l_Released;
        }
    }

    void Input::TakeMouseScroll()
    {
        s_State.Scene.Scroll = glm::vec2(0.0f);
    }

    void Input::TakeGamepadButton(std::uint32_t gamepad, GamepadButton button)
    {
        const std::size_t l_Index = std::to_underlying(button);
        if (gamepad >= c_MaxGamepads || l_Index >= std::to_underlying(GamepadButton::Count))
        {
            return;
        }

        PadState& l_Pad = s_State.Scene.Gamepads[gamepad];
        const std::uint32_t l_Bit = 1u << l_Index;
        bool l_Held = (l_Pad.Buttons & l_Bit) != 0;
        bool l_Pressed = (l_Pad.Pressed & l_Bit) != 0;
        bool l_Released = (l_Pad.Released & l_Bit) != 0;
        TakeHeld(l_Held, l_Pressed, l_Released);
        l_Pad.Buttons = l_Held ? l_Pad.Buttons | l_Bit : l_Pad.Buttons & ~l_Bit;
        l_Pad.Pressed = l_Pressed ? l_Pad.Pressed | l_Bit : l_Pad.Pressed & ~l_Bit;
        l_Pad.Released = l_Released ? l_Pad.Released | l_Bit : l_Pad.Released & ~l_Bit;
    }

    void Input::TakeGamepadAxis(std::uint32_t gamepad, GamepadAxis axis)
    {
        const std::size_t l_Index = std::to_underlying(axis);
        if (gamepad < c_MaxGamepads && l_Index < c_AxisCount)
        {
            s_State.TakenAxes[gamepad] |= 1u << l_Index;
            s_State.Scene.Gamepads[gamepad].Axes[l_Index] = 0.0f;
        }
    }

    // The UI and the scene see a key or button go down only while the filter lets it in, and see every one they hold go up. A repeat brings in no key they did not see go down. The UI's strokes and typed text are those the filter lets in, and a key going up is its stroke when the UI saw it held
    void Input::OnEvent(const Event& event)
    {
        DeviceState& l_Window = s_State.Window;
        const SceneInputFilter& l_Filter = s_State.Filter;
        switch (event.GetEventType())
        {
            case EventType::KeyPressed:
            {
                const auto& l_Pressed = static_cast<const KeyPressedEvent&>(event);
                const std::size_t l_Index = std::to_underlying(l_Pressed.GetKeyCode());
                if (l_Index < c_KeyCount)
                {
                    l_Window.KeysPressed[l_Index] = l_Window.KeysPressed[l_Index] || !l_Pressed.IsRepeat();
                    l_Window.Keys[l_Index] = true;
                    if (l_Filter.Keyboard && (!l_Pressed.IsRepeat() || s_State.UI.Keys[l_Index]) && s_State.StrokeCount < s_State.Strokes.size())
                    {
                        s_State.Strokes[s_State.StrokeCount++] = { l_Pressed.GetKeyCode(), true, l_Pressed.IsRepeat() };
                    }

                    for (DeviceState* it_Devices : GetFiltered())
                    {
                        if (l_Filter.Keyboard && !l_Pressed.IsRepeat())
                        {
                            it_Devices->KeysPressed[l_Index] = true;
                            it_Devices->Keys[l_Index] = true;
                        }
                    }
                }

                break;
            }
            case EventType::KeyReleased:
            {
                const KeyCode l_Key = static_cast<const KeyEvent&>(event).GetKeyCode();
                const std::size_t l_Index = std::to_underlying(l_Key);
                if (l_Index < c_KeyCount)
                {
                    l_Window.KeysReleased[l_Index] = true;
                    l_Window.Keys[l_Index] = false;
                    if (s_State.UI.Keys[l_Index] && s_State.StrokeCount < s_State.Strokes.size())
                    {
                        s_State.Strokes[s_State.StrokeCount++] = { l_Key, false, false };
                    }

                    for (DeviceState* it_Devices : GetFiltered())
                    {
                        it_Devices->KeysReleased[l_Index] = it_Devices->KeysReleased[l_Index] || it_Devices->Keys[l_Index];
                        it_Devices->Keys[l_Index] = false;
                    }
                }

                break;
            }
            case EventType::KeyTyped:
            {
                if (l_Filter.Keyboard && s_State.TypedCount < s_State.Typed.size())
                {
                    s_State.Typed[s_State.TypedCount++] = static_cast<const KeyTypedEvent&>(event).GetCodepoint();
                }

                break;
            }
            case EventType::MouseButtonPressed:
            case EventType::MouseButtonReleased:
            {
                const std::size_t l_Index = std::to_underlying(static_cast<const MouseButtonEvent&>(event).GetMouseButton());
                if (l_Index < c_MouseButtonCount)
                {
                    const bool l_Down = event.GetEventType() == EventType::MouseButtonPressed;
                    (l_Down ? l_Window.MouseButtonsPressed : l_Window.MouseButtonsReleased)[l_Index] = true;
                    l_Window.MouseButtons[l_Index] = l_Down;
                    for (DeviceState* it_Devices : GetFiltered())
                    {
                        if (l_Down && l_Filter.Mouse && l_Filter.MouseOver)
                        {
                            it_Devices->MouseButtonsPressed[l_Index] = true;
                            it_Devices->MouseButtons[l_Index] = true;
                        }
                        else if (!l_Down)
                        {
                            it_Devices->MouseButtonsReleased[l_Index] = it_Devices->MouseButtonsReleased[l_Index] || it_Devices->MouseButtons[l_Index];
                            it_Devices->MouseButtons[l_Index] = false;
                        }
                    }
                }

                break;
            }
            case EventType::MouseMoved:
            {
                const auto& l_Moved = static_cast<const MouseMovedEvent&>(event);
                const MousePosition l_Position{ l_Moved.GetX(), l_Moved.GetY() };
                l_Window.Mouse = l_Position;
                const bool l_First = !std::exchange(s_State.MouseKnown, true);
                if (l_First)
                {
                    l_Window.MouseAtFrameStart = l_Position;
                }

                for (DeviceState* it_Devices : GetFiltered())
                {
                    if (l_Filter.Mouse)
                    {
                        it_Devices->Mouse = l_Position;
                    }

                    if (l_First)
                    {
                        it_Devices->Mouse = l_Position;
                        it_Devices->MouseAtFrameStart = l_Position;
                    }
                }

                break;
            }
            case EventType::MouseScrolled:
            {
                const auto& l_Scrolled = static_cast<const MouseScrolledEvent&>(event);
                const glm::vec2 l_Offset(l_Scrolled.GetXOffset(), l_Scrolled.GetYOffset());
                l_Window.Scroll += l_Offset;
                for (DeviceState* it_Devices : GetFiltered())
                {
                    if (l_Filter.Mouse && l_Filter.MouseOver)
                    {
                        it_Devices->Scroll += l_Offset;
                    }
                }

                break;
            }
            case EventType::WindowLostFocus:
            {
                l_Window.Keys.fill(false);
                l_Window.MouseButtons.fill(false);
                for (DeviceState* it_Devices : GetFiltered())
                {
                    ReleaseAll(it_Devices->Keys, it_Devices->KeysReleased);
                    ReleaseAll(it_Devices->MouseButtons, it_Devices->MouseButtonsReleased);
                }

                break;
            }
            default:
            {
                break;
            }
        }
    }
}