#include "Export/PackBuilder.hpp"

#include <zstd.h>

#include <array>
#include <cstring>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <set>
#include <system_error>
#include <utility>

namespace
{
    using Bytes = std::vector<std::byte>;

    struct CompressionContextDeleter
    {
        void operator()(ZSTD_CCtx* context) const
        {
            ZSTD_freeCCtx(context);
        }
    };

    struct DecompressionContextDeleter
    {
        void operator()(ZSTD_DCtx* context) const
        {
            ZSTD_freeDCtx(context);
        }
    };

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    Trinity::Expected<Bytes, std::string> ReadNativeFile(const std::filesystem::path& path)
    {
        std::ifstream l_File(path, std::ios::binary | std::ios::ate);
        if (!l_File)
        {
            return Trinity::Unexpected{ std::string("it could not be opened") };
        }

        const std::streamoff l_Size = l_File.tellg();
        if (l_Size < 0)
        {
            return Trinity::Unexpected{ std::string("it could not be read") };
        }

        Bytes l_Bytes(static_cast<std::size_t>(l_Size));
        l_File.seekg(0);
        l_File.read(reinterpret_cast<char*>(l_Bytes.data()), static_cast<std::streamsize>(l_Size));
        if (l_File.gcount() != static_cast<std::streamsize>(l_Size))
        {
            return Trinity::Unexpected{ std::string("it could not be read") };
        }

        return l_Bytes;
    }

    void WriteBytes(std::ofstream& stream, std::span<const std::byte> bytes)
    {
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // The cache's folder for this zstd at this level, since another version or level compresses into other bytes
    std::filesystem::path GetCacheFolder(const std::filesystem::path & cacheDirectory)
    {
        return cacheDirectory / std::format("zstd-{}-{}", ZSTD_versionString(), PackBuilder::c_CompressionLevel);
    }

    // A file's entry, by its content's hash and size: its compressed bytes, or an empty file saying it is stored as it is, since compressing it saves too little
    std::filesystem::path GetCachePath(const std::filesystem::path & folder, std::uint64_t hash, std::uint64_t size, bool stored)
    {
        return folder / std::format("{:016x}-{:x}.{}", hash, size, stored ? "stored" : "zst");
    }

    // Written beside its place and renamed into it, so an entry is whole or missing. One that cannot be written is left out, and the file is compressed again next time
    void WriteCacheEntry(const std::filesystem::path & path, std::span<const std::byte> bytes)
    {
        std::filesystem::path l_Temporary = path;
        l_Temporary += std::format(".{}.tmp", Trinity::UUID::Generate());
        std::error_code l_Error;
        {
            std::ofstream l_Stream(l_Temporary, std::ios::binary | std::ios::trunc);
            WriteBytes(l_Stream, bytes);
            l_Stream.flush();
            if (!l_Stream)
            {
                l_Stream.close();
                std::filesystem::remove(l_Temporary, l_Error);

                return;
            }
        }

        std::filesystem::rename(l_Temporary, path, l_Error);
        if (l_Error)
        {
            std::filesystem::remove(l_Temporary, l_Error);
        }
    }

    // Every entry this write did not use goes, with the folders of other zstd versions and levels
    void PruneCache(const std::filesystem::path & cacheDirectory, const std::vector<std::filesystem::path>&used)
    {
        const std::filesystem::path l_Folder = GetCacheFolder(cacheDirectory);
        const std::set<std::filesystem::path> l_Used(used.begin(), used.end());
        std::error_code l_Error;
        for (std::filesystem::directory_iterator it_Entry(cacheDirectory, l_Error), it_End; !l_Error && it_Entry != it_End; it_Entry.increment(l_Error))
        {
            std::error_code l_Ignored;
            if (it_Entry->path() != l_Folder)
            {
                std::filesystem::remove_all(it_Entry->path(), l_Ignored);
            }
        }

        for (std::filesystem::directory_iterator it_Entry(l_Folder, l_Error), it_End; !l_Error && it_Entry != it_End; it_Entry.increment(l_Error))
        {
            std::error_code l_Ignored;
            if (!l_Used.contains(it_Entry->path()))
            {
                std::filesystem::remove_all(it_Entry->path(), l_Ignored);
            }
        }
    }
}

std::optional<std::string> PackBuilder::MakeKey(std::string_view packPath) const
{
    const std::optional<std::string> l_Normalized = Trinity::FileSystem::NormalizePath(std::string("/").append(packPath));
    if (!l_Normalized || *l_Normalized == "/")
    {
        return std::nullopt;
    }

    std::string l_Key = l_Normalized->substr(1);
    if (m_Files.contains(l_Key))
    {
        return std::nullopt;
    }

    // A file cannot also be a folder: nothing may sit below the new path, and none of the folders above it may be a file
    const std::string l_AsDirectory = l_Key + "/";
    if (const auto a_Below = m_Files.lower_bound(l_AsDirectory); a_Below != m_Files.end() && a_Below->first.starts_with(l_AsDirectory))
    {
        return std::nullopt;
    }

    for (std::size_t l_Slash = l_Key.find('/'); l_Slash != std::string::npos; l_Slash = l_Key.find('/', l_Slash + 1))
    {
        if (m_Files.contains(std::string_view(l_Key).substr(0, l_Slash)))
        {
            return std::nullopt;
        }
    }

    return l_Key;
}

bool PackBuilder::AddFile(std::string_view packPath, const std::filesystem::path& nativePath)
{
    std::optional<std::string> l_Key = MakeKey(packPath);
    if (!l_Key)
    {
        return false;
    }

    m_Files.emplace(std::move(*l_Key), nativePath);

    return true;
}

bool PackBuilder::AddData(std::string_view packPath, std::span<const std::byte> data)
{
    std::optional<std::string> l_Key = MakeKey(packPath);
    if (!l_Key)
    {
        return false;
    }

    m_Files.emplace(std::move(*l_Key), Bytes(data.begin(), data.end()));

    return true;
}

Trinity::Expected<PackSummary, std::string> PackBuilder::Write(const std::filesystem::path& output, const PackWriteOptions& options) const
{
    TR_PROFILE_FUNCTION();

    std::error_code l_Error;
    if (output.has_parent_path())
    {
        std::filesystem::create_directories(output.parent_path(), l_Error);
        if (l_Error)
        {
            return Trinity::Unexpected{ std::format("its folder could not be made: {}", l_Error.message()) };
        }
    }

    std::filesystem::path l_Temporary = output;
    l_Temporary += std::format(".{}.tmp", Trinity::UUID::Generate());

    std::vector<std::filesystem::path> l_CacheFiles;
    Trinity::Expected<PackSummary, std::string> l_Summary = WriteTo(l_Temporary, options, l_CacheFiles);
    if (l_Summary)
    {
        std::filesystem::rename(l_Temporary, output, l_Error);
        if (!l_Error)
        {
            if (!options.CacheDirectory.empty())
            {
                PruneCache(options.CacheDirectory, l_CacheFiles);
            }

            return l_Summary;
        }

        l_Summary = Trinity::Unexpected{ std::format("it could not replace '{}': {}", ToUtf8(output), l_Error.message()) };
    }

    std::error_code l_Ignored;
    std::filesystem::remove(l_Temporary, l_Ignored);

    return l_Summary;
}

Trinity::Expected<PackSummary, std::string> PackBuilder::WriteTo(const std::filesystem::path& path, const PackWriteOptions& options, std::vector<std::filesystem::path>& cacheFiles) const
{
    if (m_Files.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return Trinity::Unexpected{ std::string("it holds more files than a pack can") };
    }

    std::ofstream l_Stream(path, std::ios::binary | std::ios::trunc);
    if (!l_Stream)
    {
        return Trinity::Unexpected{ std::format("'{}' could not be created", ToUtf8(path)) };
    }

    const std::unique_ptr<ZSTD_CCtx, CompressionContextDeleter> l_Context(ZSTD_createCCtx());
    const std::unique_ptr<ZSTD_DCtx, DecompressionContextDeleter> l_Checker(ZSTD_createDCtx());
    if (l_Context == nullptr || l_Checker == nullptr)
    {
        return Trinity::Unexpected{ std::string("zstd could not start") };
    }

    // A cache that cannot be made is not used
    std::filesystem::path l_CacheFolder;
    if (!options.CacheDirectory.empty())
    {
        std::error_code l_Error;
        l_CacheFolder = GetCacheFolder(options.CacheDirectory);
        std::filesystem::create_directories(l_CacheFolder, l_Error);
        if (l_Error)
        {
            l_CacheFolder.clear();
        }
    }

    // The header goes in last, once the table's place and hash are known
    constexpr std::array<std::byte, Trinity::PackLayout::c_PageSize> c_Zeros{};
    WriteBytes(l_Stream, std::span(c_Zeros).first(sizeof(Trinity::PackHeader)));
    std::uint64_t l_Offset = sizeof(Trinity::PackHeader);

    std::vector<Trinity::PackEntry> l_Entries;
    l_Entries.reserve(m_Files.size());
    std::string l_Paths;
    PackSummary l_Summary;
    Bytes l_Read;
    Bytes l_Compressed;
    Bytes l_Checked;

    for (const auto& [it_Path, it_Source] : m_Files)
    {
        if (options.Cancel != nullptr && options.Cancel->load(std::memory_order_relaxed))
        {
            return Trinity::Unexpected{ std::string("it was cancelled") };
        }

        std::span<const std::byte> l_Bytes;
        if (const std::filesystem::path* l_NativePath = std::get_if<std::filesystem::path>(&it_Source))
        {
            Trinity::Expected<Bytes, std::string> l_File = ReadNativeFile(*l_NativePath);
            if (!l_File)
            {
                return Trinity::Unexpected{ std::format("'{}' could not be packed from '{}': {}", it_Path, ToUtf8(*l_NativePath), l_File.GetError()) };
            }

            l_Read = std::move(*l_File);
            l_Bytes = l_Read;
        }
        else
        {
            l_Bytes = std::get<Bytes>(it_Source);
        }

        if (l_Paths.size() + it_Path.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return Trinity::Unexpected{ std::string("its paths are longer than a pack can hold") };
        }

        Trinity::PackEntry l_Entry;
        l_Entry.Size = l_Bytes.size();
        l_Entry.Hash = Trinity::Hash::XXHash64(l_Bytes);
        l_Entry.PathOffset = static_cast<std::uint32_t>(l_Paths.size());
        l_Entry.PathSize = static_cast<std::uint32_t>(it_Path.size());
        l_Paths += it_Path;

        // From the cache when it holds the file's content, an entry being checked by decompressing it, since a pack made from a damaged one would be damaged too. Otherwise compressed, and kept there
        std::span<const std::byte> l_Stored = l_Bytes;
        if (!l_Bytes.empty())
        {
            std::error_code l_Error;
            const std::filesystem::path l_StoredEntry = l_CacheFolder.empty() ? std::filesystem::path() : GetCachePath(l_CacheFolder, l_Entry.Hash, l_Entry.Size, true);
            const std::filesystem::path l_CompressedEntry = l_CacheFolder.empty() ? std::filesystem::path() : GetCachePath(l_CacheFolder, l_Entry.Hash, l_Entry.Size, false);
            std::optional<std::size_t> l_CompressedSize;
            bool l_Cached = false;
            if (!l_CacheFolder.empty() && std::filesystem::exists(l_StoredEntry, l_Error))
            {
                l_Cached = true;
            }
            else if (!l_CacheFolder.empty() && std::filesystem::exists(l_CompressedEntry, l_Error))
            {
                Trinity::Expected<Bytes, std::string> l_Cache = ReadNativeFile(l_CompressedEntry);
                if (l_Cache)
                {
                    l_Checked.resize(l_Bytes.size());
                    const std::size_t l_Size = ZSTD_decompressDCtx(l_Checker.get(), l_Checked.data(), l_Checked.size(), l_Cache->data(), l_Cache->size());
                    if (!ZSTD_isError(l_Size) && l_Size == l_Bytes.size() && std::memcmp(l_Checked.data(), l_Bytes.data(), l_Size) == 0)
                    {
                        l_Compressed = std::move(*l_Cache);
                        l_CompressedSize = l_Compressed.size();
                        l_Cached = true;
                    }
                }
            }

            if (!l_Cached)
            {
                l_Compressed.resize(ZSTD_compressBound(l_Bytes.size()));
                const std::size_t l_Size = ZSTD_compressCCtx(l_Context.get(), l_Compressed.data(), l_Compressed.size(), l_Bytes.data(), l_Bytes.size(), c_CompressionLevel);
                if (ZSTD_isError(l_Size))
                {
                    return Trinity::Unexpected{ std::format("'{}' could not be compressed: {}", it_Path, ZSTD_getErrorName(l_Size)) };
                }

                if (Trinity::PackLayout::IsWorthCompressing(l_Bytes.size(), l_Size))
                {
                    l_CompressedSize = l_Size;
                }

                if (!l_CacheFolder.empty())
                {
                    WriteCacheEntry(l_CompressedSize ? l_CompressedEntry : l_StoredEntry, l_CompressedSize ? std::span<const std::byte>(l_Compressed).first(l_Size) : std::span<const std::byte>());
                }
            }
            else
            {
                ++l_Summary.Cached;
            }

            if (!l_CacheFolder.empty())
            {
                cacheFiles.push_back(l_CompressedSize ? l_CompressedEntry : l_StoredEntry);
            }

            if (l_CompressedSize && Trinity::PackLayout::IsWorthCompressing(l_Bytes.size(), *l_CompressedSize))
            {
                l_Stored = std::span<const std::byte>(l_Compressed).first(*l_CompressedSize);
                l_Entry.Compression = Trinity::PackCompression::Zstd;
                ++l_Summary.Compressed;
            }
        }

        // Zeros up to the file's alignment, which is never more than a page
        const std::uint64_t l_Alignment = Trinity::PackLayout::GetAlignment(l_Stored.size());
        const std::uint64_t l_Start = (l_Offset + l_Alignment - 1) / l_Alignment * l_Alignment;
        WriteBytes(l_Stream, std::span(c_Zeros).first(static_cast<std::size_t>(l_Start - l_Offset)));
        WriteBytes(l_Stream, l_Stored);

        l_Entry.Offset = l_Start;
        l_Entry.StoredSize = l_Stored.size();
        l_Offset = l_Start + l_Entry.StoredSize;
        l_Entries.push_back(l_Entry);

        ++l_Summary.Files;
        l_Summary.Bytes += l_Entry.Size;
        if (options.Written != nullptr)
        {
            options.Written->fetch_add(1, std::memory_order_relaxed);
        }
    }

    const std::size_t l_EntriesSize = l_Entries.size() * sizeof(Trinity::PackEntry);
    Bytes l_Table(l_EntriesSize + l_Paths.size());
    if (!l_Entries.empty())
    {
        std::memcpy(l_Table.data(), l_Entries.data(), l_EntriesSize);
    }
    if (!l_Paths.empty())
    {
        std::memcpy(l_Table.data() + l_EntriesSize, l_Paths.data(), l_Paths.size());
    }

    Trinity::PackHeader l_Header;
    l_Header.FileCount = static_cast<std::uint32_t>(l_Entries.size());
    l_Header.TableOffset = l_Offset;
    l_Header.TableSize = l_Table.size();
    l_Header.TableHash = Trinity::Hash::XXHash64(l_Table);
    l_Header.PackSize = l_Offset + l_Table.size();

    WriteBytes(l_Stream, l_Table);
    l_Stream.seekp(0);
    WriteBytes(l_Stream, std::as_bytes(std::span(&l_Header, 1)));
    l_Stream.flush();
    if (!l_Stream)
    {
        return Trinity::Unexpected{ std::format("'{}' could not be written", ToUtf8(path)) };
    }

    l_Summary.PackBytes = l_Header.PackSize;

    return l_Summary;
}