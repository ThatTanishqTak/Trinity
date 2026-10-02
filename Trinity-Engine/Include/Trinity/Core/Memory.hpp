#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Trinity
{
    enum class MemoryTag : std::uint8_t
    {
        Untagged = 0,
        Engine,
        Jobs,
        FileSystem,
        Renderer,
        Scene,
        Assets,
        Audio,
        Physics,
        Scripting,
        Game,

        Count
    };

    struct MemoryTagStats
    {
        std::uint64_t CurrentBytes = 0;
        std::uint64_t PeakBytes = 0;
        std::uint64_t LiveAllocations = 0;
        std::uint64_t TotalAllocations = 0;
    };

    [[nodiscard]] std::string_view ToString(MemoryTag tag);

    namespace Memory
    {
        void Initialize();
        void Shutdown();

        [[nodiscard]] void* Allocate(std::size_t size, MemoryTag tag, std::size_t alignment = alignof(std::max_align_t));
        void Free(void* memory);

        template<typename T, typename... Args>
        [[nodiscard]] T* New(MemoryTag tag, Args&&... args)
        {
            void* l_Memory = Allocate(sizeof(T), tag, alignof(T));
            try
            {
                return ::new (l_Memory) T(std::forward<Args>(args)...);
            }
            catch (...)
            {
                Free(l_Memory);
                throw;
            }
        }

        template<typename T>
        void Delete(T* object)
        {
            if (object == nullptr)
            {
                return;
            }

            void* l_Memory = nullptr;
            if constexpr (std::is_polymorphic_v<T>)
            {
                l_Memory = dynamic_cast<void*>(object);
            }
            else
            {
                l_Memory = object;
            }

            object->~T();
            Free(l_Memory);
        }

        [[nodiscard]] bool IsTrackingGlobalAllocations();

        [[nodiscard]] MemoryTagStats GetStats(MemoryTag tag);
        [[nodiscard]] MemoryTagStats GetTotalStats();

        void LogUsage();

        [[nodiscard]] std::string FormatBytes(std::uint64_t bytes);
    }
}