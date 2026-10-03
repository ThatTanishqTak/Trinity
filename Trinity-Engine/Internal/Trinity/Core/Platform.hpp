#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace Trinity
{
    namespace Platform
    {
        void Initialize();
        void Shutdown();

        [[nodiscard]] const char* GetName();

        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment);
        void Free(void* memory);

        [[nodiscard]] std::filesystem::path GetUserDataDirectory();
        [[nodiscard]] std::filesystem::path GetExecutableDirectory();

        // Null on failure, with the reason in error
        [[nodiscard]] void* LoadSharedLibrary(const std::filesystem::path& path, std::string& error);
        [[nodiscard]] void* GetSharedLibrarySymbol(void* library, const char* name);
        void UnloadSharedLibrary(void* library);
    }
}