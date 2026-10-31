#include "Trinity/UI/GameUIFileInterface.hpp"

#include "Trinity/Core/Memory.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"

#include <cstdint>
#include <cstdio>
#include <span>

namespace Trinity
{
    namespace
    {
        struct OpenFile
        {
            Ref<FileReadHandle> Handle;
            std::uint64_t Position = 0;
        };

        OpenFile& GetFile(Rml::FileHandle file)
        {
            return *reinterpret_cast<OpenFile*>(file);
        }
    }

    // A file that cannot be read is 0, which RmlUi reports with its path
    Rml::FileHandle GameUIFileInterface::Open(const Rml::String& path)
    {
        Expected<Ref<FileReadHandle>, FileError> l_Handle = FileSystem::OpenFile(path);
        if (!l_Handle)
        {
            return 0;
        }

        return reinterpret_cast<Rml::FileHandle>(Memory::New<OpenFile>(MemoryTag::GameUI, std::move(*l_Handle)));
    }

    void GameUIFileInterface::Close(Rml::FileHandle file)
    {
        Memory::Delete(&GetFile(file));
    }

    size_t GameUIFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file)
    {
        OpenFile& l_File = GetFile(file);
        const Expected<std::size_t, FileError> l_Read = l_File.Handle->Read(l_File.Position, std::span(static_cast<std::byte*>(buffer), size));
        if (!l_Read)
        {
            return 0;
        }

        l_File.Position += *l_Read;

        return *l_Read;
    }

    // As fseek does, a place past the end is kept, where a read gives nothing, and one before the start is refused
    bool GameUIFileInterface::Seek(Rml::FileHandle file, long offset, int origin)
    {
        OpenFile& l_File = GetFile(file);
        std::int64_t l_Base = 0;
        if (origin == SEEK_CUR)
        {
            l_Base = static_cast<std::int64_t>(l_File.Position);
        }
        else if (origin == SEEK_END)
        {
            l_Base = static_cast<std::int64_t>(l_File.Handle->GetSize());
        }

        const std::int64_t l_Position = l_Base + offset;
        if (l_Position < 0)
        {
            return false;
        }

        l_File.Position = static_cast<std::uint64_t>(l_Position);

        return true;
    }

    size_t GameUIFileInterface::Tell(Rml::FileHandle file)
    {
        return static_cast<size_t>(GetFile(file).Position);
    }

    size_t GameUIFileInterface::Length(Rml::FileHandle file)
    {
        return static_cast<size_t>(GetFile(file).Handle->GetSize());
    }

    bool GameUIFileInterface::LoadFile(const Rml::String& path, Rml::String& data)
    {
        const Expected<FileBuffer, FileError> l_File = FileSystem::ReadFile(path);
        if (!l_File)
        {
            return false;
        }

        data.assign(reinterpret_cast<const char*>(l_File->data()), l_File->size());

        return true;
    }
}