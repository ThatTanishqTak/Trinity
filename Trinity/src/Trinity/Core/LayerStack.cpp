#include "trpch.h"
#include "LayerStack.h"

namespace Trinity
{
	LayerStack::LayerStack()
	{

	}

	LayerStack::~LayerStack()
	{
		for (Layer* layer : m_Layers)
		{
			delete layer;
		}
	}

	void LayerStack::PushLayer(Layer* layer)
	{
		m_Layers.emplace(m_Layers.begin() + m_LayersInsertIndex, layer);
		m_LayersInsertIndex++;
	}

	void LayerStack::PushOverlay(Layer* overLayer)
	{
		m_Layers.emplace_back(overLayer);
	}

	void LayerStack::PopLayer(Layer* layer)
	{
		auto it = std::find(m_Layers.begin(), m_Layers.end(), layer);
		if (it != m_Layers.end())
		{
			m_Layers.erase(it);
			m_LayersInsertIndex--;
		}
	}

	void LayerStack::PopOverlay(Layer* overLayer)
	{
		auto it = std::find(m_Layers.begin(), m_Layers.end(), overLayer);
		if (it != m_Layers.end())
		{
			m_Layers.erase(it);
		}
	}
}