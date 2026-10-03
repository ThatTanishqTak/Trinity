#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Types.hpp"

#include <cstdint>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        // Shaders index one resource heap and one sampler heap, ResourceDescriptorHeap and SamplerDescriptorHeap on D3D12 and one array per descriptor type on Vulkan
        constexpr std::uint32_t c_BindlessResourceCapacity = 65536;
        constexpr std::uint32_t c_BindlessSamplerCapacity = 2048;

        // Hands out indices below a capacity, reusing freed ones first
        class IndexAllocator
        {
        public:
            void Reset(std::uint32_t capacity)
            {
                m_Capacity = capacity;
                m_Next = 0;
                m_FreeIndices.clear();
            }

            [[nodiscard]] std::uint32_t Allocate()
            {
                if (!m_FreeIndices.empty())
                {
                    const std::uint32_t l_Index = m_FreeIndices.back();
                    m_FreeIndices.pop_back();

                    return l_Index;
                }

                return m_Next < m_Capacity ? m_Next++ : c_NoBindlessIndex;
            }

            void Free(std::uint32_t index)
            {
                if (index != c_NoBindlessIndex)
                {
                    m_FreeIndices.push_back(index);
                }
            }

        private:
            std::uint32_t m_Capacity = 0;
            std::uint32_t m_Next = 0;
            std::vector<std::uint32_t, TaggedAllocator<std::uint32_t, MemoryTag::Renderer>> m_FreeIndices;
        };
    }
}