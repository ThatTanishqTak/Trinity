#include "Trinity/FileSystem/PackSource.hpp"

#include "Trinity/Core/Hash.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"
#include "Trinity/FileSystem/MemoryReadHandle.hpp"

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

namespace Trinity
{
    // The pack's open file, shared by its source and every read handle on a stored file. The stream has one position, so each read holds the lock from its seek to its read
    class PackFile
    {
    public:
        PackFile(std::ifstream stream, std::uint64_t size) : m_Stream(std::move(stream)), m_Size(size)
        {

        }

        [[nodiscard]] std::uint64_t GetSize() const
        {
            return m_Size;
        }

        // Fills destination from offset, which the caller keeps inside the pack
        [[nodiscard]] bool ReadAt(std::uint64_t offset, std::span<std::byte> destination) const
        {
            TR_PROFILE_SCOPE("PackFile::ReadAt");

            if (destination.empty())
            {
                return true;
            }

            std::scoped_lock l_Lock(m_Mutex);
            m_Stream.clear();
            m_Stream.seekg(static_cast<std::streamoff>(offset));
            m_Stream.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));

            return m_Stream.gcount() == static_cast<std::streamsize>(destination.size());
        }

    private:
        mutable std::mutex m_Mutex;
        mutable std::ifstream m_Stream;
        std::uint64_t m_Size = 0;
    };

    namespace
    {
        // A stored file, read in ranges straight from the pack
        class PackReadHandle final : public FileReadHandle
        {
        public:
            PackReadHandle(Ref<const PackFile> file, std::uint64_t offset, std::uint64_t size) : m_File(std::move(file)), m_Offset(offset), m_Size(size)
            {

            }

            [[nodiscard]] std::uint64_t GetSize() const override
            {
                return m_Size;
            }

            [[nodiscard]] Expected<std::size_t, FileError> Read(std::uint64_t offset, std::span<std::byte> destination) const override
            {
                if (offset >= m_Size || destination.empty())
                {
                    return std::size_t{ 0 };
                }

                const std::size_t l_Count = static_cast<std::size_t>(std::min<std::uint64_t>(destination.size(), m_Size - offset));
                if (!m_File->ReadAt(m_Offset + offset, destination.first(l_Count)))
                {
                    return Unexpected{ FileError::ReadFailed };
                }

                return l_Count;
            }

        private:
            Ref<const PackFile> m_File;
            std::uint64_t m_Offset = 0;
            std::uint64_t m_Size = 0;
        };

        // What zstd allocates to decompress is counted under the file system's tag
        void* AllocateZstd([[maybe_unused]] void* opaque, std::size_t size)
        {
            return Memory::TryAllocate(size, MemoryTag::FileSystem);
        }

        void FreeZstd([[maybe_unused]] void* opaque, void* memory)
        {
            Memory::Free(memory);
        }

        constexpr ZSTD_customMem c_ZstdMemory{ &AllocateZstd, &FreeZstd, nullptr };

        struct DecompressionContextDeleter
        {
            void operator()(ZSTD_DCtx* context) const
            {
                ZSTD_freeDCtx(context);
            }
        };
    }

    PackSource::~PackSource() = default;

    Expected<Scope<PackSource>, std::string> PackSource::Load(const std::filesystem::path& nativePath)
    {
        TR_PROFILE_FUNCTION();

        std::ifstream l_Stream(nativePath, std::ios::binary | std::ios::ate);
        if (!l_Stream)
        {
            return Unexpected{ std::string("it could not be opened") };
        }

        const std::streamoff l_End = l_Stream.tellg();
        if (l_End < 0)
        {
            return Unexpected{ std::string("it could not be read") };
        }

        const std::uint64_t l_Size = static_cast<std::uint64_t>(l_End);
        const Ref<PackFile> l_File = std::allocate_shared<PackFile>(TaggedAllocator<PackFile, MemoryTag::FileSystem>(), std::move(l_Stream), l_Size);

        PackHeader l_Header{};
        if (l_Size < sizeof(PackHeader) || !l_File->ReadAt(0, std::as_writable_bytes(std::span(&l_Header, 1))))
        {
            return Unexpected{ std::string("it is too short to be a pack") };
        }

        if (l_Header.Magic != PackHeader::c_Magic || l_Header.Version == 0)
        {
            return Unexpected{ std::string("it is not a pack") };
        }

        if (l_Header.Version > PackHeader::c_Version)
        {
            return Unexpected{ std::format("format {} was written by a newer Trinity, which reads up to {}", l_Header.Version, PackHeader::c_Version) };
        }

        if (l_Header.PackSize != l_Size)
        {
            return Unexpected{ std::format("it is {} bytes long where it was written {} long, so it was cut short or changed", l_Size, l_Header.PackSize) };
        }

        const std::uint64_t l_EntriesSize = std::uint64_t{ l_Header.FileCount } * sizeof(PackEntry);
        if (l_Header.TableOffset < sizeof(PackHeader) || l_Header.TableOffset > l_Size || l_Header.TableSize > l_Size - l_Header.TableOffset || l_Header.TableSize < l_EntriesSize)
        {
            return Unexpected{ std::string("its table is corrupted") };
        }

        Scope<PackSource> l_Source(new PackSource());
        l_Source->m_NativePath = nativePath;
        l_Source->m_File = l_File;
        l_Source->m_Table.resize(static_cast<std::size_t>(l_Header.TableSize));
        if (!l_File->ReadAt(l_Header.TableOffset, l_Source->m_Table))
        {
            return Unexpected{ std::string("its table could not be read") };
        }

        if (Hash::XXHash64(l_Source->m_Table) != l_Header.TableHash)
        {
            return Unexpected{ std::string("its table is corrupted") };
        }

        const std::byte* l_Entries = l_Source->m_Table.data();
        const std::string_view l_Paths(reinterpret_cast<const char*>(l_Entries) + l_EntriesSize, l_Source->m_Table.size() - static_cast<std::size_t>(l_EntriesSize));

        // The hash matched, so a bad entry can only come from a writer with a bug, and is refused rather than read outside the pack
        l_Source->m_Entries.reserve(l_Header.FileCount);
        for (std::size_t it_Index = 0; it_Index < l_Header.FileCount; ++it_Index)
        {
            Entry l_Entry;
            std::memcpy(&l_Entry.File, l_Entries + it_Index * sizeof(PackEntry), sizeof(PackEntry));

            const PackEntry& l_Data = l_Entry.File;
            const bool l_PathFits = l_Data.PathSize > 0 && l_Data.PathOffset <= l_Paths.size() && l_Data.PathSize <= l_Paths.size() - l_Data.PathOffset;
            const bool l_BytesFit = l_Data.Offset >= sizeof(PackHeader) && l_Data.Offset <= l_Header.TableOffset && l_Data.StoredSize <= l_Header.TableOffset - l_Data.Offset;
            const bool l_Readable = l_Data.Compression == PackCompression::Zstd || (l_Data.Compression == PackCompression::None && l_Data.StoredSize == l_Data.Size);
            if (l_PathFits)
            {
                l_Entry.Path = l_Paths.substr(l_Data.PathOffset, l_Data.PathSize);
            }

            if (!l_PathFits || !l_BytesFit || !l_Readable || (!l_Source->m_Entries.empty() && l_Source->m_Entries.back().Path >= l_Entry.Path))
            {
                return Unexpected{ std::format("its table's entry {} is malformed", it_Index) };
            }

            l_Source->m_Entries.push_back(l_Entry);
        }

        return l_Source;
    }

    const PackSource::Entry* PackSource::Find(std::string_view relativePath) const
    {
        const auto a_Found = std::ranges::lower_bound(m_Entries, relativePath, {}, &Entry::Path);

        return a_Found != m_Entries.end() && a_Found->Path == relativePath ? &*a_Found : nullptr;
    }

    bool PackSource::IsDirectory(std::string_view relativePath) const
    {
        if (relativePath.empty())
        {
            return true;
        }

        const std::string l_Prefix = std::string(relativePath) + "/";
        const auto a_Found = std::ranges::lower_bound(m_Entries, std::string_view(l_Prefix), {}, &Entry::Path);

        return a_Found != m_Entries.end() && a_Found->Path.starts_with(l_Prefix);
    }

    FileError PackSource::GetMissingError(std::string_view relativePath) const
    {
        const std::string l_AsDirectory = std::string(relativePath) + "/";
        for (const Entry& it_Entry : m_Entries)
        {
            if (FileSystemUtilities::EqualsIgnoreAsciiCase(it_Entry.Path, relativePath) || FileSystemUtilities::StartsWithIgnoreAsciiCase(it_Entry.Path, l_AsDirectory))
            {
                return FileError::CaseMismatch;
            }
        }

        return FileError::NotFound;
    }

    Expected<FileBuffer, FileError> PackSource::ReadWhole(const Entry& entry) const
    {
        const PackEntry& l_File = entry.File;
        if (static_cast<std::uintmax_t>(l_File.Size) > std::numeric_limits<std::size_t>::max())
        {
            return Unexpected{ FileError::TooLarge };
        }

        const auto a_Corrupted = [this, &entry](std::string_view reason)
        {
            TR_CORE_ERROR("'{}' in pack '{}' is corrupted: {}", entry.Path, FileSystemUtilities::ToUtf8(m_NativePath), reason);

            return Unexpected{ FileError::Corrupted };
        };

        FileBuffer l_Data(static_cast<std::size_t>(l_File.Size));
        if (l_File.Compression == PackCompression::None)
        {
            if (!m_File->ReadAt(l_File.Offset, l_Data))
            {
                return Unexpected{ FileError::ReadFailed };
            }
        }
        else
        {
            FileBuffer l_Stored(static_cast<std::size_t>(l_File.StoredSize));
            if (!m_File->ReadAt(l_File.Offset, l_Stored))
            {
                return Unexpected{ FileError::ReadFailed };
            }

            const std::unique_ptr<ZSTD_DCtx, DecompressionContextDeleter> l_Context(ZSTD_createDCtx_advanced(c_ZstdMemory));
            if (l_Context == nullptr)
            {
                return Unexpected{ FileError::ReadFailed };
            }

            const std::size_t l_Decompressed = ZSTD_decompressDCtx(l_Context.get(), l_Data.data(), l_Data.size(), l_Stored.data(), l_Stored.size());
            if (ZSTD_isError(l_Decompressed))
            {
                return a_Corrupted(std::format("it does not decompress ({})", ZSTD_getErrorName(l_Decompressed)));
            }

            if (l_Decompressed != l_Data.size())
            {
                return a_Corrupted(std::format("it decompresses to {} bytes where it was written {}", l_Decompressed, l_Data.size()));
            }
        }

        if (Hash::XXHash64(l_Data) != l_File.Hash)
        {
            return a_Corrupted("its bytes do not match its hash");
        }

        return l_Data;
    }

    Expected<FileBuffer, FileError> PackSource::Read(std::string_view relativePath) const
    {
        TR_PROFILE_SCOPE("PackSource::Read");

        if (const Entry* l_Entry = Find(relativePath))
        {
            return ReadWhole(*l_Entry);
        }

        return Unexpected{ IsDirectory(relativePath) ? FileError::NotAFile : GetMissingError(relativePath) };
    }

    Expected<Ref<FileReadHandle>, FileError> PackSource::Open(std::string_view relativePath) const
    {
        const Entry* l_Entry = Find(relativePath);
        if (l_Entry == nullptr)
        {
            return Unexpected{ IsDirectory(relativePath) ? FileError::NotAFile : GetMissingError(relativePath) };
        }

        if (l_Entry->File.Compression == PackCompression::None)
        {
            return Ref<FileReadHandle>(std::allocate_shared<PackReadHandle>(TaggedAllocator<PackReadHandle, MemoryTag::FileSystem>(), m_File, l_Entry->File.Offset, l_Entry->File.Size));
        }

        // A compressed file cannot be read in ranges, so it is decompressed whole, and checked, as it opens
        TR_PROFILE_SCOPE("PackSource::Open");

        Expected<FileBuffer, FileError> l_Data = ReadWhole(*l_Entry);
        if (!l_Data)
        {
            return Unexpected{ l_Data.GetError() };
        }

        std::shared_ptr<const FileBuffer> l_Shared = std::allocate_shared<FileBuffer>(TaggedAllocator<FileBuffer, MemoryTag::FileSystem>(), std::move(*l_Data));

        return Ref<FileReadHandle>(std::allocate_shared<MemoryReadHandle>(TaggedAllocator<MemoryReadHandle, MemoryTag::FileSystem>(), std::move(l_Shared)));
    }

    Expected<FileType, FileError> PackSource::Stat(std::string_view relativePath) const
    {
        if (Find(relativePath) != nullptr)
        {
            return FileType::File;
        }

        if (IsDirectory(relativePath))
        {
            return FileType::Directory;
        }

        return Unexpected{ GetMissingError(relativePath) };
    }

    Expected<std::vector<DirectoryEntry>, FileError> PackSource::List(std::string_view relativeDirectory) const
    {
        if (!IsDirectory(relativeDirectory))
        {
            return Unexpected{ Find(relativeDirectory) != nullptr ? FileError::NotFound : GetMissingError(relativeDirectory) };
        }

        const std::string l_Prefix = relativeDirectory.empty() ? std::string() : std::string(relativeDirectory) + "/";

        std::vector<DirectoryEntry> l_Entries;
        for (auto it_Entry = std::ranges::lower_bound(m_Entries, std::string_view(l_Prefix), {}, &Entry::Path); it_Entry != m_Entries.end() && it_Entry->Path.starts_with(l_Prefix); ++it_Entry)
        {
            const std::string_view l_Rest = it_Entry->Path.substr(l_Prefix.size());
            const std::size_t l_Slash = l_Rest.find('/');
            const std::string_view l_Name = l_Rest.substr(0, l_Slash);

            if (l_Entries.empty() || l_Entries.back().Name != l_Name)
            {
                l_Entries.push_back({ std::string(l_Name), l_Slash == std::string_view::npos ? FileType::File : FileType::Directory });
            }
        }

        return l_Entries;
    }

    std::string PackSource::Describe() const
    {
        return std::format("pack '{}' ({} file(s))", FileSystemUtilities::ToUtf8(m_NativePath), m_Entries.size());
    }
}