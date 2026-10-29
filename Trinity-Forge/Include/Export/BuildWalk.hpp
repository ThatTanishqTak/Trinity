#pragma once

#include <Trinity.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <variant>
#include <vector>

// A file of the build's pack: where it lies there, under /cache once the pack mounts, and where it comes from, a file on disk as the project's cache or Assets folder has it, or bytes cooked as the build is gathered, such as a binary scene
struct BuildFile
{
    std::string PackPath;
    std::variant<std::filesystem::path, std::vector<std::byte>> Source;
};

// An asset the build reaches but cannot ship, with the path of what names it, or the part of the project that does, and why
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

// What a build holds: its scenes, the project's input actions and mixer, and every asset they reach, nested prefabs and models, materials and their textures, and clips inside sound events, found as a running scene finds them, so a game ships only those
namespace BuildWalk
{
    // The scenes in the build's order, the first being the one the game starts with. Each is read from its file and flattened as it is cooked, so a prefab it only places ships no file of its own, and prefabs are cooked whole with their nested instances still instances. A scene a script field names is not reached, as a game loads only its build's scenes. Main thread only, as templates are found through the serializer's resolver
    [[nodiscard]] BuildContents Gather(const Trinity::Project& project, const Trinity::AssetRegistry& registry, std::span<const Trinity::UUID> scenes);
}