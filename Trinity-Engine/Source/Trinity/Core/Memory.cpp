#include "Trinity/Core/Memory.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Platform.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <format>

namespace Trinity
{
    namespace
    {
        struct AllocationHeader
        {
            std::uint64_t Size = 0;
            std::uint32_t Alignment = 0;
            MemoryTag Tag = MemoryTag::Untagged;
        };

        static_assert(sizeof(AllocationHeader) == 16);

        struct TagCounters
        {
            std::atomic<std::uint64_t> CurrentBytes{ 0 };
            std::atomic<std::uint64_t> PeakBytes{ 0 };
            std::atomic<std::uint64_t> LiveAllocations{ 0 };
            std::atomic<std::uint64_t> TotalAllocations{ 0 };
        };

        constexpr std::size_t c_TagCount = static_cast<std::size_t>(MemoryTag::Count);

        constexpr std::array<std::string_view, c_TagCount> c_TagNames =
        {
            "Untagged", "Engine", "Jobs", "FileSystem", "Renderer", "Scene", "Assets", "Audio", "Physics", "Scripting", "Game"
        };

        constinit std::array<TagCounters, c_TagCount> s_TagCounters{};
        constinit TagCounters s_TotalCounters{};
        constinit bool s_Initialized = false;

        std::size_t GetHeaderOffset(std::size_t alignment)
        {
            return (sizeof(AllocationHeader) + alignment - 1) & ~(alignment - 1);
        }

        void RecordAllocation(TagCounters& counters, std::uint64_t size)
        {
            const std::uint64_t l_Current = counters.CurrentBytes.fetch_add(size, std::memory_order_relaxed) + size;
            counters.LiveAllocations.fetch_add(1, std::memory_order_relaxed);
            counters.TotalAllocations.fetch_add(1, std::memory_order_relaxed);

            std::uint64_t l_Peak = counters.PeakBytes.load(std::memory_order_relaxed);
            while (l_Current > l_Peak && !counters.PeakBytes.compare_exchange_weak(l_Peak, l_Current, std::memory_order_relaxed))
            {

            }
        }

        void RecordFree(TagCounters& counters, std::uint64_t size)
        {
            counters.CurrentBytes.fetch_sub(size, std::memory_order_relaxed);
            counters.LiveAllocations.fetch_sub(1, std::memory_order_relaxed);
        }

        MemoryTagStats ReadCounters(const TagCounters& counters)
        {
            MemoryTagStats l_Stats;
            l_Stats.CurrentBytes = counters.CurrentBytes.load(std::memory_order_relaxed);
            l_Stats.PeakBytes = counters.PeakBytes.load(std::memory_order_relaxed);
            l_Stats.LiveAllocations = counters.LiveAllocations.load(std::memory_order_relaxed);
            l_Stats.TotalAllocations = counters.TotalAllocations.load(std::memory_order_relaxed);

            return l_Stats;
        }
    }

    std::string_view ToString(MemoryTag tag)
    {
        const std::size_t l_Index = static_cast<std::size_t>(tag);

        return l_Index < c_TagCount ? c_TagNames[l_Index] : "Invalid";
    }

    namespace Memory
    {
        void Initialize()
        {
            TR_CORE_ASSERT(!s_Initialized, "Memory is already initialized.");

            s_Initialized = true;
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_Initialized, "Memory is not initialized.");

            LogUsage();

            // Untagged memory is left out: static objects legitimately free theirs after Shutdown.
            for (std::size_t it_Index = 1; it_Index < c_TagCount; ++it_Index)
            {
                const MemoryTag l_Tag = static_cast<MemoryTag>(it_Index);
                const MemoryTagStats l_Stats = GetStats(l_Tag);
                if (l_Stats.LiveAllocations != 0)
                {
                    TR_CORE_WARN("Memory leak: {} in {} allocation(s) under tag '{}'", FormatBytes(l_Stats.CurrentBytes), l_Stats.LiveAllocations, ToString(l_Tag));
                }
            }

            s_Initialized = false;
        }

        void* Allocate(std::size_t size, MemoryTag tag, std::size_t alignment)
        {
            TR_CORE_ASSERT(std::has_single_bit(alignment), "Alignment must be a power of two.");
            TR_CORE_ASSERT(tag < MemoryTag::Count, "Invalid memory tag.");

            const std::size_t l_Alignment = std::max(alignment, alignof(AllocationHeader));
            const std::size_t l_HeaderOffset = GetHeaderOffset(l_Alignment);

            std::byte* l_Base = static_cast<std::byte*>(Platform::Allocate(l_HeaderOffset + size, l_Alignment));
            if (l_Base == nullptr)
            {
                TR_CORE_CRITICAL("Out of memory: {} requested under tag '{}'", FormatBytes(size), ToString(tag));

                throw std::bad_alloc();
            }

            std::byte* l_Memory = l_Base + l_HeaderOffset;
            ::new (l_Memory - sizeof(AllocationHeader)) AllocationHeader{ size, static_cast<std::uint32_t>(l_Alignment), tag };

            RecordAllocation(s_TagCounters[static_cast<std::size_t>(tag)], size);
            RecordAllocation(s_TotalCounters, size);

            return l_Memory;
        }

        void Free(void* memory)
        {
            if (memory == nullptr)
            {
                return;
            }

            std::byte* l_Memory = static_cast<std::byte*>(memory);
            const AllocationHeader l_Header = *std::launder(reinterpret_cast<AllocationHeader*>(l_Memory - sizeof(AllocationHeader)));

            RecordFree(s_TagCounters[static_cast<std::size_t>(l_Header.Tag)], l_Header.Size);
            RecordFree(s_TotalCounters, l_Header.Size);

            Platform::Free(l_Memory - GetHeaderOffset(l_Header.Alignment));
        }

        MemoryTagStats GetStats(MemoryTag tag)
        {
            TR_CORE_ASSERT(tag < MemoryTag::Count, "Invalid memory tag.");

            return ReadCounters(s_TagCounters[static_cast<std::size_t>(tag)]);
        }

        MemoryTagStats GetTotalStats()
        {
            return ReadCounters(s_TotalCounters);
        }

        void LogUsage()
        {
            TR_CORE_INFO("Memory usage by tag:");

            for (std::size_t it_Index = 0; it_Index < c_TagCount; ++it_Index)
            {
                const MemoryTag l_Tag = static_cast<MemoryTag>(it_Index);
                const MemoryTagStats l_Stats = GetStats(l_Tag);
                if (l_Stats.TotalAllocations == 0)
                {
                    continue;
                }

                TR_CORE_INFO("  {:<10} {:>12} in {:>6} live (peak {}, {} total)", ToString(l_Tag), FormatBytes(l_Stats.CurrentBytes), l_Stats.LiveAllocations, FormatBytes(l_Stats.PeakBytes), l_Stats.TotalAllocations);
            }

            const MemoryTagStats l_Total = GetTotalStats();
            TR_CORE_INFO("  {:<10} {:>12} in {:>6} live (peak {}, {} total)", "Total", FormatBytes(l_Total.CurrentBytes), l_Total.LiveAllocations, FormatBytes(l_Total.PeakBytes), l_Total.TotalAllocations);
        }

        std::string FormatBytes(std::uint64_t bytes)
        {
            constexpr std::array<std::string_view, 5> c_Units = { "B", "KiB", "MiB", "GiB", "TiB" };

            double l_Value = static_cast<double>(bytes);
            std::size_t l_Unit = 0;
            while (l_Value >= 1024.0 && l_Unit + 1 < c_Units.size())
            {
                l_Value /= 1024.0;
                ++l_Unit;
            }

            if (l_Unit == 0)
            {
                return std::format("{} B", bytes);
            }

            return std::format("{:.2f} {}", l_Value, c_Units[l_Unit]);
        }
    }
}