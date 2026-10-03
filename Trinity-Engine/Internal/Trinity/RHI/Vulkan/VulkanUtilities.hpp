#pragma once

#include "Trinity/RHI/Types.hpp"
#include "Trinity/RHI/Vulkan/VulkanHeaders.hpp"

#include <string>

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
    }
}