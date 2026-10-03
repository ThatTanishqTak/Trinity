#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <filesystem>
#include <string>
#include <type_traits>

namespace Trinity
{
    // A library loaded at runtime, such as a game module, unloaded when destroyed. Load and unload on the main thread
    class TRINITY_API SharedLibrary
    {
    public:
        SharedLibrary() = default;
        ~SharedLibrary();

        SharedLibrary(SharedLibrary&& other) noexcept;
        SharedLibrary& operator=(SharedLibrary&& other) noexcept;

        SharedLibrary(const SharedLibrary&) = delete;
        SharedLibrary& operator=(const SharedLibrary&) = delete;

        // A relative path starts at the executable's folder, where Trinity builds its modules
        [[nodiscard]] static Expected<SharedLibrary, std::string> Load(const std::filesystem::path& path);

        void Unload();

        [[nodiscard]] bool IsLoaded() const { return m_Handle != nullptr; }
        [[nodiscard]] const std::filesystem::path& GetPath() const { return m_Path; }

        [[nodiscard]] void* GetSymbol(const char* name) const;

        template<typename T>
        [[nodiscard]] T GetFunction(const char* name) const
        {
            static_assert(std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>, "GetFunction takes a function pointer type.");

            return reinterpret_cast<T>(GetSymbol(name));
        }

    private:
        SharedLibrary(void* handle, std::filesystem::path path);

        void* m_Handle = nullptr;
        std::filesystem::path m_Path;
    };
}