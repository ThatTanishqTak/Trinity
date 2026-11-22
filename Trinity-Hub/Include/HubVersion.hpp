#pragma once

#include <string_view>

// Hub's own version, apart from the engine's, as TRINITY_HUB_VERSION in the root CMakeLists.txt
namespace HubVersion
{
    inline constexpr std::string_view c_Version = TR_HUB_VERSION;
}