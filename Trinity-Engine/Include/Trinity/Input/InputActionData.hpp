#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    template<typename T>
    using InputVector = std::vector<T, TaggedAllocator<T, MemoryTag::Input>>;

    using InputName = TaggedString<MemoryTag::Input>;

    // A Button has a strength from 0 to 1. An Axis, and a Vector2's X and Y, are each from -1 to 1
    enum class InputActionType : std::uint8_t
    {
        Button,
        Axis,
        Vector2
    };

    // Code is the key, button or axis. A stick: 0 left, 1 right. Mouse movement: 0 both, 1 X, 2 Y
    enum class InputSourceKind : std::uint8_t
    {
        None,
        Key,
        MouseButton,
        MouseDelta,
        MouseScroll,
        GamepadButton,
        GamepadAxis,
        GamepadStick
    };

    struct InputSource
    {
        InputSourceKind Kind = InputSourceKind::None;
        std::uint16_t Code = 0;

        [[nodiscard]] bool operator==(const InputSource&) const = default;
    };

    // An Axis from a Negative and a Positive button, and a Vector2 from Up, Down, Left and Right
    enum class InputCompositeKind : std::uint8_t
    {
        None,
        Axis,
        Vector2
    };

    // Applied in turn. Radial deadzones suit sticks, axial ones triggers. Normalize caps a length at 1
    enum class InputProcessorKind : std::uint8_t
    {
        RadialDeadzone,
        AxialDeadzone,
        Invert,
        Scale,
        Normalize
    };

    // Min and Max are a deadzone's, InvertX and InvertY are Invert's, and Scale is Scale's
    struct InputProcessor
    {
        InputProcessorKind Kind = InputProcessorKind::Normalize;
        float Min = 0.125f;
        float Max = 0.925f;
        glm::vec2 Scale{ 1.0f };
        bool InvertX = true;
        bool InvertY = true;

        [[nodiscard]] bool operator==(const InputProcessor&) const = default;
    };

    // One source, or a composite's parts in InputCompositeKind's order, then its processors in order
    struct InputBinding
    {
        InputCompositeKind Composite = InputCompositeKind::None;
        std::array<InputSource, 4> Sources{};
        InputVector<InputProcessor> Processors;

        [[nodiscard]] bool operator==(const InputBinding&) const = default;
    };

    // None performs as pressed, Hold after Duration, Tap on a quick release, MultiTap on the last tap
    enum class InputInteractionKind : std::uint8_t
    {
        None,
        Hold,
        Tap,
        MultiTap
    };

    // Duration is Hold's and Tap's. Count, Duration and Spacing are MultiTap's. In scene-time seconds
    struct InputInteraction
    {
        InputInteractionKind Kind = InputInteractionKind::None;
        float Duration = 0.0f;
        float Spacing = 0.0f;
        std::uint32_t Count = 0;

        [[nodiscard]] bool operator==(const InputInteraction&) const = default;
    };

    struct InputActionDefinition
    {
        InputName Name;
        InputActionType Type = InputActionType::Button;
        InputInteraction Interaction;
        InputVector<InputBinding> Bindings;

        [[nodiscard]] bool operator==(const InputActionDefinition&) const = default;
    };

    struct InputActionMap
    {
        InputName Name;
        InputVector<InputActionDefinition> Actions;

        [[nodiscard]] bool operator==(const InputActionMap&) const = default;
    };

    // Maps, each with uniquely named actions. A name has no '/'. A format 1 file has no interactions
    struct InputActionsData
    {
        static constexpr std::uint32_t c_FormatVersion = 2;
        static constexpr std::string_view c_Extension = ".trinput";
        static constexpr char c_PathSeparator = '/';

        InputVector<InputActionMap> Maps;

        [[nodiscard]] bool operator==(const InputActionsData&) const = default;
    };

    // Kept in /saves apart from the asset. From is the asset's source, so a stale rebind is dropped
    struct InputBindingOverride
    {
        InputName Action;
        std::uint32_t Binding = 0;
        std::uint32_t Part = 0;
        InputSource From;
        InputSource To;

        [[nodiscard]] bool operator==(const InputBindingOverride&) const = default;
    };

    // What /saves keeps for one actions asset: each player slot's rebinds, in use or not
    struct InputBindingOverlay
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_Extension = ".trbindings";
        static constexpr std::uint32_t c_PlayerSlots = 8;

        UUID Asset;
        std::array<InputVector<InputBindingOverride>, c_PlayerSlots> Players;

        [[nodiscard]] bool operator==(const InputBindingOverlay&) const = default;
    };

    class TRINITY_API InputActionsAsset final : public Asset
    {
    public:
        static constexpr std::string_view c_AssetType = "InputActions";

        explicit InputActionsAsset(InputActionsData data);

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }

        [[nodiscard]] const InputActionsData& GetData() const { return m_Data; }
        // A number of its own for each version loaded, so a reload is told from the version before
        [[nodiscard]] std::uint64_t GetSerial() const { return m_Serial; }

    private:
        InputActionsData m_Data;
        std::uint64_t m_Serial;
    };

    // Reads .trinput files, which the asset registry imports as InputActions
    class TRINITY_API InputActionsLoader final : public AssetLoader
    {
    public:
        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return InputActionsAsset::c_AssetType;
        }

        [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override;
    };

    // YAML, with empty parts and each kind's unused fields left out
    [[nodiscard]] TRINITY_API std::string WriteInputActions(const InputActionsData& data);
    // A missing key keeps its default. A malformed or unknown one, or a newer format, refuses the file
    [[nodiscard]] TRINITY_API Expected<InputActionsData, std::string> ParseInputActions(std::string_view text);

    // YAML, with only the players that have rebinds, a Part of 0 and a source of none left out
    [[nodiscard]] TRINITY_API std::string WriteInputBindingOverlay(const InputBindingOverlay& overlay);
    // A missing key keeps its default. A malformed or unknown one, or a newer format, refuses the file
    [[nodiscard]] TRINITY_API Expected<InputBindingOverlay, std::string> ParseInputBindingOverlay(std::string_view text);

    // As a file names it, such as Keyboard/Space or Gamepad/LeftStick. Keys as C# names them
    [[nodiscard]] TRINITY_API std::string ToString(InputSource source);
    [[nodiscard]] TRINITY_API std::optional<InputSource> ParseInputSource(std::string_view text);
    // Every source a file can name, as a picker lists them: keys, then the mouse, then the gamepad
    [[nodiscard]] TRINITY_API std::span<const InputSource> GetInputSources();

    [[nodiscard]] TRINITY_API std::string_view ToString(InputActionType type);
    [[nodiscard]] TRINITY_API std::optional<InputActionType> ParseInputActionType(std::string_view text);
    [[nodiscard]] TRINITY_API std::string_view ToString(InputCompositeKind kind);
    [[nodiscard]] TRINITY_API std::optional<InputCompositeKind> ParseInputCompositeKind(std::string_view text);
    [[nodiscard]] TRINITY_API std::string_view ToString(InputProcessorKind kind);
    [[nodiscard]] TRINITY_API std::optional<InputProcessorKind> ParseInputProcessorKind(std::string_view text);
    [[nodiscard]] TRINITY_API std::string_view ToString(InputInteractionKind kind);
    [[nodiscard]] TRINITY_API std::optional<InputInteractionKind> ParseInputInteractionKind(std::string_view text);

    // Held for 0.4 seconds, a tap of up to 0.2 seconds, and 2 taps up to 0.75 seconds apart
    [[nodiscard]] TRINITY_API InputInteraction GetDefaultInteraction(InputInteractionKind kind);


    // As a file names them: Negative and Positive, or Up, Down, Left and Right. None for one source
    [[nodiscard]] TRINITY_API std::span<const std::string_view> GetCompositeParts(InputCompositeKind kind);

    // As Unity's UI input module: Navigate moves the focus, Submit presses it, Cancel takes it away
    namespace UIInputMap
    {
        inline constexpr std::string_view c_Name = "UI";
        inline constexpr std::string_view c_Navigate = "Navigate";
        inline constexpr std::string_view c_Submit = "Submit";
        inline constexpr std::string_view c_Cancel = "Cancel";
    }

    // Unity's defaults: WASD, arrows, left stick and d-pad. Enter, Space and A. Escape and B
    [[nodiscard]] TRINITY_API InputActionMap MakeDefaultUIInputMap();
}