#include "Trinity/Physics/PhysicsWorld3DState.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <utility>

namespace Trinity
{
    namespace
    {
        using JoltConvert::FromJolt;
        using JoltConvert::ToJolt;

        // Shapes smaller than this are taken as this, since Jolt refuses empty ones
        constexpr float c_MinimumSize = 1e-3f;

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

    PhysicsWorld3D::PhysicsWorld3D(const PhysicsWorldSettings3D& settings)
    {
        JoltContext::Acquire();

        m_State = Memory::New<State>(MemoryTag::Physics, settings.StepMemoryBytes);
        m_State->System.Init(settings.MaxBodies, 0, settings.MaxBodyPairs, settings.MaxContactConstraints, m_State->BroadPhase, m_State->ObjectVsBroadPhase, m_State->ObjectVsObject);
        m_State->System.SetGravity(ToJolt(settings.Gravity));

        JPH::PhysicsSettings l_Physics = m_State->System.GetPhysicsSettings();
        l_Physics.mNumVelocitySteps = std::max(settings.VelocitySteps, 1u);
        l_Physics.mNumPositionSteps = std::max(settings.PositionSteps, 1u);
        l_Physics.mPointVelocitySleepThreshold = settings.SleepVelocity;
        m_State->System.SetPhysicsSettings(l_Physics);
    }

    // The system frees every body still in it
    PhysicsWorld3D::~PhysicsWorld3D()
    {
        Memory::Delete(m_State);
        JoltContext::Release();
    }

    PhysicsBodyID PhysicsWorld3D::CreateBody(const BodyDescription3D& description)
    {
        JPH::BodyCreationSettings l_Settings(CreateShape(description), JPH::RVec3(ToJolt(description.Position)), ToJolt(description.Rotation), ToJolt(description.Motion), PhysicsLayers3D::Get(description.Motion));
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