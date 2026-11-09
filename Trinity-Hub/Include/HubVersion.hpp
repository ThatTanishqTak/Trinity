#pragma once

#include <string_view>

// Hub's own version, which moves apart from the engine's from 0.1.0, as TRINITY_HUB_VERSION in the root CMakeLists.txt names it, and which Hub's zip and its release on GitHub are named for. Every Hub opens every engine version, so a Hub of any version reads any of them but those of a format newer than it knows
namespace HubVersion
{
    inline constexpr std::string_view c_Version = TR_HUB_VERSION;
}