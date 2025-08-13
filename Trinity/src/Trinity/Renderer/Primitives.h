#pragma once

#include <memory>

namespace Trinity
{
    class VulkanContext;
    class Mesh;

    enum class PrimitiveType
    {
        Cube,
        Sphere,
        Square,
        Circle
    };

    std::shared_ptr<Mesh> CreatePrimitive(VulkanContext* context, PrimitiveType type);
}