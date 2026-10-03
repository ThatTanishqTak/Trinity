#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Layer.hpp"

#include <cstddef>
#include <vector>

namespace Trinity
{
    class TRINITY_API LayerStack
    {
    public:
        LayerStack() = default;
        ~LayerStack();

        LayerStack(const LayerStack&) = delete;
        LayerStack& operator=(const LayerStack&) = delete;

        Layer& PushLayer(Scope<Layer> layer);
        Layer& PushOverlay(Scope<Layer> overlay);

        void Clear();

        [[nodiscard]] auto begin() { return m_Layers.begin(); }
        [[nodiscard]] auto end() { return m_Layers.end(); }
        [[nodiscard]] auto rbegin() { return m_Layers.rbegin(); }
        [[nodiscard]] auto rend() { return m_Layers.rend(); }

    private:
        std::vector<Scope<Layer>> m_Layers;
        std::size_t m_LayerInsertIndex = 0;
    };
}