#pragma once

#include "Trinity/Core/Memory.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        template<typename T, typename HandleType>
        class HandlePool
        {
        public:
            [[nodiscard]] HandleType Add(T value)
            {
                std::uint32_t l_Index = 0;
                if (!m_FreeSlots.empty())
                {
                    l_Index = m_FreeSlots.back();
                    m_FreeSlots.pop_back();
                }
                else
                {
                    l_Index = static_cast<std::uint32_t>(m_Slots.size());
                    m_Slots.emplace_back();
                }

                Slot& l_Slot = m_Slots[l_Index];
                l_Slot.Value = std::move(value);
                l_Slot.Alive = true;
                ++m_Count;

                return { l_Index, l_Slot.Generation };
            }

            [[nodiscard]] T* Get(HandleType handle)
            {
                if (handle.Index >= m_Slots.size())
                {
                    return nullptr;
                }

                Slot& l_Slot = m_Slots[handle.Index];

                return l_Slot.Alive && l_Slot.Generation == handle.Generation ? &l_Slot.Value : nullptr;
            }

            [[nodiscard]] std::optional<T> Remove(HandleType handle)
            {
                if (Get(handle) == nullptr)
                {
                    return std::nullopt;
                }

                Slot& l_Slot = m_Slots[handle.Index];
                std::optional<T> l_Value(std::move(l_Slot.Value));
                l_Slot.Value = T{};
                l_Slot.Alive = false;
                l_Slot.Generation = l_Slot.Generation == std::numeric_limits<std::uint32_t>::max() ? 1 : l_Slot.Generation + 1;
                m_FreeSlots.push_back(handle.Index);
                --m_Count;

                return l_Value;
            }

            [[nodiscard]] std::size_t GetCount() const { return m_Count; }

        private:
            struct Slot
            {
                T Value{};
                std::uint32_t Generation = 1;
                bool Alive = false;
            };

            std::vector<Slot, TaggedAllocator<Slot, MemoryTag::Renderer>> m_Slots;
            std::vector<std::uint32_t, TaggedAllocator<std::uint32_t, MemoryTag::Renderer>> m_FreeSlots;
            std::size_t m_Count = 0;
        };
    }
}