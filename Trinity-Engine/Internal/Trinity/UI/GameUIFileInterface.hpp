#pragma once

#include <RmlUi/Core/FileInterface.h>

namespace Trinity
{
    // Reads through the virtual file system. Stylesheets and documents are read whole, others by range
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