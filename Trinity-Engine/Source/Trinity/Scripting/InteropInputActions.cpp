#include "Trinity/Scripting/Interop.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Input/InputActions.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace Trinity::Interop
{
    namespace
    {
        // C# writes its InputDevice out by hand, so a change to these numbers is one to make there too
        static_assert(std::to_underlying(InputDevice::KeyboardMouse) == 0 && std::to_underlying(InputDevice::Gamepad0) == 1 && std::to_underlying(InputDevice::Gamepad7) == 8 && std::to_underlying(InputDevice::None) == 0xFF, "Trinity.InputDevice must number the devices as InputDevice does");
        static_assert(InputActions::c_MaxPlayers <= 8u, "An action's edges have a byte of players for each of its four edges");
        // C# writes its InputRebindState out by hand too
        static_assert(std::to_underlying(InputRebindState::None) == 0 && std::to_underlying(InputRebindState::Waiting) == 1 && std::to_underlying(InputRebindState::Bound) == 2 && std::to_underlying(InputRebindState::Refused) == 3 && std::to_underlying(InputRebindState::Canceled) == 4, "Trinity.InputRebindState must number the states as InputRebindState does");

        // A player C# gives below 0 is none, and one above the last is one there is not
        [[nodiscard]] std::uint32_t ToPlayer(Int32 player)
        {
            return player >= 0 ? static_cast<std::uint32_t>(player) : InputActions::c_NoPlayer;
        }

        [[nodiscard]] Int32 FromPlayer(std::uint32_t player)
        {
            return player != InputActions::c_NoPlayer ? static_cast<Int32>(player) : -1;
        }

        // A binding or part C# gives below 0 is one there is not
        [[nodiscard]] std::uint32_t ToIndex(Int32 index)
        {
            return index >= 0 ? static_cast<std::uint32_t>(index) : std::numeric_limits<std::uint32_t>::max();
        }

        // A source's name, kept until the next is asked for, as C# copies it at once
        [[nodiscard]] Utf8 ToName(InputSource source)
        {
            static std::string s_Name;
            s_Name = ToString(source);

            return ToUtf8(s_Name);
        }

        // A number C# gives that is no device is None, which no player reads or can be assigned
        [[nodiscard]] InputDevice ToDevice(Int32 device)
        {
            return device >= 0 && device < static_cast<Int32>(std::to_underlying(InputDevice::Count)) ? static_cast<InputDevice>(device) : InputDevice::None;
        }
    }

    UInt64 InputAction_Find(Utf8 path)
    {
        return InputAction::Find(ToStringView(path)).ToHandle();
    }

    Bool InputAction_IsValid(UInt64 action)
    {
        return InputAction::FromHandle(action).IsValid();
    }

    Bool InputAction_IsPressed(UInt64 action, Int32 player)
    {
        return InputAction::FromHandle(action).IsPressed(ToPlayer(player));
    }

    Bool InputAction_WasPressed(UInt64 action, Int32 player)
    {
        return InputAction::FromHandle(action).WasPressed(ToPlayer(player));
    }

    Bool InputAction_WasReleased(UInt64 action, Int32 player)
    {
        return InputAction::FromHandle(action).WasReleased(ToPlayer(player));
    }

    Bool InputAction_WasPerformed(UInt64 action, Int32 player)
    {
        return InputAction::FromHandle(action).WasPerformed(ToPlayer(player));
    }

    Bool InputAction_WasCanceled(UInt64 action, Int32 player)
    {
        return InputAction::FromHandle(action).WasCanceled(ToPlayer(player));
    }

    Float InputAction_ReadProgress(UInt64 action, Int32 player)
    {
        return InputAction::FromHandle(action).ReadProgress(ToPlayer(player));
    }

    Float InputAction_ReadValue(UInt64 action, Int32 player)
    {
        return InputAction::FromHandle(action).ReadValue(ToPlayer(player));
    }

    Void InputAction_ReadVector2(UInt64 action, Int32 player, Vector2* value)
    {
        *value = InputAction::FromHandle(action).ReadVector2(ToPlayer(player));
    }

    // As C# raises its events, outside any fixed step, so these are the frame's
    Int64 InputAction_GetEdges(UInt64 action)
    {
        const InputAction l_Action = InputAction::FromHandle(action);
        if (!l_Action.IsCurrent())
        {
            return -1;
        }

        Int64 l_Edges = 0;
        for (std::uint32_t it_Player = 0; it_Player < InputActions::c_MaxPlayers; ++it_Player)
        {
            l_Edges |= l_Action.WasPressed(it_Player) ? Int64{ 1 } << it_Player : 0;
            l_Edges |= l_Action.WasReleased(it_Player) ? Int64{ 1 } << (it_Player + 8) : 0;
            l_Edges |= l_Action.WasPerformed(it_Player) ? Int64{ 1 } << (it_Player + 16) : 0;
            l_Edges |= l_Action.WasCanceled(it_Player) ? Int64{ 1 } << (it_Player + 24) : 0;
        }

        return l_Edges;
    }

    Int32 InputAction_GetBindingCount(UInt64 action)
    {
        return static_cast<Int32>(InputAction::FromHandle(action).GetBindingCount());
    }

    Int32 InputAction_GetPartCount(UInt64 action, Int32 binding)
    {
        return static_cast<Int32>(InputAction::FromHandle(action).GetPartCount(ToIndex(binding)));
    }

    Utf8 InputAction_GetSource(UInt64 action, Int32 binding, Int32 part, Int32 player, Bool asset)
    {
        const InputAction l_Action = InputAction::FromHandle(action);

        return ToName(asset ? l_Action.GetDefaultSource(ToIndex(binding), ToIndex(part)) : l_Action.GetSource(ToIndex(binding), ToIndex(part), ToPlayer(player)));
    }

    // A name this build does not know rebinds nothing
    UInt64 InputAction_Rebind(UInt64 action, Int32 binding, Int32 part, Utf8 source, Int32 player)
    {
        const std::optional<InputSource> l_Source = ParseInputSource(ToStringView(source));
        if (!l_Source)
        {
            TR_CORE_WARN("Scripting: nothing was rebound to '{}', which is no source this build knows", ToStringView(source));

            return 0;
        }

        return InputAction::FromHandle(action).Rebind(ToIndex(binding), ToIndex(part), *l_Source, ToPlayer(player)).ToHandle();
    }

    UInt64 InputAction_StartRebind(UInt64 action, Int32 binding, Int32 part, Int32 player)
    {
        return InputAction::FromHandle(action).StartRebind(ToIndex(binding), ToIndex(part), ToPlayer(player)).ToHandle();
    }

    Void InputAction_ResetBinding(UInt64 action, Int32 binding, Int32 part, Int32 player)
    {
        InputAction::FromHandle(action).ResetBinding(ToIndex(binding), ToIndex(part), ToPlayer(player));
    }

    Int32 InputRebind_GetState(UInt64 rebind)
    {
        return static_cast<Int32>(std::to_underlying(InputRebind::FromHandle(rebind).GetState()));
    }

    Utf8 InputRebind_GetSource(UInt64 rebind)
    {
        return ToName(InputRebind::FromHandle(rebind).GetSource());
    }

    Utf8 InputRebind_GetConflict(UInt64 rebind)
    {
        return ToUtf8(InputRebind::FromHandle(rebind).GetConflict());
    }

    Void InputRebind_Cancel(UInt64 rebind)
    {
        InputRebind::FromHandle(rebind).Cancel();
    }

    Int32 InputActions_GetMaxPlayers()
    {
        return static_cast<Int32>(InputActions::c_MaxPlayers);
    }

    Float InputActions_GetPressPoint()
    {
        return InputActions::c_PressPoint;
    }

    UInt64 InputActions_GetAsset()
    {
        return InputActions::GetAsset().GetValue();
    }

    Bool InputActions_IsReady()
    {
        return InputActions::IsReady();
    }

    Bool InputActions_EnableMap(Utf8 map, Int32 player)
    {
        return InputActions::EnableMap(ToStringView(map), ToPlayer(player));
    }

    Bool InputActions_DisableMap(Utf8 map, Int32 player)
    {
        return InputActions::DisableMap(ToStringView(map), ToPlayer(player));
    }

    Bool InputActions_IsMapEnabled(Utf8 map, Int32 player)
    {
        return InputActions::IsMapEnabled(ToStringView(map), ToPlayer(player));
    }

    Int32 InputActions_AddPlayer()
    {
        return FromPlayer(InputActions::AddPlayer());
    }

    Void InputActions_RemovePlayer(Int32 player)
    {
        InputActions::RemovePlayer(ToPlayer(player));
    }

    Bool InputActions_IsPlayer(Int32 player)
    {
        return InputActions::IsPlayer(ToPlayer(player));
    }

    Int32 InputActions_GetPlayerCount()
    {
        return static_cast<Int32>(InputActions::GetPlayerCount());
    }

    Bool InputActions_AssignDevice(Int32 device, Int32 player)
    {
        return InputActions::AssignDevice(ToDevice(device), ToPlayer(player));
    }

    Void InputActions_UnassignDevice(Int32 device)
    {
        InputActions::UnassignDevice(ToDevice(device));
    }

    Int32 InputActions_GetDevicePlayer(Int32 device)
    {
        return FromPlayer(InputActions::GetDevicePlayer(ToDevice(device)));
    }

    Bool InputActions_ReadsDevice(Int32 player, Int32 device)
    {
        return InputActions::ReadsDevice(ToPlayer(player), ToDevice(device));
    }

    Int32 InputActions_GetJoiningDevice()
    {
        return static_cast<Int32>(std::to_underlying(InputActions::GetJoiningDevice()));
    }

    Void InputActions_ResetBindings(Int32 player)
    {
        InputActions::ResetBindings(ToPlayer(player));
    }
}