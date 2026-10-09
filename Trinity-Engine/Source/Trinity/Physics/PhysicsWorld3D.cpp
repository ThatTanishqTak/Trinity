#include "Trinity/Physics/JoltContext.hpp"
#include "Trinity/Physics/PhysicsWorld3D.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <utility>

namespace Trinity
{
    namespace
    {
        // Until collision layers come in Step 8: static bodies, which never meet each other, and every body that moves, in a broadphase layer each
        constexpr JPH::ObjectLayer c_StaticLayer = 0;
        constexpr JPH::ObjectLayer c_MovingLayer = 1;
        constexpr JPH::BroadPhaseLayer c_StaticBroadPhase{ 0 };
        constexpr JPH::BroadPhaseLayer c_MovingBroadPhase{ 1 };
        constexpr JPH::uint c_BroadPhaseLayerCount = 2;

        // Shapes smaller than this are taken as this, since Jolt refuses empty ones
        constexpr float c_MinimumSize = 1e-3f;

        class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
        {
        public:
            [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override
            {
                return c_BroadPhaseLayerCount;
            }

            [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return layer == c_StaticLayer ? c_StaticBroadPhase : c_MovingBroadPhase;
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
                return layer == c_MovingLayer || broadPhase == c_MovingBroadPhase;
            }
        };

        class ObjectLayerFilter final : public JPH::ObjectLayerPairFilter
        {
        public:
            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
            {
                return first == c_MovingLayer || second == c_MovingLayer;
            }
        };

        JPH::Vec3 ToJolt(const glm::vec3& vector)
        {
            return JPH::Vec3(vector.x, vector.y, vector.z);
        }

        JPH::Quat ToJolt(const glm::quat& rotation)
        {
            return JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w).Normalized();
        }

        glm::vec3 FromJolt(JPH::Vec3Arg vector)
        {
            return { vector.GetX(), vector.GetY(), vector.GetZ() };
        }

        glm::quat FromJolt(JPH::QuatArg rotation)
        {
            return { rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ() };
        }

        JPH::EMotionType ToJolt(BodyMotion motion)
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

        JPH::BodyID ToJolt(PhysicsBodyID body)
        {
            return JPH::BodyID(body.Value);
        }

        // A box's corners are rounded by a convex radius, which must fit inside its smallest half extent
        JPH::ShapeRefC CreateShape(const BodyDescription3D& description)
        {
            if (description.Shape == BodyShape3D::Sphere)
            {
                return new JPH::SphereShape(std::max(description.Radius, c_MinimumSize));
            }

            const glm::vec3 l_HalfExtents = glm::max(description.HalfExtents, glm::vec3(c_MinimumSize));
            const float l_ConvexRadius = std::min(JPH::cDefaultConvexRadius, std::min({ l_HalfExtents.x, l_HalfExtents.y, l_HalfExtents.z }) * 0.5f);

            return new JPH::BoxShape(ToJolt(l_HalfExtents), l_ConvexRadius);
        }
    }

    // The filters and the scratch memory must outlive the system, so they come before it
    struct PhysicsWorld3D::State
    {
        explicit State(std::uint32_t stepMemoryBytes) : StepMemory(stepMemoryBytes)
        {

        }

        BroadPhaseLayers BroadPhase;
        ObjectVsBroadPhaseFilter ObjectVsBroadPhase;
        ObjectLayerFilter ObjectVsObject;
        JPH::TempAllocatorImpl StepMemory;
        JPH::PhysicsSystem System;
        bool ReportedFull = false;
    };

    PhysicsWorld3D::PhysicsWorld3D(const PhysicsWorldSettings3D& settings)
    {
        JoltContext::Acquire();

        m_State = Memory::New<State>(MemoryTag::Physics, settings.StepMemoryBytes);
        m_State->System.Init(settings.MaxBodies, 0, settings.MaxBodyPairs, settings.MaxContactConstraints, m_State->BroadPhase, m_State->ObjectVsBroadPhase, m_State->ObjectVsObject);
        m_State->System.SetGravity(ToJolt(settings.Gravity));
    }

    // The system frees every body still in it
    PhysicsWorld3D::~PhysicsWorld3D()
    {
        Memory::Delete(m_State);
        JoltContext::Release();
    }

    PhysicsBodyID PhysicsWorld3D::CreateBody(const BodyDescription3D& description)
    {
        const bool l_Moving = description.Motion != BodyMotion::Static;
        JPH::BodyCreationSettings l_Settings(CreateShape(description), JPH::RVec3(ToJolt(description.Position)), ToJolt(description.Rotation), ToJolt(description.Motion), l_Moving ? c_MovingLayer : c_StaticLayer);
        l_Settings.mLinearVelocity = ToJolt(description.LinearVelocity);
        l_Settings.mAngularVelocity = ToJolt(description.AngularVelocity);
        l_Settings.mFriction = description.Friction;
        l_Settings.mRestitution = description.Restitution;
        l_Settings.mLinearDamping = description.LinearDamping;
        l_Settings.mAngularDamping = description.AngularDamping;
        l_Settings.mGravityFactor = description.GravityFactor;
        l_Settings.mUserData = description.UserData;
        if (description.Mass > 0.0f)
        {
            l_Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            l_Settings.mMassPropertiesOverride.mMass = description.Mass;
        }

        const JPH::BodyID l_Body = m_State->System.GetBodyInterface().CreateAndAddBody(l_Settings, description.Motion == BodyMotion::Dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (l_Body.IsInvalid())
        {
            TR_CORE_ERROR("PhysicsWorld3D: the world holds {} bodies already, its most, so no more are made", m_State->System.GetNumBodies());

            return {};
        }

        return { l_Body.GetIndexAndSequenceNumber() };
    }

    void PhysicsWorld3D::DestroyBody(PhysicsBodyID body)
    {
        if (!body)
        {
            return;
        }

        JPH::BodyInterface& l_Bodies = m_State->System.GetBodyInterface();
        l_Bodies.RemoveBody(ToJolt(body));
        l_Bodies.DestroyBody(ToJolt(body));
    }

    glm::vec3 PhysicsWorld3D::GetBodyPosition(PhysicsBodyID body) const
    {
        return FromJolt(JPH::Vec3(m_State->System.GetBodyInterface().GetPosition(ToJolt(body))));
    }

    glm::quat PhysicsWorld3D::GetBodyRotation(PhysicsBodyID body) const
    {
        return FromJolt(m_State->System.GetBodyInterface().GetRotation(ToJolt(body)));
    }

    glm::vec3 PhysicsWorld3D::GetBodyLinearVelocity(PhysicsBodyID body) const
    {
        return FromJolt(m_State->System.GetBodyInterface().GetLinearVelocity(ToJolt(body)));
    }

    bool PhysicsWorld3D::IsBodyActive(PhysicsBodyID body) const
    {
        return m_State->System.GetBodyInterface().IsActive(ToJolt(body));
    }

    // A full cache drops contacts, so it is reported, once for each world
    void PhysicsWorld3D::Step(Timestep step)
    {
        TR_PROFILE_FUNCTION();

        const JPH::EPhysicsUpdateError l_Error = m_State->System.Update(step.GetSeconds(), 1, &m_State->StepMemory, &JoltContext::GetJobSystem());
        if (l_Error != JPH::EPhysicsUpdateError::None && !std::exchange(m_State->ReportedFull, true))
        {
            TR_CORE_WARN("PhysicsWorld3D: a step ran out of room for{}{}{}, so some contacts were dropped", (l_Error & JPH::EPhysicsUpdateError::ManifoldCacheFull) != JPH::EPhysicsUpdateError::None ? " contact manifolds" : "", (l_Error & JPH::EPhysicsUpdateError::BodyPairCacheFull) != JPH::EPhysicsUpdateError::None ? " body pairs" : "", (l_Error & JPH::EPhysicsUpdateError::ContactConstraintsFull) != JPH::EPhysicsUpdateError::None ? " contact constraints" : "");
        }
    }

    void PhysicsWorld3D::SetGravity(const glm::vec3& gravity)
    {
        m_State->System.SetGravity(ToJolt(gravity));
    }

    glm::vec3 PhysicsWorld3D::GetGravity() const
    {
        return FromJolt(m_State->System.GetGravity());
    }

    std::uint32_t PhysicsWorld3D::GetBodyCount() const
    {
        return m_State->System.GetNumBodies();
    }

    std::uint32_t PhysicsWorld3D::GetActiveBodyCount() const
    {
        return m_State->System.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    }
}