#pragma once

#include <Trinity.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <variant>
#include <vector>

// Where it lies in the pack, and where it comes from: a file on disk, or bytes cooked as gathered
struct BuildFile
{
    std::string PackPath;
    std::variant<std::filesystem::path, std::vector<std::byte>> Source;
};

// An asset the build reaches but cannot ship, with the path of what names it, and why
struct BrokenReference
{
    Trinity::UUID Asset;
    std::string NamedBy;
    std::string Reason;

    [[nodiscard]] bool operator==(const BrokenReference&) const = default;
};

struct BuildContents
{
    // Each asset the build reaches, in the order reached, as the game's asset table lists it
    std::vector<Trinity::AssetRecord> Records;
    // Every file of the pack, the asset table last
    std::vector<BuildFile> Files;
    std::vector<BrokenReference> Broken;
};

// A build's scenes, input actions and mixer, and every asset they reach, as a running scene does
namespace BuildWalk
{
    // Scenes in the build's order, the first starting the game, flattened as cooked. Main thread only
    [[nodiscard]] BuildContents Gather(const Trinity::Project& project, const Trinity::AssetRegistry& registry, std::span<const Trinity::UUID> scenes);
}