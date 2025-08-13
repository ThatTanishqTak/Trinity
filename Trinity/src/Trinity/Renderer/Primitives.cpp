#include "Trinity/trpch.h"

#include "Trinity/Renderer/Primitives.h"
#include "Trinity/Renderer/Mesh.h"
#include "Trinity/Vulkan/VulkanContext.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <vector>

namespace Trinity
{
    std::shared_ptr<Mesh> CreatePrimitive(VulkanContext* context, PrimitiveType type)
    {
        std::vector<Vertex> l_Vertices{};
        std::vector<uint32_t> l_Indices{};

        switch (type)
        {
        case PrimitiveType::Cube:
        {
            l_Vertices = 
            {
                {{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
                {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
                {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
                {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
                {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
                {{ 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
                {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
                {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
            };

            l_Indices = 
            {
                0, 1, 2, 2, 3, 0,
                4, 5, 6, 6, 7, 4,
                0, 1, 5, 5, 4, 0,
                2, 3, 7, 7, 6, 2,
                0, 3, 7, 7, 4, 0,
                1, 2, 6, 6, 5, 1
            };
            break;
        }
        case PrimitiveType::Sphere:
        {
            const uint32_t l_XSegments = 32;
            const uint32_t l_YSegments = 16;

            for (uint32_t l_Y = 0; l_Y <= l_YSegments; ++l_Y)
            {
                for (uint32_t l_X = 0; l_X <= l_XSegments; ++l_X)
                {
                    float l_XSegment = static_cast<float>(l_X) / static_cast<float>(l_XSegments);
                    float l_YSegment = static_cast<float>(l_Y) / static_cast<float>(l_YSegments);
                    float l_XPos = std::cos(l_XSegment * 2.0f * glm::pi<float>()) * std::sin(l_YSegment * glm::pi<float>());
                    float l_YPos = std::cos(l_YSegment * glm::pi<float>());
                    float l_ZPos = std::sin(l_XSegment * 2.0f * glm::pi<float>()) * std::sin(l_YSegment * glm::pi<float>());

                    l_Vertices.push_back({ { l_XPos, l_YPos, l_ZPos }, {1.0f, 1.0f, 1.0f}, { l_XSegment, l_YSegment } });
                }
            }

            for (uint32_t l_Y = 0; l_Y < l_YSegments; ++l_Y)
            {
                for (uint32_t l_X = 0; l_X < l_XSegments; ++l_X)
                {
                    l_Indices.push_back(l_Y * (l_XSegments + 1) + l_X);
                    l_Indices.push_back((l_Y + 1) * (l_XSegments + 1) + l_X);
                    l_Indices.push_back((l_Y + 1) * (l_XSegments + 1) + l_X + 1);

                    l_Indices.push_back(l_Y * (l_XSegments + 1) + l_X);
                    l_Indices.push_back((l_Y + 1) * (l_XSegments + 1) + l_X + 1);
                    l_Indices.push_back(l_Y * (l_XSegments + 1) + l_X + 1);
                }
            }
            break;
        }
        case PrimitiveType::Square:
        {
            l_Vertices = {
                {{-0.5f, 0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
                {{ 0.5f, 0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
                {{ 0.5f, 0.0f,  0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
                {{-0.5f, 0.0f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
            };

            l_Indices = { 0, 1, 2, 2, 3, 0 };
            break;
        }
        case PrimitiveType::Circle:
        {
            const uint32_t l_Segments = 32;
            l_Vertices.push_back({ {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.5f, 0.5f} });
            for (uint32_t l_I = 0; l_I <= l_Segments; ++l_I)
            {
                float l_Angle = (static_cast<float>(l_I) / static_cast<float>(l_Segments)) * 2.0f * glm::pi<float>();
                float l_X = std::cos(l_Angle) * 0.5f;
                float l_Y = std::sin(l_Angle) * 0.5f;

                l_Vertices.push_back({ {l_X, 0.0f, l_Y}, {1.0f, 1.0f, 1.0f}, {(l_X + 0.5f), (l_Y + 0.5f)} });
            }

            for (uint32_t l_I = 1; l_I <= l_Segments; ++l_I)
            {
                l_Indices.push_back(0);
                l_Indices.push_back(l_I);
                l_Indices.push_back(l_I + 1);
            }
            break;
        }
        default:
            break;
        }

        if (l_Vertices.empty())
        {
            return nullptr;
        }

        std::shared_ptr<Mesh> l_Mesh = std::make_shared<Mesh>(context);
        if (auto l_Error = l_Mesh->Create(l_Vertices, l_Indices))
        {
            (void)l_Error;

            return nullptr;
        }

        return l_Mesh;
    }
}