#pragma once

#include <vector>
#include <optional>
#include <string>
#include <cstddef>

namespace Trinity
{
    class VulkanContext;

    class Texture
    {
    public:
        explicit Texture(VulkanContext* context)
        {

        }

        std::optional<std::string> CreateFromPixels(const std::vector<std::byte>& pixels, int width, int height)
        {
            return {};
        }
    };
}