#pragma once

#include <cstddef>

namespace Trinity
{
    namespace Platform
    {
        void Initialize();
        void Shutdown();

        [[nodiscard]] const char* GetName();

        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment);
        void Free(void* memory);
    }
}