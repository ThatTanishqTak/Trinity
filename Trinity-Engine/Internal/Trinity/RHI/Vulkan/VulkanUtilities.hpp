#pragma once

#include "Trinity/RHI/Types.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        // A Vulkan structure with every member zero except its type
        template<typename T>
        [[nodiscard]] T MakeInfo(VkStructureType type)
        {
            T l_Info{};
            l_Info.sType = type;

            return l_Info;
        }

        [[nodiscard]] VkFormat ToVkFormat(Format format);
        [[nodiscard]] std::string FormatResult(VkResult result);
        [[nodiscard]] VkImageAspectFlags GetAspect(Format format);
        [[nodiscard]] std::string FormatVersion(std::uint32_t version);
        [[nodiscard]] bool HasExtension(const std::vector<VkExtensionProperties>& extensions, std::string_view name);
    }
}