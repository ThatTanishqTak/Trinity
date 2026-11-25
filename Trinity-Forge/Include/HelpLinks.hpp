#pragma once

#include <string_view>

// Where Forge's help goes: Trinity's pages on GitHub, each address in this one place
namespace HelpLinks
{
    inline constexpr std::string_view c_Documentation = "https://github.com/ThatTanishqTak/Trinity#readme";
    inline constexpr std::string_view c_ReleaseNotes = "https://github.com/ThatTanishqTak/Trinity/releases";
    inline constexpr std::string_view c_ReportBug = "https://github.com/ThatTanishqTak/Trinity/issues/new";

    // In the system's browser. One that cannot be opened is told of in the Console, with its address
    void Open(std::string_view address);
}