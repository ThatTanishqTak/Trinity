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
        // Until collision layers come in Step 8: static bodies, which never meet each other, and every body that moves, in a broadphase layer each
        constexpr JPH::ObjectLayer c_Static = 0;
        constexpr JPH::ObjectLayer c_Moving = 1;
        constexpr JPH::BroadPhaseLayer c_StaticBroadPhase{ 0 };
        constexpr JPH::BroadPhaseLayer c_MovingBroadPhase{ 1 };
        constexpr JPH::uint c_BroadPhaseCount = 2;

        [[nodiscard]] inline JPH::ObjectLayer Get(BodyMotion motion)
        {
            return motion == BodyMotion::Static ? c_Static : c_Moving;
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
                return layer == c_Static ? c_StaticBroadPhase : c_MovingBroadPhase;
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
                return layer == c_Moving || broadPhase == c_MovingBroadPhase;
            }
        };

        class ObjectLayerFilter final : public JPH::ObjectLayerPairFilter
        {
        public:
            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
            {
                return first == c_Moving || second == c_Moving;
            }
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
        explicit State(std::uint32_t stepMemoryBytes) : StepMemory(stepMemoryBytes)
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