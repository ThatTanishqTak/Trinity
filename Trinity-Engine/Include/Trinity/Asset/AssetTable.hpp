#pragma once

#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // What a cooked game's registry reads in place of .meta files: each asset of the build by its UUID, with its path in the project, its importer, its parent and where its own file lies in the pack, if the game reads one
    namespace AssetTable
    {
        constexpr std::uint32_t c_FormatVersion = 1;
        // At the root of the pack, which mounts where a project's cache does
        constexpr std::string_view c_FileName = "Assets.trtable";

        [[nodiscard]] TRINITY_API std::string GetPath();

        // Sorted by UUID, so the same records always give the same bytes. Settings and sub-assets are left out, as a game reads every asset cooked
        [[nodiscard]] TRINITY_API std::vector<std::byte> Write(std::span<const AssetRecord> records);
        // Refused for a table that is cut short, malformed or from a newer Trinity
        [[nodiscard]] TRINITY_API Expected<std::vector<AssetRecord>, std::string> Read(std::span<const std::byte> data);
    }
}