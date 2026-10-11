#include "Trinity/Physics/PhysicsWorld3DState.hpp"
#include "Trinity/Physics/PhysicsScene3D.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Physics/CollisionShape.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        using JoltConvert::FromJolt;
        using JoltConvert::ToJolt;

        template<typename T>
        using PhysicsVector = std::vector<T, TaggedAllocator<T, MemoryTag::Physics>>;

        // Shapes smaller than this are taken as this, since Jolt refuses empty ones
        constexpr float c_MinimumSize = 1e-3f;
        // Scales this close are taken as one, and axes this far from square as sheared
        constexpr float c_ScaleTolerance = 1e-4f;
        constexpr float c_ShearTolerance = 1e-3f;
        // A body whose shapes weigh less than this, such as one flat hull, is given a box's inertia and this mass unless its RigidBody gives one
        constexpr float c_MinimumMass = 1e-6f;
        constexpr float c_FallbackMass = 1.0f;
        // Set in a body's user data, beside the pointer to its record, when it is a record's sensor
        constexpr std::uint64_t c_SensorBit = 1;

        enum class ColliderKind : std::uint8_t
        {
            Box,
            Sphere,
            Capsule,
            Cylinder,
            ConvexHull,
            Mesh
        };

        // A collider as it was read when the scene started: a box's half extents, or a round shape's radius and height
        struct ColliderSource
        {
            entt::entity Entity = entt::null;
            ColliderKind Kind = ColliderKind::Box;
            ColliderSettings Settings;
            glm::vec3 Size{ 0.0f };
            UUID Shape;
        };

        struct BodyPose
        {
            glm::vec3 Position{ 0.0f };
            glm::quat Rotation = glm::identity<glm::quat>();
        };

        // A matrix as a pose and a scale, a mirror kept in the scale's X, and whether its axes were sheared, which neither can hold
        struct DecomposedMatrix
        {
            BodyPose Pose;
            glm::vec3 Scale{ 1.0f };
            bool Sheared = false;
        };

        // The friction and restitution of each solid collider, which contacts combine part by part
        struct ColliderMaterial
        {
            float Friction = 0.5f;
            float Restitution = 0.0f;
        };

        // A body's owner and what it was made from. Records are all made before any body, and never move after, since bodies point at theirs
        struct BodyRecord
        {
            entt::entity Entity = entt::null;
            UUID ID;
            BodyMotion Motion = BodyMotion::Static;
            bool HasRigidBody = false;
            RigidBodyComponent Settings;
            PhysicsVector<ColliderSource> Solids;
            PhysicsVector<ColliderSource> Triggers;
            PhysicsVector<ColliderMaterial> Materials;
            // A compound's parts carry their solid's index, and a single shape is the solid here
            bool Compound = false;
            std::uint32_t SingleSolid = 0;
            JPH::BodyID Body;
            JPH::BodyID Sensor;
            bool Waiting = true;
            BodyPose Previous;
            BodyPose Current;
            // Where its entity's transform puts it, last frame and this one, which a kinematic body and a sensor that follows its entity move between over the frame's steps
            BodyPose TargetFrom;
            BodyPose TargetTo;
        };

        // A contact Jolt added or removed on a worker, with its bodies in ID order
        struct RawContact
        {
            std::uint32_t First = 0;
            std::uint32_t Second = 0;
            std::uint32_t FirstPart = 0;
            std::uint32_t SecondPart = 0;
            bool Added = false;
            glm::vec3 Point{ 0.0f };
            glm::vec3 Normal{ 0.0f };
        };

        // Two bodies touching by as many parts as Count says, since the step Begun counts to. Jolt drops the contacts of bodies that fall asleep and adds them again when they wake, so a pair that only fell asleep is held as Sleeping, and ends only if a body wakes and a step passes without it touching again, from the step Woken counts to
        struct ContactPair
        {
            std::uint32_t Count = 0;
            std::uint64_t Begun = 0;
            bool Sleeping = false;
            std::uint64_t Woken = 0;
            JPH::BodyID FirstBody;
            JPH::BodyID SecondBody;
            UUID First;
            UUID Second;
            bool Trigger = false;
            // A contact whose UUIDs run against its bodies' order has its normals turned
            bool Swapped = false;
        };

        std::uint64_t GetPairKey(std::uint32_t first, std::uint32_t second)
        {
            return (static_cast<std::uint64_t>(first) << 32) | second;
        }

        // Every axis's length, with a mirror in X, and the turn of the axes made square
        DecomposedMatrix Decompose(const glm::mat4& matrix)
        {
            DecomposedMatrix l_Result;
            l_Result.Pose.Position = glm::vec3(matrix[3]);

            const glm::mat3 l_Basis(matrix);
            l_Result.Scale = glm::vec3(glm::length(l_Basis[0]), glm::length(l_Basis[1]), glm::length(l_Basis[2]));
            if (glm::determinant(l_Basis) < 0.0f)
            {
                l_Result.Scale.x = -l_Result.Scale.x;
            }

            if (std::abs(l_Result.Scale.x) < 1e-12f || std::abs(l_Result.Scale.y) < 1e-12f || std::abs(l_Result.Scale.z) < 1e-12f)
            {
                return l_Result;
            }

            glm::vec3 l_X = l_Basis[0] / l_Result.Scale.x;
            glm::vec3 l_Y = l_Basis[1] / l_Result.Scale.y;
            const glm::vec3 l_Z = l_Basis[2] / l_Result.Scale.z;
            l_Result.Sheared = std::abs(glm::dot(l_X, l_Y)) > c_ShearTolerance || std::abs(glm::dot(l_Y, l_Z)) > c_ShearTolerance || std::abs(glm::dot(l_X, l_Z)) > c_ShearTolerance;

            l_X = glm::normalize(l_X);
            l_Y = glm::normalize(l_Y - glm::dot(l_Y, l_X) * l_X);
            l_Result.Pose.Rotation = glm::normalize(glm::quat_cast(glm::mat3(l_X, l_Y, glm::cross(l_X, l_Y))));

            return l_Result;
        }

        glm::mat4 GetMatrix(const BodyPose& pose)
        {
            return glm::translate(glm::mat4(1.0f), pose.Position) * glm::mat4_cast(pose.Rotation);
        }

        BodyPose Interpolate(const BodyPose& from, const BodyPose& to, float factor)
        {
            return { glm::mix(from.Position, to.Position, factor), glm::normalize(glm::slerp(from.Rotation, to.Rotation, factor)) };
        }

        bool IsUneven(float first, float second)
        {
            return std::abs(first - second) > c_ScaleTolerance * std::max({ std::abs(first), std::abs(second), 1.0f });
        }

        std::string GetName(const Scene& scene, entt::entity entity)
        {
            const TagComponent* l_Tag = scene.GetRegistry().try_get<TagComponent>(entity);

            return l_Tag != nullptr ? std::string(l_Tag->Tag) : std::string("An entity");
        }

        const BodyRecord* GetRecord(const JPH::Body& body)
        {
            return reinterpret_cast<const BodyRecord*>(body.GetUserData() & ~c_SensorBit);
        }

        // The material of the solid collider a part of the body came from
        const ColliderMaterial* GetMaterial(const JPH::Body& body, const JPH::SubShapeID& part)
        {
            const BodyRecord* l_Record = GetRecord(body);
            if (l_Record == nullptr || (body.GetUserData() & c_SensorBit) != 0 || l_Record->Materials.empty())
            {
                return nullptr;
            }

            std::uint32_t l_Index = l_Record->SingleSolid;
            if (l_Record->Compound)
            {
                const JPH::CompoundShape& l_Compound = static_cast<const JPH::CompoundShape&>(*body.GetShape());
                JPH::SubShapeID l_Remainder;
                l_Index = l_Compound.GetCompoundUserData(l_Compound.GetSubShapeIndexFromID(part, l_Remainder));
            }

            return &l_Record->Materials[std::min<std::size_t>(l_Index, l_Record->Materials.size() - 1)];
        }

        // As Jolt combines them by default, with each part's own values: friction as the geometric mean, and restitution as the larger
        float CombineFriction(const JPH::Body& first, const JPH::SubShapeID& firstPart, const JPH::Body& second, const JPH::SubShapeID& secondPart)
        {
            const ColliderMaterial* l_First = GetMaterial(first, firstPart);
            const ColliderMaterial* l_Second = GetMaterial(second, secondPart);

            return std::sqrt((l_First != nullptr ? l_First->Friction : first.GetFriction()) * (l_Second != nullptr ? l_Second->Friction : second.GetFriction()));
        }

        float CombineRestitution(const JPH::Body& first, const JPH::SubShapeID& firstPart, const JPH::Body& second, const JPH::SubShapeID& secondPart)
        {
            const ColliderMaterial* l_First = GetMaterial(first, firstPart);
            const ColliderMaterial* l_Second = GetMaterial(second, secondPart);

            return std::max(l_First != nullptr ? l_First->Restitution : first.GetRestitution(), l_Second != nullptr ? l_Second->Restitution : second.GetRestitution());
        }

        bool HasCollider(const SceneRegistry& registry, entt::entity entity)
        {
            return registry.any_of<BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent, CylinderColliderComponent, ConvexHullColliderComponent, MeshColliderComponent>(entity);
        }

        void AddCollider(BodyRecord& record, ColliderSource collider)
        {
            (collider.Settings.Trigger ? record.Triggers : record.Solids).push_back(std::move(collider));
        }

        void AddColliders(const SceneRegistry& registry, entt::entity entity, BodyRecord& record)
        {
            if (const BoxColliderComponent* l_Box = registry.try_get<BoxColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::Box, *l_Box, l_Box->HalfExtents, {} });
            }

            if (const SphereColliderComponent* l_Sphere = registry.try_get<SphereColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::Sphere, *l_Sphere, glm::vec3(l_Sphere->Radius, 0.0f, 0.0f), {} });
            }

            if (const CapsuleColliderComponent* l_Capsule = registry.try_get<CapsuleColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::Capsule, *l_Capsule, glm::vec3(l_Capsule->Radius, l_Capsule->Height, 0.0f), {} });
            }

            if (const CylinderColliderComponent* l_Cylinder = registry.try_get<CylinderColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::Cylinder, *l_Cylinder, glm::vec3(l_Cylinder->Radius, l_Cylinder->Height, 0.0f), {} });
            }

            if (const ConvexHullColliderComponent* l_Hull = registry.try_get<ConvexHullColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::ConvexHull, *l_Hull, glm::vec3(0.0f), l_Hull->Shape });
            }

            if (const MeshColliderComponent* l_Mesh = registry.try_get<MeshColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::Mesh, *l_Mesh, glm::vec3(0.0f), l_Mesh->Shape });
            }
        }

        bool IsCooked(ColliderKind kind)
        {
            return kind == ColliderKind::ConvexHull || kind == ColliderKind::Mesh;
        }

        // A part of a body's shape: where it sits on the body, and which solid it came from
        struct ShapePart
        {
            JPH::Vec3 Position = JPH::Vec3::sZero();
            JPH::Quat Rotation = JPH::Quat::sIdentity();
            JPH::RefConst<JPH::Shape> Shape;
            std::uint32_t Solid = 0;
        };
    }

    std::string_view ToString(PhysicsEventType type)
    {
        switch (type)
        {
            case PhysicsEventType::ContactBegin:
                return "contact begin";
            case PhysicsEventType::ContactStay:
                return "contact stay";
            case PhysicsEventType::ContactEnd:
                return "contact end";
            case PhysicsEventType::TriggerEnter:
                return "trigger enter";
            case PhysicsEventType::TriggerExit:
                return "trigger exit";
        }

        return "unknown";
    }

    // Jolt calls the listener on its workers during a step, so what it hears is only queued, under a lock, for the main thread to sort and pair after the step
    struct PhysicsScene3D::State final : public JPH::ContactListener
    {
        explicit State(Scene& scene) : Owner(scene)
        {

        }

        // A body's own sensor never reports touching the body
        [[nodiscard]] JPH::ValidateResult OnContactValidate(const JPH::Body& first, const JPH::Body& second, [[maybe_unused]] JPH::RVec3Arg baseOffset, [[maybe_unused]] const JPH::CollideShapeResult& result) override
        {
            return GetRecord(first) != nullptr && GetRecord(first) == GetRecord(second) ? JPH::ValidateResult::RejectAllContactsForThisBodyPair : JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        }

        void OnContactAdded(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold, [[maybe_unused]] JPH::ContactSettings& settings) override
        {
            RawContact l_Contact;
            l_Contact.Added = true;
            l_Contact.First = first.GetID().GetIndexAndSequenceNumber();
            l_Contact.Second = second.GetID().GetIndexAndSequenceNumber();
            l_Contact.FirstPart = manifold.mSubShapeID1.GetValue();
            l_Contact.SecondPart = manifold.mSubShapeID2.GetValue();
            l_Contact.Normal = FromJolt(manifold.mWorldSpaceNormal);
            for (JPH::uint it_Point = 0; it_Point < manifold.mRelativeContactPointsOn1.size(); ++it_Point)
            {
                l_Contact.Point += FromJolt(JPH::Vec3(manifold.GetWorldSpaceContactPointOn1(it_Point)));
            }

            l_Contact.Point /= static_cast<float>(std::max<std::size_t>(manifold.mRelativeContactPointsOn1.size(), 1));
            Queue(l_Contact);
        }

        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
        {
            RawContact l_Contact;
            l_Contact.First = pair.GetBody1ID().GetIndexAndSequenceNumber();
            l_Contact.Second = pair.GetBody2ID().GetIndexAndSequenceNumber();
            l_Contact.FirstPart = pair.GetSubShapeID1().GetValue();
            l_Contact.SecondPart = pair.GetSubShapeID2().GetValue();
            Queue(l_Contact);
        }

        void Queue(RawContact contact)
        {
            if (contact.First > contact.Second)
            {
                std::swap(contact.First, contact.Second);
                std::swap(contact.FirstPart, contact.SecondPart);
                contact.Normal = -contact.Normal;
            }

            const std::scoped_lock l_Lock(RawMutex);
            Raw.push_back(contact);
        }

        [[nodiscard]] bool MakeBody(BodyRecord& record);
        void PairContacts(std::uint32_t index);

        Scene& Owner;
        PhysicsWorld3D World;
        JPH::PhysicsSystem* System = nullptr;
        PhysicsVector<BodyRecord> Records;
        PhysicsVector<UUID> Shapes;
        std::mutex RawMutex;
        PhysicsVector<RawContact> Raw;
        PhysicsVector<RawContact> Sorting;
        std::map<std::uint64_t, ContactPair, std::less<>, TaggedAllocator<std::pair<const std::uint64_t, ContactPair>, MemoryTag::Physics>> Pairs;
        PhysicsVector<PhysicsEvent3D> Events;
        PhysicsVector<PhysicsEvent3D> StepEvents;
        std::uint32_t Steps = 1;
        // Steps taken, so a pair begun this step does not also stay
        std::uint64_t StepCount = 0;
        std::uint32_t Waiting = 0;
        bool WarnedShear = false;
        bool WarnedRoundScale = false;
    };

    // Bodies are found in hierarchy order, so a parent's record comes before its children's, and the same scene makes the same bodies in the same order every time
    PhysicsScene3D::PhysicsScene3D(Scene& scene)
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's physics is made on the main thread.");

        m_State = Memory::New<State>(MemoryTag::Physics, scene);
        State& l_State = *m_State;
        l_State.System = &l_State.World.m_State->System;
        l_State.System->SetContactListener(&l_State);
        l_State.System->SetCombineFriction(&CombineFriction);
        l_State.System->SetCombineRestitution(&CombineRestitution);

        const SceneRegistry& l_Registry = scene.GetRegistry();
        std::unordered_map<entt::entity, std::size_t> l_Owners;
        constexpr std::size_t c_NoOwner = SIZE_MAX;
        for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            const entt::entity l_Handle = it_Entity.GetHandle();
            const entt::entity l_Parent = l_Registry.get<RelationshipComponent>(l_Handle).Parent;
            const std::size_t l_Inherited = l_Parent != entt::null ? l_Owners[l_Parent] : c_NoOwner;

            std::size_t l_Owner = l_Inherited;
            const RigidBodyComponent* l_RigidBody = l_Registry.try_get<RigidBodyComponent>(l_Handle);
            if (l_RigidBody != nullptr || (l_Inherited == c_NoOwner && HasCollider(l_Registry, l_Handle)))
            {
                BodyRecord& l_Record = l_State.Records.emplace_back();
                l_Record.Entity = l_Handle;
                l_Record.ID = l_Registry.get<IDComponent>(l_Handle).ID;
                l_Record.HasRigidBody = l_RigidBody != nullptr;
                l_Record.Motion = l_RigidBody != nullptr ? l_RigidBody->Motion : BodyMotion::Static;
                l_Record.Settings = l_RigidBody != nullptr ? *l_RigidBody : RigidBodyComponent{};
                l_Owner = l_State.Records.size() - 1;
            }

            l_Owners[l_Handle] = l_Owner;
            if (l_Owner != c_NoOwner)
            {
                AddColliders(l_Registry, l_Handle, l_State.Records[l_Owner]);
            }
        }

        for (BodyRecord& it_Record : l_State.Records)
        {
            if (it_Record.HasRigidBody && it_Record.Solids.empty() && it_Record.Triggers.empty())
            {
                TR_CORE_WARN("Physics: {} has a RigidBody and no colliders on it or below it, so it has no body", GetName(scene, it_Record.Entity));
            }

            for (const PhysicsVector<ColliderSource>* it_List : { &it_Record.Solids, &it_Record.Triggers })
            {
                for (const ColliderSource& it_Collider : *it_List)
                {
                    if (IsCooked(it_Collider.Kind) && it_Collider.Shape && std::ranges::find(l_State.Shapes, it_Collider.Shape) == l_State.Shapes.end())
                    {
                        l_State.Shapes.push_back(it_Collider.Shape);
                        AssetManager::Acquire(it_Collider.Shape);
                    }
                }
            }
        }

        std::uint32_t l_Made = 0;
        for (BodyRecord& it_Record : l_State.Records)
        {
            l_Made += l_State.MakeBody(it_Record) ? 1u : 0u;
        }

        l_State.Waiting = static_cast<std::uint32_t>(l_State.Records.size()) - l_Made;
        TR_CORE_INFO("Physics: the scene starts with {} bod(ies){}", GetBodyCount(), l_State.Waiting > 0 ? std::format(", and {} waiting for their collision shapes to load", l_State.Waiting) : std::string());
    }

    // The world goes first, and the shapes its bodies held with it, and the drawn poses with them, so the scene draws from its transforms again
    PhysicsScene3D::~PhysicsScene3D()
    {
        Scene& l_Scene = m_State->Owner;
        const PhysicsVector<UUID> l_Shapes = std::move(m_State->Shapes);
        Memory::Delete(m_State);

        for (const UUID it_Shape : l_Shapes)
        {
            AssetManager::Release(it_Shape);
        }

        l_Scene.GetRegistry().clear<PhysicsPoseComponent>();
    }

    bool PhysicsScene3D::IsNeeded(Scene& scene)
    {
        SceneRegistry& l_Registry = scene.GetRegistry();

        return !l_Registry.storage<RigidBodyComponent>().empty() || !l_Registry.storage<BoxColliderComponent>().empty() || !l_Registry.storage<SphereColliderComponent>().empty() || !l_Registry.storage<CapsuleColliderComponent>().empty() || !l_Registry.storage<CylinderColliderComponent>().empty() || !l_Registry.storage<ConvexHullColliderComponent>().empty() || !l_Registry.storage<MeshColliderComponent>().empty();
    }

    // Bodies still waiting for their shapes are tried again. A frame without steps leaves the kinematic targets where they were, so the next frame with steps moves from there
    void PhysicsScene3D::BeginFrame(std::uint32_t steps)
    {
        State& l_State = *m_State;
        l_State.Events.clear();
        if (l_State.Waiting > 0)
        {
            for (BodyRecord& it_Record : l_State.Records)
            {
                if (it_Record.Waiting && l_State.MakeBody(it_Record))
                {
                    --l_State.Waiting;
                }
            }
        }

        if (steps == 0)
        {
            return;
        }

        l_State.Steps = steps;
        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (BodyRecord& it_Record : l_State.Records)
        {
            const bool l_Follows = it_Record.Motion == BodyMotion::Kinematic || (it_Record.Motion == BodyMotion::Dynamic && it_Record.Body.IsInvalid() && !it_Record.Sensor.IsInvalid());
            if (it_Record.Waiting || !l_Follows || !l_Registry.valid(it_Record.Entity))
            {
                continue;
            }

            const DecomposedMatrix l_World = Decompose(l_State.Owner.ComputeWorldMatrix(Entity(it_Record.Entity, &l_State.Owner)));
            it_Record.TargetFrom = it_Record.TargetTo;
            it_Record.TargetTo = l_World.Pose;
        }
    }

    // Kinematic bodies and sensors are moved for the step, the world steps, events are paired, and dynamic bodies that moved are written back in hierarchy order, so each parent is placed before its children are made local to it
    void PhysicsScene3D::Step(Timestep step, std::uint32_t index)
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        JPH::BodyInterface& l_Bodies = l_State.System->GetBodyInterface();
        const float l_Seconds = step.GetSeconds();
        const float l_Factor = static_cast<float>(std::min(index + 1, l_State.Steps)) / static_cast<float>(l_State.Steps);
        for (BodyRecord& it_Record : l_State.Records)
        {
            if (it_Record.Waiting || it_Record.Motion == BodyMotion::Static)
            {
                continue;
            }

            const BodyPose l_Target = Interpolate(it_Record.TargetFrom, it_Record.TargetTo, l_Factor);
            if (it_Record.Motion == BodyMotion::Kinematic && !it_Record.Body.IsInvalid())
            {
                l_Bodies.MoveKinematic(it_Record.Body, JPH::RVec3(ToJolt(l_Target.Position)), ToJolt(l_Target.Rotation), l_Seconds);
            }

            if (!it_Record.Sensor.IsInvalid())
            {
                if (it_Record.Motion == BodyMotion::Dynamic && !it_Record.Body.IsInvalid())
                {
                    JPH::RVec3 l_Position;
                    JPH::Quat l_Rotation;
                    l_Bodies.GetPositionAndRotation(it_Record.Body, l_Position, l_Rotation);
                    l_Bodies.SetPositionAndRotation(it_Record.Sensor, l_Position, l_Rotation, JPH::EActivation::DontActivate);
                }
                else
                {
                    l_Bodies.MoveKinematic(it_Record.Sensor, JPH::RVec3(ToJolt(l_Target.Position)), ToJolt(l_Target.Rotation), l_Seconds);
                }
            }
        }

        l_State.World.Step(step);
        ++l_State.StepCount;
        l_State.PairContacts(index);

        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (BodyRecord& it_Record : l_State.Records)
        {
            if (it_Record.Waiting || it_Record.Motion == BodyMotion::Static || it_Record.Body.IsInvalid())
            {
                continue;
            }

            it_Record.Previous = it_Record.Current;
            JPH::RVec3 l_Position;
            JPH::Quat l_Rotation;
            l_Bodies.GetPositionAndRotation(it_Record.Body, l_Position, l_Rotation);
            it_Record.Current = { FromJolt(JPH::Vec3(l_Position)), FromJolt(l_Rotation) };

            if (it_Record.Motion != BodyMotion::Dynamic || !l_Registry.valid(it_Record.Entity) || (it_Record.Previous.Position == it_Record.Current.Position && it_Record.Previous.Rotation == it_Record.Current.Rotation))
            {
                continue;
            }

            TransformComponent& l_Transform = l_Registry.get<TransformComponent>(it_Record.Entity);
            const entt::entity l_Parent = l_Registry.get<RelationshipComponent>(it_Record.Entity).Parent;
            if (l_Parent == entt::null)
            {
                l_Transform.Position = it_Record.Current.Position;
                l_Transform.Rotation = it_Record.Current.Rotation;

                continue;
            }

            const glm::mat4 l_ParentWorld = l_State.Owner.ComputeWorldMatrix(Entity(l_Parent, &l_State.Owner));
            l_Transform.Position = glm::vec3(glm::inverse(l_ParentWorld) * glm::vec4(it_Record.Current.Position, 1.0f));
            l_Transform.Rotation = glm::normalize(glm::inverse(Decompose(l_ParentWorld).Pose.Rotation) * it_Record.Current.Rotation);
        }
    }

    // Moving bodies are drawn between their last two steps. A kinematic body's last step reached its transform, so it is drawn a little behind it, beside the bodies it carries
    void PhysicsScene3D::EndFrame(float interpolation)
    {
        State& l_State = *m_State;
        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (const BodyRecord& it_Record : l_State.Records)
        {
            if (it_Record.Waiting || it_Record.Motion == BodyMotion::Static || it_Record.Body.IsInvalid() || !l_Registry.valid(it_Record.Entity))
            {
                continue;
            }

            const BodyPose l_Pose = Interpolate(it_Record.Previous, it_Record.Current, interpolation);
            l_Registry.emplace_or_replace<PhysicsPoseComponent>(it_Record.Entity, l_Pose.Position, l_Pose.Rotation);
        }
    }

    std::span<const PhysicsEvent3D> PhysicsScene3D::GetEvents() const
    {
        return m_State->Events;
    }

    PhysicsWorld3D& PhysicsScene3D::GetWorld()
    {
        return m_State->World;
    }

    std::uint32_t PhysicsScene3D::GetBodyCount() const
    {
        return m_State->World.GetBodyCount();
    }

    std::uint32_t PhysicsScene3D::GetWaitingCount() const
    {
        return m_State->Waiting;
    }

    // Once every cooked shape it uses has loaded or failed. Each collider is placed relative to the body's pose, its entity's world scale taken into its shape, and the solids made one shape and the triggers another. False while it waits
    bool PhysicsScene3D::State::MakeBody(BodyRecord& record)
    {
        State& l_State = *this;
        BodyRecord& l_Record = record;
        for (const PhysicsVector<ColliderSource>* it_List : { &l_Record.Solids, &l_Record.Triggers })
        {
            for (const ColliderSource& it_Collider : *it_List)
            {
                if (IsCooked(it_Collider.Kind) && AssetManager::GetState(it_Collider.Shape) == AssetState::Loading)
                {
                    return false;
                }
            }
        }

        l_Record.Waiting = false;
        Scene& l_Scene = l_State.Owner;
        SceneRegistry& l_Registry = l_Scene.GetRegistry();
        if (!l_Registry.valid(l_Record.Entity) || (l_Record.Solids.empty() && l_Record.Triggers.empty()))
        {
            return true;
        }

        const DecomposedMatrix l_Body = Decompose(l_Scene.ComputeWorldMatrix(Entity(l_Record.Entity, &l_Scene)));
        const glm::mat4 l_ToBody = glm::inverse(GetMatrix(l_Body.Pose));
        if (l_Body.Sheared && !std::exchange(l_State.WarnedShear, true))
        {
            TR_CORE_WARN("Physics: {} is sheared by a scaled parent's turn, which a body cannot be, so it takes the nearest shape that is not. Later shears are not reported", GetName(l_Scene, l_Record.Entity));
        }

        BodyMotion l_Motion = l_Record.Motion;
        JPH::EAllowedDOFs l_Allowed = JPH::EAllowedDOFs::All;
        if (l_Motion == BodyMotion::Dynamic)
        {
            const glm::bvec3& l_Position = l_Record.Settings.LockPosition;
            const glm::bvec3& l_Rotation = l_Record.Settings.LockRotation;
            const std::uint8_t l_Locked = static_cast<std::uint8_t>((l_Position.x ? 1 : 0) | (l_Position.y ? 2 : 0) | (l_Position.z ? 4 : 0) | (l_Rotation.x ? 8 : 0) | (l_Rotation.y ? 16 : 0) | (l_Rotation.z ? 32 : 0));
            l_Allowed = static_cast<JPH::EAllowedDOFs>(static_cast<std::uint8_t>(JPH::EAllowedDOFs::All) & ~l_Locked);
            if (l_Allowed == JPH::EAllowedDOFs::None)
            {
                TR_CORE_WARN("Physics: {} is dynamic with every axis locked, so it is kinematic and stays where its transform puts it", GetName(l_Scene, l_Record.Entity));
                l_Motion = BodyMotion::Kinematic;
                l_Record.Motion = BodyMotion::Kinematic;
                l_Allowed = JPH::EAllowedDOFs::All;
            }
        }

        // A collider's shape, scaled by its entity's world scale, or nothing when it is left out
        const auto a_MakeShape = [&](const ColliderSource& collider, const glm::vec3& scale) -> JPH::RefConst<JPH::Shape>
        {
            const std::string l_Name = GetName(l_Scene, collider.Entity);
            const glm::vec3 l_Size = glm::abs(scale);
            const float l_Around = std::max(l_Size.x, l_Size.z);
            const bool l_RoundUneven = collider.Kind == ColliderKind::Sphere ? (IsUneven(l_Size.x, l_Size.y) || IsUneven(l_Size.y, l_Size.z)) : IsUneven(l_Size.x, l_Size.z);
            if ((collider.Kind == ColliderKind::Sphere || collider.Kind == ColliderKind::Capsule || collider.Kind == ColliderKind::Cylinder) && l_RoundUneven && !std::exchange(l_State.WarnedRoundScale, true))
            {
                TR_CORE_WARN("Physics: {}'s round collider is scaled unevenly across it, so it takes the larger scale. Later round colliders scaled so are not reported", l_Name);
            }

            switch (collider.Kind)
            {
                case ColliderKind::Box:
                {
                    const glm::vec3 l_HalfExtents = glm::max(collider.Size * l_Size, glm::vec3(c_MinimumSize));
                    const float l_ConvexRadius = std::min(JPH::cDefaultConvexRadius, std::min({ l_HalfExtents.x, l_HalfExtents.y, l_HalfExtents.z }) * 0.5f);

                    return new JPH::BoxShape(ToJolt(l_HalfExtents), l_ConvexRadius);
                }
                case ColliderKind::Sphere:
                {
                    return new JPH::SphereShape(std::max(collider.Size.x * std::max({ l_Size.x, l_Size.y, l_Size.z }), c_MinimumSize));
                }
                case ColliderKind::Capsule:
                {
                    const float l_Radius = std::max(collider.Size.x * l_Around, c_MinimumSize);
                    const float l_HalfSegment = std::max(collider.Size.y * 0.5f - collider.Size.x, 0.0f) * l_Size.y;
                    if (l_HalfSegment < c_MinimumSize)
                    {
                        return new JPH::SphereShape(l_Radius);
                    }

                    return new JPH::CapsuleShape(l_HalfSegment, l_Radius);
                }
                case ColliderKind::Cylinder:
                {
                    const float l_HalfHeight = std::max(collider.Size.y * 0.5f * l_Size.y, c_MinimumSize);
                    const float l_Radius = std::max(collider.Size.x * l_Around, c_MinimumSize);

                    return new JPH::CylinderShape(l_HalfHeight, l_Radius, std::min(JPH::cDefaultConvexRadius, std::min(l_HalfHeight, l_Radius) * 0.5f));
                }
                case ColliderKind::ConvexHull:
                case ColliderKind::Mesh:
                {
                    const bool l_Hull = collider.Kind == ColliderKind::ConvexHull;
                    if (!l_Hull && l_Motion != BodyMotion::Static)
                    {
                        TR_CORE_WARN("Physics: {}'s MeshCollider is left out of its {} body, since triangle meshes are for static bodies alone", l_Name, l_Motion == BodyMotion::Dynamic ? "dynamic" : "kinematic");

                        return nullptr;
                    }

                    const Asset* l_Asset = collider.Shape ? AssetManager::GetAsset(collider.Shape) : nullptr;
                    const std::string_view l_Type = l_Hull ? ConvexHullAsset::c_AssetType : CollisionMeshAsset::c_AssetType;
                    if (l_Asset == nullptr || l_Asset->GetAssetType() != l_Type || static_cast<const CollisionShapeAsset*>(l_Asset)->GetShape() == nullptr)
                    {
                        TR_CORE_WARN("Physics: {}'s {} is left out, since {}", l_Name, l_Hull ? "ConvexHullCollider" : "MeshCollider", !collider.Shape ? std::string("it has no shape") : std::format("its shape {} did not load as a {}", collider.Shape, l_Type));

                        return nullptr;
                    }

                    const JPH::Shape* l_Cooked = static_cast<const CollisionShapeAsset*>(l_Asset)->GetShape();
                    if (std::min({ l_Size.x, l_Size.y, l_Size.z }) < c_MinimumSize)
                    {
                        TR_CORE_WARN("Physics: {}'s {} is left out, since it is scaled to nothing", l_Name, l_Hull ? "ConvexHullCollider" : "MeshCollider");

                        return nullptr;
                    }

                    if (!IsUneven(scale.x, 1.0f) && !IsUneven(scale.y, 1.0f) && !IsUneven(scale.z, 1.0f))
                    {
                        return l_Cooked;
                    }

                    return new JPH::ScaledShape(l_Cooked, ToJolt(scale));
                }
            }

            return nullptr;
        };

        // The parts placed on the body, then made one shape. A single part needs no compound, and one with no offset nothing around it
        const auto a_Combine = [&](const PhysicsVector<ColliderSource>& colliders, bool& compound, std::uint32_t& single) -> JPH::RefConst<JPH::Shape>
        {
            PhysicsVector<ShapePart> l_Parts;
            for (std::size_t it_Collider = 0; it_Collider < colliders.size(); ++it_Collider)
            {
                const ColliderSource& l_Collider = colliders[it_Collider];
                const glm::mat4 l_Local = glm::translate(glm::mat4(1.0f), l_Collider.Settings.Offset) * glm::mat4_cast(l_Collider.Settings.OffsetRotation);
                const DecomposedMatrix l_Placed = Decompose(l_ToBody * l_Scene.ComputeWorldMatrix(Entity(l_Collider.Entity, &l_Scene)) * l_Local);
                if (l_Placed.Sheared && !std::exchange(l_State.WarnedShear, true))
                {
                    TR_CORE_WARN("Physics: {}'s collider is sheared by a scaled parent's turn, so it takes the nearest shape that is not. Later shears are not reported", GetName(l_Scene, l_Collider.Entity));
                }

                if (JPH::RefConst<JPH::Shape> l_Shape = a_MakeShape(l_Collider, l_Placed.Scale))
                {
                    l_Parts.push_back({ ToJolt(l_Placed.Pose.Position), ToJolt(l_Placed.Pose.Rotation), std::move(l_Shape), static_cast<std::uint32_t>(it_Collider) });
                }
            }

            compound = false;
            single = 0;
            if (l_Parts.empty())
            {
                return nullptr;
            }

            if (l_Parts.size() == 1)
            {
                const ShapePart& l_Part = l_Parts.front();
                single = l_Part.Solid;
                if (l_Part.Position.IsNearZero(1e-12f) && l_Part.Rotation.IsClose(JPH::Quat::sIdentity(), 1e-12f))
                {
                    return l_Part.Shape;
                }

                return new JPH::RotatedTranslatedShape(l_Part.Position, l_Part.Rotation, l_Part.Shape);
            }

            JPH::StaticCompoundShapeSettings l_Settings;
            for (const ShapePart& it_Part : l_Parts)
            {
                l_Settings.AddShape(it_Part.Position, it_Part.Rotation, it_Part.Shape, it_Part.Solid);
            }

            const JPH::Shape::ShapeResult l_Result = l_Settings.Create();
            if (l_Result.HasError())
            {
                TR_CORE_ERROR("Physics: {}'s colliders could not be made one shape, since {}, so it has no body", GetName(l_Scene, l_Record.Entity), l_Result.GetError().c_str());

                return nullptr;
            }

            compound = true;

            return l_Result.Get();
        };

        JPH::BodyInterface& l_Bodies = l_State.System->GetBodyInterface();
        const JPH::RVec3 l_Position(ToJolt(l_Body.Pose.Position));
        const JPH::Quat l_Rotation = ToJolt(l_Body.Pose.Rotation);
        const std::uint64_t l_UserData = reinterpret_cast<std::uint64_t>(&l_Record);

        for (const ColliderSource& it_Solid : l_Record.Solids)
        {
            l_Record.Materials.push_back({ it_Solid.Settings.Friction, it_Solid.Settings.Restitution });
        }

        bool l_Compound = false;
        std::uint32_t l_Single = 0;
        if (const JPH::RefConst<JPH::Shape> l_Shape = a_Combine(l_Record.Solids, l_Compound, l_Single))
        {
            l_Record.Compound = l_Compound;
            l_Record.SingleSolid = l_Single;

            JPH::BodyCreationSettings l_Settings(l_Shape, l_Position, l_Rotation, ToJolt(l_Motion), PhysicsLayers3D::Get(l_Motion));
            l_Settings.mUserData = l_UserData;
            if (l_Motion == BodyMotion::Dynamic)
            {
                const RigidBodyComponent& l_RigidBody = l_Record.Settings;
                l_Settings.mLinearDamping = l_RigidBody.LinearDamping;
                l_Settings.mAngularDamping = l_RigidBody.AngularDamping;
                l_Settings.mGravityFactor = l_RigidBody.GravityFactor;
                l_Settings.mMotionQuality = l_RigidBody.ContinuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
                l_Settings.mAllowedDOFs = l_Allowed;
                l_Settings.mLinearVelocity = ToJolt(l_RigidBody.InitialLinearVelocity);
                l_Settings.mAngularVelocity = ToJolt(l_RigidBody.InitialAngularVelocity);

                // A shape with no volume to weigh, such as a flat hull, gets the inertia of a box around it
                if (l_Shape->GetMassProperties().mMass < c_MinimumMass)
                {
                    TR_CORE_WARN("Physics: {}'s colliders have no volume to weigh, so it takes the inertia of a box around them and {} kg", GetName(l_Scene, l_Record.Entity), l_RigidBody.Mass > 0.0f ? l_RigidBody.Mass : c_FallbackMass);
                    l_Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
                    l_Settings.mMassPropertiesOverride.SetMassAndInertiaOfSolidBox(JPH::Vec3::sMax(l_Shape->GetLocalBounds().GetSize(), JPH::Vec3::sReplicate(c_MinimumSize)), 1000.0f);
                    l_Settings.mMassPropertiesOverride.ScaleToMass(l_RigidBody.Mass > 0.0f ? l_RigidBody.Mass : c_FallbackMass);
                }
                else if (l_RigidBody.Mass > 0.0f)
                {
                    l_Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                    l_Settings.mMassPropertiesOverride.mMass = l_RigidBody.Mass;
                }
            }

            const JPH::EActivation l_Activation = l_Motion == BodyMotion::Dynamic && !l_Record.Settings.StartAsleep ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
            l_Record.Body = l_Bodies.CreateAndAddBody(l_Settings, l_Activation);
            if (l_Record.Body.IsInvalid())
            {
                TR_CORE_ERROR("Physics: the world is full, so {} has no body", GetName(l_Scene, l_Record.Entity));
            }
        }

        // Triggers on a static body stay with it, and the rest follow it as a kinematic sensor
        bool l_SensorCompound = false;
        std::uint32_t l_SensorSingle = 0;
        if (const JPH::RefConst<JPH::Shape> l_Shape = a_Combine(l_Record.Triggers, l_SensorCompound, l_SensorSingle))
        {
            const BodyMotion l_SensorMotion = l_Motion == BodyMotion::Static ? BodyMotion::Static : BodyMotion::Kinematic;
            JPH::BodyCreationSettings l_Settings(l_Shape, l_Position, l_Rotation, ToJolt(l_SensorMotion), PhysicsLayers3D::Get(l_SensorMotion));
            l_Settings.mUserData = l_UserData | c_SensorBit;
            l_Settings.mIsSensor = true;
            l_Record.Sensor = l_Bodies.CreateAndAddBody(l_Settings, JPH::EActivation::DontActivate);
            if (l_Record.Sensor.IsInvalid())
            {
                TR_CORE_ERROR("Physics: the world is full, so {}'s triggers have no body", GetName(l_Scene, l_Record.Entity));
            }
        }

        l_Record.Previous = l_Body.Pose;
        l_Record.Current = l_Body.Pose;
        l_Record.TargetFrom = l_Body.Pose;
        l_Record.TargetTo = l_Body.Pose;

        return true;
    }

    // Jolt's adds and removes, in an order that does not depend on its workers, are counted by body pair. A pair's first part begins it and its last ends it, unless both bodies fell asleep, and a contact pair touching through the step with one body awake stays. Each step's events go out sorted by their entities
    void PhysicsScene3D::State::PairContacts(std::uint32_t index)
    {
        State& l_State = *this;
        {
            const std::scoped_lock l_Lock(l_State.RawMutex);
            std::swap(l_State.Raw, l_State.Sorting);
        }

        PhysicsVector<RawContact>& l_Raw = l_State.Sorting;
        std::ranges::sort(l_Raw, [](const RawContact& left, const RawContact& right)
        {
            return std::tie(left.First, left.Second, left.FirstPart, left.SecondPart, left.Added) < std::tie(right.First, right.Second, right.FirstPart, right.SecondPart, right.Added);
        });

        JPH::BodyInterface& l_Bodies = l_State.System->GetBodyInterfaceNoLock();
        PhysicsVector<PhysicsEvent3D>& l_Events = l_State.StepEvents;
        l_Events.clear();

        const auto a_Owner = [&l_Bodies](JPH::BodyID body) -> std::pair<const BodyRecord*, bool>
        {
            const std::uint64_t l_UserData = l_Bodies.GetUserData(body);

            return { reinterpret_cast<const BodyRecord*>(l_UserData & ~c_SensorBit), (l_UserData & c_SensorBit) != 0 };
        };

        for (std::size_t it_Raw = 0; it_Raw < l_Raw.size();)
        {
            const RawContact& l_First = l_Raw[it_Raw];
            const std::uint64_t l_Key = GetPairKey(l_First.First, l_First.Second);
            std::int64_t l_Delta = 0;
            const RawContact* l_Added = nullptr;
            for (; it_Raw < l_Raw.size() && GetPairKey(l_Raw[it_Raw].First, l_Raw[it_Raw].Second) == l_Key; ++it_Raw)
            {
                l_Delta += l_Raw[it_Raw].Added ? 1 : -1;
                l_Added = l_Added == nullptr && l_Raw[it_Raw].Added ? &l_Raw[it_Raw] : l_Added;
            }

            const auto a_Found = l_State.Pairs.find(l_Key);
            const std::uint32_t l_Before = a_Found != l_State.Pairs.end() ? a_Found->second.Count : 0;
            const std::uint32_t l_After = static_cast<std::uint32_t>(std::max<std::int64_t>(static_cast<std::int64_t>(l_Before) + l_Delta, 0));
            if (a_Found != l_State.Pairs.end() && a_Found->second.Sleeping)
            {
                if (l_After > 0)
                {
                    a_Found->second.Sleeping = false;
                    a_Found->second.Count = l_After;
                }

                continue;
            }

            if (l_Before == 0 && l_After > 0)
            {
                const JPH::BodyID l_FirstBody(l_First.First);
                const JPH::BodyID l_SecondBody(l_First.Second);
                const auto [l_FirstRecord, l_FirstSensor] = a_Owner(l_FirstBody);
                const auto [l_SecondRecord, l_SecondSensor] = a_Owner(l_SecondBody);
                if (l_FirstRecord == nullptr || l_SecondRecord == nullptr)
                {
                    continue;
                }

                ContactPair l_Pair;
                l_Pair.Count = l_After;
                l_Pair.Begun = l_State.StepCount;
                l_Pair.FirstBody = l_FirstBody;
                l_Pair.SecondBody = l_SecondBody;
                l_Pair.Trigger = l_FirstSensor || l_SecondSensor;
                l_Pair.Swapped = l_Pair.Trigger ? !l_FirstSensor : l_SecondRecord->ID < l_FirstRecord->ID;
                l_Pair.First = l_Pair.Swapped ? l_SecondRecord->ID : l_FirstRecord->ID;
                l_Pair.Second = l_Pair.Swapped ? l_FirstRecord->ID : l_SecondRecord->ID;
                l_State.Pairs.insert_or_assign(l_Key, l_Pair);

                PhysicsEvent3D l_Event{ l_Pair.Trigger ? PhysicsEventType::TriggerEnter : PhysicsEventType::ContactBegin, l_Pair.First, l_Pair.Second, glm::vec3(0.0f), glm::vec3(0.0f), index };
                if (!l_Pair.Trigger && l_Added != nullptr)
                {
                    l_Event.Point = l_Added->Point;
                    l_Event.Normal = l_Pair.Swapped ? -l_Added->Normal : l_Added->Normal;
                }

                l_Events.push_back(l_Event);
            }
            else if (l_Before > 0 && l_After == 0)
            {
                ContactPair& l_Pair = a_Found->second;
                if (!l_Bodies.IsActive(l_Pair.FirstBody) && !l_Bodies.IsActive(l_Pair.SecondBody))
                {
                    l_Pair.Count = 0;
                    l_Pair.Sleeping = true;
                    l_Pair.Woken = 0;

                    continue;
                }

                l_Events.push_back({ l_Pair.Trigger ? PhysicsEventType::TriggerExit : PhysicsEventType::ContactEnd, l_Pair.First, l_Pair.Second, glm::vec3(0.0f), glm::vec3(0.0f), index });
                l_State.Pairs.erase(a_Found);
            }
            else if (a_Found != l_State.Pairs.end())
            {
                a_Found->second.Count = l_After;
            }
        }

        l_Raw.clear();
        for (auto a_Pair = l_State.Pairs.begin(); a_Pair != l_State.Pairs.end();)
        {
            ContactPair& l_Pair = a_Pair->second;
            const bool l_Awake = l_Bodies.IsActive(l_Pair.FirstBody) || l_Bodies.IsActive(l_Pair.SecondBody);
            if (l_Pair.Sleeping)
            {
                l_Pair.Woken = l_Awake ? (l_Pair.Woken == 0 ? l_State.StepCount : l_Pair.Woken) : 0;
                if (l_Pair.Woken != 0 && l_State.StepCount > l_Pair.Woken)
                {
                    l_Events.push_back({ l_Pair.Trigger ? PhysicsEventType::TriggerExit : PhysicsEventType::ContactEnd, l_Pair.First, l_Pair.Second, glm::vec3(0.0f), glm::vec3(0.0f), index });
                    a_Pair = l_State.Pairs.erase(a_Pair);

                    continue;
                }
            }
            else if (!l_Pair.Trigger && l_Pair.Begun != l_State.StepCount && l_Awake)
            {
                l_Events.push_back({ PhysicsEventType::ContactStay, l_Pair.First, l_Pair.Second, glm::vec3(0.0f), glm::vec3(0.0f), index });
            }

            ++a_Pair;
        }

        std::ranges::stable_sort(l_Events, [](const PhysicsEvent3D& left, const PhysicsEvent3D& right) { return std::tie(left.First, left.Second, left.Type) < std::tie(right.First, right.Second, right.Type); });
        l_State.Events.insert(l_State.Events.end(), l_Events.begin(), l_Events.end());
    }
}