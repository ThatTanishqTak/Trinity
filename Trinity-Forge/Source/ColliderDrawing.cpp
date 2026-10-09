#include "ColliderDrawing.hpp"

#include <algorithm>
#include <array>

namespace
{
    constexpr glm::vec4 c_SolidColor{ 0.45f, 0.95f, 0.45f, 1.0f };
    constexpr glm::vec4 c_TriggerColor{ 0.4f, 0.75f, 1.0f, 1.0f };

    // The collider's own frame: the entity's world transform, then the collider's offset and turn
    glm::mat4 GetColliderMatrix(const glm::mat4& world, const Trinity::ColliderSettings& collider)
    {
        return world * glm::translate(glm::mat4(1.0f), collider.Offset) * glm::mat4_cast(collider.OffsetRotation);
    }

    glm::vec4 GetColor(const Trinity::ColliderSettings& collider)
    {
        return collider.Trigger ? c_TriggerColor : c_SolidColor;
    }

    // A round shape along the frame's Y: its centre, its unit axis and the scale along it, and the larger scale across it
    struct RoundFrame
    {
        glm::vec3 Center{ 0.0f };
        glm::vec3 Axis{ 0.0f, 1.0f, 0.0f };
        glm::vec3 Side{ 1.0f, 0.0f, 0.0f };
        glm::vec3 Front{ 0.0f, 0.0f, 1.0f };
        float AxialScale = 1.0f;
        float RadialScale = 1.0f;
    };

    RoundFrame GetRoundFrame(const glm::mat4& frame)
    {
        const glm::vec3 l_X(frame[0]);
        const glm::vec3 l_Y(frame[1]);
        const glm::vec3 l_Z(frame[2]);

        RoundFrame l_Round;
        l_Round.Center = glm::vec3(frame[3]);
        l_Round.AxialScale = glm::length(l_Y);
        l_Round.RadialScale = std::max(glm::length(l_X), glm::length(l_Z));
        l_Round.Axis = l_Round.AxialScale > 1e-6f ? l_Y / l_Round.AxialScale : glm::vec3(0.0f, 1.0f, 0.0f);
        l_Round.Side = glm::length(l_X) > 1e-6f ? glm::normalize(l_X) : glm::vec3(1.0f, 0.0f, 0.0f);
        l_Round.Front = glm::normalize(glm::cross(l_Round.Side, l_Round.Axis));

        return l_Round;
    }

    void DrawEntityColliders(Trinity::Entity entity)
    {
        const glm::mat4& l_World = entity.Get<Trinity::WorldTransformComponent>().Matrix;

        if (entity.Has<Trinity::BoxColliderComponent>())
        {
            const Trinity::BoxColliderComponent& l_Box = entity.Get<Trinity::BoxColliderComponent>();
            Trinity::DebugDraw::Box(glm::scale(GetColliderMatrix(l_World, l_Box), l_Box.HalfExtents), GetColor(l_Box));
        }

        if (entity.Has<Trinity::SphereColliderComponent>())
        {
            const Trinity::SphereColliderComponent& l_Sphere = entity.Get<Trinity::SphereColliderComponent>();
            const glm::mat4 l_Frame = GetColliderMatrix(l_World, l_Sphere);
            const float l_Scale = std::max({ glm::length(glm::vec3(l_Frame[0])), glm::length(glm::vec3(l_Frame[1])), glm::length(glm::vec3(l_Frame[2])) });
            Trinity::DebugDraw::Sphere(glm::vec3(l_Frame[3]), l_Sphere.Radius * l_Scale, GetColor(l_Sphere));
        }

        if (entity.Has<Trinity::CapsuleColliderComponent>())
        {
            const Trinity::CapsuleColliderComponent& l_Capsule = entity.Get<Trinity::CapsuleColliderComponent>();
            const RoundFrame l_Round = GetRoundFrame(GetColliderMatrix(l_World, l_Capsule));
            const glm::vec3 l_HalfSegment = l_Round.Axis * (l_Capsule.GetHalfSegment() * l_Round.AxialScale);
            Trinity::DebugDraw::Capsule(l_Round.Center - l_HalfSegment, l_Round.Center + l_HalfSegment, l_Capsule.Radius * l_Round.RadialScale, GetColor(l_Capsule));
        }

        if (entity.Has<Trinity::CylinderColliderComponent>())
        {
            const Trinity::CylinderColliderComponent& l_Cylinder = entity.Get<Trinity::CylinderColliderComponent>();
            const RoundFrame l_Round = GetRoundFrame(GetColliderMatrix(l_World, l_Cylinder));
            const glm::vec3 l_HalfHeight = l_Round.Axis * (l_Cylinder.Height * 0.5f * l_Round.AxialScale);
            const float l_Radius = l_Cylinder.Radius * l_Round.RadialScale;
            const glm::vec4 l_Color = GetColor(l_Cylinder);
            Trinity::DebugDraw::Circle(l_Round.Center - l_HalfHeight, l_Round.Axis, l_Radius, l_Color);
            Trinity::DebugDraw::Circle(l_Round.Center + l_HalfHeight, l_Round.Axis, l_Radius, l_Color);
            for (const glm::vec3& it_Side : { l_Round.Side, -l_Round.Side, l_Round.Front, -l_Round.Front })
            {
                Trinity::DebugDraw::Line(l_Round.Center - l_HalfHeight + it_Side * l_Radius, l_Round.Center + l_HalfHeight + it_Side * l_Radius, l_Color);
            }
        }
    }
}

void DrawColliders(Trinity::Scene& scene, Trinity::Entity root)
{
    for (Trinity::Entity it_Entity = root; it_Entity; it_Entity = scene.GetNextInSubtree(it_Entity, root))
    {
        DrawEntityColliders(it_Entity);
    }
}