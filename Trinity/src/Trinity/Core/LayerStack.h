#pragma once

#include "Trinity/Core/Core.h"
#include "Layer.h"

#include <vector>

namespace Trinity
{
	class LayerStack
	{
	public:
		LayerStack();
		~LayerStack();

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overLayer);
		void PopLayer(Layer* layer);
		void PopOverlay(Layer* overLayer);

		std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
		std::vector<Layer*>::iterator end() { return m_Layers.end(); }

	private:
		std::vector<Layer*> m_Layers;
		unsigned int m_LayersInsertIndex = 0;
	};
}