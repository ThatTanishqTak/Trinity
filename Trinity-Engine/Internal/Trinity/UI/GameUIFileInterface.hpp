#pragma once

#include <RmlUi/Core/FileInterface.h>

namespace Trinity
{
    // RmlUi's files, read through the virtual file system, so documents, stylesheets, fonts and images come from a project's folders in Forge and from the pack in a player alike. Each opened file is a handle for ranged reads, so a large font is read once, as RmlUi asks for it
    class GameUIFileInterface final : public Rml::FileInterface
    {
    public:
        Rml::FileHandle Open(const Rml::String& path) override;
        void Close(Rml::FileHandle file) override;
        size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
        bool Seek(Rml::FileHandle file, long offset, int origin) override;
        size_t Tell(Rml::FileHandle file) override;
        size_t Length(Rml::FileHandle file) override;
        bool LoadFile(const Rml::String& path, Rml::String& data) override;
    };
}