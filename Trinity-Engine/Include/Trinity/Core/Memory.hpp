#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
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
        Log,
        Frame,
        Jobs,
        FileSystem,
        Renderer,
        UI,
        Scene,
        Assets,
        Audio,
        Physics,
        Scripting,
        Game,
        Input,
        GameUI,

        Count
    };

    struct MemoryTagStats
    {
        std::uint64_t CurrentBytes = 0;
        std::uint64_t PeakBytes = 0;
        std::uint64_t LiveAllocations = 0;
        std::uint64_t TotalAllocations = 0;
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(MemoryTag tag);

    namespace Memory
    {
        TRINITY_API void Initialize();
        TRINITY_API void Shutdown();

        [[nodiscard]] TRINITY_API void* Allocate(std::size_t size, MemoryTag tag, std::size_t alignment = alignof(std::max_align_t));
        TRINITY_API void Free(void* memory);

        // What global new allocates: under the tag of the innermost MemoryTagScope on this thread, and untagged outside one
        [[nodiscard]] TRINITY_API void* AllocateGlobal(std::size_t size, std::size_t alignment) noexcept;
        // What global new allocated under a MemoryTagScope of the tag, and is still live, is counted as untagged from now on, as a library's statics are, which it keeps past its own shutdown until the process ends. What was moved, by bytes and allocations
        TRINITY_API MemoryTagStats UntagScopedAllocations(MemoryTag tag);
        [[nodiscard]] TRINITY_API void* TryAllocate(std::size_t size, MemoryTag tag, std::size_t alignment = alignof(std::max_align_t)) noexcept;
        [[nodiscard]] TRINITY_API void* Reallocate(void* memory, std::size_t size, MemoryTag tag, std::size_t alignment = alignof(std::max_align_t)) noexcept;

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

        [[nodiscard]] TRINITY_API bool IsTrackingGlobalAllocations();

        [[nodiscard]] TRINITY_API MemoryTagStats GetStats(MemoryTag tag);
        [[nodiscard]] TRINITY_API MemoryTagStats GetTotalStats();

        TRINITY_API void LogUsage();

        [[nodiscard]] TRINITY_API std::string FormatBytes(std::uint64_t bytes);
    }

    // While one lives, what its thread allocates through global new is counted under its tag, as a library a system calls allocates on that system's behalf. Each such allocation is linked, so Memory::UntagScopedAllocations can find what the library still holds once it has shut down. Scopes nest, each giving the one before back as it ends. Global new is only counted where allocations are tracked, and a game module's is always its own
    class TRINITY_API MemoryTagScope
    {
    public:
        explicit MemoryTagScope(MemoryTag tag);
        ~MemoryTagScope();

        MemoryTagScope(const MemoryTagScope&) = delete;
        MemoryTagScope& operator=(const MemoryTagScope&) = delete;

    private:
        MemoryTag m_Previous;
    };

    template<typename T, MemoryTag Tag>
    class TaggedAllocator
    {
    public:
        using value_type = T;

        template<typename U>
        struct rebind
        {
            using other = TaggedAllocator<U, Tag>;
        };

        TaggedAllocator() noexcept = default;

        template<typename U>
        TaggedAllocator(const TaggedAllocator<U, Tag>&) noexcept
        {

        }

        [[nodiscard]] T* allocate(std::size_t count)
        {
            if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            {
                throw std::bad_array_new_length();
            }

            return static_cast<T*>(Memory::Allocate(count * sizeof(T), Tag, alignof(T)));
        }

        void deallocate(T* memory, std::size_t) noexcept
        {
            Memory::Free(memory);
        }

        template<typename U>
        [[nodiscard]] bool operator==(const TaggedAllocator<U, Tag>&) const noexcept
        {
            return true;
        }
    };

    template<MemoryTag Tag>
    using TaggedString = std::basic_string<char, std::char_traits<char>, TaggedAllocator<char, Tag>>;
}