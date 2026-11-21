#pragma once

#include <Trinity.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct PackSummary
{
    std::size_t Files = 0;
    std::size_t Compressed = 0;
    // Files whose compressed bytes, or whose being stored as they are, the cache already held
    std::size_t Cached = 0;
    // The files' own sizes, and the pack's on disk
    std::uint64_t Bytes = 0;
    std::uint64_t PackBytes = 0;
};

// The cache folder for compressed bytes, a flag that stops the write, and a count for progress
struct PackWriteOptions
{
    std::filesystem::path CacheDirectory;
    const std::atomic<bool>* Cancel = nullptr;
    std::atomic<std::size_t>* Written = nullptr;
};

// Gathers files under their paths, then writes one .trpak. The same files give the same bytes
class PackBuilder
{
public:
    // zstd decompresses as fast at any level, and Build And Run packs often, so the level stays fast
    static constexpr int c_CompressionLevel = 12;

    // A file read from disk as the pack is written. False for a bad path or one already in the pack
    bool AddFile(std::string_view packPath, const std::filesystem::path& nativePath);
    // A file made in memory, such as Game.trgame
    bool AddData(std::string_view packPath, std::span<const std::byte> data);

    [[nodiscard]] std::size_t GetFileCount() const { return m_Files.size(); }

    // Written beside the target and renamed over it, so a failed write leaves the old pack. Any thread
    [[nodiscard]] Trinity::Expected<PackSummary, std::string> Write(const std::filesystem::path& output, const PackWriteOptions& options = {}) const;

private:
    using Source = std::variant<std::filesystem::path, std::vector<std::byte>>;

    [[nodiscard]] std::optional<std::string> MakeKey(std::string_view packPath) const;
    [[nodiscard]] Trinity::Expected<PackSummary, std::string> WriteTo(const std::filesystem::path& path, const PackWriteOptions& options, std::vector<std::filesystem::path>& cacheFiles) const;

    // Sorted by path as the table is, since std::string compares bytes as the pack's reader does
    std::map<std::string, Source, std::less<>> m_Files;
};