#include "Trinity/Project/TargetPlatform.hpp"

#include <algorithm>
#include <array>

namespace Trinity
{
    namespace
    {
        constexpr std::array<GraphicsAPI, 2> c_WindowsAPIs{ GraphicsAPI::D3D12, GraphicsAPI::Vulkan };
        constexpr std::array<GraphicsAPI, 1> c_VulkanAPIs{ GraphicsAPI::Vulkan };
        // MoltenVK first, then Metal of its own, as the roadmap has it
        constexpr std::array<GraphicsAPI, 2> c_MacAPIs{ GraphicsAPI::Vulkan, GraphicsAPI::Metal };
        constexpr std::array<GraphicsAPI, 1> c_XboxAPIs{ GraphicsAPI::D3D12 };

        // PlayStation's API is its own, which the engine has no backend for until M16
        constexpr std::array<TargetPlatform, 6> c_Platforms{ {
            { "Windows", "Windows",{}, c_WindowsAPIs, false },
            { "Linux", "Linux", "M14", c_VulkanAPIs, false },
            { "macOS", "macOS", "M15", c_MacAPIs, false },
            { "Xbox", "Xbox Series", "M16", c_XboxAPIs, true },
            { "PlayStation", "PlayStation 5", "M16",{}, true },
            { "Switch", "Nintendo Switch", "M16", c_VulkanAPIs, true }
        } };
    }

    namespace TargetPlatforms
    {
        std::span<const TargetPlatform> GetAll()
        {
            return c_Platforms;
        }

        const TargetPlatform* Find(std::string_view name)
        {
            const auto a_Found = std::ranges::find(c_Platforms, name, &TargetPlatform::Name);

            return a_Found != c_Platforms.end() ? &*a_Found : nullptr;
        }
    }
}