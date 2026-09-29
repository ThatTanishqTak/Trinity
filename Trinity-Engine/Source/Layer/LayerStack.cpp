#include "Trinity/Layer/LayerStack.hpp"

#include "Trinity/Core/Log.hpp"

#include <algorithm>

namespace Trinity
{
	LayerStack::LayerStack() = default;
	LayerStack::~LayerStack() = default;

	void LayerStack::PushLayer(std::unique_ptr<Layer> layer)
	{
		if (!layer)
		{
			TR_CORE_WARN("Attempted to push a null layer");

			return;
		}

		Layer* l_Layer = layer.get();

		m_Layers.emplace(m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex), std::move(layer));
		++m_LayerInsertIndex;

		TR_CORE_TRACE("Pushed layer: {}", l_Layer->GetName());

		l_Layer->OnAttach();
	}

	void LayerStack::PushOverlay(std::unique_ptr<Layer> overlay)
	{
		if (!overlay)
		{
			TR_CORE_WARN("Attempted to push a null overlay");

			return;
		}

		Layer* l_Overlay = overlay.get();

		m_Layers.emplace_back(std::move(overlay));

		TR_CORE_TRACE("Pushed overlay: {}", l_Overlay->GetName());

		l_Overlay->OnAttach();
	}

	std::unique_ptr<Layer> LayerStack::PopLayer(Layer* layer)
	{
		const auto l_End = m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex);
		const auto l_Iterator = std::find_if(m_Layers.begin(), l_End, [layer](const std::unique_ptr<Layer>& entry) { return entry.get() == layer; });

		if (l_Iterator == l_End)
		{
			TR_CORE_WARN("PopLayer: layer not found in stack");

			return nullptr;
		}

		(*l_Iterator)->OnDetach();

		std::unique_ptr<Layer> l_Layer = std::move(*l_Iterator);
		m_Layers.erase(l_Iterator);
		--m_LayerInsertIndex;

		TR_CORE_TRACE("Popped layer: {}", l_Layer->GetName());

		return l_Layer;
	}

	std::unique_ptr<Layer> LayerStack::PopOverlay(Layer* overlay)
	{
		const auto l_Begin = m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex);
		const auto l_Iterator = std::find_if(l_Begin, m_Layers.end(), [overlay](const std::unique_ptr<Layer>& entry) { return entry.get() == overlay; });

		if (l_Iterator == m_Layers.end())
		{
			TR_CORE_WARN("PopOverlay: overlay not found in stack");

			return nullptr;
		}

		(*l_Iterator)->OnDetach();

		std::unique_ptr<Layer> l_Overlay = std::move(*l_Iterator);
		m_Layers.erase(l_Iterator);

		TR_CORE_TRACE("Popped overlay: {}", l_Overlay->GetName());

		return l_Overlay;
	}

	void LayerStack::Clear()
	{
		for (auto l_Layer = m_Layers.rbegin(); l_Layer != m_Layers.rend(); ++l_Layer)
		{
			(*l_Layer)->OnDetach();
		}

		m_Layers.clear();
		m_LayerInsertIndex = 0;
	}
}