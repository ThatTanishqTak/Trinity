#include "Trinity/Core/Memory.hpp"

#include <cstddef>
#include <new>

namespace
{
    // A game module compiles this file with TR_GLOBAL_ALLOCATION_TAG=Game, so what its code allocates is counted as the module's own. Everything else's is untagged
    void* TryAllocateGlobal(std::size_t size, std::size_t alignment) noexcept
    {
#if defined(TR_GLOBAL_ALLOCATION_TAG)
        return Trinity::Memory::TryAllocate(size, Trinity::MemoryTag::TR_GLOBAL_ALLOCATION_TAG, alignment);
#else
        return Trinity::Memory::AllocateUntagged(size, alignment);
#endif
    }

    void* AllocateGlobal(std::size_t size, std::size_t alignment)
    {
        while (true)
        {
            if (void* l_Memory = TryAllocateGlobal(size, alignment))
            {
                return l_Memory;
            }

            const std::new_handler l_Handler = std::get_new_handler();
            if (l_Handler == nullptr)
            {
                throw std::bad_alloc();
            }

            l_Handler();
        }
    }

    void* AllocateGlobalNoThrow(std::size_t size, std::size_t alignment) noexcept
    {
        try
        {
            return AllocateGlobal(size, alignment);
        }
        catch (...)
        {
            return nullptr;
        }
    }
}

void* operator new(std::size_t size) { return AllocateGlobal(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__); }
void* operator new[](std::size_t size) { return AllocateGlobal(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__); }
void* operator new(std::size_t size, std::align_val_t alignment) { return AllocateGlobal(size, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return AllocateGlobal(size, static_cast<std::size_t>(alignment)); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return AllocateGlobalNoThrow(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return AllocateGlobalNoThrow(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return AllocateGlobalNoThrow(size, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return AllocateGlobalNoThrow(size, static_cast<std::size_t>(alignment)); }

void operator delete(void* memory) noexcept { Trinity::Memory::Free(memory); }
void operator delete[](void* memory) noexcept { Trinity::Memory::Free(memory); }
void operator delete(void* memory, std::size_t) noexcept { Trinity::Memory::Free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { Trinity::Memory::Free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { Trinity::Memory::Free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { Trinity::Memory::Free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { Trinity::Memory::Free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { Trinity::Memory::Free(memory); }
void operator delete(void* memory, const std::nothrow_t&) noexcept { Trinity::Memory::Free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { Trinity::Memory::Free(memory); }
void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept { Trinity::Memory::Free(memory); }
void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept { Trinity::Memory::Free(memory); }