#pragma once

#include <memory>

namespace Trinity
{
    class VulkanContext;
    class Mesh;

    enum class PrimitiveType
    {
        Quad = 0
    };

    std::shared_ptr<Mesh> CreatePrimitive(VulkanContext*, PrimitiveType);
}