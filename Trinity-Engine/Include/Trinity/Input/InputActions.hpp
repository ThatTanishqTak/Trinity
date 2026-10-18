#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Input/Input.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
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

    // An action of the running scene's actions asset, found once by its path, Map/Action, or by its name alone while only one map has it, as a script's OnCreate does, and read each frame for nothing. One found before the asset is ready, or for an action it lacks, reads as released and zero until the asset has it, as after a reload. In OnFixedUpdate, WasPressed and WasReleased are what happened since the step before, so each press is seen by exactly one fixed step whatever the frame rate. Elsewhere they are this frame's. Each read is for a player, player 0 when none is named. Main thread only
    class TRINITY_API InputAction
    {
    public:
        InputAction() = default;

        [[nodiscard]] static InputAction Find(std::string_view path);
        // The handle as one number, for C#, and back. 0 for one never found
        [[nodiscard]] static InputAction FromHandle(std::uint64_t handle);
        [[nodiscard]] std::uint64_t ToHandle() const;

        // Found while a scene runs, and in its asset now
        [[nodiscard]] bool IsValid() const;
        // Found since the running scene started, so it reads its action whenever the asset has one by its path, valid or not. False for one never found, and for one found before the scene last stopped, which reads nothing again
        [[nodiscard]] bool IsCurrent() const;
        [[nodiscard]] bool IsPressed(std::uint32_t player = 0) const;
        [[nodiscard]] bool WasPressed(std::uint32_t player = 0) const;
        [[nodiscard]] bool WasReleased(std::uint32_t player = 0) const;
        // A Button's strength from 0 to 1, an Axis from -1 to 1, and a Vector2's length
        [[nodiscard]] float ReadValue(std::uint32_t player = 0) const;
        // A Vector2's X and Y, and a Button's or an Axis's value as X, with Y 0
        [[nodiscard]] glm::vec2 ReadVector2(std::uint32_t player = 0) const;

    private:
        static constexpr std::uint32_t c_NoSlot = std::numeric_limits<std::uint32_t>::max();

        InputAction(std::uint32_t slot, std::uint32_t session) : m_Slot(slot), m_Session(session)
        {

        }

        std::uint32_t m_Slot = c_NoSlot;
        std::uint32_t m_Session = 0;
    };

    // The players a running scene reads actions for, the maps each has enabled and the devices each reads. A SceneManager starts them with its actions asset, the project's default in Forge's Play, and ends them with it. Player 0 always is, has the first map enabled, and reads every device no other player has been assigned, so one player needs nothing set up. Main thread only
    class TRINITY_API InputActions
    {
    public:
        static constexpr std::uint32_t c_MaxPlayers = 8;
        static constexpr std::uint32_t c_NoPlayer = std::numeric_limits<std::uint32_t>::max();
        // How far a Button or an Axis, or a Vector2's length, has to go for the action to be pressed
        static constexpr float c_PressPoint = 0.5f;

        // Invalid when the running scene has none
        [[nodiscard]] static UUID GetAsset();
        [[nodiscard]] static bool IsReady();

        // By name. A map the asset does not have, or a player there is not, is false and logged. Before the asset is ready the name is kept and checked once it is
        static bool EnableMap(std::string_view map, std::uint32_t player = 0);
        static bool DisableMap(std::string_view map, std::uint32_t player = 0);
        [[nodiscard]] static bool IsMapEnabled(std::string_view map, std::uint32_t player = 0);

        // The lowest player not in use, with the first map enabled and no device, so it reads nothing until one is assigned. c_NoPlayer when all c_MaxPlayers are, or no scene runs
        [[nodiscard]] static std::uint32_t AddPlayer();
        // Its devices go back to player 0, which is never removed
        static void RemovePlayer(std::uint32_t player);
        [[nodiscard]] static bool IsPlayer(std::uint32_t player);
        [[nodiscard]] static std::uint32_t GetPlayerCount();

        // A device is one player's at a time, so assigning it takes it from any other. A gamepad keeps its player while it is disconnected, and the same pad reconnecting into its slot is that player's again. Assigning player 0 its own devices keeps them from joining as another
        static bool AssignDevice(InputDevice device, std::uint32_t player);
        static void UnassignDevice(InputDevice device);
        // The player assigned the device, or c_NoPlayer for one none has, which player 0 reads
        [[nodiscard]] static std::uint32_t GetDevicePlayer(InputDevice device);
        [[nodiscard]] static bool ReadsDevice(std::uint32_t player, InputDevice device);
        // A device no player has been assigned that had a key or button go down this frame, the keyboard and mouse first and then the gamepads by slot, or None, for a game's press to join
        [[nodiscard]] static InputDevice GetJoiningDevice();
    };
}