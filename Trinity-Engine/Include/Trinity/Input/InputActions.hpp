#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Input/Input.hpp"
#include "Trinity/Input/InputActionData.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace Trinity
{
    // A device a player reads: the keyboard and mouse together, or the gamepad in one of Input's slots
    enum class InputDevice : std::uint8_t
    {
        KeyboardMouse = 0,
        Gamepad0, Gamepad1, Gamepad2, Gamepad3, Gamepad4, Gamepad5, Gamepad6, Gamepad7,

        Count,
        None = 0xFF
    };

    static_assert(static_cast<std::uint32_t>(std::to_underlying(InputDevice::Count)) == 1 + Input::c_MaxGamepads, "InputDevice has the keyboard and mouse, then one device for each of Input's gamepad slots");

    [[nodiscard]] constexpr InputDevice GetGamepadDevice(std::uint32_t gamepad)
    {
        return gamepad < Input::c_MaxGamepads ? static_cast<InputDevice>(gamepad + 1) : InputDevice::None;
    }

    // None for no rebind, one over InputRebind::c_Kept rebinds old, or one from before the scene ended
    enum class InputRebindState : std::uint8_t
    {
        None,
        Waiting,
        Bound,
        Refused,
        Canceled
    };

    // Read until c_Kept more have been made, as a game's controls menu waits on it. Main thread only
    class TRINITY_API InputRebind
    {
    public:
        static constexpr std::uint32_t c_Kept = 8;

        InputRebind() = default;

        // The handle as one number, for C#, and back. 0 for none
        [[nodiscard]] static InputRebind FromHandle(std::uint64_t handle);
        [[nodiscard]] std::uint64_t ToHandle() const { return m_ID; }

        [[nodiscard]] InputRebindState GetState() const;
        [[nodiscard]] bool IsDone() const { return GetState() != InputRebindState::Waiting; }
        // The source it took, or the one it refused, and none while it waits
        [[nodiscard]] InputSource GetSource() const;
        // When refused, the action of the same map that reads the source. Empty otherwise
        [[nodiscard]] std::string_view GetConflict() const;
        // Stops one that waits, which then reads as Canceled
        void Cancel() const;

    private:
        friend class InputAction;

        explicit InputRebind(std::uint32_t id) : m_ID(id)
        {

        }

        std::uint32_t m_ID = 0;
    };

    // Found once by its Map/Action path. In OnFixedUpdate, Was is since the step before. Main thread
    class TRINITY_API InputAction
    {
    public:
        InputAction() = default;

        [[nodiscard]] static InputAction Find(std::string_view path);
        // The handle as one number, for C#, and back. 0 for one never found
        [[nodiscard]] static InputAction FromHandle(std::uint64_t handle);
        [[nodiscard]] std::uint64_t ToHandle() const;
        // As it was found, valid until the next Find. Empty if never found, or found before the scene ended
        [[nodiscard]] std::string_view GetPath() const;

        // Found while a scene runs, and in its asset now
        [[nodiscard]] bool IsValid() const;
        // Found since the running scene started, so it reads its action whenever the asset has one
        [[nodiscard]] bool IsCurrent() const;
        [[nodiscard]] bool IsPressed(std::uint32_t player = 0) const;
        [[nodiscard]] bool WasPressed(std::uint32_t player = 0) const;
        [[nodiscard]] bool WasReleased(std::uint32_t player = 0) const;
        // Its interaction completing, and failing once begun. Without one, it performs as it is pressed
        [[nodiscard]] bool WasPerformed(std::uint32_t player = 0) const;
        [[nodiscard]] bool WasCanceled(std::uint32_t player = 0) const;
        // From 0 to 1: a Hold's time over its Duration, or a MultiTap's taps over its Count
        [[nodiscard]] float ReadProgress(std::uint32_t player = 0) const;
        // A Button's strength from 0 to 1, an Axis from -1 to 1, and a Vector2's length
        [[nodiscard]] float ReadValue(std::uint32_t player = 0) const;
        // A Vector2's X and Y, and a Button's or an Axis's value as X, with Y 0
        [[nodiscard]] glm::vec2 ReadVector2(std::uint32_t player = 0) const;

        // Each a single source, or a composite of 2 or 4 parts. 0 for an action the asset lacks
        [[nodiscard]] std::uint32_t GetBindingCount() const;
        [[nodiscard]] std::uint32_t GetPartCount(std::uint32_t binding) const;
        // From the player's rebind if it has one, else the asset's. None for a part there is not
        [[nodiscard]] InputSource GetSource(std::uint32_t binding, std::uint32_t part = 0, std::uint32_t player = 0) const;
        [[nodiscard]] InputSource GetDefaultSource(std::uint32_t binding, std::uint32_t part = 0) const;

        // Saved in /saves, never in the asset. Refused if its map already reads the source for the player
        InputRebind Rebind(std::uint32_t binding, std::uint32_t part, InputSource source, std::uint32_t player = 0) const;
        // Listens from the next frame for a key, button, stick or trigger. Escape and Cancel stop it
        InputRebind StartRebind(std::uint32_t binding, std::uint32_t part = 0, std::uint32_t player = 0) const;
        // Back to the asset's source, saved as Rebind is
        void ResetBinding(std::uint32_t binding, std::uint32_t part = 0, std::uint32_t player = 0) const;

    private:
        static constexpr std::uint32_t c_NoSlot = std::numeric_limits<std::uint32_t>::max();

        InputAction(std::uint32_t slot, std::uint32_t session) : m_Slot(slot), m_Session(session)
        {

        }

        std::uint32_t m_Slot = c_NoSlot;
        std::uint32_t m_Session = 0;
    };

    // A SceneManager starts and ends them. Player 0 reads every unassigned device. Main thread only
    class TRINITY_API InputActions
    {
    public:
        static constexpr std::uint32_t c_MaxPlayers = 8;
        static constexpr std::uint32_t c_NoPlayer = std::numeric_limits<std::uint32_t>::max();
        // How far a Button or an Axis, or a Vector2's length, has to go for the action to be pressed
        static constexpr float c_PressPoint = 0.5f;

        // As a debugger such as Forge's Game panel shows it, with WasPerformed as outside a fixed step
        struct ActionReading
        {
            glm::vec2 Value{ 0.0f };
            bool Held = false;
            float Progress = 0.0f;
            bool Performed = false;
        };

        // Invalid when the running scene has none
        [[nodiscard]] static UUID GetAsset();
        [[nodiscard]] static bool IsReady();
        // Null until it is ready. A reload replaces it, so it is asked for again each frame
        [[nodiscard]] static const InputActionsData* GetData();
        // By its index among every map's actions in order. None for an unknown player or a bad index
        [[nodiscard]] static std::optional<ActionReading> ReadAction(std::uint32_t action, std::uint32_t player);

        // False, logged, for an unknown map or player. Before the asset is ready the name is kept
        static bool EnableMap(std::string_view map, std::uint32_t player = 0);
        static bool DisableMap(std::string_view map, std::uint32_t player = 0);
        [[nodiscard]] static bool IsMapEnabled(std::string_view map, std::uint32_t player = 0);

        // The lowest free player, with the first map and no device. c_NoPlayer when full or no scene runs
        [[nodiscard]] static std::uint32_t AddPlayer();
        // Its devices go back to player 0, which is never removed
        static void RemovePlayer(std::uint32_t player);
        [[nodiscard]] static bool IsPlayer(std::uint32_t player);
        [[nodiscard]] static std::uint32_t GetPlayerCount();

        // A device is one player's at a time. A gamepad keeps its player while it is disconnected
        static bool AssignDevice(InputDevice device, std::uint32_t player);
        static void UnassignDevice(InputDevice device);
        // The player assigned the device, or c_NoPlayer for one none has, which player 0 reads
        [[nodiscard]] static std::uint32_t GetDevicePlayer(InputDevice device);
        [[nodiscard]] static bool ReadsDevice(std::uint32_t player, InputDevice device);
        // An unassigned device that had a key or button go down this frame, for a game's press to join
        [[nodiscard]] static InputDevice GetJoiningDevice();

        // Takes away every rebind the player slot has, as a game's Reset to Defaults does, and saves
        static void ResetBindings(std::uint32_t player = 0);
    };
}