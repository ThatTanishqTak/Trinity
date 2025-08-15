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

    inline std::shared_ptr<Mesh> CreatePrimitive(VulkanContext*, PrimitiveType)
    {
        return nullptr;
    }
}