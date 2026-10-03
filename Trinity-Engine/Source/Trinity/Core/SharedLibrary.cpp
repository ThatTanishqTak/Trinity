#include "Trinity/Core/SharedLibrary.hpp"

#include "Trinity/Core/Platform.hpp"

#include <format>
#include <utility>

namespace Trinity
{
    SharedLibrary::SharedLibrary(void* handle, std::filesystem::path path) : m_Handle(handle), m_Path(std::move(path))
    {

    }

    SharedLibrary::~SharedLibrary()
    {
        Unload();
    }

    SharedLibrary::SharedLibrary(SharedLibrary&& other) noexcept : m_Handle(std::exchange(other.m_Handle, nullptr)), m_Path(std::move(other.m_Path))
    {

    }

    SharedLibrary& SharedLibrary::operator=(SharedLibrary&& other) noexcept
    {
        if (this != &other)
        {
            Unload();
            m_Handle = std::exchange(other.m_Handle, nullptr);
            m_Path = std::move(other.m_Path);
        }

        return *this;
    }

    Expected<SharedLibrary, std::string> SharedLibrary::Load(const std::filesystem::path& path)
    {
        const std::filesystem::path l_Path = path.is_absolute() ? path : Platform::GetExecutableDirectory() / path;

        std::string l_Error;
        void* l_Handle = Platform::LoadSharedLibrary(l_Path, l_Error);
        if (l_Handle == nullptr)
        {
            return Unexpected{ std::format("Could not load '{}': {}", l_Path.string(), l_Error) };
        }

        return SharedLibrary(l_Handle, l_Path);
    }

    void SharedLibrary::Unload()
    {
        if (m_Handle != nullptr)
        {
            Platform::UnloadSharedLibrary(m_Handle);
            m_Handle = nullptr;
        }
    }

    void* SharedLibrary::GetSymbol(const char* name) const
    {
        return m_Handle != nullptr ? Platform::GetSharedLibrarySymbol(m_Handle, name) : nullptr;
    }
}