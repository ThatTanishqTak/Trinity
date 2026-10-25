#include "Trinity/Input/InputActions.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Input/InputActionData.hpp"
#include "Trinity/Input/InputActionsRuntime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

namespace Trinity
{
    namespace
    {
        constexpr std::uint32_t c_NoAction = std::numeric_limits<std::uint32_t>::max();
        constexpr std::size_t c_DeviceCount = std::to_underlying(InputDevice::Count);
        constexpr std::uint32_t c_PartCount = static_cast<std::uint32_t>(std::tuple_size_v<decltype(InputBinding::Sources)>);
        // How far a stick or trigger has to be pushed for a rebind's Listen to take it, as Forge's Listen takes it
        constexpr float c_ListenPush = 0.5f;
        constexpr std::string_view c_RebindsDirectory = "/saves/Input";

        static_assert(InputActions::c_MaxPlayers == InputBindingOverlay::c_PlayerSlots, "A rebinds file keeps a slot for each player");

        // A path a handle was found by, and the action it leads to in the asset now, by its index among every map's actions in order
        struct ActionSlot
        {
            InputName Path;
            std::uint32_t Action = c_NoAction;
        };

        // Where each action is in the asset, its type, and where its bindings' sources start among a player's, c_PartCount for each binding, kept here so reading an action never looks into an asset a reload may just have replaced
        struct ActionLocation
        {
            std::uint32_t Map = 0;
            std::uint32_t Action = 0;
            InputActionType Type = InputActionType::Button;
            std::uint32_t FirstSource = 0;
        };

        // A map enabled or disabled before the asset was ready, made once it is, after the first map
        struct MapChange
        {
            InputName Map;
            bool Enable = true;
        };

        // Where an interaction is: waiting for a press, Down in a press that can still perform, Between a MultiTap's taps, Done with a Hold that performed until it is released, or Failed in a press held too long to be a tap until it is released
        enum class InteractionPhase : std::uint8_t
        {
            Idle,
            Down,
            Between,
            Done,
            Failed
        };

        // One player's reading of one action: its value, whether it is held, whether it went down or up this frame, and the same since the last fixed step. Then its interaction's: where it is, the time it has been in that phase, the taps done, its progress, and whether it performed or canceled this frame and since the last fixed step. Last, whether it is held back after a rebind's Listen took an input, reading as released until its sources are let go
        struct ActionState
        {
            glm::vec2 Value{ 0.0f };
            bool Held = false;
            bool Pressed = false;
            bool Released = false;
            bool FixedPressed = false;
            bool FixedReleased = false;
            InteractionPhase Phase = InteractionPhase::Idle;
            float Elapsed = 0.0f;
            std::uint32_t Taps = 0;
            float Progress = 0.0f;
            bool Performed = false;
            bool Canceled = false;
            bool FixedPerformed = false;
            bool FixedCanceled = false;
            bool HeldBack = false;
        };

        // The maps a player enabled, by name so they outlive a reload, and by index into the asset now. A new player has the first map enabled once the asset is there, then the changes asked for until it was. Its sources are the asset's with its slot's rebinds on top, by ActionLocation::FirstSource
        struct PlayerState
        {
            bool Active = false;
            bool FirstMapPending = false;
            InputVector<InputName> EnabledNames;
            InputVector<MapChange> PendingChanges;
            InputVector<std::uint8_t> EnabledMaps;
            InputVector<ActionState> Actions;
            InputVector<InputSource> Sources;
        };

        // A rebind's outcome, in the slot of InputRebind::c_Kept its ID gives it: what it came to, the source it took or refused, and when refused the name of the action of its map that reads that source
        struct RebindOutcome
        {
            std::uint32_t ID = 0;
            InputRebindState State = InputRebindState::None;
            InputSource Source;
            InputName Conflict;
        };

        // The rebind that listens: the outcome it fills, its action by the handle's slot, so it follows a reload, the part and the player, whether it takes a stick whole, and each pad's axes pushed as it started, bit 1 << GamepadAxis, which it takes only once they have come back under c_ListenPush
        struct Listening
        {
            std::uint32_t ID = 0;
            std::uint32_t Slot = 0;
            std::uint32_t Binding = 0;
            std::uint32_t Part = 0;
            std::uint32_t Player = 0;
            bool WholeStick = false;
            std::array<std::uint32_t, Input::c_MaxGamepads> HeldAxes{};
        };

        struct State
        {
            InputActionsLoader Loader;
            UUID Asset;
            bool Running = false;
            bool InFixedStep = false;
            bool WarnedType = false;
            std::uint64_t BuiltSerial = 0;
            // The version built from, null until it is ready
            const InputActionsData* Data = nullptr;
            InputVector<ActionLocation> Locations;
            InputVector<ActionSlot> Slots;
            std::array<PlayerState, InputActions::c_MaxPlayers> Players;
            // Each device's assigned player, or c_NoPlayer, and the devices each player reads this frame, bit 1 << InputDevice
            std::array<std::uint32_t, c_DeviceCount> DevicePlayers{};
            std::array<std::uint32_t, InputActions::c_MaxPlayers> ReadMasks{};
            // Counts up as a scene starts and ends, so a handle found in an earlier one reads nothing
            std::uint32_t Session = 1;
            // How many sources a player has, c_PartCount for each binding of every action
            std::uint32_t SourceCount = 0;
            // Each player slot's rebinds, read from /saves as the scene starts and saved there as they change, whether or not the player is in use
            std::array<InputVector<InputBindingOverride>, InputActions::c_MaxPlayers> Rebinds;
            std::array<RebindOutcome, InputRebind::c_Kept> Outcomes;
            // Never starts again from 1, so a rebind of an earlier scene reads as None
            std::uint32_t NextRebind = 1;
            std::optional<Listening> Listen;
        };

        State* s_State = nullptr;

        // A source's value, its X alone for one that has no Y, and whether a key or button of it went down this frame, which a press and a release between two frames would otherwise hide
        struct Reading
        {
            glm::vec2 Value{ 0.0f };
            bool IsVector = false;
            bool WentDown = false;
        };

        [[nodiscard]] bool IsRunning()
        {
            return s_State != nullptr && s_State->Running;
        }

        [[nodiscard]] bool IsActive(std::uint32_t player)
        {
            return IsRunning() && player < InputActions::c_MaxPlayers && s_State->Players[player].Active;
        }

        [[nodiscard]] float GetMagnitude(const Reading& reading)
        {
            return reading.IsVector ? glm::length(reading.Value) : std::abs(reading.Value.x);
        }

        // The vector's X or Y alone, by the code of a mouse source: 0 for both, 1 for X and 2 for Y
        [[nodiscard]] Reading Pick(glm::vec2 value, std::uint16_t code)
        {
            Reading l_Reading;
            l_Reading.IsVector = code == 0;
            l_Reading.Value = code == 2 ? glm::vec2(value.y, 0.0f) : code == 1 ? glm::vec2(value.x, 0.0f) : value;

            return l_Reading;
        }

        // The strongest of the player's gamepads, so player 0 reads every pad it has as one
        template<typename Read>
        [[nodiscard]] Reading ReadGamepads(std::uint32_t mask, const Read& read)
        {
            Reading l_Best;
            for (std::uint32_t it_Gamepad = 0; it_Gamepad < Input::c_MaxGamepads; ++it_Gamepad)
            {
                if ((mask & (1u << std::to_underlying(GetGamepadDevice(it_Gamepad)))) == 0 || !Input::IsGamepadConnected(it_Gamepad))
                {
                    continue;
                }

                const Reading l_Reading = read(it_Gamepad);
                l_Best.WentDown = l_Best.WentDown || l_Reading.WentDown;
                l_Best.IsVector = l_Reading.IsVector;
                if (GetMagnitude(l_Reading) > GetMagnitude(l_Best))
                {
                    l_Best.Value = l_Reading.Value;
                }
            }

            return l_Best;
        }

        [[nodiscard]] Reading ReadSource(InputSource source, std::uint32_t mask)
        {
            const bool l_KeyboardMouse = (mask & (1u << std::to_underlying(InputDevice::KeyboardMouse))) != 0;
            Reading l_Reading;
            switch (source.Kind)
            {
                case InputSourceKind::None:
                {
                    break;
                }
                case InputSourceKind::Key:
                {
                    if (l_KeyboardMouse)
                    {
                        const KeyCode l_Key = static_cast<KeyCode>(source.Code);
                        l_Reading.Value.x = Input::IsKeyPressed(l_Key) ? 1.0f : 0.0f;
                        l_Reading.WentDown = Input::WasKeyPressed(l_Key);
                    }

                    break;
                }
                case InputSourceKind::MouseButton:
                {
                    if (l_KeyboardMouse)
                    {
                        const MouseCode l_Button = static_cast<MouseCode>(source.Code);
                        l_Reading.Value.x = Input::IsMouseButtonPressed(l_Button) ? 1.0f : 0.0f;
                        l_Reading.WentDown = Input::WasMouseButtonPressed(l_Button);
                    }

                    break;
                }
                case InputSourceKind::MouseDelta:
                {
                    l_Reading = Pick(l_KeyboardMouse ? Input::GetMouseDelta() : glm::vec2(0.0f), source.Code);

                    break;
                }
                case InputSourceKind::MouseScroll:
                {
                    l_Reading = Pick(l_KeyboardMouse ? Input::GetMouseScroll() : glm::vec2(0.0f), source.Code);

                    break;
                }
                case InputSourceKind::GamepadButton:
                {
                    l_Reading = ReadGamepads(mask, [source](std::uint32_t gamepad)
                    {
                        const GamepadButton l_Button = static_cast<GamepadButton>(source.Code);

                        return Reading{ glm::vec2(Input::IsGamepadButtonPressed(gamepad, l_Button) ? 1.0f : 0.0f, 0.0f), false, Input::WasGamepadButtonPressed(gamepad, l_Button) };
                    });

                    break;
                }
                case InputSourceKind::GamepadAxis:
                {
                    l_Reading = ReadGamepads(mask, [source](std::uint32_t gamepad)
                    {
                        return Reading{ glm::vec2(Input::GetGamepadAxis(gamepad, static_cast<GamepadAxis>(source.Code)), 0.0f), false, false };
                    });

                    break;
                }
                case InputSourceKind::GamepadStick:
                {
                    l_Reading = ReadGamepads(mask, [source](std::uint32_t gamepad)
                    {
                        const GamepadAxis l_X = source.Code == 0 ? GamepadAxis::TR_LEFT_STICK_X : GamepadAxis::TR_RIGHT_STICK_X;
                        const GamepadAxis l_Y = source.Code == 0 ? GamepadAxis::TR_LEFT_STICK_Y : GamepadAxis::TR_RIGHT_STICK_Y;

                        return Reading{ glm::vec2(Input::GetGamepadAxis(gamepad, l_X), Input::GetGamepadAxis(gamepad, l_Y)), true, false };
                    });
                    l_Reading.IsVector = true;

                    break;
                }
            }

            return l_Reading;
        }

        // Nothing below min, all of it from max, and in between in proportion
        [[nodiscard]] float ApplyDeadzone(float size, float min, float max)
        {
            if (size < min)
            {
                return 0.0f;
            }

            return max > min ? std::min((size - min) / (max - min), 1.0f) : 1.0f;
        }

        void ApplyProcessor(const InputProcessor& processor, Reading& reading)
        {
            glm::vec2& l_Value = reading.Value;
            switch (processor.Kind)
            {
                case InputProcessorKind::RadialDeadzone:
                {
                    const float l_Length = GetMagnitude(reading);
                    l_Value = l_Length > 0.0f ? l_Value * (ApplyDeadzone(l_Length, processor.Min, processor.Max) / l_Length) : glm::vec2(0.0f);

                    break;
                }
                case InputProcessorKind::AxialDeadzone:
                {
                    l_Value.x = std::copysign(ApplyDeadzone(std::abs(l_Value.x), processor.Min, processor.Max), l_Value.x);
                    l_Value.y = std::copysign(ApplyDeadzone(std::abs(l_Value.y), processor.Min, processor.Max), l_Value.y);

                    break;
                }
                case InputProcessorKind::Invert:
                {
                    l_Value.x = processor.InvertX ? -l_Value.x : l_Value.x;
                    l_Value.y = processor.InvertY ? -l_Value.y : l_Value.y;

                    break;
                }
                case InputProcessorKind::Scale:
                {
                    l_Value *= processor.Scale;

                    break;
                }
                case InputProcessorKind::Normalize:
                {
                    const float l_Length = GetMagnitude(reading);
                    l_Value = l_Length > 1.0f ? l_Value / l_Length : l_Value;

                    break;
                }
            }
        }

        // A composite's part counts by its size if it is a stick, and by its value otherwise, so a trigger can be half of an axis
        [[nodiscard]] float ReadPart(InputSource source, std::uint32_t mask, bool& wentDown)
        {
            const Reading l_Reading = ReadSource(source, mask);
            wentDown = wentDown || l_Reading.WentDown;

            return l_Reading.IsVector ? glm::length(l_Reading.Value) : l_Reading.Value.x;
        }

        // From the player's sources for the binding, its c_PartCount starting at sources
        [[nodiscard]] Reading ReadBinding(const InputBinding& binding, const InputSource* sources, std::uint32_t mask)
        {
            Reading l_Reading;
            switch (binding.Composite)
            {
                case InputCompositeKind::None:
                {
                    l_Reading = ReadSource(sources[0], mask);

                    break;
                }
                case InputCompositeKind::Axis:
                {
                    const float l_Negative = ReadPart(sources[0], mask, l_Reading.WentDown);
                    const float l_Positive = ReadPart(sources[1], mask, l_Reading.WentDown);
                    l_Reading.Value.x = l_Positive - l_Negative;

                    break;
                }
                case InputCompositeKind::Vector2:
                {
                    const float l_Up = ReadPart(sources[0], mask, l_Reading.WentDown);
                    const float l_Down = ReadPart(sources[1], mask, l_Reading.WentDown);
                    const float l_Left = ReadPart(sources[2], mask, l_Reading.WentDown);
                    const float l_Right = ReadPart(sources[3], mask, l_Reading.WentDown);
                    l_Reading.Value = glm::vec2(l_Right - l_Left, l_Up - l_Down);
                    l_Reading.IsVector = true;

                    break;
                }
            }

            for (const InputProcessor& it_Processor : binding.Processors)
            {
                ApplyProcessor(it_Processor, l_Reading);
            }

            return l_Reading;
        }

        // The binding's value as the action's type has it: a Button's strength, an Axis's X, or a Vector2, with how far it goes
        [[nodiscard]] glm::vec2 ToActionValue(InputActionType type, const Reading& reading, float& magnitude)
        {
            switch (type)
            {
                case InputActionType::Button:
                {
                    magnitude = GetMagnitude(reading);

                    return glm::vec2(magnitude, 0.0f);
                }
                case InputActionType::Axis:
                {
                    magnitude = std::abs(reading.Value.x);

                    return glm::vec2(reading.Value.x, 0.0f);
                }
                case InputActionType::Vector2:
                {
                    const glm::vec2 l_Value = reading.IsVector ? reading.Value : glm::vec2(reading.Value.x, 0.0f);
                    magnitude = glm::length(l_Value);

                    return l_Value;
                }
            }

            magnitude = 0.0f;

            return glm::vec2(0.0f);
        }

        // The interaction's part of the frame, once its press is read. Time counts only across a frame the action stayed down, or stayed up between taps, so a press or a release is taken as at the frame's start and a tap is never judged longer than it was
        void Interact(const InputInteraction& interaction, float seconds, ActionState& state)
        {
            state.Performed = false;
            state.Canceled = false;
            switch (interaction.Kind)
            {
                case InputInteractionKind::None:
                {
                    state.Performed = state.Pressed;
                    state.Progress = state.Held ? 1.0f : 0.0f;

                    break;
                }
                case InputInteractionKind::Hold:
                {
                    if (state.Pressed)
                    {
                        state.Phase = InteractionPhase::Down;
                        state.Elapsed = 0.0f;
                    }
                    else if (state.Phase == InteractionPhase::Down && state.Held)
                    {
                        state.Elapsed += seconds;
                    }

                    if (state.Phase == InteractionPhase::Down && state.Held && state.Elapsed >= interaction.Duration)
                    {
                        state.Performed = true;
                        state.Phase = InteractionPhase::Done;
                    }
                    else if (!state.Held)
                    {
                        state.Canceled = state.Phase == InteractionPhase::Down;
                        state.Phase = InteractionPhase::Idle;
                    }

                    state.Progress = state.Phase == InteractionPhase::Done ? 1.0f : state.Phase == InteractionPhase::Down ? std::min(state.Elapsed / interaction.Duration, 1.0f) : 0.0f;

                    break;
                }
                case InputInteractionKind::Tap:
                {
                    if (state.Pressed)
                    {
                        state.Phase = InteractionPhase::Down;
                        state.Elapsed = 0.0f;
                    }
                    else if (state.Phase == InteractionPhase::Down && state.Held)
                    {
                        state.Elapsed += seconds;
                    }

                    if (state.Phase == InteractionPhase::Down && state.Held && state.Elapsed > interaction.Duration)
                    {
                        state.Canceled = true;
                        state.Phase = InteractionPhase::Failed;
                    }
                    else if (!state.Held)
                    {
                        state.Performed = state.Phase == InteractionPhase::Down;
                        state.Phase = InteractionPhase::Idle;
                    }

                    state.Progress = state.Performed ? 1.0f : 0.0f;

                    break;
                }
                case InputInteractionKind::MultiTap:
                {
                    if (state.Phase == InteractionPhase::Between && !state.Pressed)
                    {
                        state.Elapsed += seconds;
                        if (state.Elapsed > interaction.Spacing)
                        {
                            state.Canceled = true;
                            state.Phase = InteractionPhase::Idle;
                            state.Taps = 0;
                        }
                    }

                    if (state.Pressed)
                    {
                        state.Taps = state.Phase == InteractionPhase::Between ? state.Taps : 0;
                        state.Phase = InteractionPhase::Down;
                        state.Elapsed = 0.0f;
                    }
                    else if (state.Phase == InteractionPhase::Down && state.Held)
                    {
                        state.Elapsed += seconds;
                        if (state.Elapsed > interaction.Duration)
                        {
                            state.Canceled = true;
                            state.Phase = InteractionPhase::Failed;
                            state.Taps = 0;
                        }
                    }

                    if (!state.Held && state.Phase == InteractionPhase::Down)
                    {
                        ++state.Taps;
                        state.Performed = state.Taps >= interaction.Count;
                        state.Phase = state.Performed ? InteractionPhase::Idle : InteractionPhase::Between;
                        state.Elapsed = 0.0f;
                    }
                    else if (!state.Held && state.Phase == InteractionPhase::Failed)
                    {
                        state.Phase = InteractionPhase::Idle;
                    }

                    const bool l_Running = state.Phase == InteractionPhase::Down || state.Phase == InteractionPhase::Between;
                    state.Progress = state.Performed ? 1.0f : l_Running ? static_cast<float>(state.Taps) / static_cast<float>(interaction.Count) : 0.0f;
                    state.Taps = state.Performed ? 0 : state.Taps;

                    break;
                }
            }

            state.FixedPerformed = state.FixedPerformed || state.Performed;
            state.FixedCanceled = state.FixedCanceled || state.Canceled;
        }

        // Of several bindings the strongest wins, the first among equals, each read from the player's sources, which start at sources. An action is pressed as it goes from below the press point to it, and also when a key or button of it went down this frame and came up again before it ended. One held back reads as released until it is below the press point with nothing gone down, as after a rebind. Its interaction follows, over the frame's seconds
        void Evaluate(const InputActionDefinition& action, const InputSource* sources, bool enabled, std::uint32_t mask, float seconds, ActionState& state)
        {
            glm::vec2 l_Value(0.0f);
            float l_Magnitude = 0.0f;
            bool l_WentDown = false;
            if (enabled)
            {
                for (std::size_t it_Binding = 0; it_Binding < action.Bindings.size(); ++it_Binding)
                {
                    const Reading l_Reading = ReadBinding(action.Bindings[it_Binding], sources + it_Binding * c_PartCount, mask);
                    float l_BindingMagnitude = 0.0f;
                    const glm::vec2 l_BindingValue = ToActionValue(action.Type, l_Reading, l_BindingMagnitude);
                    l_WentDown = l_WentDown || l_Reading.WentDown;
                    if (l_BindingMagnitude > l_Magnitude)
                    {
                        l_Magnitude = l_BindingMagnitude;
                        l_Value = l_BindingValue;
                    }
                }
            }

            if (state.HeldBack)
            {
                state.HeldBack = l_Magnitude >= InputActions::c_PressPoint || l_WentDown;
                if (state.HeldBack)
                {
                    l_Value = glm::vec2(0.0f);
                    l_Magnitude = 0.0f;
                    l_WentDown = false;
                }
            }

            const bool l_WasHeld = state.Held;
            state.Value = l_Value;
            state.Held = l_Magnitude >= InputActions::c_PressPoint;
            state.Pressed = !l_WasHeld && (state.Held || l_WentDown);
            state.Released = (l_WasHeld || state.Pressed) && !state.Held;
            state.FixedPressed = state.FixedPressed || state.Pressed;
            state.FixedReleased = state.FixedReleased || state.Released;
            Interact(action.Interaction, seconds, state);
        }

        [[nodiscard]] std::uint32_t FindMap(const InputActionsData& data, std::string_view name)
        {
            const auto a_Found = std::ranges::find_if(data.Maps, [name](const InputActionMap& map) { return std::string_view(map.Name) == name; });

            return a_Found != data.Maps.end() ? static_cast<std::uint32_t>(a_Found - data.Maps.begin()) : c_NoAction;
        }

        // Map/Action, or an action's name alone where only one map has it. Logged when it leads nowhere
        [[nodiscard]] std::uint32_t Resolve(std::string_view path)
        {
            const InputActionsData* l_Data = s_State->Data;
            if (l_Data == nullptr)
            {
                return c_NoAction;
            }

            const std::size_t l_Separator = path.find(InputActionsData::c_PathSeparator);
            const std::string_view l_Map = l_Separator != std::string_view::npos ? path.substr(0, l_Separator) : std::string_view();
            const std::string_view l_Action = l_Separator != std::string_view::npos ? path.substr(l_Separator + 1) : path;
            std::uint32_t l_Found = c_NoAction;
            std::uint32_t l_Matches = 0;
            for (std::uint32_t it_Index = 0; it_Index < s_State->Locations.size(); ++it_Index)
            {
                const ActionLocation& l_Location = s_State->Locations[it_Index];
                const InputActionMap& l_ActionMap = l_Data->Maps[l_Location.Map];
                if ((l_Separator == std::string_view::npos || std::string_view(l_ActionMap.Name) == l_Map) && std::string_view(l_ActionMap.Actions[l_Location.Action].Name) == l_Action)
                {
                    l_Found = it_Index;
                    ++l_Matches;
                }
            }

            if (l_Matches > 1)
            {
                TR_CORE_WARN("Input: {} actions are named '{}', so it reads as released until it is found as Map/{}", l_Matches, path, path);

                return c_NoAction;
            }

            if (l_Matches == 0)
            {
                TR_CORE_WARN("Input: the actions asset has no action '{}', so it reads as released until it does", path);
            }

            return l_Found;
        }

        [[nodiscard]] bool HasName(const InputVector<InputName>& names, std::string_view name)
        {
            return std::ranges::any_of(names, [name](const InputName& it_Name) { return std::string_view(it_Name) == name; });
        }

        void SetEnabledName(PlayerState& player, std::string_view map, bool enable)
        {
            if (!enable)
            {
                std::erase_if(player.EnabledNames, [map](const InputName& name) { return std::string_view(name) == map; });
            }
            else if (!HasName(player.EnabledNames, map))
            {
                player.EnabledNames.push_back(InputName(map));
            }
        }

        void ApplyFirstMap(PlayerState& player)
        {
            if (s_State->Data == nullptr)
            {
                return;
            }

            if (std::exchange(player.FirstMapPending, false) && !s_State->Data->Maps.empty())
            {
                SetEnabledName(player, s_State->Data->Maps.front().Name, true);
            }

            for (const MapChange& it_Change : player.PendingChanges)
            {
                if (FindMap(*s_State->Data, it_Change.Map) == c_NoAction)
                {
                    TR_CORE_WARN("Input: map '{}' was not {}, since the actions asset has no map of that name", std::string_view(it_Change.Map), it_Change.Enable ? "enabled" : "disabled");

                    continue;
                }

                SetEnabledName(player, it_Change.Map, it_Change.Enable);
            }

            player.PendingChanges = InputVector<MapChange>();
        }

        // Sized for the asset now, with each enabled name's map, so reading allocates nothing until the next build. A name the asset lacks is kept, and logged
        void Fit(PlayerState& player)
        {
            ApplyFirstMap(player);

            const InputActionsData* l_Data = s_State->Data;
            const std::size_t l_Maps = l_Data != nullptr ? l_Data->Maps.size() : 0;
            player.EnabledMaps.assign(l_Maps, 0);
            player.Actions.assign(s_State->Locations.size(), ActionState{});
            for (const InputName& it_Name : player.EnabledNames)
            {
                const std::uint32_t l_Map = l_Data != nullptr ? FindMap(*l_Data, it_Name) : c_NoAction;
                if (l_Map != c_NoAction)
                {
                    player.EnabledMaps[l_Map] = 1;
                }
                else if (l_Data != nullptr)
                {
                    TR_CORE_WARN("Input: the actions asset has no map '{}', which stays enabled in case it comes back", std::string_view(it_Name));
                }
            }
        }

        [[nodiscard]] const InputActionDefinition& GetAction(const ActionLocation& location)
        {
            return s_State->Data->Maps[location.Map].Actions[location.Action];
        }

        // 1 for a single source, and a composite's parts
        [[nodiscard]] std::uint32_t GetPartCount(const InputBinding& binding)
        {
            return std::max(static_cast<std::uint32_t>(GetCompositeParts(binding.Composite).size()), 1u);
        }

        // Whether a rebind's path, Map/Action, is of the action there, compared without making one
        [[nodiscard]] bool NamesLocation(std::string_view path, const ActionLocation& location)
        {
            const InputActionMap& l_Map = s_State->Data->Maps[location.Map];
            const std::string_view l_MapName(l_Map.Name);
            const std::string_view l_ActionName(l_Map.Actions[location.Action].Name);

            return path.size() == l_MapName.size() + 1 + l_ActionName.size() && path.starts_with(l_MapName) && path[l_MapName.size()] == InputActionsData::c_PathSeparator && path.ends_with(l_ActionName);
        }

        // The action at a rebind's path, without logging
        [[nodiscard]] std::uint32_t FindLocation(std::string_view path)
        {
            for (std::uint32_t it_Location = 0; s_State->Data != nullptr && it_Location < s_State->Locations.size(); ++it_Location)
            {
                if (NamesLocation(path, s_State->Locations[it_Location]))
                {
                    return it_Location;
                }
            }

            return c_NoAction;
        }

        [[nodiscard]] std::string DescribeSource(InputSource source)
        {
            return source.Kind != InputSourceKind::None ? ToString(source) : std::string("nothing");
        }

        // Such as Gameplay/Jump binding 0, or Gameplay/Move binding 1 part 2
        [[nodiscard]] std::string DescribePart(std::string_view action, std::uint32_t binding, std::uint32_t part)
        {
            return part != 0 ? std::format("{} binding {} part {}", action, binding, part) : std::format("{} binding {}", action, binding);
        }

        [[nodiscard]] std::string GetRebindsPath()
        {
            return std::format("{}/{}{}", c_RebindsDirectory, s_State->Asset, InputBindingOverlay::c_Extension);
        }

        // Every slot's rebinds, or the file taken away once there are none. A failure is logged, and the rebinds last until play stops
        void SaveRebinds()
        {
            if (!s_State->Asset)
            {
                return;
            }

            const std::string l_Path = GetRebindsPath();
            if (std::ranges::all_of(s_State->Rebinds, [](const InputVector<InputBindingOverride>& rebinds) { return rebinds.empty(); }))
            {
                if (FileSystem::Exists(l_Path))
                {
                    if (const Expected<void, FileError> l_Removed = FileSystem::RemoveFile(l_Path); !l_Removed)
                    {
                        TR_CORE_WARN("Input: {} was not removed, since {}, so its rebinds come back when play next starts", l_Path, ToString(l_Removed.GetError()));
                    }
                }

                return;
            }

            InputBindingOverlay l_Overlay;
            l_Overlay.Asset = s_State->Asset;
            l_Overlay.Players = s_State->Rebinds;
            if (const Expected<void, FileError> l_Written = FileSystem::WriteText(l_Path, WriteInputBindingOverlay(l_Overlay)); !l_Written)
            {
                TR_CORE_WARN("Input: the rebinds were not saved to {}, since {}, so they last until play stops", l_Path, ToString(l_Written.GetError()));
            }
        }

        // The asset's rebinds on this machine, as the scene starts, logged with how many there are. A file that cannot be read is logged and left for the next rebind to replace
        void LoadRebinds()
        {
            const std::string l_Path = GetRebindsPath();
            if (!FileSystem::Exists(l_Path))
            {
                return;
            }

            const auto a_Refuse = [&l_Path](std::string_view reason)
            {
                TR_CORE_WARN("Input: the rebinds in {} were not read, since {}, so the asset's own bindings are used until the next rebind replaces them", l_Path, reason);
            };

            const Expected<std::string, FileError> l_Text = FileSystem::ReadText(l_Path);
            if (!l_Text)
            {
                a_Refuse(ToString(l_Text.GetError()));

                return;
            }

            Expected<InputBindingOverlay, std::string> l_Overlay = ParseInputBindingOverlay(*l_Text);
            if (!l_Overlay || l_Overlay->Asset != s_State->Asset)
            {
                a_Refuse(l_Overlay ? std::string_view("they are another asset's") : std::string_view(l_Overlay.GetError()));

                return;
            }

            std::size_t l_Count = 0;
            std::size_t l_Players = 0;
            for (std::uint32_t it_Player = 0; it_Player < InputActions::c_MaxPlayers; ++it_Player)
            {
                s_State->Rebinds[it_Player] = std::move(l_Overlay->Players[it_Player]);
                l_Count += s_State->Rebinds[it_Player].size();
                l_Players += s_State->Rebinds[it_Player].empty() ? 0 : 1;
            }

            TR_CORE_INFO("Input: {} rebind(s) of {} player(s) read from {}", l_Count, l_Players, l_Path);
        }

        // A rebind whose action, binding or part the asset no longer has, or whose part the asset now gives another source than the one it replaced, is dropped and logged, and the rest are saved
        void DropStaleRebinds()
        {
            bool l_Dropped = false;
            for (std::uint32_t it_Player = 0; it_Player < InputActions::c_MaxPlayers; ++it_Player)
            {
                std::erase_if(s_State->Rebinds[it_Player], [it_Player, &l_Dropped](const InputBindingOverride& rebind)
                {
                    const std::uint32_t l_Location = FindLocation(rebind.Action);
                    const InputActionDefinition* l_Action = l_Location != c_NoAction ? &GetAction(s_State->Locations[l_Location]) : nullptr;
                    const InputBinding* l_Binding = l_Action != nullptr && rebind.Binding < l_Action->Bindings.size() ? &l_Action->Bindings[rebind.Binding] : nullptr;
                    if (l_Binding != nullptr && rebind.Part < GetPartCount(*l_Binding) && l_Binding->Sources[rebind.Part] == rebind.From)
                    {
                        return false;
                    }

                    TR_CORE_WARN("Input: player {}'s rebind of {} from {} to {} was dropped, since the actions asset no longer has {} there", it_Player, DescribePart(std::string_view(rebind.Action), rebind.Binding, rebind.Part), DescribeSource(rebind.From), DescribeSource(rebind.To), DescribeSource(rebind.From));
                    l_Dropped = true;

                    return true;
                });
            }

            if (l_Dropped)
            {
                SaveRebinds();
            }
        }

        // A part's source for a player slot: its rebind where it has one, and the asset's otherwise
        [[nodiscard]] InputSource GetPlayerSource(std::uint32_t player, std::uint32_t location, std::uint32_t binding, std::uint32_t part)
        {
            const ActionLocation& l_Location = s_State->Locations[location];
            for (const InputBindingOverride& it_Rebind : s_State->Rebinds[player])
            {
                if (it_Rebind.Binding == binding && it_Rebind.Part == part && NamesLocation(std::string_view(it_Rebind.Action), l_Location))
                {
                    return it_Rebind.To;
                }
            }

            return GetAction(l_Location).Bindings[binding].Sources[part];
        }

        // The asset's sources with the slot's rebinds on top, so reading them allocates nothing until the next build or rebind
        void FitSources(std::uint32_t player)
        {
            InputVector<InputSource>& l_Sources = s_State->Players[player].Sources;
            l_Sources.assign(s_State->SourceCount, InputSource{});
            for (const ActionLocation& it_Location : s_State->Locations)
            {
                const InputActionDefinition& l_Action = GetAction(it_Location);
                for (std::size_t it_Binding = 0; it_Binding < l_Action.Bindings.size(); ++it_Binding)
                {
                    std::ranges::copy(l_Action.Bindings[it_Binding].Sources, l_Sources.begin() + static_cast<std::ptrdiff_t>(it_Location.FirstSource + it_Binding * c_PartCount));
                }
            }

            for (const InputBindingOverride& it_Rebind : s_State->Rebinds[player])
            {
                const std::uint32_t l_Location = FindLocation(std::string_view(it_Rebind.Action));
                if (l_Location != c_NoAction && it_Rebind.Binding < GetAction(s_State->Locations[l_Location]).Bindings.size() && it_Rebind.Part < c_PartCount)
                {
                    l_Sources[s_State->Locations[l_Location].FirstSource + it_Rebind.Binding * c_PartCount + it_Rebind.Part] = it_Rebind.To;
                }
            }
        }

        // As the asset becomes ready and after each reload, with each rebind the asset was changed under dropped. Every action reads as released until the next frame's update
        void Build(const InputActionsAsset* asset)
        {
            s_State->Data = asset != nullptr ? &asset->GetData() : nullptr;
            s_State->BuiltSerial = asset != nullptr ? asset->GetSerial() : 0;
            s_State->Locations.clear();
            std::uint32_t l_FirstSource = 0;
            if (s_State->Data != nullptr)
            {
                for (std::uint32_t it_Map = 0; it_Map < s_State->Data->Maps.size(); ++it_Map)
                {
                    for (std::uint32_t it_Action = 0; it_Action < s_State->Data->Maps[it_Map].Actions.size(); ++it_Action)
                    {
                        const InputActionDefinition& l_Action = s_State->Data->Maps[it_Map].Actions[it_Action];
                        s_State->Locations.push_back({ it_Map, it_Action, l_Action.Type, l_FirstSource });
                        l_FirstSource += static_cast<std::uint32_t>(l_Action.Bindings.size()) * c_PartCount;
                    }
                }

                DropStaleRebinds();
            }

            s_State->SourceCount = l_FirstSource;
            for (std::uint32_t it_Player = 0; it_Player < InputActions::c_MaxPlayers; ++it_Player)
            {
                if (s_State->Players[it_Player].Active)
                {
                    Fit(s_State->Players[it_Player]);
                    FitSources(it_Player);
                }
            }

            for (ActionSlot& it_Slot : s_State->Slots)
            {
                it_Slot.Action = Resolve(it_Slot.Path);
            }
        }

        // The asset as it is now, built from again when a reload has replaced it, so nothing reads the version it replaced
        const InputActionsData* Sync()
        {
            if (!IsRunning() || !s_State->Asset)
            {
                return nullptr;
            }

            const Asset* l_Loaded = AssetManager::GetAsset(s_State->Asset);
            const InputActionsAsset* l_Asset = l_Loaded != nullptr && l_Loaded->GetAssetType() == InputActionsAsset::c_AssetType ? static_cast<const InputActionsAsset*>(l_Loaded) : nullptr;
            if (l_Loaded != nullptr && l_Asset == nullptr && !std::exchange(s_State->WarnedType, true))
            {
                TR_CORE_ERROR("Input: asset {} is a {} asset, not input actions, so every action reads as released", s_State->Asset, l_Loaded->GetAssetType());
            }

            if ((l_Asset != nullptr ? l_Asset->GetSerial() : 0) != s_State->BuiltSerial)
            {
                Build(l_Asset);
            }

            return s_State->Data;
        }

        // Each player reads its assigned devices, and player 0 every one no player that is in use has
        void UpdateReadMasks()
        {
            s_State->ReadMasks.fill(0);
            for (std::size_t it_Device = 0; it_Device < c_DeviceCount; ++it_Device)
            {
                const std::uint32_t l_Assigned = s_State->DevicePlayers[it_Device];
                const std::uint32_t l_Player = IsActive(l_Assigned) ? l_Assigned : 0;
                s_State->ReadMasks[l_Player] |= 1u << it_Device;
            }
        }

        [[nodiscard]] const ActionState* FindState(std::uint32_t slot, std::uint32_t session, std::uint32_t player)
        {
            if (!IsActive(player) || session != s_State->Session || slot >= s_State->Slots.size())
            {
                return nullptr;
            }

            const std::uint32_t l_Action = s_State->Slots[slot].Action;
            const PlayerState& l_Player = s_State->Players[player];

            return l_Action < l_Player.Actions.size() ? &l_Player.Actions[l_Action] : nullptr;
        }

        void ResetPlayers()
        {
            for (PlayerState& it_Player : s_State->Players)
            {
                it_Player = PlayerState{};
            }

            s_State->DevicePlayers.fill(InputActions::c_NoPlayer);
        }

        [[nodiscard]] bool AnyGamepadButtonPressed(std::uint32_t gamepad)
        {
            for (std::uint8_t it_Button = 0; it_Button < std::to_underlying(GamepadButton::Count); ++it_Button)
            {
                if (Input::WasGamepadButtonPressed(gamepad, static_cast<GamepadButton>(it_Button)))
                {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] bool AnyKeyOrMouseButtonPressed()
        {
            for (std::uint16_t it_Key = 0; it_Key < std::to_underlying(KeyCode::Count); ++it_Key)
            {
                if (Input::WasKeyPressed(static_cast<KeyCode>(it_Key)))
                {
                    return true;
                }
            }

            for (std::uint8_t it_Button = 0; it_Button < std::to_underlying(MouseCode::Count); ++it_Button)
            {
                if (Input::WasMouseButtonPressed(static_cast<MouseCode>(it_Button)))
                {
                    return true;
                }
            }

            return false;
        }

        bool SetMapEnabled(std::string_view map, std::uint32_t player, bool enable)
        {
            if (!IsActive(player))
            {
                TR_CORE_WARN("Input: map '{}' was not {}, since there is no player {}", map, enable ? "enabled" : "disabled", player);

                return false;
            }

            PlayerState& l_Player = s_State->Players[player];
            const InputActionsData* l_Data = Sync();
            if (l_Data == nullptr)
            {
                l_Player.PendingChanges.push_back({ InputName(map), enable });

                return true;
            }

            const std::uint32_t l_Map = FindMap(*l_Data, map);
            if (l_Map == c_NoAction)
            {
                TR_CORE_WARN("Input: map '{}' was not {}, since the actions asset has no map of that name", map, enable ? "enabled" : "disabled");

                return false;
            }

            SetEnabledName(l_Player, map, enable);
            l_Player.EnabledMaps[l_Map] = enable ? 1 : 0;

            return true;
        }

        // Another binding or part of the action's map that reads the source for the player, by its action. None never conflicts
        [[nodiscard]] const InputActionDefinition* FindConflict(std::uint32_t player, std::uint32_t location, std::uint32_t binding, std::uint32_t part, InputSource source)
        {
            if (source.Kind == InputSourceKind::None)
            {
                return nullptr;
            }

            const std::uint32_t l_Map = s_State->Locations[location].Map;
            for (std::uint32_t it_Location = 0; it_Location < s_State->Locations.size(); ++it_Location)
            {
                if (s_State->Locations[it_Location].Map != l_Map)
                {
                    continue;
                }

                const InputActionDefinition& l_Action = GetAction(s_State->Locations[it_Location]);
                for (std::uint32_t it_Binding = 0; it_Binding < l_Action.Bindings.size(); ++it_Binding)
                {
                    for (std::uint32_t it_Part = 0; it_Part < GetPartCount(l_Action.Bindings[it_Binding]); ++it_Part)
                    {
                        const bool l_Itself = it_Location == location && it_Binding == binding && it_Part == part;
                        if (!l_Itself && GetPlayerSource(player, it_Location, it_Binding, it_Part) == source)
                        {
                            return &l_Action;
                        }
                    }
                }
            }

            return nullptr;
        }

        // The slot's rebind of a part to the source, or none where that is the asset's own, then the player's sources fitted again and the rebinds saved
        void SetRebind(std::uint32_t player, std::uint32_t location, std::uint32_t binding, std::uint32_t part, InputSource source)
        {
            const ActionLocation& l_Location = s_State->Locations[location];
            InputVector<InputBindingOverride>& l_Rebinds = s_State->Rebinds[player];
            std::erase_if(l_Rebinds, [&l_Location, binding, part](const InputBindingOverride& rebind) { return rebind.Binding == binding && rebind.Part == part && NamesLocation(std::string_view(rebind.Action), l_Location); });

            const InputSource l_Asset = GetAction(l_Location).Bindings[binding].Sources[part];
            if (source != l_Asset)
            {
                const InputActionMap& l_Map = s_State->Data->Maps[l_Location.Map];
                InputBindingOverride& l_Rebind = l_Rebinds.emplace_back();
                l_Rebind.Action = InputName(std::string_view(std::format("{}{}{}", std::string_view(l_Map.Name), InputActionsData::c_PathSeparator, std::string_view(l_Map.Actions[l_Location.Action].Name))));
                l_Rebind.Binding = binding;
                l_Rebind.Part = part;
                l_Rebind.From = l_Asset;
                l_Rebind.To = source;
            }

            if (IsActive(player))
            {
                FitSources(player);
            }

            SaveRebinds();
        }

        // A new rebind's outcome, in the slot of the one c_Kept rebinds before it
        [[nodiscard]] RebindOutcome& NewOutcome()
        {
            const std::uint32_t l_ID = std::exchange(s_State->NextRebind, s_State->NextRebind + 1 != 0 ? s_State->NextRebind + 1 : 1);
            RebindOutcome& l_Outcome = s_State->Outcomes[l_ID % InputRebind::c_Kept];
            l_Outcome = RebindOutcome{};
            l_Outcome.ID = l_ID;

            return l_Outcome;
        }

        [[nodiscard]] RebindOutcome* FindOutcome(std::uint32_t id)
        {
            RebindOutcome* l_Outcome = s_State != nullptr && id != 0 ? &s_State->Outcomes[id % InputRebind::c_Kept] : nullptr;

            return l_Outcome != nullptr && l_Outcome->ID == id ? l_Outcome : nullptr;
        }

        // The place in the asset now of an action a handle has, or c_NoAction
        [[nodiscard]] std::uint32_t FindHandleLocation(std::uint32_t slot, std::uint32_t session)
        {
            return IsRunning() && session == s_State->Session && slot < s_State->Slots.size() ? s_State->Slots[slot].Action : c_NoAction;
        }

        // The handle's action, for a rebind of a part there is for a player slot there is, or c_NoAction and logged
        [[nodiscard]] std::uint32_t FindRebindLocation(std::uint32_t slot, std::uint32_t session, std::uint32_t binding, std::uint32_t part, std::uint32_t player, std::string_view what)
        {
            const std::uint32_t l_Location = FindHandleLocation(slot, session);
            if (l_Location == c_NoAction)
            {
                TR_CORE_WARN("Input: no binding was {}, since the action is not one of the running scene's actions asset", what);

                return c_NoAction;
            }

            const InputActionDefinition& l_Action = GetAction(s_State->Locations[l_Location]);
            if (binding >= l_Action.Bindings.size() || part >= GetPartCount(l_Action.Bindings[binding]) || player >= InputActions::c_MaxPlayers)
            {
                TR_CORE_WARN("Input: {} was not {} for player {}, since the action has {} binding(s) and there are {} player slots", DescribePart(std::string_view(s_State->Slots[slot].Path), binding, part), what, player, l_Action.Bindings.size(), InputActions::c_MaxPlayers);

                return c_NoAction;
            }

            return l_Location;
        }

        // The listening rebind done, with what it came to. One ended by the frame's input holds the player's actions back until their sources are let go, so that input reaches none
        void EndListen(InputRebindState state, InputSource source = {}, std::string_view conflict = {}, bool byInput = false)
        {
            const std::uint32_t l_Player = s_State->Listen->Player;
            if (RebindOutcome* l_Outcome = FindOutcome(s_State->Listen->ID))
            {
                l_Outcome->State = state;
                l_Outcome->Source = source;
                l_Outcome->Conflict = InputName(conflict);
            }

            s_State->Listen.reset();
            if (byInput && IsActive(l_Player))
            {
                for (ActionState& it_Action : s_State->Players[l_Player].Actions)
                {
                    it_Action.HeldBack = true;
                }
            }
        }

        // The first key but Escape, mouse button or gamepad button of the devices in the mask that went down this frame, or else stick or trigger pushed past c_ListenPush, which one pushed as the rebind started has first to come back under
        [[nodiscard]] std::optional<InputSource> FindPushed(Listening& listen, std::uint32_t mask)
        {
            if ((mask & (1u << std::to_underlying(InputDevice::KeyboardMouse))) != 0)
            {
                for (std::uint16_t it_Key = 0; it_Key < std::to_underlying(KeyCode::Count); ++it_Key)
                {
                    if (it_Key != std::to_underlying(KeyCode::TR_ESCAPE) && Input::WasKeyPressed(static_cast<KeyCode>(it_Key)))
                    {
                        return InputSource{ InputSourceKind::Key, it_Key };
                    }
                }

                for (std::uint8_t it_Button = 0; it_Button < std::to_underlying(MouseCode::Count); ++it_Button)
                {
                    if (Input::WasMouseButtonPressed(static_cast<MouseCode>(it_Button)))
                    {
                        return InputSource{ InputSourceKind::MouseButton, it_Button };
                    }
                }
            }

            for (std::uint32_t it_Gamepad = 0; it_Gamepad < Input::c_MaxGamepads; ++it_Gamepad)
            {
                if ((mask & (1u << std::to_underlying(GetGamepadDevice(it_Gamepad)))) == 0 || !Input::IsGamepadConnected(it_Gamepad))
                {
                    continue;
                }

                for (std::uint8_t it_Button = 0; it_Button < std::to_underlying(GamepadButton::Count); ++it_Button)
                {
                    if (Input::WasGamepadButtonPressed(it_Gamepad, static_cast<GamepadButton>(it_Button)))
                    {
                        return InputSource{ InputSourceKind::GamepadButton, it_Button };
                    }
                }

                std::uint32_t& l_Held = listen.HeldAxes[it_Gamepad];
                for (std::uint8_t it_Axis = 0; it_Axis < std::to_underlying(GamepadAxis::Count); ++it_Axis)
                {
                    const std::uint32_t l_Bit = 1u << it_Axis;
                    if (std::abs(Input::GetGamepadAxis(it_Gamepad, static_cast<GamepadAxis>(it_Axis))) < c_ListenPush)
                    {
                        l_Held &= ~l_Bit;

                        continue;
                    }

                    if ((l_Held & l_Bit) != 0)
                    {
                        continue;
                    }

                    if (listen.WholeStick && it_Axis <= std::to_underlying(GamepadAxis::TR_RIGHT_STICK_Y))
                    {
                        return InputSource{ InputSourceKind::GamepadStick, static_cast<std::uint16_t>(it_Axis < std::to_underlying(GamepadAxis::TR_RIGHT_STICK_X) ? 0 : 1) };
                    }

                    return InputSource{ InputSourceKind::GamepadAxis, it_Axis };
                }
            }

            return std::nullopt;
        }

        // The listening rebind's part of the frame, before the actions are read: Escape cancels it, and the first input pushed of the player's devices rebinds to it, or is refused. Its action leaving the asset, or its player, cancels it
        void UpdateListen()
        {
            if (!s_State->Listen)
            {
                return;
            }

            Listening& l_Listen = *s_State->Listen;
            const std::uint32_t l_Location = l_Listen.Slot < s_State->Slots.size() ? s_State->Slots[l_Listen.Slot].Action : c_NoAction;
            const InputActionDefinition* l_Action = l_Location != c_NoAction ? &GetAction(s_State->Locations[l_Location]) : nullptr;
            if (l_Action == nullptr || l_Listen.Binding >= l_Action->Bindings.size() || l_Listen.Part >= GetPartCount(l_Action->Bindings[l_Listen.Binding]) || !IsActive(l_Listen.Player))
            {
                EndListen(InputRebindState::Canceled);

                return;
            }

            const std::uint32_t l_Mask = s_State->ReadMasks[l_Listen.Player];
            if ((l_Mask & (1u << std::to_underlying(InputDevice::KeyboardMouse))) != 0 && Input::WasKeyPressed(KeyCode::TR_ESCAPE))
            {
                EndListen(InputRebindState::Canceled, {}, {}, true);

                return;
            }

            const std::optional<InputSource> l_Source = FindPushed(l_Listen, l_Mask);
            if (!l_Source)
            {
                return;
            }

            if (const InputActionDefinition* l_Conflict = FindConflict(l_Listen.Player, l_Location, l_Listen.Binding, l_Listen.Part, *l_Source))
            {
                EndListen(InputRebindState::Refused, *l_Source, std::string_view(l_Conflict->Name), true);

                return;
            }

            SetRebind(l_Listen.Player, l_Location, l_Listen.Binding, l_Listen.Part, *l_Source);
            EndListen(InputRebindState::Bound, *l_Source, {}, true);
        }
    }

    InputAction InputAction::Find(std::string_view path)
    {
        if (!IsRunning() || path.empty())
        {
            return {};
        }

        const auto a_Known = std::ranges::find_if(s_State->Slots, [path](const ActionSlot& slot) { return std::string_view(slot.Path) == path; });
        if (a_Known != s_State->Slots.end())
        {
            return InputAction(static_cast<std::uint32_t>(a_Known - s_State->Slots.begin()), s_State->Session);
        }

        Sync();
        ActionSlot& l_Slot = s_State->Slots.emplace_back();
        l_Slot.Path = InputName(path);
        l_Slot.Action = Resolve(path);

        return InputAction(static_cast<std::uint32_t>(s_State->Slots.size() - 1), s_State->Session);
    }

    // The session above the slot. Sessions count up from 1, so a handle of 0 is one never found
    InputAction InputAction::FromHandle(std::uint64_t handle)
    {
        const std::uint32_t l_Session = static_cast<std::uint32_t>(handle >> 32);

        return l_Session != 0 ? InputAction(static_cast<std::uint32_t>(handle), l_Session) : InputAction();
    }

    std::uint64_t InputAction::ToHandle() const
    {
        return m_Session != 0 ? (static_cast<std::uint64_t>(m_Session) << 32) | m_Slot : 0;
    }

    std::string_view InputAction::GetPath() const
    {
        return IsCurrent() ? std::string_view(s_State->Slots[m_Slot].Path) : std::string_view();
    }

    bool InputAction::IsValid() const
    {
        return IsCurrent() && s_State->Slots[m_Slot].Action != c_NoAction;
    }

    bool InputAction::IsCurrent() const
    {
        return IsRunning() && m_Session == s_State->Session && m_Slot < s_State->Slots.size();
    }

    bool InputAction::IsPressed(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);

        return l_State != nullptr && l_State->Held;
    }

    bool InputAction::WasPressed(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);

        return l_State != nullptr && (s_State->InFixedStep ? l_State->FixedPressed : l_State->Pressed);
    }

    bool InputAction::WasReleased(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);

        return l_State != nullptr && (s_State->InFixedStep ? l_State->FixedReleased : l_State->Released);
    }

    bool InputAction::WasPerformed(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);

        return l_State != nullptr && (s_State->InFixedStep ? l_State->FixedPerformed : l_State->Performed);
    }

    bool InputAction::WasCanceled(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);

        return l_State != nullptr && (s_State->InFixedStep ? l_State->FixedCanceled : l_State->Canceled);
    }

    float InputAction::ReadProgress(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);

        return l_State != nullptr ? l_State->Progress : 0.0f;
    }

    float InputAction::ReadValue(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);
        if (l_State == nullptr)
        {
            return 0.0f;
        }

        return s_State->Locations[s_State->Slots[m_Slot].Action].Type == InputActionType::Vector2 ? glm::length(l_State->Value) : l_State->Value.x;
    }

    glm::vec2 InputAction::ReadVector2(std::uint32_t player) const
    {
        const ActionState* l_State = FindState(m_Slot, m_Session, player);

        return l_State != nullptr ? l_State->Value : glm::vec2(0.0f);
    }

    std::uint32_t InputAction::GetBindingCount() const
    {
        const std::uint32_t l_Location = FindHandleLocation(m_Slot, m_Session);

        return l_Location != c_NoAction ? static_cast<std::uint32_t>(GetAction(s_State->Locations[l_Location]).Bindings.size()) : 0;
    }

    std::uint32_t InputAction::GetPartCount(std::uint32_t binding) const
    {
        const std::uint32_t l_Location = FindHandleLocation(m_Slot, m_Session);
        const InputActionDefinition* l_Action = l_Location != c_NoAction ? &GetAction(s_State->Locations[l_Location]) : nullptr;

        return l_Action != nullptr && binding < l_Action->Bindings.size() ? Trinity::GetPartCount(l_Action->Bindings[binding]) : 0;
    }

    InputSource InputAction::GetSource(std::uint32_t binding, std::uint32_t part, std::uint32_t player) const
    {
        const std::uint32_t l_Location = FindHandleLocation(m_Slot, m_Session);

        return part < GetPartCount(binding) && player < InputActions::c_MaxPlayers ? GetPlayerSource(player, l_Location, binding, part) : InputSource{};
    }

    InputSource InputAction::GetDefaultSource(std::uint32_t binding, std::uint32_t part) const
    {
        const std::uint32_t l_Location = FindHandleLocation(m_Slot, m_Session);

        return part < GetPartCount(binding) ? GetAction(s_State->Locations[l_Location]).Bindings[binding].Sources[part] : InputSource{};
    }

    // The source is checked by its name, so it is one a rebinds file can hold
    InputRebind InputAction::Rebind(std::uint32_t binding, std::uint32_t part, InputSource source, std::uint32_t player) const
    {
        const std::uint32_t l_Location = FindRebindLocation(m_Slot, m_Session, binding, part, player, "rebound");
        if (l_Location == c_NoAction)
        {
            return {};
        }

        if (source.Kind != InputSourceKind::None && ParseInputSource(ToString(source)) != source)
        {
            TR_CORE_WARN("Input: {} was not rebound, since its source is none this build knows", DescribePart(std::string_view(s_State->Slots[m_Slot].Path), binding, part));

            return {};
        }

        RebindOutcome& l_Outcome = NewOutcome();
        l_Outcome.Source = source;
        if (const InputActionDefinition* l_Conflict = FindConflict(player, l_Location, binding, part, source))
        {
            l_Outcome.State = InputRebindState::Refused;
            l_Outcome.Conflict = l_Conflict->Name;

            return InputRebind(l_Outcome.ID);
        }

        SetRebind(player, l_Location, binding, part, source);
        l_Outcome.State = InputRebindState::Bound;

        return InputRebind(l_Outcome.ID);
    }

    // Each pad's axes are read as they are now, as the scene reads them, so one already pushed is not taken until it has been let go
    InputRebind InputAction::StartRebind(std::uint32_t binding, std::uint32_t part, std::uint32_t player) const
    {
        const std::uint32_t l_Location = FindRebindLocation(m_Slot, m_Session, binding, part, player, "listened for");
        if (l_Location == c_NoAction)
        {
            return {};
        }

        if (!IsActive(player))
        {
            TR_CORE_WARN("Input: {} was not listened for, since there is no player {} to listen to", DescribePart(std::string_view(s_State->Slots[m_Slot].Path), binding, part), player);

            return {};
        }

        if (s_State->Listen)
        {
            EndListen(InputRebindState::Canceled);
        }

        RebindOutcome& l_Outcome = NewOutcome();
        l_Outcome.State = InputRebindState::Waiting;
        const InputActionDefinition& l_Action = GetAction(s_State->Locations[l_Location]);
        Listening& l_Listen = s_State->Listen.emplace();
        l_Listen.ID = l_Outcome.ID;
        l_Listen.Slot = m_Slot;
        l_Listen.Binding = binding;
        l_Listen.Part = part;
        l_Listen.Player = player;
        l_Listen.WholeStick = l_Action.Type == InputActionType::Vector2 && l_Action.Bindings[binding].Composite == InputCompositeKind::None;
        for (std::uint32_t it_Gamepad = 0; it_Gamepad < Input::c_MaxGamepads; ++it_Gamepad)
        {
            for (std::uint8_t it_Axis = 0; it_Axis < std::to_underlying(GamepadAxis::Count); ++it_Axis)
            {
                l_Listen.HeldAxes[it_Gamepad] |= std::abs(Input::GetGamepadAxis(it_Gamepad, static_cast<GamepadAxis>(it_Axis))) >= c_ListenPush ? 1u << it_Axis : 0u;
            }
        }

        return InputRebind(l_Outcome.ID);
    }

    void InputAction::ResetBinding(std::uint32_t binding, std::uint32_t part, std::uint32_t player) const
    {
        const std::uint32_t l_Location = FindRebindLocation(m_Slot, m_Session, binding, part, player, "reset");
        if (l_Location == c_NoAction)
        {
            return;
        }

        SetRebind(player, l_Location, binding, part, GetAction(s_State->Locations[l_Location]).Bindings[binding].Sources[part]);
    }

    InputRebind InputRebind::FromHandle(std::uint64_t handle)
    {
        return handle <= std::numeric_limits<std::uint32_t>::max() ? InputRebind(static_cast<std::uint32_t>(handle)) : InputRebind();
    }

    InputRebindState InputRebind::GetState() const
    {
        const RebindOutcome* l_Outcome = FindOutcome(m_ID);

        return l_Outcome != nullptr ? l_Outcome->State : InputRebindState::None;
    }

    InputSource InputRebind::GetSource() const
    {
        const RebindOutcome* l_Outcome = FindOutcome(m_ID);

        return l_Outcome != nullptr ? l_Outcome->Source : InputSource{};
    }

    std::string_view InputRebind::GetConflict() const
    {
        const RebindOutcome* l_Outcome = FindOutcome(m_ID);

        return l_Outcome != nullptr ? std::string_view(l_Outcome->Conflict) : std::string_view();
    }

    void InputRebind::Cancel() const
    {
        if (s_State != nullptr && s_State->Listen && s_State->Listen->ID == m_ID && m_ID != 0)
        {
            EndListen(InputRebindState::Canceled);
        }
    }

    UUID InputActions::GetAsset()
    {
        return IsRunning() ? s_State->Asset : UUID();
    }

    bool InputActions::IsReady()
    {
        return Sync() != nullptr;
    }

    const InputActionsData* InputActions::GetData()
    {
        return Sync();
    }

    // Read from the states alone, which a reload sizes again, so it is safe whether or not the asset has been asked for since
    std::optional<InputActions::ActionReading> InputActions::ReadAction(std::uint32_t action, std::uint32_t player)
    {
        if (!IsActive(player) || action >= s_State->Players[player].Actions.size())
        {
            return std::nullopt;
        }

        const ActionState& l_State = s_State->Players[player].Actions[action];

        return ActionReading{ l_State.Value, l_State.Held, l_State.Progress, l_State.Performed };
    }

    bool InputActions::EnableMap(std::string_view map, std::uint32_t player)
    {
        return SetMapEnabled(map, player, true);
    }

    // Its actions read as released from the next frame, so a held one is released once
    bool InputActions::DisableMap(std::string_view map, std::uint32_t player)
    {
        return SetMapEnabled(map, player, false);
    }

    // Before the asset is ready, as the change asked for last says it will be
    bool InputActions::IsMapEnabled(std::string_view map, std::uint32_t player)
    {
        if (!IsActive(player))
        {
            return false;
        }

        const PlayerState& l_Player = s_State->Players[player];
        if (Sync() == nullptr)
        {
            const auto a_Last = std::ranges::find_if(l_Player.PendingChanges | std::views::reverse, [map](const MapChange& change) { return std::string_view(change.Map) == map; });

            return a_Last != (l_Player.PendingChanges | std::views::reverse).end() && a_Last->Enable;
        }

        return HasName(l_Player.EnabledNames, map);
    }

    std::uint32_t InputActions::AddPlayer()
    {
        if (!IsRunning())
        {
            return c_NoPlayer;
        }

        for (std::uint32_t it_Player = 1; it_Player < c_MaxPlayers; ++it_Player)
        {
            PlayerState& l_Player = s_State->Players[it_Player];
            if (!l_Player.Active)
            {
                l_Player = PlayerState{};
                l_Player.Active = true;
                l_Player.FirstMapPending = true;
                Fit(l_Player);
                FitSources(it_Player);
                UpdateReadMasks();

                return it_Player;
            }
        }

        return c_NoPlayer;
    }

    void InputActions::RemovePlayer(std::uint32_t player)
    {
        if (player == 0 || !IsActive(player))
        {
            return;
        }

        s_State->Players[player] = PlayerState{};
        for (std::uint32_t& it_Assigned : s_State->DevicePlayers)
        {
            it_Assigned = it_Assigned == player ? c_NoPlayer : it_Assigned;
        }

        UpdateReadMasks();
    }

    bool InputActions::IsPlayer(std::uint32_t player)
    {
        return IsActive(player);
    }

    std::uint32_t InputActions::GetPlayerCount()
    {
        return IsRunning() ? static_cast<std::uint32_t>(std::ranges::count_if(s_State->Players, &PlayerState::Active)) : 0;
    }

    bool InputActions::AssignDevice(InputDevice device, std::uint32_t player)
    {
        if (!IsActive(player) || std::to_underlying(device) >= c_DeviceCount)
        {
            return false;
        }

        s_State->DevicePlayers[std::to_underlying(device)] = player;
        UpdateReadMasks();

        return true;
    }

    void InputActions::UnassignDevice(InputDevice device)
    {
        if (IsRunning() && std::to_underlying(device) < c_DeviceCount)
        {
            s_State->DevicePlayers[std::to_underlying(device)] = c_NoPlayer;
            UpdateReadMasks();
        }
    }

    std::uint32_t InputActions::GetDevicePlayer(InputDevice device)
    {
        if (!IsRunning() || std::to_underlying(device) >= c_DeviceCount)
        {
            return c_NoPlayer;
        }

        const std::uint32_t l_Player = s_State->DevicePlayers[std::to_underlying(device)];

        return IsActive(l_Player) ? l_Player : c_NoPlayer;
    }

    bool InputActions::ReadsDevice(std::uint32_t player, InputDevice device)
    {
        return IsActive(player) && std::to_underlying(device) < c_DeviceCount && (s_State->ReadMasks[player] & (1u << std::to_underlying(device))) != 0;
    }

    InputDevice InputActions::GetJoiningDevice()
    {
        if (!IsRunning())
        {
            return InputDevice::None;
        }

        if (GetDevicePlayer(InputDevice::KeyboardMouse) == c_NoPlayer && AnyKeyOrMouseButtonPressed())
        {
            return InputDevice::KeyboardMouse;
        }

        for (std::uint32_t it_Gamepad = 0; it_Gamepad < Input::c_MaxGamepads; ++it_Gamepad)
        {
            const InputDevice l_Device = GetGamepadDevice(it_Gamepad);
            if (GetDevicePlayer(l_Device) == c_NoPlayer && Input::IsGamepadConnected(it_Gamepad) && AnyGamepadButtonPressed(it_Gamepad))
            {
                return l_Device;
            }
        }

        return InputDevice::None;
    }

    void InputActions::ResetBindings(std::uint32_t player)
    {
        if (!IsRunning() || player >= c_MaxPlayers)
        {
            TR_CORE_WARN("Input: no bindings were reset, since {}", IsRunning() ? std::format("there is no player slot {}", player) : std::string("no scene runs"));

            return;
        }

        s_State->Rebinds[player] = InputVector<InputBindingOverride>();
        if (IsActive(player))
        {
            FitSources(player);
        }

        SaveRebinds();
    }

    namespace InputActionsRuntime
    {
        void Initialize()
        {
            TR_CORE_ASSERT(s_State == nullptr, "InputActions is initialized once.");

            s_State = Memory::New<State>(MemoryTag::Input);
            ResetPlayers();
            AssetManager::RegisterLoader(s_State->Loader);
        }

        void Shutdown()
        {
            if (s_State == nullptr)
            {
                return;
            }

            Stop();
            AssetManager::UnregisterLoader(s_State->Loader.GetAssetType());
            Memory::Delete(s_State);
            s_State = nullptr;
        }

        void Start(UUID asset)
        {
            Stop();

            ++s_State->Session;
            s_State->Running = true;
            s_State->Asset = asset;
            s_State->WarnedType = false;
            ResetPlayers();
            s_State->Players[0].Active = true;
            s_State->Players[0].FirstMapPending = true;
            UpdateReadMasks();
            if (!asset)
            {
                TR_CORE_INFO("Input: no input actions asset is set, so every action reads as released");

                return;
            }

            AssetManager::Acquire(asset);
            LoadRebinds();
        }

        // Rumble is the SceneManager's to stop. What was found and enabled goes, and the memory with it
        void Stop()
        {
            if (!s_State->Running)
            {
                return;
            }

            AssetManager::Release(s_State->Asset);
            ++s_State->Session;
            s_State->Running = false;
            s_State->InFixedStep = false;
            s_State->Asset = {};
            s_State->Data = nullptr;
            s_State->BuiltSerial = 0;
            s_State->Locations = InputVector<ActionLocation>();
            s_State->Slots = InputVector<ActionSlot>();
            s_State->SourceCount = 0;
            s_State->Rebinds = {};
            s_State->Outcomes = {};
            s_State->Listen.reset();
            ResetPlayers();
        }

        // A rebind that listens takes its input first, and while it waits its player's actions read as released
        void Update(Timestep timestep)
        {
            const InputActionsData* l_Data = Sync();
            if (l_Data == nullptr)
            {
                return;
            }

            UpdateListen();
            const float l_Seconds = timestep.GetSeconds();

            for (std::uint32_t it_Player = 0; it_Player < InputActions::c_MaxPlayers; ++it_Player)
            {
                PlayerState& l_Player = s_State->Players[it_Player];
                if (!l_Player.Active)
                {
                    continue;
                }

                const std::uint32_t l_Mask = s_State->ReadMasks[it_Player];
                const bool l_Listening = s_State->Listen && s_State->Listen->Player == it_Player;
                for (std::size_t it_Action = 0; it_Action < s_State->Locations.size(); ++it_Action)
                {
                    const ActionLocation& l_Location = s_State->Locations[it_Action];
                    Evaluate(l_Data->Maps[l_Location.Map].Actions[l_Location.Action], l_Player.Sources.data() + l_Location.FirstSource, l_Player.EnabledMaps[l_Location.Map] != 0 && !l_Listening, l_Mask, l_Seconds, l_Player.Actions[it_Action]);
                }
            }
        }

        void BeginFixedStep()
        {
            if (IsRunning())
            {
                s_State->InFixedStep = true;
            }
        }

        void EndFixedStep()
        {
            if (!IsRunning())
            {
                return;
            }

            s_State->InFixedStep = false;
            for (PlayerState& it_Player : s_State->Players)
            {
                for (ActionState& it_Action : it_Player.Actions)
                {
                    it_Action.FixedPressed = false;
                    it_Action.FixedReleased = false;
                    it_Action.FixedPerformed = false;
                    it_Action.FixedCanceled = false;
                }
            }
        }
    }
}