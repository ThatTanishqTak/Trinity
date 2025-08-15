#pragma once

#include <vector>
#include <optional>
#include <string>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace Trinity
{
    class VulkanContext;

    struct Vertex
    {
        glm::vec3 Position{};
        glm::vec3 Color{};
        glm::vec2 TexCoord{};
    };

    class Mesh
    {
    public:
        explicit Mesh(VulkanContext* context)
        {

        }

        std::optional<std::string> Create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
        {
            return {};
        }
    };
}