#pragma once

#include <Trinity/Core/Expected.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A PE image: a C++ module or a player on Windows, or a C# assembly anywhere, read whole to change
namespace PortableExecutable
{
    // A resource's type or name: a number, or a name when Name is not empty
    struct ResourceID
    {
        std::uint16_t Number = 0;
        std::u16string Name;

        [[nodiscard]] bool operator==(const ResourceID&) const = default;
    };

    struct Resource
    {
        ResourceID Type;
        ResourceID Name;
        std::uint16_t Language = 0;
        std::vector<char> Data;
    };

    [[nodiscard]] std::optional<std::vector<char>> ReadImage(const std::filesystem::path& path);
    [[nodiscard]] bool WriteImage(const std::filesystem::path& path, const std::vector<char>& bytes);
    [[nodiscard]] bool IsImage(const std::vector<char>& image);

    // Every resource the image holds, by type, name and language, or nothing when they cannot be read
    [[nodiscard]] std::optional<std::vector<Resource>> ReadResources(const std::vector<char>& image);
    // Replaces resources of the given types. An image it cannot change safely is refused with why
    [[nodiscard]] Trinity::Expected<void, std::string> SetResources(std::vector<char>& image, std::span<const Resource> resources);

    // Sets the PDB path the image records to a file name, found beside it. False if it does not fit
    [[nodiscard]] bool SetPdbName(std::vector<char>& image, std::string_view name);
}