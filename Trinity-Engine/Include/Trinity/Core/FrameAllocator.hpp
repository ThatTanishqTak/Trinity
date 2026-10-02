#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace Trinity
{
    // Memory handed out during frame N stays valid until the start of frame N + 2, Nothing is destructed
    class FrameAllocator
    {
    public:
        explicit FrameAllocator(std::size_t capacity);
        ~FrameAllocator();

        FrameAllocator(const FrameAllocator&) = delete;
        FrameAllocator& operator=(const FrameAllocator&) = delete;

        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

        template<typename T, typename... Args>
        [[nodiscard]] T* New(Args&&... args)
        {
            static_assert(std::is_trivially_destructible_v<T>, "Frame memory is released without running destructors.");

            return ::new(Allocate(sizeof(T), alignof(T))) T(std::forward<Args>(args)...);
        }

        template<typename T>
        [[nodiscard]] std::span<T> AllocateArray(std::size_t count)
        {
            static_assert(std::is_trivially_destructible_v<T>, "Frame memory is released without running destructors.");

            if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            {
                throw std::bad_array_new_length();
            }

            T* l_Data = static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
            std::uninitialized_default_construct_n(l_Data, count);

            return { l_Data, count };
        }

        void BeginFrame();

        [[nodiscard]] std::size_t GetCapacity() const { return m_Capacity; }
        [[nodiscard]] std::size_t GetUsed() const;
        [[nodiscard]] std::size_t GetPeakUsed() const;
        [[nodiscard]] std::uint64_t GetOverflowFrameCount() const { return m_OverflowFrameCount; }

    private:
        struct Buffer
        {
            std::byte* Data = nullptr;
            std::atomic<std::size_t> Offset{ 0 };
            std::atomic<std::size_t> OverflowBytes{ 0 };
            std::mutex OverflowMutex;
            std::vector<void*> OverflowAllocations;
        };

        void* AllocateOverflow(Buffer& buffer, std::size_t size, std::size_t alignment);
        void Reset(Buffer& buffer);

        std::array<Buffer, 2> m_Buffers;
        std::size_t m_Capacity = 0;
        std::size_t m_CurrentBuffer = 0;
        std::size_t m_PeakUsed = 0;
        std::uint64_t m_OverflowFrameCount = 0;
    };
}