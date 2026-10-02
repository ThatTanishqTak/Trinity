#include "Trinity/Core/FrameAllocator.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"

#include <algorithm>
#include <bit>

namespace Trinity
{
    namespace
    {
        constexpr std::size_t c_BufferAlignment = 64;
    }

    FrameAllocator::FrameAllocator(std::size_t capacity) : m_Capacity(capacity)
    {
        for (Buffer& it_Buffer : m_Buffers)
        {
            it_Buffer.Data = static_cast<std::byte*>(Memory::Allocate(m_Capacity, MemoryTag::Frame, c_BufferAlignment));
        }
    }

    FrameAllocator::~FrameAllocator()
    {
        for (Buffer& it_Buffer : m_Buffers)
        {
            Reset(it_Buffer);
            Memory::Free(it_Buffer.Data);
        }
    }

    void* FrameAllocator::Allocate(std::size_t size, std::size_t alignment)
    {
        TR_CORE_ASSERT(std::has_single_bit(alignment), "Alignment must be a power of two.");

        Buffer& l_Buffer = m_Buffers[m_CurrentBuffer];
        const std::uintptr_t l_Base = reinterpret_cast<std::uintptr_t>(l_Buffer.Data);
        const std::uintptr_t l_AlignmentMask = static_cast<std::uintptr_t>(alignment) - 1;

        std::size_t l_Offset = l_Buffer.Offset.load(std::memory_order_relaxed);
        while (true)
        {
            const std::size_t l_Start = static_cast<std::size_t>(((l_Base + l_Offset + l_AlignmentMask) & ~l_AlignmentMask) - l_Base);
            if (l_Start > m_Capacity || size > m_Capacity - l_Start)
            {
                return AllocateOverflow(l_Buffer, size, alignment);
            }

            if (l_Buffer.Offset.compare_exchange_weak(l_Offset, l_Start + size, std::memory_order_relaxed))
            {
                return l_Buffer.Data + l_Start;
            }
        }
    }

    void FrameAllocator::BeginFrame()
    {
        const Buffer& l_Finished = m_Buffers[m_CurrentBuffer];
        const std::size_t l_Overflow = l_Finished.OverflowBytes.load(std::memory_order_relaxed);
        const std::size_t l_Used = l_Finished.Offset.load(std::memory_order_relaxed) + l_Overflow;

        m_PeakUsed = std::max(m_PeakUsed, l_Used);
        if (l_Overflow != 0)
        {
            ++m_OverflowFrameCount;
            TR_CORE_WARN("Frame allocator overflowed: the frame used {} of a {} buffer, {} of it from the heap", Memory::FormatBytes(l_Used), Memory::FormatBytes(m_Capacity), Memory::FormatBytes(l_Overflow));
        }

        m_CurrentBuffer = (m_CurrentBuffer + 1) % m_Buffers.size();
        Reset(m_Buffers[m_CurrentBuffer]);
    }

    std::size_t FrameAllocator::GetUsed() const
    {
        const Buffer& l_Buffer = m_Buffers[m_CurrentBuffer];

        return l_Buffer.Offset.load(std::memory_order_relaxed) + l_Buffer.OverflowBytes.load(std::memory_order_relaxed);
    }

    std::size_t FrameAllocator::GetPeakUsed() const
    {
        return std::max(m_PeakUsed, GetUsed());
    }

    void* FrameAllocator::AllocateOverflow(Buffer& buffer, std::size_t size, std::size_t alignment)
    {
        void* l_Memory = Memory::Allocate(size, MemoryTag::Frame, alignment);
        try
        {
            std::scoped_lock l_Lock(buffer.OverflowMutex);
            buffer.OverflowAllocations.push_back(l_Memory);
        }
        catch (...)
        {
            Memory::Free(l_Memory);
            throw;
        }

        buffer.OverflowBytes.fetch_add(size, std::memory_order_relaxed);

        return l_Memory;
    }

    void FrameAllocator::Reset(Buffer& buffer)
    {
        std::scoped_lock l_Lock(buffer.OverflowMutex);
        for (void* it_Allocation : buffer.OverflowAllocations)
        {
            Memory::Free(it_Allocation);
        }

        buffer.OverflowAllocations.clear();
        buffer.OverflowBytes.store(0, std::memory_order_relaxed);
        buffer.Offset.store(0, std::memory_order_relaxed);
    }
}