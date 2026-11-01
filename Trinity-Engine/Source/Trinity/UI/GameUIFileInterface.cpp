#include "Trinity/UI/GameUIFileInterface.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/UI/UIAssets.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace Trinity
{
    namespace
    {
        // A stylesheet or document whose @font-face sources were resolved is read from its text, and any other file through its handle
        struct OpenFile
        {
            Ref<FileReadHandle> Handle;
            Rml::String Text;
            std::uint64_t Position = 0;

            [[nodiscard]] std::uint64_t GetSize() const { return Handle ? Handle->GetSize() : Text.size(); }
        };

        OpenFile& GetFile(Rml::FileHandle file)
        {
            return *reinterpret_cast<OpenFile*>(file);
        }

        // Documents name each other, their stylesheets, fonts and images by their paths in the project, which a cooked game's asset table finds where its pack keeps them. Anything else, as the engine's own fonts, is read where it is named
        const std::string& GetReadPath(const Rml::String& path)
        {
            const AssetRegistry* l_Registry = AssetManager::GetRegistry();
            const AssetRecord* l_Record = l_Registry != nullptr ? l_Registry->FindByPath(path) : nullptr;

            return l_Record != nullptr ? l_Record->GetReadPath() : path;
        }

        bool HasFontFaces(const Rml::String& path)
        {
            const auto a_EndsWith = [&path](std::string_view extension) { return path.size() >= extension.size() && std::ranges::equal(std::string_view(path).substr(path.size() - extension.size()), extension, [](char left, char right) { return (left >= 'A' && left <= 'Z' ? left - 'A' + 'a' : left) == right; }); };

            return a_EndsWith(".rcss") || a_EndsWith(".rml");
        }
    }

    // A file that cannot be read is 0, which RmlUi reports with its path. A stylesheet, or a document with its style blocks, is read whole, its @font-face sources resolved from the path RmlUi named it by
    Rml::FileHandle GameUIFileInterface::Open(const Rml::String& path)
    {
        if (HasFontFaces(path))
        {
            Expected<std::string, FileError> l_Text = FileSystem::ReadText(GetReadPath(path));
            if (!l_Text)
            {
                return 0;
            }

            OpenFile* l_File = Memory::New<OpenFile>(MemoryTag::GameUI);
            l_File->Text = UIAssets::ResolveFontFaceSources(*l_Text, path);

            return reinterpret_cast<Rml::FileHandle>(l_File);
        }

        Expected<Ref<FileReadHandle>, FileError> l_Handle = FileSystem::OpenFile(GetReadPath(path));
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
        if (!l_File.Handle)
        {
            if (l_File.Position >= l_File.Text.size())
            {
                return 0;
            }

            const std::size_t l_Read = static_cast<std::size_t>(std::min<std::uint64_t>(size, l_File.Text.size() - l_File.Position));
            std::memcpy(buffer, l_File.Text.data() + l_File.Position, l_Read);
            l_File.Position += l_Read;

            return l_Read;
        }

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
            l_Base = static_cast<std::int64_t>(l_File.GetSize());
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
        return static_cast<size_t>(GetFile(file).GetSize());
    }

    bool GameUIFileInterface::LoadFile(const Rml::String& path, Rml::String& data)
    {
        const Expected<FileBuffer, FileError> l_File = FileSystem::ReadFile(GetReadPath(path));
        if (!l_File)
        {
            return false;
        }

        data.assign(reinterpret_cast<const char*>(l_File->data()), l_File->size());
        if (HasFontFaces(path))
        {
            data = UIAssets::ResolveFontFaceSources(data, path);
        }

        return true;
    }
}