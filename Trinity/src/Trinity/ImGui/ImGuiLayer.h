#pragma once

#include "Trinity/Core/Layer.h"

#include "Trinity/Events/Event.h"
#include "Trinity/Events/ApplicationEvent.h"
#include "Trinity/Events/MouseEvent.h"
#include "Trinity/Events/KeyEvent.h"

namespace Trinity
{
	class ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer();

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Event& e) override;

		void Begin();
		void End();

		void BlockEvents(bool block) { m_BlockEvents = block; }

	private:
		bool m_BlockEvents = true;

		float m_Time = 0.0f;
	};
}