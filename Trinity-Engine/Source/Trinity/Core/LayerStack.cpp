#include "Trinity/Core/LayerStack.hpp"

#include <iterator>
#include <utility>

namespace Trinity
{
    LayerStack::~LayerStack()
    {
        Clear();
    }

    Layer& LayerStack::PushLayer(Scope<Layer> layer)
    {
        Layer& l_Reference = *layer;
        m_Layers.insert(m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex), std::move(layer));
        ++m_LayerInsertIndex;
        l_Reference.OnAttach();

        return l_Reference;
    }

    Layer& LayerStack::PushOverlay(Scope<Layer> overlay)
    {
        Layer& l_Reference = *overlay;
        m_Layers.push_back(std::move(overlay));
        l_Reference.OnAttach();
        
        return l_Reference;
    }

    void LayerStack::Clear()
    {
        while (!m_Layers.empty())
        {
            m_Layers.back()->OnDetach();
            m_Layers.pop_back();
        }
        m_LayerInsertIndex = 0;
    }
}