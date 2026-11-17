#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"

#include <span>
#include <string_view>

namespace Trinity
{
    // Named as TRINITY_PLATFORM names it. The first graphics API is tried first
    struct TargetPlatform
    {
        std::string_view Name;
        std::string_view DisplayName;
        // Empty for one Trinity builds for now
        std::string_view Milestone;
        std::span<const GraphicsAPI> GraphicsAPIs;
        // Built with the platform holder's SDK, rather than with a version of Trinity for that platform
        bool Console = false;
    };

    namespace TargetPlatforms
    {
        // Windows, Linux and macOS, then the consoles, as the Build panel lists them
        [[nodiscard]] TRINITY_API std::span<const TargetPlatform> GetAll();
        [[nodiscard]] TRINITY_API const TargetPlatform* Find(std::string_view name);
    }
}