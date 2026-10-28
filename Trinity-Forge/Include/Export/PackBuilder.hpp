#pragma once

#include <Trinity.hpp>

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
    // The files' own sizes, and the pack's on disk
    std::uint64_t Bytes = 0;
    std::uint64_t PackBytes = 0;
};

// Gathers files under their paths in a pack, then writes them as one .trpak. The same files always give the same bytes, in whatever order they were added
class PackBuilder
{
public:
    // zstd decompresses as fast at any level, and Build And Run packs often, so the level stays where zstd still compresses tens of megabytes a second
    static constexpr int c_CompressionLevel = 12;

    // A file read from disk as the pack is written, such as a cooked asset in the project's Cache. False for a path that is not valid, is in the pack already, or is a folder of a file in it, or the other way round
    bool AddFile(std::string_view packPath, const std::filesystem::path& nativePath);
    // A file made in memory, such as Game.trgame
    bool AddData(std::string_view packPath, std::span<const std::byte> data);

    [[nodiscard]] std::size_t GetFileCount() const { return m_Files.size(); }

    // Written beside the target and renamed over it, so a write that fails leaves the pack that was there as it was
    [[nodiscard]] Trinity::Expected<PackSummary, std::string> Write(const std::filesystem::path& output) const;

private:
    using Source = std::variant<std::filesystem::path, std::vector<std::byte>>;

    [[nodiscard]] std::optional<std::string> MakeKey(std::string_view packPath) const;
    [[nodiscard]] Trinity::Expected<PackSummary, std::string> WriteTo(const std::filesystem::path& path) const;

    // Sorted by path as the table is, since std::string compares bytes as the pack's reader does
    std::map<std::string, Source, std::less<>> m_Files;
};