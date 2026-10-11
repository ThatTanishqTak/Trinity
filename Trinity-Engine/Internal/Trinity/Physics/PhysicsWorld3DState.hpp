#pragma once

// Included only by sources in Source/Trinity/Physics, which are built for Jolt's instructions
#include "Trinity/Physics/JoltContext.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Physics/PhysicsWorld3D.hpp"

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace Trinity
{
    namespace PhysicsLayers3D
    {
        static_assert(sizeof(JPH::ObjectLayer) == sizeof(std::uint32_t), "Jolt is built with 32-bit object layers, OBJECT_LAYER_BITS in Vendor/CMakeLists.txt");

        // An object layer holds the collision layers of a body's colliders as a mask, bit N for layer N, and a bit above them for a body that moves. Static bodies, which never meet each other, and moving ones are in a broadphase layer each
        constexpr JPH::ObjectLayer c_MovingBit = 1u << c_MaxCollisionLayers;
        constexpr JPH::BroadPhaseLayer c_StaticBroadPhase{ 0 };
        constexpr JPH::BroadPhaseLayer c_MovingBroadPhase{ 1 };
        constexpr JPH::uint c_BroadPhaseCount = 2;

        [[nodiscard]] inline JPH::ObjectLayer Get(BodyMotion motion, std::uint32_t layers)
        {
            return (layers & c_AllCollisionLayers) | (motion == BodyMotion::Static ? 0u : c_MovingBit);
        }

        [[nodiscard]] inline std::uint32_t GetLayers(JPH::ObjectLayer layer)
        {
            return layer & c_AllCollisionLayers;
        }

        [[nodiscard]] inline bool IsMoving(JPH::ObjectLayer layer)
        {
            return (layer & c_MovingBit) != 0;
        }

        class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
        {
        public:
            [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override
            {
                return c_BroadPhaseCount;
            }

            [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return IsMoving(layer) ? c_MovingBroadPhase : c_StaticBroadPhase;
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return layer == c_StaticBroadPhase ? "Static" : "Moving";
            }
#endif
        };

        class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
            {
                return IsMoving(layer) || broadPhase == c_MovingBroadPhase;
            }
        };

        // Two bodies meet when one moves and any layer of one collides with any of the other's. A body with colliders on several layers is let through here, and its parts are sorted out by PhysicsScene3D's filter on the shapes
        class ObjectLayerFilter final : public JPH::ObjectLayerPairFilter
        {
        public:
            explicit ObjectLayerFilter(const CollisionLayers& layers) : Layers(layers)
            {

            }

            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
            {
                return (IsMoving(first) || IsMoving(second)) && (Layers.GetCollidingLayers(GetLayers(first)) & GetLayers(second)) != 0;
            }

            CollisionLayers Layers;
        };
    }

    namespace JoltConvert
    {
        [[nodiscard]] inline JPH::Vec3 ToJolt(const glm::vec3& vector)
        {
            return JPH::Vec3(vector.x, vector.y, vector.z);
        }

        [[nodiscard]] inline JPH::Quat ToJolt(const glm::quat& rotation)
        {
            return JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w).Normalized();
        }

        [[nodiscard]] inline glm::vec3 FromJolt(JPH::Vec3Arg vector)
        {
            return { vector.GetX(), vector.GetY(), vector.GetZ() };
        }

        [[nodiscard]] inline glm::quat FromJolt(JPH::QuatArg rotation)
        {
            return { rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ() };
        }

        [[nodiscard]] inline JPH::EMotionType ToJolt(BodyMotion motion)
        {
            switch (motion)
            {
                case BodyMotion::Static:
                    return JPH::EMotionType::Static;
                case BodyMotion::Kinematic: 
                    return JPH::EMotionType::Kinematic;
                case BodyMotion::Dynamic:
                    break;
            }

            return JPH::EMotionType::Dynamic;
        }
    }

    // The filters and the scratch memory must outlive the system, so they come before it
    struct PhysicsWorld3D::State
    {
        explicit State(const PhysicsWorldSettings3D& settings) : ObjectVsObject(settings.Layers), StepMemory(settings.StepMemoryBytes)
        {

        }

        PhysicsLayers3D::BroadPhaseLayers BroadPhase;
        PhysicsLayers3D::ObjectVsBroadPhaseFilter ObjectVsBroadPhase;
        PhysicsLayers3D::ObjectLayerFilter ObjectVsObject;
        JPH::TempAllocatorImpl StepMemory;
        JPH::PhysicsSystem System;
        bool ReportedFull = false;
    };
}