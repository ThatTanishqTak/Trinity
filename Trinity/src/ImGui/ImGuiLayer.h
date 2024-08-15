#pragma once

#include "Trinity/Layer.h"

#include "Trinity/Events/Event.h"
#include "Trinity/Events/ApplicationEvent.h"
#include "Trinity/Events/MouseEvent.h"
#include "Trinity/Events/KeyEvent.h"

namespace Trinity
{
	class TRINITY_API ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer();

		void OnAttach();
		void OnDetach();
		void OnUpdate();
		void OnEvent(Event& event);

	private:
		bool OnMouseButtonPressedEvent(MouseButtonPressedEvent& event);
		bool OnMouseButtonReleasedEvent(MouseButtonReleasedEvent& event);
		bool OnMouseMovedEvent(MouseMovedEvent& event);
		bool OnMouseScrolledEvent(MouseScrolledEvent& event);

		bool OnKeyPressedEvent(KeyPressedEvent& event);
		bool OnKeyTypedEvent(KeyTypedEvent& event);
		bool OnKeyReleasedEvent(KeyReleasedEvent& event);

		bool OnWindowResizeEvent(WindowResizeEvent& event);

	private:
		float m_Time = 0.0f;
	};
}