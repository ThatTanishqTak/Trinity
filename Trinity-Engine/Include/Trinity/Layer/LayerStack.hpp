#pragma once

#include "Trinity/Layer/Layer.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace Trinity
{
	class LayerStack
	{
	public:
		LayerStack();
		~LayerStack();

		LayerStack(const LayerStack&) = delete;
		LayerStack& operator=(const LayerStack&) = delete;
		LayerStack(LayerStack&&) = delete;
		LayerStack& operator=(LayerStack&&) = delete;

		void PushLayer(std::unique_ptr<Layer> layer);
		void PushOverlay(std::unique_ptr<Layer> overlay);

		std::unique_ptr<Layer> PopLayer(Layer* layer);
		std::unique_ptr<Layer> PopOverlay(Layer* overlay);

		void Clear();

		auto begin() { return m_Layers.begin(); }
		auto end() { return m_Layers.end(); }
		auto rbegin() { return m_Layers.rbegin(); }
		auto rend() { return m_Layers.rend(); }

		auto begin() const { return m_Layers.begin(); }
		auto end() const { return m_Layers.end(); }
		auto rbegin() const { return m_Layers.rbegin(); }
		auto rend() const { return m_Layers.rend(); }

	private:
		std::vector<std::unique_ptr<Layer>> m_Layers;
		size_t m_LayerInsertIndex = 0;
	};
}