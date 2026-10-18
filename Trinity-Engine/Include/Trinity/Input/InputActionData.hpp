#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"

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

    // What an action gives: a Button is pressed or not, with how hard from 0 to 1, an Axis is from -1 to 1, and a Vector2 has an X and a Y, each from -1 to 1
    enum class InputActionType : std::uint8_t
    {
        Button,
        Axis,
        Vector2
    };

    // Where a binding reads. Code is the KeyCode, MouseCode, GamepadButton or GamepadAxis, the stick, 0 for the left and 1 for the right, or for the mouse's movement and scroll, 0 for both axes, 1 for X alone and 2 for Y alone
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

    // A binding's parts made into one value: an Axis from a Negative and a Positive button, and a Vector2 from Up, Down, Left and Right
    enum class InputCompositeKind : std::uint8_t
    {
        None,
        Axis,
        Vector2
    };

    // Applied to a binding's value in turn. A radial deadzone is for a stick: nothing below Min of its length, all of it from Max, the direction kept. An axial one does the same to each axis on its own, as a trigger needs. Invert flips X and Y where set, Scale multiplies them, and Normalize brings a value longer than 1 back to 1, so four buttons' diagonal is no faster than a stick's
    enum class InputProcessorKind : std::uint8_t
    {
        RadialDeadzone,
        AxialDeadzone,
        Invert,
        Scale,
        Normalize
    };

    // Each kind reads only its own fields: Min and Max for a deadzone, InvertX and InvertY for Invert, and Scale for Scale
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

    // One source, or a composite's parts in the order InputCompositeKind gives them, then its processors in order
    struct InputBinding
    {
        InputCompositeKind Composite = InputCompositeKind::None;
        std::array<InputSource, 4> Sources{};
        InputVector<InputProcessor> Processors;

        [[nodiscard]] bool operator==(const InputBinding&) const = default;
    };

    struct InputActionDefinition
    {
        InputName Name;
        InputActionType Type = InputActionType::Button;
        InputVector<InputBinding> Bindings;

        [[nodiscard]] bool operator==(const InputActionDefinition&) const = default;
    };

    struct InputActionMap
    {
        InputName Name;
        InputVector<InputActionDefinition> Actions;

        [[nodiscard]] bool operator==(const InputActionMap&) const = default;
    };

    // What a .trinput file holds: maps, each with its actions, each named uniquely within its map. A name has no '/', which a path such as Gameplay/Jump puts between a map and its action
    struct InputActionsData
    {
        static constexpr std::uint32_t c_FormatVersion = 1;
        static constexpr std::string_view c_Extension = ".trinput";
        static constexpr char c_PathSeparator = '/';

        InputVector<InputActionMap> Maps;

        [[nodiscard]] bool operator==(const InputActionsData&) const = default;
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
        // A number of its own for each version loaded, so a reload is told from the version before, wherever it lies
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

    // YAML, with each binding's empty parts and its processors' unused fields left out
    [[nodiscard]] TRINITY_API std::string WriteInputActions(const InputActionsData& data);
    // A key that is missing keeps its default. A malformed one, a source or kind this build does not know, a name with '/' or one used twice, or a newer format, refuses the file
    [[nodiscard]] TRINITY_API Expected<InputActionsData, std::string> ParseInputActions(std::string_view text);

    // As a file names it, such as Keyboard/Space, Mouse/Left, Mouse/Delta, Mouse/ScrollY, Gamepad/A, Gamepad/LeftStickPress, Gamepad/LeftStick or Gamepad/LeftTrigger, and empty for none. Keys are named as C# names them
    [[nodiscard]] TRINITY_API std::string ToString(InputSource source);
    [[nodiscard]] TRINITY_API std::optional<InputSource> ParseInputSource(std::string_view text);

    [[nodiscard]] TRINITY_API std::string_view ToString(InputActionType type);
    [[nodiscard]] TRINITY_API std::optional<InputActionType> ParseInputActionType(std::string_view text);
    [[nodiscard]] TRINITY_API std::string_view ToString(InputCompositeKind kind);
    [[nodiscard]] TRINITY_API std::optional<InputCompositeKind> ParseInputCompositeKind(std::string_view text);
    [[nodiscard]] TRINITY_API std::string_view ToString(InputProcessorKind kind);
    [[nodiscard]] TRINITY_API std::optional<InputProcessorKind> ParseInputProcessorKind(std::string_view text);

    // A composite's parts as a file names them, Negative and Positive, or Up, Down, Left and Right, and none for a single source
    [[nodiscard]] TRINITY_API std::span<const std::string_view> GetCompositeParts(InputCompositeKind kind);
}