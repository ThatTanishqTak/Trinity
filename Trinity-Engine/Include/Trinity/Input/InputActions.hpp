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

    // What a rebind came to. Waiting while it listens, Bound once made and saved, Refused when another binding of the action's map already reads the source for the player, and Canceled by Escape, Cancel, another rebind starting to listen, or its action leaving the asset. None for no rebind, and for one made more than InputRebind::c_Kept rebinds ago or before the scene last stopped
    enum class InputRebindState : std::uint8_t
    {
        None,
        Waiting,
        Bound,
        Refused,
        Canceled
    };

    // A rebind an action made, read until c_Kept more have been made, as a game's controls menu waits on it. Main thread only
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
        // When it was refused, the name of the action of the same map that reads the source, and empty otherwise
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

    // An action of the running scene's actions asset, found once by its path, Map/Action, or by its name alone while only one map has it, as a script's OnCreate does, and read each frame for nothing. One found before the asset is ready, or for an action it lacks, reads as released and zero until the asset has it, as after a reload. In OnFixedUpdate, WasPressed and WasReleased are what happened since the step before, so each press is seen by exactly one fixed step whatever the frame rate. Elsewhere they are this frame's. Each read is for a player, player 0 when none is named. A script's member of this type is a field, which Properties sets to an action of the project's default asset, and which is found as the script is given its fields. Main thread only
    class TRINITY_API InputAction
    {
    public:
        InputAction() = default;

        [[nodiscard]] static InputAction Find(std::string_view path);
        // The handle as one number, for C#, and back. 0 for one never found
        [[nodiscard]] static InputAction FromHandle(std::uint64_t handle);
        [[nodiscard]] std::uint64_t ToHandle() const;
        // As it was found, valid until the next Find. Empty for one never found, and for one found before the scene last stopped
        [[nodiscard]] std::string_view GetPath() const;

        // Found while a scene runs, and in its asset now
        [[nodiscard]] bool IsValid() const;
        // Found since the running scene started, so it reads its action whenever the asset has one by its path, valid or not. False for one never found, and for one found before the scene last stopped, which reads nothing again
        [[nodiscard]] bool IsCurrent() const;
        [[nodiscard]] bool IsPressed(std::uint32_t player = 0) const;
        [[nodiscard]] bool WasPressed(std::uint32_t player = 0) const;
        [[nodiscard]] bool WasReleased(std::uint32_t player = 0) const;
        // Its interaction completing, and failing once begun, read as WasPressed is. One with no interaction performs as it is pressed and never cancels
        [[nodiscard]] bool WasPerformed(std::uint32_t player = 0) const;
        [[nodiscard]] bool WasCanceled(std::uint32_t player = 0) const;
        // How far its interaction has come, from 0 to 1: a Hold's time held over its Duration, staying 1 once it performs until it is released, a MultiTap's taps done over its Count, and 1 in the frame a Tap or MultiTap performs. One with no interaction is 1 while pressed
        [[nodiscard]] float ReadProgress(std::uint32_t player = 0) const;
        // A Button's strength from 0 to 1, an Axis from -1 to 1, and a Vector2's length
        [[nodiscard]] float ReadValue(std::uint32_t player = 0) const;
        // A Vector2's X and Y, and a Button's or an Axis's value as X, with Y 0
        [[nodiscard]] glm::vec2 ReadVector2(std::uint32_t player = 0) const;

        // Its bindings as the asset has them, each a single source, or a composite of 2 or 4 parts in the order InputCompositeKind gives them. 0 for an action the asset lacks
        [[nodiscard]] std::uint32_t GetBindingCount() const;
        [[nodiscard]] std::uint32_t GetPartCount(std::uint32_t binding) const;
        // A part's source for a player, from the player's rebind where it has one and from the asset otherwise, and the asset's alone. None for a part there is not
        [[nodiscard]] InputSource GetSource(std::uint32_t binding, std::uint32_t part = 0, std::uint32_t player = 0) const;
        [[nodiscard]] InputSource GetDefaultSource(std::uint32_t binding, std::uint32_t part = 0) const;

        // Rebinds a part for a player at once, and saves the player's rebinds under /saves, so the asset never changes. Refused when another binding or part of the action's map already reads the source for the player, and rebinding a part to the asset's own source takes its rebind away. A player slot keeps its rebinds whether or not the player is in use. None, and logged, for an action the asset lacks, or a binding, part, player or source there is not
        InputRebind Rebind(std::uint32_t binding, std::uint32_t part, InputSource source, std::uint32_t player = 0) const;
        // Listens from the next frame for the next key but Escape, mouse button or gamepad button of the devices the player reads, or stick or trigger pushed past half, and rebinds the part to it as Rebind does. One already pushed as it starts is taken only once it has come back. A stick is taken whole for a Vector2 action's own binding, and as the axis pushed otherwise. While it waits the player's actions read as released, and once it has taken an input, or Escape, each reads again only after its sources are let go, so that press reaches none. Escape and Cancel stop it, and so does another starting. None, and logged, as for Rebind, and for a player there is not
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

    // The players a running scene reads actions for, the maps each has enabled and the devices each reads. A SceneManager starts them with its actions asset, the project's default in Forge's Play, and ends them with it. Player 0 always is, has the first map enabled, and reads every device no other player has been assigned, so one player needs nothing set up. Each player slot's rebinds are kept on the machine in /saves/Input, in a .trbindings file named by the asset's UUID, read as a SceneManager starts and saved as they change, and a rebind the asset has since been changed under is dropped and logged. Main thread only
    class TRINITY_API InputActions
    {
    public:
        static constexpr std::uint32_t c_MaxPlayers = 8;
        static constexpr std::uint32_t c_NoPlayer = std::numeric_limits<std::uint32_t>::max();
        // How far a Button or an Axis, or a Vector2's length, has to go for the action to be pressed
        static constexpr float c_PressPoint = 0.5f;

        // How a player reads an action, as a debugger such as Forge's Game panel shows it: its value as ReadVector2 gives it, whether it is held, its interaction's progress, and whether it performed this frame, as WasPerformed outside a fixed step reads it
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
        // The running scene's asset as its actions read it now, null until it is ready. A reload replaces it, so it is asked for again each frame
        [[nodiscard]] static const InputActionsData* GetData();
        // The action at its index among every map's actions in order, as GetData had them this frame. None for a player there is not, or an index past the last
        [[nodiscard]] static std::optional<ActionReading> ReadAction(std::uint32_t action, std::uint32_t player);

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

        // Takes away every rebind the player slot has, as a game's Reset to Defaults does, and saves
        static void ResetBindings(std::uint32_t player = 0);
    };
}