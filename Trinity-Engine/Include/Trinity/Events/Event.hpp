#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace Trinity
{
	enum class EventType : uint8_t
	{
		None = 0,
		WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved, WindowContentScale,
		WindowMinimize, WindowRestore, WindowMaximize, WindowUnmaximize,
		KeyPressed, KeyReleased, KeyTyped,
		MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled,
		GamepadConnected, GamepadDisconnected, GamepadButtonPressed, GamepadButtonReleased, GamepadAxisMoved
	};

	enum EventCategory : uint32_t
	{
		EventCategoryNone = 0,
		EventCategoryApplication = 1u << 0,
		EventCategoryInput = 1u << 1,
		EventCategoryKeyboard = 1u << 2,
		EventCategoryMouse = 1u << 3,
		EventCategoryMouseButton = 1u << 4,
		EventCategoryGamepad = 1u << 5
	};

	class Event
	{
	public:
		Event() = default;
		virtual ~Event() = default;

		Event(const Event&) = default;
		Event& operator=(const Event&) = default;

		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual uint32_t GetCategoryFlags() const = 0;
		virtual std::string ToString() const { return GetName(); }
		virtual std::unique_ptr<Event> Clone() const = 0;

		bool IsInCategory(uint32_t category) const { return (GetCategoryFlags() & category) != 0; }

		bool Handled = false;
	};

	using EventCallbackFunction = std::function<void(Event&)>;

	class EventSource
	{
	public:
		void SetEventCallback(EventCallbackFunction callback) { m_EventCallback = std::move(callback); }

	protected:
		EventSource() = default;
		~EventSource() = default;

		template<typename T, typename... Args>
			requires std::derived_from<T, Event>&& std::constructible_from<T, Args...>
		void Emit(Args&&... args)
		{
			if (m_EventCallback)
			{
				T l_Event(std::forward<Args>(args)...);
				m_EventCallback(l_Event);
			}
		}

		EventCallbackFunction m_EventCallback;
	};

	class EventDispatcher
	{
	public:
		EventDispatcher(Event& event) : m_Event(event) {}

		template<typename T, typename F>
			requires std::derived_from<T, Event>&& std::invocable<F, T&>&& std::convertible_to<std::invoke_result_t<F, T&>, bool>
		bool Dispatch(F&& function)
		{
			if (m_Event.GetEventType() != T::GetStaticType())
			{
				return false;
			}

			m_Event.Handled |= static_cast<bool>(std::invoke(std::forward<F>(function), static_cast<T&>(m_Event)));

			return true;
		}

	private:
		Event& m_Event;
	};
}

#define TR_EVENT_CLASS_TYPE(type) \
	static constexpr ::Trinity::EventType GetStaticType() { return ::Trinity::EventType::type; } \
	::Trinity::EventType GetEventType() const override { return GetStaticType(); } \
	const char* GetName() const override { return #type; } \
	std::unique_ptr<::Trinity::Event> Clone() const override { return std::make_unique<std::remove_cvref_t<decltype(*this)>>(*this); }

#define TR_EVENT_CLASS_CATEGORY(category) \
	uint32_t GetCategoryFlags() const override { return category; }