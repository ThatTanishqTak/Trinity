#include "Trinity/Core/Memory.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Platform.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <format>
#include <limits>
#include <mutex>

namespace Trinity
{
    namespace
    {
        struct AllocationHeader
        {
            std::uint64_t Size = 0;
            std::uint32_t Alignment = 0;
            MemoryTag Tag = MemoryTag::Untagged;
            // Made by global new under a MemoryTagScope, with a ScopedLink just before the header
            bool Scoped = false;
        };

        static_assert(sizeof(AllocationHeader) == 16);

        // Links every live allocation global new made under a MemoryTagScope, so what a library keeps in its statics past its own shutdown can be counted as untagged, as every other static's memory is
        struct ScopedLink
        {
            ScopedLink* Previous = nullptr;
            ScopedLink* Next = nullptr;
        };

        static_assert(sizeof(ScopedLink) == 16);

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
            "Untagged", "Engine", "Log", "Frame", "Jobs", "FileSystem", "Renderer", "UI", "Scene", "Assets", "Audio", "Physics", "Scripting", "Game", "Input", "GameUI"
        };

        constinit std::array<TagCounters, c_TagCount> s_TagCounters{};
        constinit TagCounters s_TotalCounters{};
        constinit bool s_Initialized = false;
        constinit thread_local MemoryTag t_ScopeTag = MemoryTag::Untagged;
        constinit ScopedLink* s_ScopedAllocations = nullptr;

        std::size_t GetHeaderOffset(std::size_t alignment, bool scoped)
        {
            const std::size_t l_Prefix = sizeof(AllocationHeader) + (scoped ? sizeof(ScopedLink) : 0);

            return (l_Prefix + alignment - 1) & ~(alignment - 1);
        }

        // Made with the first scoped allocation, which no static a scope makes can come before, so it ends after every one of them
        std::mutex& GetScopedMutex()
        {
            static std::mutex s_Mutex;

            return s_Mutex;
        }

        AllocationHeader& GetHeader(ScopedLink* link)
        {
            return *std::launder(reinterpret_cast<AllocationHeader*>(reinterpret_cast<std::byte*>(link) + sizeof(ScopedLink)));
        }

        void AddLive(TagCounters& counters, std::uint64_t size)
        {
            const std::uint64_t l_Current = counters.CurrentBytes.fetch_add(size, std::memory_order_relaxed) + size;
            counters.LiveAllocations.fetch_add(1, std::memory_order_relaxed);

            std::uint64_t l_Peak = counters.PeakBytes.load(std::memory_order_relaxed);
            while (l_Current > l_Peak && !counters.PeakBytes.compare_exchange_weak(l_Peak, l_Current, std::memory_order_relaxed))
            {

            }
        }

        void RecordAllocation(TagCounters& counters, std::uint64_t size)
        {
            AddLive(counters, size);
            counters.TotalAllocations.fetch_add(1, std::memory_order_relaxed);
        }

        void RecordFree(TagCounters& counters, std::uint64_t size)
        {
            counters.CurrentBytes.fetch_sub(size, std::memory_order_relaxed);
            counters.LiveAllocations.fetch_sub(1, std::memory_order_relaxed);
        }

        void* AllocateTagged(std::size_t size, MemoryTag tag, std::size_t alignment, bool scoped = false)
        {
            const std::size_t l_Alignment = std::max(alignment, alignof(AllocationHeader));
            const std::size_t l_HeaderOffset = GetHeaderOffset(l_Alignment, scoped);
            if (size > std::numeric_limits<std::size_t>::max() - l_HeaderOffset - l_Alignment)
            {
                return nullptr;
            }

            std::byte* l_Base = static_cast<std::byte*>(Platform::Allocate(l_HeaderOffset + size, l_Alignment));
            if (l_Base == nullptr)
            {
                return nullptr;
            }

            std::byte* l_Memory = l_Base + l_HeaderOffset;
            ::new (l_Memory - sizeof(AllocationHeader)) AllocationHeader{ size, static_cast<std::uint32_t>(l_Alignment), tag, scoped };

            RecordAllocation(s_TagCounters[static_cast<std::size_t>(tag)], size);
            RecordAllocation(s_TotalCounters, size);

            if (scoped)
            {
                ScopedLink* l_Link = ::new (l_Memory - sizeof(AllocationHeader) - sizeof(ScopedLink)) ScopedLink{};

                std::scoped_lock l_Lock(GetScopedMutex());
                l_Link->Next = s_ScopedAllocations;
                if (s_ScopedAllocations != nullptr)
                {
                    s_ScopedAllocations->Previous = l_Link;
                }

                s_ScopedAllocations = l_Link;
            }

            return l_Memory;
        }

        // A scoped allocation's tag is read under the lock, as UntagScopedAllocations may be changing it
        void FreeTagged(void* memory)
        {
            if (memory == nullptr)
            {
                return;
            }

            std::byte* l_Memory = static_cast<std::byte*>(memory);
            const AllocationHeader& l_Header = *std::launder(reinterpret_cast<AllocationHeader*>(l_Memory - sizeof(AllocationHeader)));
            const std::uint32_t l_Alignment = l_Header.Alignment;
            const bool l_Scoped = l_Header.Scoped;
            {
                std::unique_lock l_Lock(GetScopedMutex(), std::defer_lock);
                if (l_Scoped)
                {
                    ScopedLink* l_Link = reinterpret_cast<ScopedLink*>(l_Memory - sizeof(AllocationHeader) - sizeof(ScopedLink));

                    l_Lock.lock();
                    (l_Link->Previous != nullptr ? l_Link->Previous->Next : s_ScopedAllocations) = l_Link->Next;
                    if (l_Link->Next != nullptr)
                    {
                        l_Link->Next->Previous = l_Link->Previous;
                    }
                }

                RecordFree(s_TagCounters[static_cast<std::size_t>(l_Header.Tag)], l_Header.Size);
                RecordFree(s_TotalCounters, l_Header.Size);
            }

            Platform::Free(l_Memory - GetHeaderOffset(l_Alignment, l_Scoped));
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

    MemoryTagScope::MemoryTagScope(MemoryTag tag) : m_Previous(std::exchange(t_ScopeTag, tag))
    {
        TR_CORE_ASSERT(tag < MemoryTag::Count, "Invalid memory tag.");
    }

    MemoryTagScope::~MemoryTagScope()
    {
        t_ScopeTag = m_Previous;
    }

    namespace Memory
    {
        void Initialize()
        {
            TR_CORE_ASSERT(!s_Initialized, "Memory is already initialized.");

            s_Initialized = true;

            TR_CORE_INFO("Global allocation tracking is {}", IsTrackingGlobalAllocations() ? "on" : "off");
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

            void* l_Memory = AllocateTagged(size, tag, alignment);
            if (l_Memory == nullptr)
            {
                TR_CORE_CRITICAL("Out of memory: {} requested under tag '{}'", FormatBytes(size), ToString(tag));

                throw std::bad_alloc();
            }

            return l_Memory;
        }

        void Free(void* memory)
        {
            FreeTagged(memory);
        }

        void* AllocateGlobal(std::size_t size, std::size_t alignment) noexcept
        {
            const MemoryTag l_Tag = t_ScopeTag;

            return AllocateTagged(size, l_Tag, alignment, l_Tag != MemoryTag::Untagged);
        }

        // The tag's peak stays as it was, since the memory was counted under it while it was used
        MemoryTagStats UntagScopedAllocations(MemoryTag tag)
        {
            TR_CORE_ASSERT(tag < MemoryTag::Count, "Invalid memory tag.");

            MemoryTagStats l_Moved;
            if (tag == MemoryTag::Untagged)
            {
                return l_Moved;
            }

            std::scoped_lock l_Lock(GetScopedMutex());
            for (ScopedLink* it_Link = s_ScopedAllocations; it_Link != nullptr; it_Link = it_Link->Next)
            {
                AllocationHeader& l_Header = GetHeader(it_Link);
                if (l_Header.Tag != tag)
                {
                    continue;
                }

                RecordFree(s_TagCounters[static_cast<std::size_t>(tag)], l_Header.Size);
                AddLive(s_TagCounters[static_cast<std::size_t>(MemoryTag::Untagged)], l_Header.Size);
                l_Header.Tag = MemoryTag::Untagged;
                l_Moved.CurrentBytes += l_Header.Size;
                ++l_Moved.LiveAllocations;
            }

            return l_Moved;
        }

        void* TryAllocate(std::size_t size, MemoryTag tag, std::size_t alignment) noexcept
        {
            TR_CORE_ASSERT(std::has_single_bit(alignment), "Alignment must be a power of two.");
            TR_CORE_ASSERT(tag < MemoryTag::Count, "Invalid memory tag.");

            return AllocateTagged(size, tag, alignment);
        }

        void* Reallocate(void* memory, std::size_t size, MemoryTag tag, std::size_t alignment) noexcept
        {
            if (memory == nullptr)
            {
                return TryAllocate(size, tag, alignment);
            }

            if (size == 0)
            {
                FreeTagged(memory);

                return nullptr;
            }

            const AllocationHeader l_Header = *std::launder(reinterpret_cast<AllocationHeader*>(static_cast<std::byte*>(memory) - sizeof(AllocationHeader)));

            void* l_Memory = TryAllocate(size, tag, alignment);
            if (l_Memory == nullptr)
            {
                return nullptr;
            }

            std::memcpy(l_Memory, memory, static_cast<std::size_t>(std::min<std::uint64_t>(l_Header.Size, size)));
            FreeTagged(memory);

            return l_Memory;
        }

        bool IsTrackingGlobalAllocations()
        {
#if defined(TR_TRACK_GLOBAL_ALLOCATIONS)
            return true;
#else
            return false;
#endif
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