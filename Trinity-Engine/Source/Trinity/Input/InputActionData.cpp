#include "Trinity/Input/InputActionData.hpp"

#include "Trinity/Asset/CookedText.hpp"
#include "Trinity/Input/GamepadCodes.hpp"
#include "Trinity/Input/KeyCodes.hpp"
#include "Trinity/Input/MouseCodes.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <format>
#include <utility>

namespace Trinity
{
    namespace
    {
        template<typename Code>
        struct SourceName
        {
            Code Value;
            std::string_view Name;
        };

        // Each key as C# names it, which InteropGen makes from KeyCodes.hpp, so a key added there needs its name here to be bound by name. One without a name is written as its number
        constexpr std::array c_KeyNames = std::to_array<SourceName<KeyCode>>({
            { KeyCode::TR_SPACE, "Space" },{ KeyCode::TR_APOSTROPHE, "Apostrophe" },{ KeyCode::TR_COMMA, "Comma" },{ KeyCode::TR_MINUS, "Minus" },{ KeyCode::TR_PERIOD, "Period" },{ KeyCode::TR_SLASH, "Slash" },
            { KeyCode::TR_D0, "D0" },{ KeyCode::TR_D1, "D1" },{ KeyCode::TR_D2, "D2" },{ KeyCode::TR_D3, "D3" },{ KeyCode::TR_D4, "D4" },{ KeyCode::TR_D5, "D5" },
            { KeyCode::TR_D6, "D6" },{ KeyCode::TR_D7, "D7" },{ KeyCode::TR_D8, "D8" },{ KeyCode::TR_D9, "D9" },{ KeyCode::TR_SEMICOLON, "Semicolon" },{ KeyCode::TR_EQUAL, "Equal" },
            { KeyCode::TR_A, "A" },{ KeyCode::TR_B, "B" },{ KeyCode::TR_C, "C" },{ KeyCode::TR_D, "D" },{ KeyCode::TR_E, "E" },{ KeyCode::TR_F, "F" },
            { KeyCode::TR_G, "G" },{ KeyCode::TR_H, "H" },{ KeyCode::TR_I, "I" },{ KeyCode::TR_J, "J" },{ KeyCode::TR_K, "K" },{ KeyCode::TR_L, "L" },
            { KeyCode::TR_M, "M" },{ KeyCode::TR_N, "N" },{ KeyCode::TR_O, "O" },{ KeyCode::TR_P, "P" },{ KeyCode::TR_Q, "Q" },{ KeyCode::TR_R, "R" },
            { KeyCode::TR_S, "S" },{ KeyCode::TR_T, "T" },{ KeyCode::TR_U, "U" },{ KeyCode::TR_V, "V" },{ KeyCode::TR_W, "W" },{ KeyCode::TR_X, "X" },
            { KeyCode::TR_Y, "Y" },{ KeyCode::TR_Z, "Z" },{ KeyCode::TR_LEFT_BRACKET, "LeftBracket" },{ KeyCode::TR_BACKSLASH, "Backslash" },{ KeyCode::TR_RIGHT_BRACKET, "RightBracket" },{ KeyCode::TR_GRAVE_ACCENT, "GraveAccent" },
            { KeyCode::TR_WORLD_2, "World2" },{ KeyCode::TR_ESCAPE, "Escape" },{ KeyCode::TR_ENTER, "Enter" },{ KeyCode::TR_TAB, "Tab" },{ KeyCode::TR_BACKSPACE, "Backspace" },{ KeyCode::TR_INSERT, "Insert" },
            { KeyCode::TR_DELETE, "Delete" },{ KeyCode::TR_RIGHT, "Right" },{ KeyCode::TR_LEFT, "Left" },{ KeyCode::TR_DOWN, "Down" },{ KeyCode::TR_UP, "Up" },{ KeyCode::TR_PAGE_UP, "PageUp" },
            { KeyCode::TR_PAGE_DOWN, "PageDown" },{ KeyCode::TR_HOME, "Home" },{ KeyCode::TR_END, "End" },{ KeyCode::TR_CAPS_LOCK, "CapsLock" },{ KeyCode::TR_SCROLL_LOCK, "ScrollLock" },{ KeyCode::TR_NUM_LOCK, "NumLock" },
            { KeyCode::TR_PRINT_SCREEN, "PrintScreen" },{ KeyCode::TR_PAUSE, "Pause" },{ KeyCode::TR_F1, "F1" },{ KeyCode::TR_F2, "F2" },{ KeyCode::TR_F3, "F3" },{ KeyCode::TR_F4, "F4" },
            { KeyCode::TR_F5, "F5" },{ KeyCode::TR_F6, "F6" },{ KeyCode::TR_F7, "F7" },{ KeyCode::TR_F8, "F8" },{ KeyCode::TR_F9, "F9" },{ KeyCode::TR_F10, "F10" },
            { KeyCode::TR_F11, "F11" },{ KeyCode::TR_F12, "F12" },{ KeyCode::TR_F13, "F13" },{ KeyCode::TR_F14, "F14" },{ KeyCode::TR_F15, "F15" },{ KeyCode::TR_F16, "F16" },
            { KeyCode::TR_F17, "F17" },{ KeyCode::TR_F18, "F18" },{ KeyCode::TR_F19, "F19" },{ KeyCode::TR_F20, "F20" },{ KeyCode::TR_F21, "F21" },{ KeyCode::TR_F22, "F22" },
            { KeyCode::TR_F23, "F23" },{ KeyCode::TR_F24, "F24" },{ KeyCode::TR_KP0, "Keypad0" },{ KeyCode::TR_KP1, "Keypad1" },{ KeyCode::TR_KP2, "Keypad2" },{ KeyCode::TR_KP3, "Keypad3" },
            { KeyCode::TR_KP4, "Keypad4" },{ KeyCode::TR_KP5, "Keypad5" },{ KeyCode::TR_KP6, "Keypad6" },{ KeyCode::TR_KP7, "Keypad7" },{ KeyCode::TR_KP8, "Keypad8" },{ KeyCode::TR_KP9, "Keypad9" },
            { KeyCode::TR_KP_DECIMAL, "KeypadDecimal" },{ KeyCode::TR_KP_DIVIDE, "KeypadDivide" },{ KeyCode::TR_KP_MULTIPLY, "KeypadMultiply" },{ KeyCode::TR_KP_SUBTRACT, "KeypadSubtract" },{ KeyCode::TR_KP_ADD, "KeypadAdd" },{ KeyCode::TR_KP_ENTER, "KeypadEnter" },
            { KeyCode::TR_LEFT_SHIFT, "LeftShift" },{ KeyCode::TR_LEFT_CONTROL, "LeftControl" },{ KeyCode::TR_LEFT_ALT, "LeftAlt" },{ KeyCode::TR_LEFT_SUPER, "LeftSuper" },{ KeyCode::TR_RIGHT_SHIFT, "RightShift" },{ KeyCode::TR_RIGHT_CONTROL, "RightControl" },
            { KeyCode::TR_RIGHT_ALT, "RightAlt" },{ KeyCode::TR_RIGHT_SUPER, "RightSuper" },{ KeyCode::TR_MENU, "Menu" },{ KeyCode::TR_APP_BACK, "AppBack" },{ KeyCode::TR_APP_FORWARD, "AppForward" }
            });

        // A stick pressed in is LeftStickPress here, since LeftStick names the stick's two axes together
        constexpr std::array<std::string_view, std::to_underlying(GamepadButton::Count)> c_GamepadButtonNames{ "A", "B", "X", "Y", "LeftShoulder", "RightShoulder", "View", "Menu", "LeftStickPress", "RightStickPress", "DpadUp", "DpadDown", "DpadLeft", "DpadRight" };
        constexpr std::array<std::string_view, std::to_underlying(GamepadAxis::Count)> c_GamepadAxisNames{ "LeftStickX", "LeftStickY", "RightStickX", "RightStickY", "LeftTrigger", "RightTrigger" };
        constexpr std::array<std::string_view, 2> c_GamepadStickNames{ "LeftStick", "RightStick" };
        constexpr std::array<std::string_view, 3> c_MouseButtonNames{ "Left", "Right", "Middle" };
        constexpr std::array<std::string_view, 3> c_MouseDeltaNames{ "Delta", "DeltaX", "DeltaY" };
        constexpr std::array<std::string_view, 3> c_MouseScrollNames{ "Scroll", "ScrollX", "ScrollY" };

        constexpr std::array<std::string_view, 2> c_AxisParts{ "Negative", "Positive" };
        constexpr std::array<std::string_view, 4> c_Vector2Parts{ "Up", "Down", "Left", "Right" };

        constexpr std::string_view c_Keyboard = "Keyboard";
        constexpr std::string_view c_Mouse = "Mouse";
        constexpr std::string_view c_Gamepad = "Gamepad";

        std::atomic<std::uint64_t> s_NextSerial{ 1 };

        template<std::size_t Count>
        std::optional<std::uint16_t> FindName(const std::array<std::string_view, Count>& names, std::string_view name)
        {
            const auto a_Found = std::ranges::find(names, name);

            return a_Found != names.end() ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(a_Found - names.begin())) : std::nullopt;
        }

        std::optional<std::uint16_t> ParseCode(std::string_view text)
        {
            std::uint16_t l_Code = 0;
            const auto [a_End, a_Error] = std::from_chars(text.data(), text.data() + text.size(), l_Code);

            return a_Error == std::errc() && a_End == text.data() + text.size() && !text.empty() ? std::optional<std::uint16_t>(l_Code) : std::nullopt;
        }

        // A map's or an action's name, which a path to an action holds, so it is not empty and has no '/'
        bool IsValidName(std::string_view name)
        {
            return !name.empty() && name.find(InputActionsData::c_PathSeparator) == std::string_view::npos;
        }

        // Map/Action, each a valid name
        bool IsValidPath(std::string_view path)
        {
            const std::size_t l_Separator = path.find(InputActionsData::c_PathSeparator);

            return l_Separator != std::string_view::npos && IsValidName(path.substr(0, l_Separator)) && IsValidName(path.substr(l_Separator + 1));
        }

        void WriteSource(ComponentWriter& writer, std::string_view key, InputSource source)
        {
            if (source.Kind != InputSourceKind::None)
            {
                writer.Write(key, std::string_view(ToString(source)));
            }
        }

        void WriteProcessor(ComponentWriter& writer, const InputProcessor& processor)
        {
            writer.Write("Kind", ToString(processor.Kind));
            switch (processor.Kind)
            {
                case InputProcessorKind::RadialDeadzone:
                case InputProcessorKind::AxialDeadzone:
                {
                    writer.Write("Min", processor.Min);
                    writer.Write("Max", processor.Max);

                    break;
                }
                case InputProcessorKind::Invert:
                {
                    writer.Write("InvertX", processor.InvertX);
                    writer.Write("InvertY", processor.InvertY);

                    break;
                }
                case InputProcessorKind::Scale:
                {
                    writer.Write("Scale", processor.Scale);

                    break;
                }
                case InputProcessorKind::Normalize:
                {
                    break;
                }
            }
        }

        void WriteInteraction(ComponentWriter& writer, const InputInteraction& interaction)
        {
            if (interaction.Kind == InputInteractionKind::None)
            {
                return;
            }

            writer.BeginMap("Interaction");
            writer.Write("Kind", ToString(interaction.Kind));
            switch (interaction.Kind)
            {
                case InputInteractionKind::None:
                {
                    break;
                }
                case InputInteractionKind::Hold:
                case InputInteractionKind::Tap:
                {
                    writer.Write("Duration", interaction.Duration);

                    break;
                }
                case InputInteractionKind::MultiTap:
                {
                    writer.Write("Count", interaction.Count);
                    writer.Write("Duration", interaction.Duration);
                    writer.Write("Spacing", interaction.Spacing);

                    break;
                }
            }

            writer.End();
        }

        // Where a part of the file lies, for its error
        std::string Describe(std::string_view map, std::string_view action)
        {
            return action.empty() ? std::format("map '{}'", map) : std::format("action '{}/{}'", map, action);
        }

        // Each malformed key or unknown name is given back as what is wrong, or nothing
        std::string ReadSource(const YAML::Node& map, std::string_view key, InputSource& source)
        {
            std::string l_Text;
            std::string l_Malformed;
            CookedText::ReadString(map, key, l_Text, l_Malformed);
            if (!l_Malformed.empty())
            {
                return std::format("{} is malformed", key);
            }

            const std::optional<InputSource> l_Source = ParseInputSource(l_Text);
            if (!l_Source)
            {
                return std::format("{} names '{}', which is no key, button, stick or axis this build knows", key, l_Text);
            }

            source = *l_Source;

            return {};
        }

        std::string ReadProcessor(const YAML::Node& node, InputProcessor& processor)
        {
            if (!node.IsMap())
            {
                return "a processor is not a map";
            }

            std::string l_Kind;
            std::string l_Malformed;
            CookedText::ReadString(node, "Kind", l_Kind, l_Malformed);
            const std::optional<InputProcessorKind> l_Parsed = ParseInputProcessorKind(l_Kind);
            if (!l_Malformed.empty() || !l_Parsed)
            {
                return std::format("a processor's Kind '{}' is not one this build knows", l_Kind);
            }

            processor.Kind = *l_Parsed;
            switch (processor.Kind)
            {
                case InputProcessorKind::RadialDeadzone:
                case InputProcessorKind::AxialDeadzone:
                {
                    CookedText::ReadNumber(node, "Min", processor.Min, l_Malformed);
                    CookedText::ReadNumber(node, "Max", processor.Max, l_Malformed);

                    break;
                }
                case InputProcessorKind::Invert:
                {
                    CookedText::ReadBool(node, "InvertX", processor.InvertX, l_Malformed);
                    CookedText::ReadBool(node, "InvertY", processor.InvertY, l_Malformed);

                    break;
                }
                case InputProcessorKind::Scale:
                {
                    CookedText::ReadFloats(node, "Scale", std::span(&processor.Scale.x, 2), l_Malformed);

                    break;
                }
                case InputProcessorKind::Normalize:
                {
                    break;
                }
            }

            return l_Malformed.empty() ? std::string() : std::format("a {} processor's {} is malformed", l_Kind, l_Malformed);
        }

        // Above 0 and finite, which a stray minus or a nan is not
        bool IsValidTime(float seconds)
        {
            return seconds > 0.0f && std::isfinite(seconds);
        }

        std::string ReadInteraction(const YAML::Node& node, InputInteraction& interaction)
        {
            if (!node.IsMap())
            {
                return "Interaction is not a map";
            }

            std::string l_Kind;
            std::string l_Malformed;
            CookedText::ReadString(node, "Kind", l_Kind, l_Malformed);
            const std::optional<InputInteractionKind> l_Parsed = ParseInputInteractionKind(l_Kind);
            if (!l_Malformed.empty() || !l_Parsed)
            {
                return std::format("Interaction's Kind '{}' is not one this build knows", l_Kind);
            }

            interaction = GetDefaultInteraction(*l_Parsed);
            switch (interaction.Kind)
            {
                case InputInteractionKind::None:
                {
                    break;
                }
                case InputInteractionKind::Hold:
                case InputInteractionKind::Tap:
                {
                    CookedText::ReadNumber(node, "Duration", interaction.Duration, l_Malformed);

                    break;
                }
                case InputInteractionKind::MultiTap:
                {
                    CookedText::ReadNumber(node, "Count", interaction.Count, l_Malformed);
                    CookedText::ReadNumber(node, "Duration", interaction.Duration, l_Malformed);
                    CookedText::ReadNumber(node, "Spacing", interaction.Spacing, l_Malformed);

                    break;
                }
            }

            if (!l_Malformed.empty())
            {
                return std::format("a {} interaction's {} is malformed", l_Kind, l_Malformed);
            }

            if (interaction.Kind != InputInteractionKind::None && !IsValidTime(interaction.Duration))
            {
                return std::format("a {} interaction's Duration is {}, and needs to be above 0 seconds", l_Kind, interaction.Duration);
            }

            if (interaction.Kind == InputInteractionKind::MultiTap && !IsValidTime(interaction.Spacing))
            {
                return std::format("a MultiTap interaction's Spacing is {}, and needs to be above 0 seconds", interaction.Spacing);
            }

            if (interaction.Kind == InputInteractionKind::MultiTap && interaction.Count < 2)
            {
                return std::format("a MultiTap interaction's Count is {}, and needs to be at least 2 taps", interaction.Count);
            }

            return {};
        }

        std::string ReadBinding(const YAML::Node& node, InputBinding& binding)
        {
            if (!node.IsMap())
            {
                return "a binding is not a map";
            }

            std::string l_Composite(ToString(InputCompositeKind::None));
            std::string l_Malformed;
            CookedText::ReadString(node, "Composite", l_Composite, l_Malformed);
            const std::optional<InputCompositeKind> l_Kind = ParseInputCompositeKind(l_Composite);
            if (!l_Malformed.empty() || !l_Kind)
            {
                return std::format("a binding's Composite '{}' is not one this build knows", l_Composite);
            }

            binding.Composite = *l_Kind;
            const std::span<const std::string_view> l_Parts = GetCompositeParts(binding.Composite);
            if (l_Parts.empty())
            {
                if (std::string l_Error = ReadSource(node, "Source", binding.Sources[0]); !l_Error.empty())
                {
                    return l_Error;
                }
            }

            for (std::size_t it_Part = 0; it_Part < l_Parts.size(); ++it_Part)
            {
                if (std::string l_Error = ReadSource(node, l_Parts[it_Part], binding.Sources[it_Part]); !l_Error.empty())
                {
                    return l_Error;
                }
            }

            const YAML::Node l_Processors = node["Processors"];
            if (!l_Processors)
            {
                return {};
            }

            if (!l_Processors.IsSequence())
            {
                return "a binding's Processors is not a list";
            }

            for (std::size_t it_Processor = 0; it_Processor < l_Processors.size(); ++it_Processor)
            {
                InputProcessor& l_Processor = binding.Processors.emplace_back();
                if (std::string l_Error = ReadProcessor(l_Processors[it_Processor], l_Processor); !l_Error.empty())
                {
                    return l_Error;
                }
            }

            return {};
        }

        // A name and its list, which every map and action has. The name must be one a path can hold, and new among those already read
        template<typename Item>
        std::string ReadNamed(const YAML::Node& node, const InputVector<Item>& read, std::string_view what, InputName& name)
        {
            if (!node.IsMap())
            {
                return std::format("a {} is not a map", what);
            }

            std::string l_Name;
            std::string l_Malformed;
            CookedText::ReadString(node, "Name", l_Name, l_Malformed);
            if (!l_Malformed.empty() || !IsValidName(l_Name))
            {
                return std::format("a {} is named '{}', which is empty or has a '/'", what, l_Name);
            }

            if (std::ranges::any_of(read, [&l_Name](const Item& item) { return std::string_view(item.Name) == l_Name; }))
            {
                return std::format("two {}s are named '{}'", what, l_Name);
            }

            name = InputName(std::string_view(l_Name));

            return {};
        }

        // A list that is missing is empty
        std::optional<YAML::Node> GetList(const YAML::Node& node, std::string_view key)
        {
            const YAML::Node l_List = node[std::string(key)];
            if (!l_List)
            {
                return YAML::Node(YAML::NodeType::Sequence);
            }

            return l_List.IsSequence() ? std::optional<YAML::Node>(l_List) : std::nullopt;
        }

        std::string ReadAction(const YAML::Node& node, const InputActionMap& map, InputActionDefinition& action)
        {
            if (std::string l_Error = ReadNamed(node, map.Actions, "action", action.Name); !l_Error.empty())
            {
                return std::format("{}: {}", Describe(map.Name, {}), l_Error);
            }

            std::string l_Type(ToString(action.Type));
            std::string l_Malformed;
            CookedText::ReadString(node, "Type", l_Type, l_Malformed);
            const std::optional<InputActionType> l_Parsed = ParseInputActionType(l_Type);
            if (!l_Malformed.empty() || !l_Parsed)
            {
                return std::format("{}: Type '{}' is not one this build knows", Describe(map.Name, action.Name), l_Type);
            }

            action.Type = *l_Parsed;
            if (const YAML::Node l_Interaction = node["Interaction"]; l_Interaction)
            {
                if (std::string l_Error = ReadInteraction(l_Interaction, action.Interaction); !l_Error.empty())
                {
                    return std::format("{}: {}", Describe(map.Name, action.Name), l_Error);
                }
            }

            const std::optional<YAML::Node> l_Bindings = GetList(node, "Bindings");
            if (!l_Bindings)
            {
                return std::format("{}: Bindings is not a list", Describe(map.Name, action.Name));
            }

            for (std::size_t it_Binding = 0; it_Binding < l_Bindings->size(); ++it_Binding)
            {
                if (std::string l_Error = ReadBinding((*l_Bindings)[it_Binding], action.Bindings.emplace_back()); !l_Error.empty())
                {
                    return std::format("{}, binding {}: {}", Describe(map.Name, action.Name), it_Binding, l_Error);
                }
            }

            return {};
        }

        std::string ReadRebind(const YAML::Node& node, InputBindingOverride& rebind)
        {
            if (!node.IsMap())
            {
                return "a rebind is not a map";
            }

            std::string l_Action;
            std::string l_Malformed;
            CookedText::ReadString(node, "Action", l_Action, l_Malformed);
            CookedText::ReadNumber(node, "Binding", rebind.Binding, l_Malformed);
            CookedText::ReadNumber(node, "Part", rebind.Part, l_Malformed);
            if (!l_Malformed.empty())
            {
                return std::format("{} is malformed", l_Malformed);
            }

            if (!IsValidPath(l_Action))
            {
                return std::format("Action '{}' is no Map/Action path", l_Action);
            }

            if (rebind.Part >= std::tuple_size_v<decltype(InputBinding::Sources)>)
            {
                return std::format("Part {} is past a binding's 4", rebind.Part);
            }

            rebind.Action = InputName(std::string_view(l_Action));
            if (std::string l_Error = ReadSource(node, "From", rebind.From); !l_Error.empty())
            {
                return l_Error;
            }

            return ReadSource(node, "To", rebind.To);
        }

        std::string ReadOverlayPlayer(const YAML::Node& node, InputBindingOverlay& overlay, std::array<bool, InputBindingOverlay::c_PlayerSlots>& read)
        {
            if (!node.IsMap())
            {
                return "a player is not a map";
            }

            std::uint32_t l_Player = InputBindingOverlay::c_PlayerSlots;
            std::string l_Malformed;
            CookedText::ReadNumber(node, "Player", l_Player, l_Malformed);
            if (!l_Malformed.empty() || l_Player >= InputBindingOverlay::c_PlayerSlots)
            {
                return std::format("a player's Player is not from 0 to {}", InputBindingOverlay::c_PlayerSlots - 1);
            }

            if (std::exchange(read[l_Player], true))
            {
                return std::format("player {} is given twice", l_Player);
            }

            const std::optional<YAML::Node> l_Rebinds = GetList(node, "Rebinds");
            if (!l_Rebinds)
            {
                return std::format("player {}: Rebinds is not a list", l_Player);
            }

            for (std::size_t it_Rebind = 0; it_Rebind < l_Rebinds->size(); ++it_Rebind)
            {
                if (std::string l_Error = ReadRebind((*l_Rebinds)[it_Rebind], overlay.Players[l_Player].emplace_back()); !l_Error.empty())
                {
                    return std::format("player {}, rebind {}: {}", l_Player, it_Rebind, l_Error);
                }
            }

            return {};
        }

        std::string ReadMap(const YAML::Node& node, const InputActionsData& data, InputActionMap& map)
        {
            if (std::string l_Error = ReadNamed(node, data.Maps, "map", map.Name); !l_Error.empty())
            {
                return l_Error;
            }

            const std::optional<YAML::Node> l_Actions = GetList(node, "Actions");
            if (!l_Actions)
            {
                return std::format("{}: Actions is not a list", Describe(map.Name, {}));
            }

            for (std::size_t it_Action = 0; it_Action < l_Actions->size(); ++it_Action)
            {
                InputActionDefinition l_Action;
                if (std::string l_Error = ReadAction((*l_Actions)[it_Action], map, l_Action); !l_Error.empty())
                {
                    return l_Error;
                }

                map.Actions.push_back(std::move(l_Action));
            }

            return {};
        }
    }

    InputActionsAsset::InputActionsAsset(InputActionsData data) : m_Data(std::move(data)), m_Serial(s_NextSerial.fetch_add(1, std::memory_order_relaxed))
    {

    }

    Expected<Asset*, std::string> InputActionsLoader::Load(std::span<const std::byte> data) const
    {
        Expected<InputActionsData, std::string> l_Data = ParseInputActions(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
        if (!l_Data)
        {
            return Unexpected{ l_Data.GetError() };
        }

        return Memory::New<InputActionsAsset>(MemoryTag::Input, std::move(*l_Data));
    }

    std::string WriteInputActions(const InputActionsData& data)
    {
        std::string l_Text = std::format("Format: {}", InputActionsData::c_FormatVersion);
        ComponentWriter l_Writer(l_Text, 0);
        l_Writer.BeginList("Maps");
        for (const InputActionMap& it_Map : data.Maps)
        {
            l_Writer.BeginItem();
            l_Writer.Write("Name", std::string_view(it_Map.Name));
            l_Writer.BeginList("Actions");
            for (const InputActionDefinition& it_Action : it_Map.Actions)
            {
                l_Writer.BeginItem();
                l_Writer.Write("Name", std::string_view(it_Action.Name));
                l_Writer.Write("Type", ToString(it_Action.Type));
                WriteInteraction(l_Writer, it_Action.Interaction);
                l_Writer.BeginList("Bindings");
                for (const InputBinding& it_Binding : it_Action.Bindings)
                {
                    l_Writer.BeginItem();
                    const std::span<const std::string_view> l_Parts = GetCompositeParts(it_Binding.Composite);
                    if (l_Parts.empty())
                    {
                        WriteSource(l_Writer, "Source", it_Binding.Sources[0]);
                    }
                    else
                    {
                        l_Writer.Write("Composite", ToString(it_Binding.Composite));
                    }

                    for (std::size_t it_Part = 0; it_Part < l_Parts.size(); ++it_Part)
                    {
                        WriteSource(l_Writer, l_Parts[it_Part], it_Binding.Sources[it_Part]);
                    }

                    if (!it_Binding.Processors.empty())
                    {
                        l_Writer.BeginList("Processors");
                        for (const InputProcessor& it_Processor : it_Binding.Processors)
                        {
                            l_Writer.BeginItem();
                            WriteProcessor(l_Writer, it_Processor);
                        }

                        l_Writer.End();
                    }
                }

                l_Writer.End();
            }

            l_Writer.End();
        }

        l_Writer.End();

        return l_Text + "\n";
    }

    Expected<InputActionsData, std::string> ParseInputActions(std::string_view text)
    {
        const Expected<YAML::Node, std::string> l_Root = CookedText::LoadRoot(text, "an input actions file", InputActionsData::c_FormatVersion);
        if (!l_Root)
        {
            return Unexpected{ l_Root.GetError() };
        }

        const std::optional<YAML::Node> l_Maps = GetList(*l_Root, "Maps");
        if (!l_Maps)
        {
            return Unexpected{ std::string("Maps is not a list") };
        }

        InputActionsData l_Data;
        for (std::size_t it_Map = 0; it_Map < l_Maps->size(); ++it_Map)
        {
            InputActionMap l_Map;
            if (std::string l_Error = ReadMap((*l_Maps)[it_Map], l_Data, l_Map); !l_Error.empty())
            {
                return Unexpected{ std::move(l_Error) };
            }

            l_Data.Maps.push_back(std::move(l_Map));
        }

        return l_Data;
    }

    std::string WriteInputBindingOverlay(const InputBindingOverlay& overlay)
    {
        std::string l_Text = std::format("Format: {}", InputBindingOverlay::c_FormatVersion);
        ComponentWriter l_Writer(l_Text, 0);
        l_Writer.Write("Asset", overlay.Asset);
        l_Writer.BeginList("Players");
        for (std::uint32_t it_Player = 0; it_Player < InputBindingOverlay::c_PlayerSlots; ++it_Player)
        {
            const InputVector<InputBindingOverride>& l_Rebinds = overlay.Players[it_Player];
            if (l_Rebinds.empty())
            {
                continue;
            }

            l_Writer.BeginItem();
            l_Writer.Write("Player", it_Player);
            l_Writer.BeginList("Rebinds");
            for (const InputBindingOverride& it_Rebind : l_Rebinds)
            {
                l_Writer.BeginItem();
                l_Writer.Write("Action", std::string_view(it_Rebind.Action));
                l_Writer.Write("Binding", it_Rebind.Binding);
                if (it_Rebind.Part != 0)
                {
                    l_Writer.Write("Part", it_Rebind.Part);
                }

                WriteSource(l_Writer, "From", it_Rebind.From);
                WriteSource(l_Writer, "To", it_Rebind.To);
            }

            l_Writer.End();
        }

        l_Writer.End();

        return l_Text + "\n";
    }

    Expected<InputBindingOverlay, std::string> ParseInputBindingOverlay(std::string_view text)
    {
        const Expected<YAML::Node, std::string> l_Root = CookedText::LoadRoot(text, "a rebinds file", InputBindingOverlay::c_FormatVersion);
        if (!l_Root)
        {
            return Unexpected{ l_Root.GetError() };
        }

        InputBindingOverlay l_Overlay;
        std::string l_Malformed;
        CookedText::ReadUUID(*l_Root, "Asset", l_Overlay.Asset, l_Malformed);
        if (!l_Malformed.empty())
        {
            return Unexpected{ std::string("Asset is malformed") };
        }

        const std::optional<YAML::Node> l_Players = GetList(*l_Root, "Players");
        if (!l_Players)
        {
            return Unexpected{ std::string("Players is not a list") };
        }

        std::array<bool, InputBindingOverlay::c_PlayerSlots> l_Read{};
        for (std::size_t it_Player = 0; it_Player < l_Players->size(); ++it_Player)
        {
            if (std::string l_Error = ReadOverlayPlayer((*l_Players)[it_Player], l_Overlay, l_Read); !l_Error.empty())
            {
                return Unexpected{ std::move(l_Error) };
            }
        }

        return l_Overlay;
    }

    std::string ToString(InputSource source)
    {
        const std::size_t l_Code = source.Code;
        switch (source.Kind)
        {
            case InputSourceKind::None:
            {
                return {};
            }
            case InputSourceKind::Key:
            {
                const auto a_Found = std::ranges::find(c_KeyNames, static_cast<KeyCode>(source.Code), &SourceName<KeyCode>::Value);

                return a_Found != c_KeyNames.end() ? std::format("{}/{}", c_Keyboard, a_Found->Name) : std::format("{}/{}", c_Keyboard, source.Code);
            }
            case InputSourceKind::MouseButton:
            {
                return l_Code < c_MouseButtonNames.size() ? std::format("{}/{}", c_Mouse, c_MouseButtonNames[l_Code]) : std::format("{}/Button{}", c_Mouse, source.Code);
            }
            case InputSourceKind::MouseDelta:
            {
                return std::format("{}/{}", c_Mouse, c_MouseDeltaNames[std::min(l_Code, c_MouseDeltaNames.size() - 1)]);
            }
            case InputSourceKind::MouseScroll:
            {
                return std::format("{}/{}", c_Mouse, c_MouseScrollNames[std::min(l_Code, c_MouseScrollNames.size() - 1)]);
            }
            case InputSourceKind::GamepadButton:
            {
                return l_Code < c_GamepadButtonNames.size() ? std::format("{}/{}", c_Gamepad, c_GamepadButtonNames[l_Code]) : std::string();
            }
            case InputSourceKind::GamepadAxis:
            {
                return l_Code < c_GamepadAxisNames.size() ? std::format("{}/{}", c_Gamepad, c_GamepadAxisNames[l_Code]) : std::string();
            }
            case InputSourceKind::GamepadStick:
            {
                return l_Code < c_GamepadStickNames.size() ? std::format("{}/{}", c_Gamepad, c_GamepadStickNames[l_Code]) : std::string();
            }
        }

        return {};
    }

    std::optional<InputSource> ParseInputSource(std::string_view text)
    {
        if (text.empty())
        {
            return InputSource{};
        }

        const std::size_t l_Separator = text.find('/');
        if (l_Separator == std::string_view::npos)
        {
            return std::nullopt;
        }

        const std::string_view l_Device = text.substr(0, l_Separator);
        const std::string_view l_Name = text.substr(l_Separator + 1);
        if (l_Device == c_Keyboard)
        {
            if (const auto a_Found = std::ranges::find(c_KeyNames, l_Name, &SourceName<KeyCode>::Name); a_Found != c_KeyNames.end())
            {
                return InputSource{ InputSourceKind::Key, std::to_underlying(a_Found->Value) };
            }

            const std::optional<std::uint16_t> l_Code = ParseCode(l_Name);

            return l_Code && *l_Code < std::to_underlying(KeyCode::Count) ? std::optional<InputSource>(InputSource{ InputSourceKind::Key, *l_Code }) : std::nullopt;
        }

        if (l_Device == c_Mouse)
        {
            if (const std::optional<std::uint16_t> l_Button = FindName(c_MouseButtonNames, l_Name))
            {
                return InputSource{ InputSourceKind::MouseButton, *l_Button };
            }

            if (const std::optional<std::uint16_t> l_Delta = FindName(c_MouseDeltaNames, l_Name))
            {
                return InputSource{ InputSourceKind::MouseDelta, *l_Delta };
            }

            if (const std::optional<std::uint16_t> l_Scroll = FindName(c_MouseScrollNames, l_Name))
            {
                return InputSource{ InputSourceKind::MouseScroll, *l_Scroll };
            }

            const std::optional<std::uint16_t> l_Code = l_Name.starts_with("Button") ? ParseCode(l_Name.substr(6)) : std::nullopt;

            return l_Code && *l_Code < std::to_underlying(MouseCode::Count) ? std::optional<InputSource>(InputSource{ InputSourceKind::MouseButton, *l_Code }) : std::nullopt;
        }

        if (l_Device == c_Gamepad)
        {
            if (const std::optional<std::uint16_t> l_Button = FindName(c_GamepadButtonNames, l_Name))
            {
                return InputSource{ InputSourceKind::GamepadButton, *l_Button };
            }

            if (const std::optional<std::uint16_t> l_Axis = FindName(c_GamepadAxisNames, l_Name))
            {
                return InputSource{ InputSourceKind::GamepadAxis, *l_Axis };
            }

            if (const std::optional<std::uint16_t> l_Stick = FindName(c_GamepadStickNames, l_Name))
            {
                return InputSource{ InputSourceKind::GamepadStick, *l_Stick };
            }
        }

        return std::nullopt;
    }

    std::span<const InputSource> GetInputSources()
    {
        static constexpr std::size_t c_Count = c_KeyNames.size() + c_MouseButtonNames.size() + c_MouseDeltaNames.size() + c_MouseScrollNames.size() + c_GamepadButtonNames.size() + c_GamepadAxisNames.size() + c_GamepadStickNames.size();
        static constexpr std::array<InputSource, c_Count> c_Sources = []
        {
            std::array<InputSource, c_Count> l_Sources{};
            std::size_t l_Next = 0;
            for (const SourceName<KeyCode>& it_Key : c_KeyNames)
            {
                l_Sources[l_Next++] = { InputSourceKind::Key, std::to_underlying(it_Key.Value) };
            }

            const auto a_Add = [&](InputSourceKind kind, std::size_t count)
            {
                for (std::size_t it_Code = 0; it_Code < count; ++it_Code)
                {
                    l_Sources[l_Next++] = { kind, static_cast<std::uint16_t>(it_Code) };
                }
            };
            a_Add(InputSourceKind::MouseButton, c_MouseButtonNames.size());
            a_Add(InputSourceKind::MouseDelta, c_MouseDeltaNames.size());
            a_Add(InputSourceKind::MouseScroll, c_MouseScrollNames.size());
            a_Add(InputSourceKind::GamepadButton, c_GamepadButtonNames.size());
            a_Add(InputSourceKind::GamepadAxis, c_GamepadAxisNames.size());
            a_Add(InputSourceKind::GamepadStick, c_GamepadStickNames.size());

            return l_Sources;
        }();

        return c_Sources;
    }

    std::string_view ToString(InputActionType type)
    {
        switch (type)
        {
            case InputActionType::Button:
            {
                return "Button";
            }
            case InputActionType::Axis:
            {
                return "Axis";
            }
            case InputActionType::Vector2:
            {
                return "Vector2";
            }
        }

        return "Unknown";
    }

    std::optional<InputActionType> ParseInputActionType(std::string_view text)
    {
        for (const InputActionType it_Type : { InputActionType::Button, InputActionType::Axis, InputActionType::Vector2 })
        {
            if (ToString(it_Type) == text)
            {
                return it_Type;
            }
        }

        return std::nullopt;
    }

    std::string_view ToString(InputCompositeKind kind)
    {
        switch (kind)
        {
            case InputCompositeKind::None:
            {
                return "None";
            }
            case InputCompositeKind::Axis:
            {
                return "Axis";
            }
            case InputCompositeKind::Vector2:
            {
                return "Vector2";
            }
        }

        return "Unknown";
    }

    std::optional<InputCompositeKind> ParseInputCompositeKind(std::string_view text)
    {
        for (const InputCompositeKind it_Kind : { InputCompositeKind::None, InputCompositeKind::Axis, InputCompositeKind::Vector2 })
        {
            if (ToString(it_Kind) == text)
            {
                return it_Kind;
            }
        }

        return std::nullopt;
    }

    std::string_view ToString(InputProcessorKind kind)
    {
        switch (kind)
        {
            case InputProcessorKind::RadialDeadzone:
            {
                return "RadialDeadzone";
            }
            case InputProcessorKind::AxialDeadzone:
            {
                return "AxialDeadzone";
            }
            case InputProcessorKind::Invert:
            {
                return "Invert";
            }
            case InputProcessorKind::Scale:
            {
                return "Scale";
            }
            case InputProcessorKind::Normalize:
            {
                return "Normalize";
            }
        }

        return "Unknown";
    }

    std::optional<InputProcessorKind> ParseInputProcessorKind(std::string_view text)
    {
        for (const InputProcessorKind it_Kind : { InputProcessorKind::RadialDeadzone, InputProcessorKind::AxialDeadzone, InputProcessorKind::Invert, InputProcessorKind::Scale, InputProcessorKind::Normalize })
        {
            if (ToString(it_Kind) == text)
            {
                return it_Kind;
            }
        }

        return std::nullopt;
    }

    std::string_view ToString(InputInteractionKind kind)
    {
        switch (kind)
        {
            case InputInteractionKind::None:
            {
                return "None";
            }
            case InputInteractionKind::Hold:
            {
                return "Hold";
            }
            case InputInteractionKind::Tap:
            {
                return "Tap";
            }
            case InputInteractionKind::MultiTap:
            {
                return "MultiTap";
            }
        }

        return "Unknown";
    }

    std::optional<InputInteractionKind> ParseInputInteractionKind(std::string_view text)
    {
        for (const InputInteractionKind it_Kind : { InputInteractionKind::None, InputInteractionKind::Hold, InputInteractionKind::Tap, InputInteractionKind::MultiTap })
        {
            if (ToString(it_Kind) == text)
            {
                return it_Kind;
            }
        }

        return std::nullopt;
    }

    // As Unity's Input System has them
    InputInteraction GetDefaultInteraction(InputInteractionKind kind)
    {
        InputInteraction l_Interaction;
        l_Interaction.Kind = kind;
        switch (kind)
        {
            case InputInteractionKind::None:
            {
                break;
            }
            case InputInteractionKind::Hold:
            {
                l_Interaction.Duration = 0.4f;

                break;
            }
            case InputInteractionKind::Tap:
            {
                l_Interaction.Duration = 0.2f;

                break;
            }
            case InputInteractionKind::MultiTap:
            {
                l_Interaction.Count = 2;
                l_Interaction.Duration = 0.2f;
                l_Interaction.Spacing = 0.75f;

                break;
            }
        }

        return l_Interaction;
    }

    std::span<const std::string_view> GetCompositeParts(InputCompositeKind kind)
    {
        switch (kind)
        {
            case InputCompositeKind::None:
            {
                return {};
            }
            case InputCompositeKind::Axis:
            {
                return c_AxisParts;
            }
            case InputCompositeKind::Vector2:
            {
                return c_Vector2Parts;
            }
        }

        return {};
    }
}