#pragma once

#include <Trinity/Core/Expected.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A PE image, the format of a C++ module and a game's player on Windows and of a C# assembly everywhere, read whole so a copy of a build can be changed before it is written
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

    // Every resource the image holds, by type, name and language as Windows finds them, none for an image without any, or nothing when its resources cannot be read
    [[nodiscard]] std::optional<std::vector<Resource>> ReadResources(const std::vector<char>& image);
    // Every resource of the types given goes, those of other types, such as the linker's manifest, stay, and the given ones are added. The resource section is written again where it lies, last but for the relocations, which move after it, as a linker places them. An image laid out otherwise, signed, carrying symbols or data past its sections is refused with why, as it could not be changed without breaking it. Its checksum is made again
    [[nodiscard]] Trinity::Expected<void, std::string> SetResources(std::vector<char>& image, std::span<const Resource> resources);

    // The PDB path the image records for its debugger, which is where the linker or the C# compiler wrote it, set to a file name, which a debugger then looks for beside the image. A copy of a build so names the PDB copied beside it, rather than the build's, which the next build writes over and a debugger would hold. False when the image records none, or the name does not fit where the path was
    [[nodiscard]] bool SetPdbName(std::vector<char>& image, std::string_view name);
}