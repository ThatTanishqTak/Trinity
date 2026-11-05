#pragma once

#include "Modules/PortableExecutable.hpp"

#include <Trinity.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// What Forge's export writes into a Windows player: its icon, made from the export's icon texture, which Explorer, the taskbar and the game's window show, and its version information, which Explorer's file Properties show
namespace PlayerResources
{
    // The icon group the player's window loads, as the first one is also the one Explorer shows
    inline constexpr std::uint16_t c_IconGroup = 1;
    // The frames Windows picks among for each place and display scale, up to 64 px as bitmaps and 256 px as PNG, as Windows' own icons have them
    inline constexpr std::array<std::uint32_t, 8> c_IconSizes{ 16, 20, 24, 32, 40, 48, 64, 256 };

    // Windows' four numbers, read from the start of the version text, so 1.2.3-beta+4 gives 1.2.3.0, a pre-release as a hyphen follows them. Not Numeric when the text starts with none, and each number at most 65535
    struct Version
    {
        std::array<std::uint16_t, 4> Numbers{};
        bool Numeric = false;
        bool PreRelease = false;
    };

    [[nodiscard]] Version ParseVersion(std::string_view text);
    // RT_ICON frames and the RT_GROUP_ICON naming them, from an image file as the texture importer reads it, scaled whole into each square with transparency round what it does not fill
    [[nodiscard]] Trinity::Expected<std::vector<PortableExecutable::Resource>, std::string> MakeIcon(const std::filesystem::path& source);
    // VS_VERSIONINFO in US English: the product as its name, description and internal name, the company, Copyright © the company, with no year, so an export next year gives the same bytes, the version as typed and as its numbers, and the executable's file name
    [[nodiscard]] PortableExecutable::Resource MakeVersion(const Trinity::ExportSettings& settings, std::string_view fileName);
}