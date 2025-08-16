#include "Trinity/trpch.h"

#include "Trinity/Renderer/Primitives.h"
#include "Trinity/Renderer/Mesh.h"

namespace Trinity
{
    std::shared_ptr<Mesh> CreatePrimitive(VulkanContext* context, PrimitiveType type)
    {
        if (!context)
        {
            return nullptr;
        }

        switch (type)
        {
        case PrimitiveType::Quad:
        {
            std::vector<Vertex> l_Vertices =
            {
                { { -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
                { {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
                { {  0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
                { { -0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }
            };

            std::vector<uint32_t> l_Indices = { 0, 1, 2, 2, 3, 0 };

            auto a_Mesh = std::make_shared<Mesh>(context);
            if (auto a_Error = a_Mesh->Create(l_Vertices, l_Indices))
            {
                TR_CORE_ERROR("{}", *a_Error);
                return nullptr;
            }

            return a_Mesh;
        }

        default:
            break;
        }

        return nullptr;
    }
}