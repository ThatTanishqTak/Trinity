#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

// A PE image, the format of a C++ module on Windows and of a C# assembly everywhere, read whole so a copy of a build can be changed before it is written
namespace PortableExecutable
{
    [[nodiscard]] std::optional<std::vector<char>> ReadImage(const std::filesystem::path& path);
    [[nodiscard]] bool WriteImage(const std::filesystem::path& path, const std::vector<char>& bytes);

    // The PDB path the image records for its debugger, which is where the linker or the C# compiler wrote it, set to a file name, which a debugger then looks for beside the image. A copy of a build so names the PDB copied beside it, rather than the build's, which the next build writes over and a debugger would hold. False when the image records none, or the name does not fit where the path was
    [[nodiscard]] bool SetPdbName(std::vector<char>& image, std::string_view name);
}