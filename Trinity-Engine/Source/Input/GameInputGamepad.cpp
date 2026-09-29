#include "Trinity/Input/GameInputGamepad.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Events/GamepadEvent.hpp"

#include <Windows.h>
#include <GameInput.h>
#include <wrl/client.h>

#if GAMEINPUT_API_VERSION == 1
using namespace GameInput::v1;
#elif GAMEINPUT_API_VERSION == 2
using namespace GameInput::v2;
#elif GAMEINPUT_API_VERSION == 3
using namespace GameInput::v3;
#else
#error "Trinity requires the GameInput headers from Vendor/GameInput (API v1 or newer), not the Windows SDK v0 header"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <vector>

namespace Trinity
{
	namespace
	{
		constexpr float s_StickDeadZone = 0.24f;
		constexpr float s_TriggerThreshold = 0.12f;
		constexpr float s_AxisEpsilon = 0.01f;

		constexpr std::array<std::pair<GameInputGamepadButtons, GamepadButton>, 14> s_ButtonMap
		{ {
			{ GameInputGamepadA, GamepadButton::South },
			{ GameInputGamepadB, GamepadButton::East },
			{ GameInputGamepadX, GamepadButton::West },
			{ GameInputGamepadY, GamepadButton::North },
			{ GameInputGamepadLeftShoulder, GamepadButton::LeftShoulder },
			{ GameInputGamepadRightShoulder, GamepadButton::RightShoulder },
			{ GameInputGamepadView, GamepadButton::Back },
			{ GameInputGamepadMenu, GamepadButton::Start },
			{ GameInputGamepadLeftThumbstick, GamepadButton::LeftStick },
			{ GameInputGamepadRightThumbstick, GamepadButton::RightStick },
			{ GameInputGamepadDPadUp, GamepadButton::DPadUp },
			{ GameInputGamepadDPadRight, GamepadButton::DPadRight },
			{ GameInputGamepadDPadDown, GamepadButton::DPadDown },
			{ GameInputGamepadDPadLeft, GamepadButton::DPadLeft }
		} };

		std::pair<float, float> ApplyStickDeadZone(float x, float y)
		{
			const float l_Magnitude = std::sqrt(x * x + y * y);
			if (l_Magnitude <= s_StickDeadZone)
			{
				return { 0.0f, 0.0f };
			}

			const float l_Scale = (std::min(l_Magnitude, 1.0f) - s_StickDeadZone) / (1.0f - s_StickDeadZone) / l_Magnitude;

			return { x * l_Scale, y * l_Scale };
		}

		float ApplyTriggerThreshold(float value)
		{
			return value <= s_TriggerThreshold ? 0.0f : (value - s_TriggerThreshold) / (1.0f - s_TriggerThreshold);
		}
	}

	struct GameInputGamepad::State
	{
		struct Slot
		{
			Microsoft::WRL::ComPtr<IGameInputDevice> Device;
			uint32_t Buttons = 0;
			std::array<float, static_cast<size_t>(GamepadAxis::Count)> Axes{};
		};

		struct PendingChange
		{
			Microsoft::WRL::ComPtr<IGameInputDevice> Device;
			bool Connected = false;
		};

		Microsoft::WRL::ComPtr<IGameInput> Input;
		GameInputCallbackToken DeviceCallbackToken = 0;

		std::array<Slot, MaxGamepads> Slots;

		std::mutex PendingMutex;
		std::vector<PendingChange> PendingChanges;

		// Runs on a GameInput worker thread: only queue the change, Poll() applies it on the main thread
		static void CALLBACK OnDeviceChanged(GameInputCallbackToken, void* context, IGameInputDevice* device, uint64_t, GameInputDeviceStatus currentStatus, GameInputDeviceStatus previousStatus)
		{
			State* l_State = static_cast<State*>(context);

			const bool l_Connected = (currentStatus & GameInputDeviceConnected) != 0;
			const bool l_WasConnected = (previousStatus & GameInputDeviceConnected) != 0;

			if (l_Connected == l_WasConnected)
			{
				return;
			}

			std::scoped_lock l_Lock(l_State->PendingMutex);
			l_State->PendingChanges.push_back({ Microsoft::WRL::ComPtr<IGameInputDevice>(device), l_Connected });
		}
	};

	GameInputGamepad::GameInputGamepad() = default;
	GameInputGamepad::~GameInputGamepad() = default;

	bool GameInputGamepad::Initialize()
	{
		m_State = std::make_unique<State>();

		const HRESULT l_CreateResult = GameInputCreate(m_State->Input.GetAddressOf());
		if (FAILED(l_CreateResult))
		{
			TR_CORE_ERROR("GameInputCreate failed (0x{:08X}), run GameInputRedist.msi from the application folder", static_cast<uint32_t>(l_CreateResult));
			m_State.reset();

			return false;
		}

		const HRESULT l_CallbackResult = m_State->Input->RegisterDeviceCallback(nullptr, GameInputKindGamepad, GameInputDeviceConnected, GameInputBlockingEnumeration, m_State.get(), &State::OnDeviceChanged, &m_State->DeviceCallbackToken);
		if (FAILED(l_CallbackResult))
		{
			TR_CORE_ERROR("GameInput RegisterDeviceCallback failed (0x{:08X})", static_cast<uint32_t>(l_CallbackResult));
			m_State.reset();

			return false;
		}

		TR_CORE_INFO("GameInput gamepad backend initialized (API v{})", GAMEINPUT_API_VERSION);

		return true;
	}

	void GameInputGamepad::Shutdown()
	{
		if (!m_State)
		{
			return;
		}

		if (m_State->DeviceCallbackToken)
		{
			m_State->Input->UnregisterCallback(m_State->DeviceCallbackToken);
		}

		m_State.reset();
	}

	void GameInputGamepad::Poll()
	{
		if (!m_State)
		{
			return;
		}

		std::vector<State::PendingChange> l_Changes;
		{
			std::scoped_lock l_Lock(m_State->PendingMutex);
			l_Changes.swap(m_State->PendingChanges);
		}

		auto& l_Slots = m_State->Slots;

		for (State::PendingChange& l_Change : l_Changes)
		{
			const auto l_Existing = std::find_if(l_Slots.begin(), l_Slots.end(), [&l_Change](const State::Slot& slot) { return slot.Device.Get() == l_Change.Device.Get(); });

			if (l_Change.Connected)
			{
				if (l_Existing != l_Slots.end())
				{
					continue;
				}

				const auto l_Free = std::find_if(l_Slots.begin(), l_Slots.end(), [](const State::Slot& slot) { return !slot.Device; });
				if (l_Free == l_Slots.end())
				{
					TR_CORE_WARN("Gamepad limit ({}) reached, ignoring new device", MaxGamepads);

					continue;
				}

				l_Free->Device = l_Change.Device;

				const uint32_t l_Id = static_cast<uint32_t>(std::distance(l_Slots.begin(), l_Free));
				TR_CORE_INFO("Gamepad {} connected", l_Id);
				Emit<GamepadConnectedEvent>(l_Id);

				continue;
			}

			if (l_Existing == l_Slots.end())
			{
				continue;
			}

			const uint32_t l_Id = static_cast<uint32_t>(std::distance(l_Slots.begin(), l_Existing));

			for (const auto& [l_NativeButton, l_Button] : s_ButtonMap)
			{
				if ((l_Existing->Buttons & static_cast<uint32_t>(l_NativeButton)) != 0)
				{
					Emit<GamepadButtonReleasedEvent>(l_Id, l_Button);
				}
			}

			for (size_t l_Axis = 0; l_Axis < l_Existing->Axes.size(); ++l_Axis)
			{
				if (l_Existing->Axes[l_Axis] != 0.0f)
				{
					Emit<GamepadAxisMovedEvent>(l_Id, static_cast<GamepadAxis>(l_Axis), 0.0f);
				}
			}

			*l_Existing = State::Slot{};

			TR_CORE_INFO("Gamepad {} disconnected", l_Id);
			Emit<GamepadDisconnectedEvent>(l_Id);
		}

		for (uint32_t l_Id = 0; l_Id < static_cast<uint32_t>(l_Slots.size()); ++l_Id)
		{
			State::Slot& l_Slot = l_Slots[l_Id];
			if (!l_Slot.Device)
			{
				continue;
			}

			Microsoft::WRL::ComPtr<IGameInputReading> l_Reading;
			if (FAILED(m_State->Input->GetCurrentReading(GameInputKindGamepad, l_Slot.Device.Get(), l_Reading.GetAddressOf())))
			{
				continue;
			}

			GameInputGamepadState l_GamepadState{};
			if (!l_Reading->GetGamepadState(&l_GamepadState))
			{
				continue;
			}

			const uint32_t l_Buttons = static_cast<uint32_t>(l_GamepadState.buttons);

			for (const auto& [l_NativeButton, l_Button] : s_ButtonMap)
			{
				const uint32_t l_Mask = static_cast<uint32_t>(l_NativeButton);
				const bool l_IsDown = (l_Buttons & l_Mask) != 0;
				const bool l_WasDown = (l_Slot.Buttons & l_Mask) != 0;

				if (l_IsDown && !l_WasDown)
				{
					Emit<GamepadButtonPressedEvent>(l_Id, l_Button);
				}
				else if (!l_IsDown && l_WasDown)
				{
					Emit<GamepadButtonReleasedEvent>(l_Id, l_Button);
				}
			}

			l_Slot.Buttons = l_Buttons;

			const auto [l_LeftX, l_LeftY] = ApplyStickDeadZone(l_GamepadState.leftThumbstickX, l_GamepadState.leftThumbstickY);
			const auto [l_RightX, l_RightY] = ApplyStickDeadZone(l_GamepadState.rightThumbstickX, l_GamepadState.rightThumbstickY);

			const std::array<float, static_cast<size_t>(GamepadAxis::Count)> l_Axes
			{
				l_LeftX,
				l_LeftY,
				l_RightX,
				l_RightY,
				ApplyTriggerThreshold(l_GamepadState.leftTrigger),
				ApplyTriggerThreshold(l_GamepadState.rightTrigger)
			};

			for (size_t l_Axis = 0; l_Axis < l_Axes.size(); ++l_Axis)
			{
				const float l_Previous = l_Slot.Axes[l_Axis];
				const float l_Current = l_Axes[l_Axis];

				if (std::fabs(l_Current - l_Previous) >= s_AxisEpsilon || (l_Current == 0.0f && l_Previous != 0.0f))
				{
					l_Slot.Axes[l_Axis] = l_Current;
					Emit<GamepadAxisMovedEvent>(l_Id, static_cast<GamepadAxis>(l_Axis), l_Current);
				}
			}
		}
	}
}