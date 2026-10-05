#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Scene/ComponentType.hpp"

#include <entt/entity/entity.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <string_view>

namespace Trinity
{
    // Every entity has one, and its scene finds the entity by it
    struct IDComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.ID";

        UUID ID;
    };

    struct TagComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Tag";

        TaggedString<MemoryTag::Scene> Tag;
    };

    // Relative to the parent. Every entity has one
    struct TransformComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Transform";

        glm::vec3 Position{ 0.0f };
        glm::quat Rotation = glm::identity<glm::quat>();
        glm::vec3 Scale{ 1.0f };

        [[nodiscard]] glm::mat4 GetMatrix() const
        {
            return glm::translate(glm::mat4(1.0f), Position) * glm::mat4_cast(Rotation) * glm::scale(glm::mat4(1.0f), Scale);
        }

        // Shear, which a parent with non-uniform scale and a rotated child can produce, has no place here and is dropped
        void SetMatrix(const glm::mat4& matrix)
        {
            Position = glm::vec3(matrix[3]);

            const glm::mat3 l_Basis(matrix);
            Scale = glm::vec3(glm::length(l_Basis[0]), glm::length(l_Basis[1]), glm::length(l_Basis[2]));
            if (glm::determinant(l_Basis) < 0.0f)
            {
                Scale.x = -Scale.x;
            }

            if (std::abs(Scale.x) < 1e-12f || std::abs(Scale.y) < 1e-12f || std::abs(Scale.z) < 1e-12f)
            {
                Rotation = glm::identity<glm::quat>();

                return;
            }

            Rotation = glm::normalize(glm::quat_cast(glm::mat3(l_Basis[0] / Scale.x, l_Basis[1] / Scale.y, l_Basis[2] / Scale.z)));
        }
    };

    // Written by Scene::UpdateWorldTransforms, parents first. Every entity has one, and it is never saved
    struct WorldTransformComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.WorldTransform";

        glm::mat4 Matrix{ 1.0f };
    };

    // Parent and ordered siblings as a linked list. Every entity has one, and only the Scene changes it
    struct RelationshipComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Relationship";

        entt::entity Parent = entt::null;
        entt::entity FirstChild = entt::null;
        entt::entity LastChild = entt::null;
        entt::entity PreviousSibling = entt::null;
        entt::entity NextSibling = entt::null;
        std::uint32_t ChildCount = 0;
    };

    // Orthographic for now. The view is the inverse of the entity's world transform
    struct CameraComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Camera";

        float OrthographicSize = 10.0f;
        float Near = -1.0f;
        float Far = 1.0f;
        bool Primary = true;

        // OrthographicSize is the visible height. Near and far are swapped for reversed depth, so near maps to 1 and far to 0
        [[nodiscard]] glm::mat4 GetProjection(float aspectRatio) const
        {
            const float l_HalfHeight = OrthographicSize * 0.5f;
            const float l_HalfWidth = l_HalfHeight * aspectRatio;

            return glm::ortho(-l_HalfWidth, l_HalfWidth, -l_HalfHeight, l_HalfHeight, Far, Near);
        }
    };

    struct SpriteRendererComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.SpriteRenderer";

        // A texture asset's UUID. An invalid one draws white
        UUID Texture;
        glm::vec4 Tint{ 1.0f };
        bool FlipX = false;
        bool FlipY = false;
        // Minimum U and V, then maximum U and V
        glm::vec4 UVRect{ 0.0f, 0.0f, 1.0f, 1.0f };
        std::int32_t SortingLayer = 0;
        std::int32_t OrderInLayer = 0;
    };
}