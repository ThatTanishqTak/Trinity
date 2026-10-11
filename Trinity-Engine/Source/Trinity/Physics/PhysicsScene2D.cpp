#include "Trinity/Physics/PhysicsWorld2DState.hpp"
#include "Trinity/Physics/PhysicsScene2D.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        template<typename T>
        using PhysicsVector = std::vector<T, TaggedAllocator<T, MemoryTag::Physics>>;

        // Shapes smaller than this are taken as this, since Box2D refuses empty ones
        constexpr float c_MinimumSize = 1e-3f;
        // Scales this close are taken as one, and an axis tilted this far out of the XY plane as tilted
        constexpr float c_ScaleTolerance = 1e-4f;
        constexpr float c_TiltTolerance = 1e-3f;
        // A ray's hit is found again from this fraction of its distance, and a metre's, before it
        constexpr float c_RefineMargin = 1e-3f;
        // A body's index among the records, for the pair a contact or trigger is counted by
        constexpr std::size_t c_NoOwner = SIZE_MAX;
        constexpr std::size_t c_Owned3D = SIZE_MAX - 1;
        // A joint's other body when it joins the world
        constexpr std::size_t c_World = SIZE_MAX - 2;
        // Box2D's linear slop, within which a contact point counts as touching
        constexpr float c_TouchingSlop = 0.005f;
        // Box2D's own bounds on a revolute joint's limits
        constexpr float c_MaxLimitAngle = 0.99f * std::numbers::pi_v<float>;

        enum class ColliderKind2D : std::uint8_t
        {
            Box,
            Circle,
            Capsule,
            Polygon
        };

        // A 2D collider as it was read when the scene started: a box's half extents, a circle's radius, a capsule's radius and height, or a polygon's points and rounding, and its entity's UUID, which events and queries report
        struct ColliderSource2D
        {
            entt::entity Entity = entt::null;
            UUID ID;
            ColliderKind2D Kind = ColliderKind2D::Box;
            Collider2DSettings Settings;
            glm::vec2 Size{ 0.0f };
            PhysicsVector<glm::vec2> Points;
        };

        // A place in the XY plane and a turn about Z, counter-clockwise in radians
        struct Pose2D
        {
            glm::vec2 Position{ 0.0f };
            float Angle = 0.0f;
        };

        // A body's owner and what it was made from. Records and their colliders are all made before any body, and never move after, since bodies and shapes point at them
        struct BodyRecord2D
        {
            entt::entity Entity = entt::null;
            UUID ID;
            BodyMotion Motion = BodyMotion::Static;
            bool HasRigidBody = false;
            RigidBody2DComponent Settings;
            PhysicsVector<ColliderSource2D> Colliders;
            b2BodyId Body = b2_nullBodyId;
            // The entity's world Z as the scene started, which write-back keeps
            float Depth = 0.0f;
            Pose2D Previous;
            Pose2D Current;
            // Where its entity's transform puts it, last frame and this one, which a kinematic body moves between over the frame's steps
            Pose2D TargetFrom;
            Pose2D TargetTo;
        };

        // A joint as it was read when the scene started: its entity, the entity it names and the records of its two bodies, the second none for the world, and its Box2D joint once made. A joint that cannot be made is dropped, and one that breaks is let go
        struct JointRecord2D
        {
            entt::entity Entity = entt::null;
            UUID ID;
            entt::entity OtherEntity = entt::null;
            UUID Other;
            Joint2DComponent Settings;
            std::size_t First = 0;
            std::size_t Second = c_World;
            b2JointId Joint = b2_nullJointId;
            // A prismatic joint's axis on its first Box2D body, which the motor's force is along
            b2Vec2 LocalAxis{ 0.0f, 1.0f };
        };

        std::string_view GetJointTypeName(Joint2DType type)
        {
            switch (type)
            {
                case Joint2DType::Revolute:
                    return "revolute";
                case Joint2DType::Prismatic:
                    return "prismatic";
                case Joint2DType::Distance:
                    return "distance";
                case Joint2DType::Weld:
                    return "weld";
                case Joint2DType::Wheel:
                    return "wheel";
            }

            return "2D";
        }

        // Two bodies touching by as many shape pairs as Count says, since the step Begun counts to. Box2D keeps the contacts of bodies that fall asleep, so a pair ends only when its last shape pair does
        struct ContactPair2D
        {
            std::uint32_t Count = 0;
            std::uint64_t Begun = 0;
            UUID First;
            UUID Second;
            std::size_t FirstRecord = 0;
            std::size_t SecondRecord = 0;
        };

        // A shape pair Box2D began or ended touching in a step, by the records of their bodies, with a contact's first point and normal from the first record to the second
        struct RawContact2D
        {
            std::size_t First = 0;
            std::size_t Second = 0;
            bool Added = false;
            glm::vec2 Point{ 0.0f };
            glm::vec2 Normal{ 0.0f };
        };

        std::uint64_t GetPairKey(std::size_t first, std::size_t second)
        {
            return (static_cast<std::uint64_t>(first) << 32) | static_cast<std::uint64_t>(second);
        }

        std::string GetName(const Scene& scene, entt::entity entity)
        {
            const TagComponent* l_Tag = scene.GetRegistry().try_get<TagComponent>(entity);

            return l_Tag != nullptr ? std::string(l_Tag->Tag) : std::string("An entity");
        }

        b2Vec2 ToBox2D(const glm::vec2& vector)
        {
            return { vector.x, vector.y };
        }

        glm::vec2 FromBox2D(b2Vec2 vector)
        {
            return { vector.x, vector.y };
        }

        glm::vec2 Rotate(const glm::vec2& vector, float angle)
        {
            const float l_Cos = std::cos(angle);
            const float l_Sin = std::sin(angle);

            return { l_Cos * vector.x - l_Sin * vector.y, l_Sin * vector.x + l_Cos * vector.y };
        }

        // The pose a world matrix gives in the XY plane: its place, and the turn of its X axis about Z
        Pose2D GetPose(const glm::mat4& matrix)
        {
            return { glm::vec2(matrix[3]), std::atan2(matrix[0][1], matrix[0][0]) };
        }

        // Whether the matrix's Z axis leaves Z, which a 2D body cannot follow
        bool IsTilted(const glm::mat4& matrix)
        {
            const glm::vec3 l_Z(matrix[2]);
            const float l_Length = glm::length(l_Z);

            return l_Length > 1e-6f && std::abs(std::abs(l_Z.z) / l_Length - 1.0f) > c_TiltTolerance;
        }

        // The world turn of a matrix's axes made square, for a parent's world rotation
        glm::quat GetRotation(const glm::mat4& matrix)
        {
            glm::vec3 l_X(matrix[0]);
            glm::vec3 l_Y(matrix[1]);
            if (glm::length(l_X) < 1e-12f || glm::length(l_Y) < 1e-12f)
            {
                return glm::identity<glm::quat>();
            }

            l_X = glm::normalize(l_X);
            l_Y = glm::normalize(l_Y - glm::dot(l_Y, l_X) * l_X);

            return glm::normalize(glm::quat_cast(glm::mat3(l_X, l_Y, glm::cross(l_X, l_Y))));
        }

        // The shortest way round from one angle to another
        float LerpAngle(float from, float to, float factor)
        {
            const float l_Difference = std::remainder(to - from, 2.0f * std::numbers::pi_v<float>);

            return from + l_Difference * factor;
        }

        Pose2D Interpolate(const Pose2D& from, const Pose2D& to, float factor)
        {
            return { glm::mix(from.Position, to.Position, factor), LerpAngle(from.Angle, to.Angle, factor) };
        }

        b2Transform ToBox2D(const Pose2D& pose)
        {
            return { ToBox2D(pose.Position), Box2DConvert::ToRotation(pose.Angle) };
        }

        Pose2D ReadPose(b2BodyId body)
        {
            return { FromBox2D(b2Body_GetPosition(body)), Box2DConvert::ToAngle(b2Body_GetRotation(body)) };
        }

        bool Has2DCollider(const SceneRegistry& registry, entt::entity entity)
        {
            return registry.any_of<BoxCollider2DComponent, CircleCollider2DComponent, CapsuleCollider2DComponent, PolygonCollider2DComponent>(entity);
        }

        // A layer past the last there can be is taken as the first, as the scene's file reads it
        void AddCollider(BodyRecord2D& record, ColliderSource2D collider)
        {
            collider.Settings.Layer = collider.Settings.Layer < c_MaxCollisionLayers ? collider.Settings.Layer : 0;
            record.Colliders.push_back(std::move(collider));
        }

        void AddColliders(const SceneRegistry& registry, entt::entity entity, BodyRecord2D& record)
        {
            const UUID l_ID = registry.get<IDComponent>(entity).ID;
            if (const BoxCollider2DComponent* l_Box = registry.try_get<BoxCollider2DComponent>(entity))
            {
                AddCollider(record, { entity, l_ID, ColliderKind2D::Box, *l_Box, l_Box->HalfExtents, {} });
            }

            if (const CircleCollider2DComponent* l_Circle = registry.try_get<CircleCollider2DComponent>(entity))
            {
                AddCollider(record, { entity, l_ID, ColliderKind2D::Circle, *l_Circle, glm::vec2(l_Circle->Radius, 0.0f), {} });
            }

            if (const CapsuleCollider2DComponent* l_Capsule = registry.try_get<CapsuleCollider2DComponent>(entity))
            {
                AddCollider(record, { entity, l_ID, ColliderKind2D::Capsule, *l_Capsule, glm::vec2(l_Capsule->Radius, l_Capsule->Height), {} });
            }

            if (const PolygonCollider2DComponent* l_Polygon = registry.try_get<PolygonCollider2DComponent>(entity))
            {
                AddCollider(record, { entity, l_ID, ColliderKind2D::Polygon, *l_Polygon, glm::vec2(l_Polygon->Radius, 0.0f), PhysicsVector<glm::vec2>(l_Polygon->Points.begin(), l_Polygon->Points.end()) });
            }
        }

        const BodyRecord2D* GetRecord(b2BodyId body)
        {
            return static_cast<const BodyRecord2D*>(b2Body_GetUserData(body));
        }

        const ColliderSource2D* GetCollider(b2ShapeId shape)
        {
            return static_cast<const ColliderSource2D*>(b2Shape_GetUserData(shape));
        }

        // The query's own category, which every shape's mask has, and the layers it asks for
        b2QueryFilter MakeQueryFilter(const PhysicsQueryFilter2D& filter)
        {
            return { Box2DLayers::c_QueryBit, static_cast<std::uint64_t>(filter.Layers & c_AllCollisionLayers) };
        }

        // Sized as the 2D colliders are made, placed and turned in the world
        b2ShapeProxy MakeProxy(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle)
        {
            const b2Vec2 l_Position = ToBox2D(position);
            const b2Rot l_Rotation = Box2DConvert::ToRotation(angle);
            switch (shape.Type)
            {
                case PhysicsQueryShapeType2D::Box:
                {
                    const glm::vec2 l_Half = glm::max(glm::abs(shape.HalfExtents), glm::vec2(c_MinimumSize));
                    const std::array<b2Vec2, 4> l_Corners{ { { -l_Half.x, -l_Half.y }, { l_Half.x, -l_Half.y }, { l_Half.x, l_Half.y }, { -l_Half.x, l_Half.y } } };

                    return b2MakeOffsetProxy(l_Corners.data(), 4, 0.0f, l_Position, l_Rotation);
                }
                case PhysicsQueryShapeType2D::Capsule:
                {
                    const float l_HalfSegment = std::max(shape.Height * 0.5f - shape.Radius, 0.0f);
                    const std::array<b2Vec2, 2> l_Ends{ { { 0.0f, -l_HalfSegment }, { 0.0f, l_HalfSegment } } };

                    return b2MakeOffsetProxy(l_Ends.data(), l_HalfSegment > 0.0f ? 2 : 1, std::max(shape.Radius, c_MinimumSize), l_Position, l_Rotation);
                }
                case PhysicsQueryShapeType2D::Circle:
                {
                    break;
                }
            }

            const b2Vec2 l_Centre{ 0.0f, 0.0f };

            return b2MakeOffsetProxy(&l_Centre, 1, std::max(shape.Radius, c_MinimumSize), l_Position, l_Rotation);
        }

        // A collider's shape in the world, as its body last stepped to
        b2ShapeProxy MakeProxy(b2ShapeId shape)
        {
            const b2Transform l_Transform = b2Body_GetTransform(b2Shape_GetBody(shape));
            switch (b2Shape_GetType(shape))
            {
                case b2_circleShape:
                {
                    const b2Circle l_Circle = b2Shape_GetCircle(shape);
                    const b2Vec2 l_Centre = b2TransformPoint(l_Transform, l_Circle.center);

                    return b2MakeProxy(&l_Centre, 1, l_Circle.radius);
                }
                case b2_capsuleShape:
                {
                    const b2Capsule l_Capsule = b2Shape_GetCapsule(shape);
                    const std::array<b2Vec2, 2> l_Ends{ { b2TransformPoint(l_Transform, l_Capsule.center1), b2TransformPoint(l_Transform, l_Capsule.center2) } };

                    return b2MakeProxy(l_Ends.data(), 2, l_Capsule.radius);
                }
                default:
                {
                    break;
                }
            }

            const b2Polygon l_Polygon = b2Shape_GetPolygon(shape);
            std::array<b2Vec2, B2_MAX_POLYGON_VERTICES> l_Points{};
            for (int it = 0; it < l_Polygon.count; ++it)
            {
                l_Points[static_cast<std::size_t>(it)] = b2TransformPoint(l_Transform, l_Polygon.vertices[it]);
            }

            return b2MakeProxy(l_Points.data(), l_Polygon.count, l_Polygon.radius);
        }

        // Box2D stops a cast within its linear slop of the collider, short of a sharp one and past a round one, where a 3D cast stops at the touch. So the hit is moved along the cast by the gap left between the two, measured without the slop, a few times over for a slanted touch, and takes its point and normal from that measure
        void SettleCast(PhysicsHit2D& hit, b2ShapeId target, const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const glm::vec2& direction, float maxDistance)
        {
            constexpr int c_Passes = 4;
            constexpr float c_Touching = 1e-6f;
            constexpr float c_Grazing = 1e-3f;

            b2DistanceInput l_Input{};
            l_Input.proxyB = MakeProxy(target);
            l_Input.transformA = b2Transform_identity;
            l_Input.transformB = b2Transform_identity;
            l_Input.useRadii = false;

            for (int it = 0; it < c_Passes; ++it)
            {
                l_Input.proxyA = MakeProxy(shape, position + direction * hit.Distance, angle);
                b2SimplexCache l_Cache{};
                const b2DistanceOutput l_Output = b2ShapeDistance(&l_Input, &l_Cache, nullptr, 0);
                // Cores that meet leave no normal to go by
                if (!(l_Output.distance > 0.0f))
                {
                    return;
                }

                const glm::vec2 l_Normal = FromBox2D(l_Output.normal);
                hit.Normal = -l_Normal;
                hit.Point = FromBox2D(l_Output.pointB) - l_Normal * l_Input.proxyB.radius;

                const float l_Gap = l_Output.distance - l_Input.proxyA.radius - l_Input.proxyB.radius;
                const float l_Closing = glm::dot(direction, l_Normal);
                if (std::abs(l_Gap) <= c_Touching || l_Closing <= c_Grazing)
                {
                    return;
                }

                hit.Distance = std::clamp(hit.Distance + l_Gap / l_Closing, 0.0f, maxDistance);
            }
        }

        // Nearest first, then by UUID, with only the nearest hit of each entity kept
        void SortHits(std::vector<PhysicsHit2D>& hits)
        {
            std::ranges::sort(hits, [](const PhysicsHit2D& left, const PhysicsHit2D& right) { return std::tie(left.Distance, left.Entity) < std::tie(right.Distance, right.Entity); });

            std::vector<PhysicsHit2D> l_Kept;
            l_Kept.reserve(hits.size());
            for (const PhysicsHit2D& it_Hit : hits)
            {
                if (std::ranges::find(l_Kept, it_Hit.Entity, &PhysicsHit2D::Entity) == l_Kept.end())
                {
                    l_Kept.push_back(it_Hit);
                }
            }

            hits = std::move(l_Kept);
        }

        // What a cast gathers: the filter it keeps to, whether it wants every hit or the nearest, and the hits with their shapes
        struct CastContext
        {
            const PhysicsQueryFilter2D* Filter = nullptr;
            bool All = false;
            std::vector<std::pair<PhysicsHit2D, b2ShapeId>> Hits;
        };

        // The shape's collider and body, or nothing for a shape the filter leaves out: a trigger it did not ask for, or one of the entity it ignores
        std::optional<PhysicsHit2D> MakeHit(b2ShapeId shape, const PhysicsQueryFilter2D& filter)
        {
            const ColliderSource2D* l_Collider = GetCollider(shape);
            const BodyRecord2D* l_Record = GetRecord(b2Shape_GetBody(shape));
            if (l_Collider == nullptr || l_Record == nullptr || l_Collider->ID == filter.Ignore || l_Record->ID == filter.Ignore || (l_Collider->Settings.Trigger && !filter.HitTriggers))
            {
                return std::nullopt;
            }

            PhysicsHit2D l_Hit;
            l_Hit.Entity = l_Collider->ID;
            l_Hit.Body = l_Record->ID;
            l_Hit.Trigger = l_Collider->Settings.Trigger;

            return l_Hit;
        }

        // Box2D takes -1 to pass a shape over, the hit's fraction to look no further than it, and 1 to go on to the end
        float GatherCast(b2ShapeId shape, b2Vec2 point, b2Vec2 normal, float fraction, void* context)
        {
            CastContext& l_Context = *static_cast<CastContext*>(context);
            std::optional<PhysicsHit2D> l_Hit = MakeHit(shape, *l_Context.Filter);
            if (!l_Hit)
            {
                return -1.0f;
            }

            l_Hit->Point = FromBox2D(point);
            l_Hit->Normal = FromBox2D(normal);
            l_Hit->Distance = fraction;
            l_Context.Hits.emplace_back(*l_Hit, shape);

            return l_Context.All ? 1.0f : fraction;
        }

        struct OverlapContext
        {
            const PhysicsQueryFilter2D* Filter = nullptr;
            std::vector<PhysicsHit2D> Hits;
        };

        bool GatherOverlap(b2ShapeId shape, void* context)
        {
            OverlapContext& l_Context = *static_cast<OverlapContext*>(context);
            if (const std::optional<PhysicsHit2D> l_Hit = MakeHit(shape, *l_Context.Filter))
            {
                l_Context.Hits.push_back(*l_Hit);
            }

            return true;
        }
    }

    struct PhysicsScene2D::State
    {
        State(Scene& scene, const CollisionLayers& layers) : Owner(scene), World(GetWorldSettings(layers))
        {

        }

        [[nodiscard]] static PhysicsWorldSettings2D GetWorldSettings(const CollisionLayers& layers)
        {
            PhysicsWorldSettings2D l_Settings;
            l_Settings.Layers = layers;

            return l_Settings;
        }

        void MakeBody(BodyRecord2D& record);
        void MakeJoint(JointRecord2D& joint);
        void PairEvents(std::uint32_t index);
        void BreakJoints(std::uint32_t index);
        void GatherContactPoints();

        // A static body with no shapes at the origin, made the first time a joint joins the world, since a Box2D joint is always between two bodies
        [[nodiscard]] b2BodyId GetGround()
        {
            if (B2_IS_NULL(Ground))
            {
                b2BodyDef l_Ground = b2DefaultBodyDef();
                l_Ground.type = b2_staticBody;
                Ground = b2CreateBody(WorldID, &l_Ground);
            }

            return Ground;
        }

        [[nodiscard]] std::vector<PhysicsHit2D> CastRay(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter, bool all) const;
        [[nodiscard]] std::vector<PhysicsHit2D> CastShape(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter, bool all) const;

        [[nodiscard]] std::size_t GetIndex(b2ShapeId shape) const
        {
            return static_cast<std::size_t>(GetRecord(b2Shape_GetBody(shape)) - Records.data());
        }

        Scene& Owner;
        PhysicsWorld2D World;
        // The world's Box2D world and layers, which the scene, a friend of the world, takes when it is made
        b2WorldId WorldID = b2_nullWorldId;
        CollisionLayers Layers;
        PhysicsVector<BodyRecord2D> Records;
        PhysicsVector<JointRecord2D> Joints;
        b2BodyId Ground = b2_nullBodyId;
        std::map<std::uint64_t, ContactPair2D, std::less<>, TaggedAllocator<std::pair<const std::uint64_t, ContactPair2D>, MemoryTag::Physics>> Contacts;
        std::map<std::uint64_t, ContactPair2D, std::less<>, TaggedAllocator<std::pair<const std::uint64_t, ContactPair2D>, MemoryTag::Physics>> Triggers;
        PhysicsVector<RawContact2D> Raw;
        PhysicsVector<PhysicsEvent2D> Events;
        PhysicsVector<PhysicsEvent2D> StepEvents;
        // The last step's contact points, kept only while asked for, and the buffer a body's contacts are read into
        PhysicsVector<PhysicsContactPoint> ContactPoints;
        PhysicsVector<b2ContactData> ContactData;
        bool GatherContacts = false;
        std::uint32_t Steps = 1;
        std::uint64_t StepCount = 0;
        bool WarnedTilt = false;
        bool WarnedRoundScale = false;
    };

    // Bodies are found in hierarchy order, as in 3D. An entity with a RigidBody, and everything below it, belongs to the 3D world, so 2D components there are reported and left out
    PhysicsScene2D::PhysicsScene2D(Scene& scene, const CollisionLayers& layers)
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's physics is made on the main thread.");

        m_State = Memory::New<State>(MemoryTag::Physics, scene, layers);
        State& l_State = *m_State;
        l_State.WorldID = l_State.World.m_State->World;
        l_State.Layers = layers;

        const SceneRegistry& l_Registry = scene.GetRegistry();
        std::unordered_map<entt::entity, std::size_t> l_Owners;
        std::vector<std::pair<entt::entity, std::size_t>> l_Joined;
        for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            const entt::entity l_Handle = it_Entity.GetHandle();
            const entt::entity l_Parent = l_Registry.get<RelationshipComponent>(l_Handle).Parent;
            const std::size_t l_Inherited = l_Parent != entt::null ? l_Owners[l_Parent] : c_NoOwner;
            const bool l_Body3D = l_Registry.all_of<RigidBodyComponent>(l_Handle);
            const RigidBody2DComponent* l_RigidBody = l_Registry.try_get<RigidBody2DComponent>(l_Handle);

            std::size_t l_Owner = l_Inherited;
            if (l_Body3D)
            {
                if (l_RigidBody != nullptr)
                {
                    TR_CORE_WARN("Physics: {} has both a RigidBody and a RigidBody2D, so it keeps its 3D body and the 2D one is left out", GetName(scene, l_Handle));
                }

                l_Owner = c_Owned3D;
            }
            else if (l_RigidBody != nullptr || (l_Inherited == c_NoOwner && Has2DCollider(l_Registry, l_Handle)))
            {
                BodyRecord2D& l_Record = l_State.Records.emplace_back();
                l_Record.Entity = l_Handle;
                l_Record.ID = l_Registry.get<IDComponent>(l_Handle).ID;
                l_Record.HasRigidBody = l_RigidBody != nullptr;
                l_Record.Motion = l_RigidBody != nullptr ? l_RigidBody->Motion : BodyMotion::Static;
                l_Record.Settings = l_RigidBody != nullptr ? *l_RigidBody : RigidBody2DComponent{};
                l_Owner = l_State.Records.size() - 1;
            }

            l_Owners[l_Handle] = l_Owner;
            if (l_Owner == c_Owned3D && Has2DCollider(l_Registry, l_Handle))
            {
                TR_CORE_WARN("Physics: {}'s 2D colliders are part of a 3D body, so they are left out", GetName(scene, l_Handle));
            }
            else if (l_Owner != c_NoOwner && l_Owner != c_Owned3D)
            {
                AddColliders(l_Registry, l_Handle, l_State.Records[l_Owner]);
            }

            if (l_Registry.all_of<Joint2DComponent>(l_Handle))
            {
                l_Joined.emplace_back(l_Handle, l_Owner);
            }
        }

        for (BodyRecord2D& it_Record : l_State.Records)
        {
            if (it_Record.HasRigidBody && it_Record.Colliders.empty())
            {
                TR_CORE_WARN("Physics: {} has a RigidBody2D and no 2D colliders on it or below it, so it has no body", GetName(scene, it_Record.Entity));

                continue;
            }

            l_State.MakeBody(it_Record);
        }

        // A joint is on the body its entity is part of, and names the body of the entity it joins. One on no 2D body, or naming an entity that is not in the scene or is part of no other 2D body, is reported and left out
        for (const auto& [it_Entity, it_Owner] : l_Joined)
        {
            const Joint2DComponent& l_Joint = l_Registry.get<Joint2DComponent>(it_Entity);
            const std::string l_Name = GetName(scene, it_Entity);
            if (it_Owner == c_NoOwner || it_Owner == c_Owned3D)
            {
                TR_CORE_WARN("Physics: {}'s Joint2D is on no 2D body, since nothing on it or above it has a RigidBody2D or a 2D collider first, so it is left out", l_Name);

                continue;
            }

            std::size_t l_Second = c_World;
            entt::entity l_OtherEntity = entt::null;
            if (l_Joint.ConnectedBody)
            {
                const Entity l_Connected = scene.FindEntityByUUID(l_Joint.ConnectedBody);
                if (!l_Connected)
                {
                    TR_CORE_WARN("Physics: {}'s Joint2D names entity {}, which is not in the scene, so it is left out", l_Name, l_Joint.ConnectedBody);

                    continue;
                }

                const auto a_Owner = l_Owners.find(l_Connected.GetHandle());
                if (a_Owner == l_Owners.end() || a_Owner->second == c_NoOwner || a_Owner->second == c_Owned3D || a_Owner->second == it_Owner)
                {
                    TR_CORE_WARN("Physics: {}'s Joint2D names {}, which is {}, so it is left out", l_Name, GetName(scene, l_Connected.GetHandle()), a_Owner != l_Owners.end() && a_Owner->second == it_Owner ? "part of its own body" : "part of no 2D body");

                    continue;
                }

                l_Second = a_Owner->second;
                l_OtherEntity = l_Connected.GetHandle();
            }

            JointRecord2D& l_Record = l_State.Joints.emplace_back();
            l_Record.Entity = it_Entity;
            l_Record.ID = l_Registry.get<IDComponent>(it_Entity).ID;
            l_Record.OtherEntity = l_OtherEntity;
            l_Record.Other = l_Second != c_World ? l_Joint.ConnectedBody : UUID();
            l_Record.Settings = l_Joint;
            l_Record.First = it_Owner;
            l_Record.Second = l_Second;
        }

        for (JointRecord2D& it_Joint : l_State.Joints)
        {
            l_State.MakeJoint(it_Joint);
        }

        TR_CORE_INFO("Physics: the 2D scene starts with {} bod(ies)", GetBodyCount());
    }

    // The world goes first, and the drawn poses with it, so the scene draws from its transforms again
    PhysicsScene2D::~PhysicsScene2D()
    {
        Scene& l_Scene = m_State->Owner;
        Memory::Delete(m_State);
        l_Scene.GetRegistry().clear<PhysicsPoseComponent>();
    }

    bool PhysicsScene2D::IsNeeded(Scene& scene)
    {
        SceneRegistry& l_Registry = scene.GetRegistry();

        return !l_Registry.storage<RigidBody2DComponent>().empty() || !l_Registry.storage<BoxCollider2DComponent>().empty() || !l_Registry.storage<CircleCollider2DComponent>().empty() || !l_Registry.storage<CapsuleCollider2DComponent>().empty() || !l_Registry.storage<PolygonCollider2DComponent>().empty();
    }

    // A frame without steps leaves the kinematic targets where they were, so the next frame with steps moves from there
    void PhysicsScene2D::BeginFrame(std::uint32_t steps)
    {
        State& l_State = *m_State;
        l_State.Events.clear();
        if (steps == 0)
        {
            return;
        }

        l_State.Steps = steps;
        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (BodyRecord2D& it_Record : l_State.Records)
        {
            if (it_Record.Motion != BodyMotion::Kinematic || B2_IS_NULL(it_Record.Body) || !l_Registry.valid(it_Record.Entity))
            {
                continue;
            }

            it_Record.TargetFrom = it_Record.TargetTo;
            it_Record.TargetTo = GetPose(l_State.Owner.ComputeWorldMatrix(Entity(it_Record.Entity, &l_State.Owner)));
        }
    }

    // Kinematic bodies are moved for the step, the world steps, events are paired, and dynamic bodies that moved are written back in hierarchy order, each at its depth and turned about Z alone
    void PhysicsScene2D::Step(Timestep step, std::uint32_t index)
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        const float l_Factor = static_cast<float>(std::min(index + 1, l_State.Steps)) / static_cast<float>(l_State.Steps);
        for (BodyRecord2D& it_Record : l_State.Records)
        {
            if (it_Record.Motion == BodyMotion::Kinematic && B2_IS_NON_NULL(it_Record.Body))
            {
                b2Body_SetTargetTransform(it_Record.Body, ToBox2D(Interpolate(it_Record.TargetFrom, it_Record.TargetTo, l_Factor)), step.GetSeconds());
            }
        }

        l_State.World.Step(step);
        ++l_State.StepCount;
        l_State.PairEvents(index);
        l_State.BreakJoints(index);
        l_State.GatherContactPoints();

        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (BodyRecord2D& it_Record : l_State.Records)
        {
            if (it_Record.Motion == BodyMotion::Static || B2_IS_NULL(it_Record.Body))
            {
                continue;
            }

            it_Record.Previous = it_Record.Current;
            it_Record.Current = ReadPose(it_Record.Body);
            if (it_Record.Motion != BodyMotion::Dynamic || !l_Registry.valid(it_Record.Entity) || (it_Record.Previous.Position == it_Record.Current.Position && it_Record.Previous.Angle == it_Record.Current.Angle))
            {
                continue;
            }

            TransformComponent& l_Transform = l_Registry.get<TransformComponent>(it_Record.Entity);
            const glm::vec3 l_Position(it_Record.Current.Position, it_Record.Depth);
            const glm::quat l_Rotation = glm::angleAxis(it_Record.Current.Angle, glm::vec3(0.0f, 0.0f, 1.0f));
            const entt::entity l_Parent = l_Registry.get<RelationshipComponent>(it_Record.Entity).Parent;
            if (l_Parent == entt::null)
            {
                l_Transform.Position = l_Position;
                l_Transform.Rotation = l_Rotation;

                continue;
            }

            const glm::mat4 l_ParentWorld = l_State.Owner.ComputeWorldMatrix(Entity(l_Parent, &l_State.Owner));
            l_Transform.Position = glm::vec3(glm::inverse(l_ParentWorld) * glm::vec4(l_Position, 1.0f));
            l_Transform.Rotation = glm::normalize(glm::inverse(GetRotation(l_ParentWorld)) * l_Rotation);
        }
    }

    // Moving bodies are drawn between their last two steps, at their depth
    void PhysicsScene2D::EndFrame(float interpolation)
    {
        State& l_State = *m_State;
        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (const BodyRecord2D& it_Record : l_State.Records)
        {
            if (it_Record.Motion == BodyMotion::Static || B2_IS_NULL(it_Record.Body) || !l_Registry.valid(it_Record.Entity))
            {
                continue;
            }

            const Pose2D l_Pose = Interpolate(it_Record.Previous, it_Record.Current, interpolation);
            l_Registry.emplace_or_replace<PhysicsPoseComponent>(it_Record.Entity, glm::vec3(l_Pose.Position, it_Record.Depth), glm::angleAxis(l_Pose.Angle, glm::vec3(0.0f, 0.0f, 1.0f)));
        }
    }

    std::span<const PhysicsEvent2D> PhysicsScene2D::GetEvents() const
    {
        return m_State->Events;
    }

    PhysicsWorld2D& PhysicsScene2D::GetWorld()
    {
        return m_State->World;
    }

    // The ground joints to the world hang from is not one of the scene's
    std::uint32_t PhysicsScene2D::GetBodyCount() const
    {
        return m_State->World.GetBodyCount() - (B2_IS_NON_NULL(m_State->Ground) ? 1u : 0u);
    }

    const CollisionLayers& PhysicsScene2D::GetLayers() const
    {
        return m_State->Layers;
    }

    // Box2D counts the bodies awake, which are never static, and the scene knows which of its bodies can move
    PhysicsBodyCounts PhysicsScene2D::GetBodyCounts() const
    {
        const std::uint32_t l_Moving = static_cast<std::uint32_t>(std::ranges::count_if(m_State->Records, [](const BodyRecord2D& record) { return record.Motion != BodyMotion::Static && B2_IS_NON_NULL(record.Body); }));
        const std::uint32_t l_Awake = m_State->World.GetActiveBodyCount();

        return { GetBodyCount(), l_Awake, l_Moving - std::min(l_Awake, l_Moving) };
    }

    void PhysicsScene2D::SetGatherContacts(bool gather)
    {
        m_State->GatherContacts = gather;
        if (!gather)
        {
            m_State->ContactPoints.clear();
        }
    }

    std::span<const PhysicsContactPoint> PhysicsScene2D::GetContactPoints() const
    {
        return m_State->ContactPoints;
    }

    std::optional<PhysicsHit2D> PhysicsScene2D::Raycast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter) const
    {
        const std::vector<PhysicsHit2D> l_Hits = m_State->CastRay(origin, direction, maxDistance, filter, false);

        return l_Hits.empty() ? std::nullopt : std::optional<PhysicsHit2D>(l_Hits.front());
    }

    std::vector<PhysicsHit2D> PhysicsScene2D::RaycastAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter) const
    {
        return m_State->CastRay(origin, direction, maxDistance, filter, true);
    }

    std::optional<PhysicsHit2D> PhysicsScene2D::ShapeCast(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter) const
    {
        const std::vector<PhysicsHit2D> l_Hits = m_State->CastShape(shape, position, angle, direction, maxDistance, filter, false);

        return l_Hits.empty() ? std::nullopt : std::optional<PhysicsHit2D>(l_Hits.front());
    }

    std::vector<PhysicsHit2D> PhysicsScene2D::ShapeCastAll(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter) const
    {
        return m_State->CastShape(shape, position, angle, direction, maxDistance, filter, true);
    }

    std::vector<PhysicsHit2D> PhysicsScene2D::Overlap(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const PhysicsQueryFilter2D& filter) const
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's physics is queried on the main thread.");

        OverlapContext l_Context{ &filter, {} };
        const b2ShapeProxy l_Proxy = MakeProxy(shape, position, angle);
        b2World_OverlapShape(m_State->WorldID, &l_Proxy, MakeQueryFilter(filter), &GatherOverlap, &l_Context);

        std::ranges::sort(l_Context.Hits, [](const PhysicsHit2D& left, const PhysicsHit2D& right) { return left.Entity < right.Entity; });
        const auto [a_First, a_Last] = std::ranges::unique(l_Context.Hits, {}, &PhysicsHit2D::Entity);
        l_Context.Hits.erase(a_First, a_Last);

        return std::move(l_Context.Hits);
    }

    // Each collider placed on the body from its entity's world transform with the offset and turn, by the whole of the matrix in X and Y, so a box or polygon takes any scale, mirror or skew exactly, a circle the larger of its scales and a capsule its scale across for its radius. The body sits at its entity's place and turn about Z, and keeps its depth in Z for write-back. Every shape reports contacts and triggers, and a trigger weighs nothing
    void PhysicsScene2D::State::MakeBody(BodyRecord2D& record)
    {
        Scene& l_Scene = Owner;
        if (!l_Scene.GetRegistry().valid(record.Entity))
        {
            return;
        }

        const glm::mat4 l_World = l_Scene.ComputeWorldMatrix(Entity(record.Entity, &l_Scene));
        const Pose2D l_Pose = GetPose(l_World);
        record.Depth = l_World[3][2];
        if (IsTilted(l_World) && !std::exchange(WarnedTilt, true))
        {
            TR_CORE_WARN("Physics: {} is tilted out of the XY plane, which a 2D body cannot be, so it keeps only its turn about Z once it moves. Later tilts are not reported", GetName(l_Scene, record.Entity));
        }

        const BodyMotion l_Motion = record.Motion;
        const RigidBody2DComponent& l_RigidBody = record.Settings;
        b2BodyDef l_Body = b2DefaultBodyDef();
        l_Body.type = l_Motion == BodyMotion::Static ? b2_staticBody : (l_Motion == BodyMotion::Kinematic ? b2_kinematicBody : b2_dynamicBody);
        l_Body.position = ToBox2D(l_Pose.Position);
        l_Body.rotation = Box2DConvert::ToRotation(l_Pose.Angle);
        l_Body.userData = &record;
        if (l_Motion == BodyMotion::Dynamic)
        {
            l_Body.linearVelocity = ToBox2D(l_RigidBody.InitialLinearVelocity);
            l_Body.angularVelocity = l_RigidBody.InitialAngularVelocity;
            l_Body.linearDamping = l_RigidBody.LinearDamping;
            l_Body.angularDamping = l_RigidBody.AngularDamping;
            l_Body.gravityScale = l_RigidBody.GravityFactor;
            l_Body.isBullet = l_RigidBody.ContinuousCollision;
            l_Body.fixedRotation = l_RigidBody.LockRotation;
            l_Body.isAwake = !l_RigidBody.StartAsleep;
            l_Body.enableSleep = l_RigidBody.CanSleep;
        }

        record.Body = b2CreateBody(WorldID, &l_Body);
        const CollisionLayers& l_Layers = Layers;
        for (ColliderSource2D& it_Collider : record.Colliders)
        {
            const std::string l_Name = GetName(l_Scene, it_Collider.Entity);
            const glm::mat4 l_Placed = l_Scene.ComputeWorldMatrix(Entity(it_Collider.Entity, &l_Scene)) * glm::translate(glm::mat4(1.0f), glm::vec3(it_Collider.Settings.Offset, 0.0f)) * glm::mat4_cast(glm::angleAxis(it_Collider.Settings.OffsetAngle, glm::vec3(0.0f, 0.0f, 1.0f)));
            // From the collider's own space to the body's, in the XY plane
            const glm::vec2 l_AxisX(l_Placed[0]);
            const glm::vec2 l_AxisY(l_Placed[1]);
            const glm::vec2 l_Origin(l_Placed[3]);
            const auto a_ToBody = [&](const glm::vec2& point) { return ToBox2D(Rotate(l_Origin + l_AxisX * point.x + l_AxisY * point.y - l_Pose.Position, -l_Pose.Angle)); };
            const float l_ScaleX = glm::length(glm::vec3(l_Placed[0]));
            const float l_ScaleY = glm::length(glm::vec3(l_Placed[1]));

            b2ShapeDef l_Shape = b2DefaultShapeDef();
            l_Shape.userData = &it_Collider;
            l_Shape.material.friction = it_Collider.Settings.Friction;
            l_Shape.material.restitution = it_Collider.Settings.Restitution;
            l_Shape.filter.categoryBits = 1ull << it_Collider.Settings.Layer;
            l_Shape.filter.maskBits = l_Layers.Rows[it_Collider.Settings.Layer] | Box2DLayers::c_QueryBit;
            l_Shape.isSensor = it_Collider.Settings.Trigger;
            l_Shape.density = it_Collider.Settings.Trigger ? 0.0f : 1.0f;
            l_Shape.enableContactEvents = true;
            l_Shape.enableSensorEvents = true;

            switch (it_Collider.Kind)
            {
                case ColliderKind2D::Box:
                case ColliderKind2D::Polygon:
                {
                    // A box is the polygon of its corners, which keeps every scale and skew
                    const glm::vec2 l_Half = glm::max(it_Collider.Size, glm::vec2(c_MinimumSize));
                    PhysicsVector<glm::vec2> l_Local = it_Collider.Kind == ColliderKind2D::Box ? PhysicsVector<glm::vec2>{ { -l_Half.x, -l_Half.y }, { l_Half.x, -l_Half.y }, { l_Half.x, l_Half.y }, { -l_Half.x, l_Half.y } } : it_Collider.Points;
                    if (l_Local.size() > B2_MAX_POLYGON_VERTICES)
                    {
                        l_Local.resize(B2_MAX_POLYGON_VERTICES);
                    }

                    std::array<b2Vec2, B2_MAX_POLYGON_VERTICES> l_Points{};
                    for (std::size_t it_Point = 0; it_Point < l_Local.size(); ++it_Point)
                    {
                        l_Points[it_Point] = a_ToBody(l_Local[it_Point]);
                    }

                    const b2Hull l_Hull = b2ComputeHull(l_Points.data(), static_cast<int>(l_Local.size()));
                    if (l_Hull.count == 0)
                    {
                        TR_CORE_WARN("Physics: {}'s {} is left out, since its points make no area, or more than one lie together", l_Name, it_Collider.Kind == ColliderKind2D::Box ? "BoxCollider2D" : "PolygonCollider2D");

                        continue;
                    }

                    const float l_Rounding = it_Collider.Kind == ColliderKind2D::Polygon ? std::max(it_Collider.Size.x, 0.0f) * std::max(l_ScaleX, l_ScaleY) : 0.0f;
                    const b2Polygon l_Polygon = b2MakePolygon(&l_Hull, l_Rounding);
                    b2CreatePolygonShape(record.Body, &l_Shape, &l_Polygon);

                    break;
                }
                case ColliderKind2D::Circle:
                {
                    if (std::abs(l_ScaleX - l_ScaleY) > c_ScaleTolerance * std::max({ l_ScaleX, l_ScaleY, 1.0f }) && !std::exchange(WarnedRoundScale, true))
                    {
                        TR_CORE_WARN("Physics: {}'s round 2D collider is scaled unevenly, so it takes the larger scale. Later ones scaled so are not reported", l_Name);
                    }

                    const b2Circle l_Circle{ a_ToBody(glm::vec2(0.0f)), std::max(it_Collider.Size.x * std::max(l_ScaleX, l_ScaleY), c_MinimumSize) };
                    b2CreateCircleShape(record.Body, &l_Shape, &l_Circle);

                    break;
                }
                case ColliderKind2D::Capsule:
                {
                    const float l_HalfSegment = std::max(it_Collider.Size.y * 0.5f - it_Collider.Size.x, 0.0f);
                    const float l_Radius = std::max(it_Collider.Size.x * l_ScaleX, c_MinimumSize);
                    if (l_HalfSegment * l_ScaleY < c_MinimumSize)
                    {
                        const b2Circle l_Circle{ a_ToBody(glm::vec2(0.0f)), l_Radius };
                        b2CreateCircleShape(record.Body, &l_Shape, &l_Circle);

                        break;
                    }

                    const b2Capsule l_Capsule{ a_ToBody(glm::vec2(0.0f, -l_HalfSegment)), a_ToBody(glm::vec2(0.0f, l_HalfSegment)), l_Radius };
                    b2CreateCapsuleShape(record.Body, &l_Shape, &l_Capsule);

                    break;
                }
            }
        }

        // The colliders' mass and inertia scaled to the mass asked for
        if (l_Motion == BodyMotion::Dynamic && l_RigidBody.Mass > 0.0f)
        {
            b2MassData l_Mass = b2Body_GetMassData(record.Body);
            const float l_Scale = l_Mass.mass > 0.0f ? l_RigidBody.Mass / l_Mass.mass : 1.0f;
            l_Mass.mass = l_RigidBody.Mass;
            l_Mass.rotationalInertia *= l_Scale;
            b2Body_SetMassData(record.Body, l_Mass);
        }

        record.Previous = l_Pose;
        record.Current = l_Pose;
        record.TargetFrom = l_Pose;
        record.TargetTo = l_Pose;
    }

    // Its frame is its entity's world transform in the XY plane as the scene starts, with the anchor as an offset and the axis turned with it, and where it meets each body is taken from where the bodies are then. The joint's own body is Box2D's second, and the body it joins, or the ground, its first, so a revolute joint's turn, a prismatic joint's travel and a motor's drive count as its own body's against the other's. A joint that holds no dynamic body, or joins a body with no 2D colliders, is reported and left out
    void PhysicsScene2D::State::MakeJoint(JointRecord2D& joint)
    {
        Scene& l_Scene = Owner;
        SceneRegistry& l_Registry = l_Scene.GetRegistry();
        if (!l_Registry.valid(joint.Entity))
        {
            return;
        }

        const std::string l_Name = GetName(l_Scene, joint.Entity);
        const BodyRecord2D& l_Own = Records[joint.First];
        const BodyRecord2D* l_Other = joint.Second != c_World ? &Records[joint.Second] : nullptr;
        if (B2_IS_NULL(l_Own.Body) || (l_Other != nullptr && B2_IS_NULL(l_Other->Body)))
        {
            TR_CORE_WARN("Physics: {}'s Joint2D joins a body with no 2D colliders, so it is left out", l_Name);

            return;
        }

        if (l_Own.Motion != BodyMotion::Dynamic && (l_Other == nullptr || l_Other->Motion != BodyMotion::Dynamic))
        {
            TR_CORE_WARN("Physics: {}'s Joint2D joins no dynamic body, so it would hold nothing and is left out", l_Name);

            return;
        }

        const Joint2DComponent& l_Joint = joint.Settings;
        const b2BodyId l_BodyA = l_Other != nullptr ? l_Other->Body : GetGround();
        const b2BodyId l_BodyB = l_Own.Body;
        const b2Transform l_A = b2Body_GetTransform(l_BodyA);
        const b2Transform l_B = b2Body_GetTransform(l_BodyB);
        const glm::mat4 l_World = l_Scene.ComputeWorldMatrix(Entity(joint.Entity, &l_Scene));
        const b2Vec2 l_Anchor = ToBox2D(glm::vec2(l_World * glm::vec4(l_Joint.Anchor, 0.0f, 1.0f)));
        const glm::vec2 l_Turned = glm::mat2(l_World) * (glm::length(l_Joint.Axis) > 1e-6f ? glm::normalize(l_Joint.Axis) : glm::vec2(0.0f, 1.0f));
        const b2Vec2 l_Axis = ToBox2D(glm::length(l_Turned) > 1e-6f ? glm::normalize(l_Turned) : glm::vec2(0.0f, 1.0f));
        const b2Vec2 l_LocalA = b2InvTransformPoint(l_A, l_Anchor);
        const b2Vec2 l_LocalB = b2InvTransformPoint(l_B, l_Anchor);
        const float l_Reference = Box2DConvert::ToAngle(b2InvMulRot(l_A.q, l_B.q));
        const float l_MotorMax = std::max(l_Joint.MotorMaxForce, 0.0f);

        switch (l_Joint.Type)
        {
            case Joint2DType::Revolute:
            {
                b2RevoluteJointDef l_Revolute = b2DefaultRevoluteJointDef();
                l_Revolute.bodyIdA = l_BodyA;
                l_Revolute.bodyIdB = l_BodyB;
                l_Revolute.localAnchorA = l_LocalA;
                l_Revolute.localAnchorB = l_LocalB;
                l_Revolute.referenceAngle = l_Reference;
                l_Revolute.enableLimit = l_Joint.UseLimits;
                l_Revolute.lowerAngle = std::clamp(l_Joint.LowerLimit, -c_MaxLimitAngle, 0.0f);
                l_Revolute.upperAngle = std::clamp(l_Joint.UpperLimit, 0.0f, c_MaxLimitAngle);
                l_Revolute.enableMotor = l_Joint.UseMotor;
                l_Revolute.maxMotorTorque = l_MotorMax;
                l_Revolute.motorSpeed = l_Joint.MotorSpeed;
                l_Revolute.collideConnected = l_Joint.CollideConnected;
                joint.Joint = b2CreateRevoluteJoint(WorldID, &l_Revolute);

                break;
            }
            case Joint2DType::Prismatic:
            {
                b2PrismaticJointDef l_Prismatic = b2DefaultPrismaticJointDef();
                l_Prismatic.bodyIdA = l_BodyA;
                l_Prismatic.bodyIdB = l_BodyB;
                l_Prismatic.localAnchorA = l_LocalA;
                l_Prismatic.localAnchorB = l_LocalB;
                l_Prismatic.localAxisA = b2InvRotateVector(l_A.q, l_Axis);
                l_Prismatic.referenceAngle = l_Reference;
                l_Prismatic.enableLimit = l_Joint.UseLimits;
                l_Prismatic.lowerTranslation = std::min(l_Joint.LowerLimit, 0.0f);
                l_Prismatic.upperTranslation = std::max(l_Joint.UpperLimit, 0.0f);
                l_Prismatic.enableMotor = l_Joint.UseMotor;
                l_Prismatic.maxMotorForce = l_MotorMax;
                l_Prismatic.motorSpeed = l_Joint.MotorSpeed;
                l_Prismatic.collideConnected = l_Joint.CollideConnected;
                joint.LocalAxis = l_Prismatic.localAxisA;
                joint.Joint = b2CreatePrismaticJoint(WorldID, &l_Prismatic);

                break;
            }
            case Joint2DType::Distance:
            {
                // The other end is on the entity the joint names, or in the world. With a spring it is pulled towards the middle of the range and stops at its ends, and without one it moves freely within the range, held rigid when the range has no width
                const b2Vec2 l_OtherEnd = ToBox2D(l_Other != nullptr && l_Registry.valid(joint.OtherEntity) ? glm::vec2(l_Scene.ComputeWorldMatrix(Entity(joint.OtherEntity, &l_Scene)) * glm::vec4(l_Joint.ConnectedAnchor, 0.0f, 1.0f)) : l_Joint.ConnectedAnchor);
                const float l_Start = b2Distance(l_OtherEnd, l_Anchor);
                const float l_Min = std::max(l_Joint.MinDistance < 0.0f ? l_Start : l_Joint.MinDistance, c_MinimumSize);
                const float l_Max = std::max(l_Joint.MaxDistance < 0.0f ? l_Start : l_Joint.MaxDistance, l_Min);
                const bool l_Spring = l_Joint.SpringFrequency > 0.0f;

                b2DistanceJointDef l_Distance = b2DefaultDistanceJointDef();
                l_Distance.bodyIdA = l_BodyA;
                l_Distance.bodyIdB = l_BodyB;
                l_Distance.localAnchorA = b2InvTransformPoint(l_A, l_OtherEnd);
                l_Distance.localAnchorB = l_LocalB;
                l_Distance.length = l_Spring ? (l_Min + l_Max) * 0.5f : std::clamp(l_Start, l_Min, l_Max);
                l_Distance.minLength = l_Min;
                l_Distance.maxLength = l_Max;
                l_Distance.enableLimit = l_Max > l_Min;
                l_Distance.enableSpring = l_Spring || l_Max > l_Min;
                l_Distance.hertz = std::max(l_Joint.SpringFrequency, 0.0f);
                l_Distance.dampingRatio = std::max(l_Joint.SpringDamping, 0.0f);
                l_Distance.collideConnected = l_Joint.CollideConnected;
                joint.Joint = b2CreateDistanceJoint(WorldID, &l_Distance);

                break;
            }
            case Joint2DType::Weld:
            {
                b2WeldJointDef l_Weld = b2DefaultWeldJointDef();
                l_Weld.bodyIdA = l_BodyA;
                l_Weld.bodyIdB = l_BodyB;
                l_Weld.localAnchorA = l_LocalA;
                l_Weld.localAnchorB = l_LocalB;
                l_Weld.referenceAngle = l_Reference;
                l_Weld.linearHertz = 0.0f;
                l_Weld.angularHertz = 0.0f;
                l_Weld.collideConnected = l_Joint.CollideConnected;
                joint.Joint = b2CreateWeldJoint(WorldID, &l_Weld);

                break;
            }
            case Joint2DType::Wheel:
            {
                b2WheelJointDef l_Wheel = b2DefaultWheelJointDef();
                l_Wheel.bodyIdA = l_BodyA;
                l_Wheel.bodyIdB = l_BodyB;
                l_Wheel.localAnchorA = l_LocalA;
                l_Wheel.localAnchorB = l_LocalB;
                l_Wheel.localAxisA = b2InvRotateVector(l_A.q, l_Axis);
                l_Wheel.enableSpring = l_Joint.SpringFrequency > 0.0f;
                l_Wheel.hertz = std::max(l_Joint.SpringFrequency, 0.0f);
                l_Wheel.dampingRatio = std::max(l_Joint.SpringDamping, 0.0f);
                l_Wheel.enableLimit = l_Joint.UseLimits;
                l_Wheel.lowerTranslation = std::min(l_Joint.LowerLimit, 0.0f);
                l_Wheel.upperTranslation = std::max(l_Joint.UpperLimit, 0.0f);
                l_Wheel.enableMotor = l_Joint.UseMotor;
                l_Wheel.maxMotorTorque = l_MotorMax;
                l_Wheel.motorSpeed = l_Joint.MotorSpeed;
                l_Wheel.collideConnected = l_Joint.CollideConnected;
                joint.Joint = b2CreateWheelJoint(WorldID, &l_Wheel);

                break;
            }
        }
    }

    // The force and the torque Box2D's last step put on each joint that can break, its motor's left out. One past its break force or torque is taken out of the world, and its bodies collide again, after it is logged and handed out as an event, in the joints' order
    void PhysicsScene2D::State::BreakJoints(std::uint32_t index)
    {
        for (JointRecord2D& it_Joint : Joints)
        {
            const Joint2DComponent& l_Joint = it_Joint.Settings;
            if (B2_IS_NULL(it_Joint.Joint) || (l_Joint.BreakForce <= 0.0f && l_Joint.BreakTorque <= 0.0f))
            {
                continue;
            }

            glm::vec2 l_Force = FromBox2D(b2Joint_GetConstraintForce(it_Joint.Joint));
            float l_Torque = b2Joint_GetConstraintTorque(it_Joint.Joint);
            switch (l_Joint.Type)
            {
                case Joint2DType::Revolute:
                {
                    l_Torque -= b2RevoluteJoint_GetMotorTorque(it_Joint.Joint);

                    break;
                }
                case Joint2DType::Prismatic:
                {
                    const b2Vec2 l_Axis = b2RotateVector(b2Body_GetRotation(b2Joint_GetBodyA(it_Joint.Joint)), it_Joint.LocalAxis);
                    l_Force -= FromBox2D(l_Axis) * b2PrismaticJoint_GetMotorForce(it_Joint.Joint);

                    break;
                }
                case Joint2DType::Wheel:
                {
                    l_Torque -= b2WheelJoint_GetMotorTorque(it_Joint.Joint);

                    break;
                }
                case Joint2DType::Distance:
                case Joint2DType::Weld:
                {
                    break;
                }
            }

            const float l_ForceSize = glm::length(l_Force);
            const float l_TorqueSize = std::abs(l_Torque);
            const bool l_ByForce = l_Joint.BreakForce > 0.0f && l_ForceSize > l_Joint.BreakForce;
            const bool l_ByTorque = !l_ByForce && l_Joint.BreakTorque > 0.0f && l_TorqueSize > l_Joint.BreakTorque;
            if (!l_ByForce && !l_ByTorque)
            {
                continue;
            }

            TR_CORE_INFO("Physics: {}'s {} joint broke under {}, past its break {}", GetName(Owner, it_Joint.Entity), GetJointTypeName(l_Joint.Type), l_ByForce ? std::format("{:.1f} N", l_ForceSize) : std::format("{:.1f} N m", l_TorqueSize), l_ByForce ? std::format("force of {:.1f} N", l_Joint.BreakForce) : std::format("torque of {:.1f} N m", l_Joint.BreakTorque));
            b2DestroyJoint(it_Joint.Joint);
            it_Joint.Joint = b2_nullJointId;
            Events.push_back({ PhysicsEventType::JointBreak, it_Joint.ID, it_Joint.Other, glm::vec2(0.0f), glm::vec2(0.0f), index });
        }
    }

    // Each moving body's touching contacts, read once: a pair of moving bodies by the one made first. Box2D keeps speculative points a little apart, which are left out
    void PhysicsScene2D::State::GatherContactPoints()
    {
        ContactPoints.clear();
        if (!GatherContacts)
        {
            return;
        }

        for (const BodyRecord2D& it_Record : Records)
        {
            if (it_Record.Motion == BodyMotion::Static || B2_IS_NULL(it_Record.Body))
            {
                continue;
            }

            ContactData.resize(static_cast<std::size_t>(b2Body_GetContactCapacity(it_Record.Body)));
            const int l_Count = b2Body_GetContactData(it_Record.Body, ContactData.data(), static_cast<int>(ContactData.size()));
            for (int it_Contact = 0; it_Contact < l_Count; ++it_Contact)
            {
                const b2ContactData& l_Data = ContactData[static_cast<std::size_t>(it_Contact)];
                const BodyRecord2D* l_First = GetRecord(b2Shape_GetBody(l_Data.shapeIdA));
                const BodyRecord2D* l_Second = GetRecord(b2Shape_GetBody(l_Data.shapeIdB));
                const BodyRecord2D* l_Other = l_First == &it_Record ? l_Second : l_First;
                if (l_Other != nullptr && l_Other->Motion != BodyMotion::Static && l_Other < &it_Record)
                {
                    continue;
                }

                for (int it_Point = 0; it_Point < l_Data.manifold.pointCount; ++it_Point)
                {
                    const b2ManifoldPoint& l_Point = l_Data.manifold.points[it_Point];
                    if (l_Point.separation <= c_TouchingSlop)
                    {
                        ContactPoints.push_back({ glm::vec3(FromBox2D(l_Point.point), it_Record.Depth), glm::vec3(FromBox2D(l_Data.manifold.normal), 0.0f) });
                    }
                }
            }
        }
    }

    // Box2D's begin and end events, for contacts and for triggers, are counted by body pair. A pair's first shape pair begins it and its last ends it. A contact pair touching through the step with one body awake stays. Each step's events go out sorted by their entities
    void PhysicsScene2D::State::PairEvents(std::uint32_t index)
    {
        StepEvents.clear();
        const b2WorldId l_World = WorldID;

        // Each kind's adds and removes counted by pair, with the first begin's point and normal
        const auto a_Count = [&](auto& pairs, bool trigger)
        {
            std::ranges::sort(Raw, [](const RawContact2D& left, const RawContact2D& right) { return std::tie(left.First, left.Second, left.Added) < std::tie(right.First, right.Second, right.Added); });
            for (std::size_t it_Raw = 0; it_Raw < Raw.size();)
            {
                const RawContact2D& l_First = Raw[it_Raw];
                const std::uint64_t l_Key = GetPairKey(l_First.First, l_First.Second);
                std::int64_t l_Delta = 0;
                const RawContact2D* l_Added = nullptr;
                for (; it_Raw < Raw.size() && GetPairKey(Raw[it_Raw].First, Raw[it_Raw].Second) == l_Key; ++it_Raw)
                {
                    l_Delta += Raw[it_Raw].Added ? 1 : -1;
                    l_Added = l_Added == nullptr && Raw[it_Raw].Added ? &Raw[it_Raw] : l_Added;
                }

                const auto a_Found = pairs.find(l_Key);
                const std::uint32_t l_Before = a_Found != pairs.end() ? a_Found->second.Count : 0;
                const std::uint32_t l_After = static_cast<std::uint32_t>(std::max<std::int64_t>(static_cast<std::int64_t>(l_Before) + l_Delta, 0));
                if (l_Before == 0 && l_After > 0)
                {
                    const BodyRecord2D& l_FirstRecord = Records[l_First.First];
                    const BodyRecord2D& l_SecondRecord = Records[l_First.Second];
                    // A trigger's own body comes first, and a contact's bodies in UUID order
                    const bool l_Swapped = !trigger && l_SecondRecord.ID < l_FirstRecord.ID;
                    ContactPair2D l_Pair;
                    l_Pair.Count = l_After;
                    l_Pair.Begun = StepCount;
                    l_Pair.First = l_Swapped ? l_SecondRecord.ID : l_FirstRecord.ID;
                    l_Pair.Second = l_Swapped ? l_FirstRecord.ID : l_SecondRecord.ID;
                    l_Pair.FirstRecord = l_First.First;
                    l_Pair.SecondRecord = l_First.Second;
                    pairs.insert_or_assign(l_Key, l_Pair);

                    PhysicsEvent2D l_Event{ trigger ? PhysicsEventType::TriggerEnter : PhysicsEventType::ContactBegin, l_Pair.First, l_Pair.Second, glm::vec2(0.0f), glm::vec2(0.0f), index };
                    if (!trigger && l_Added != nullptr)
                    {
                        l_Event.Point = l_Added->Point;
                        l_Event.Normal = l_Swapped ? -l_Added->Normal : l_Added->Normal;
                    }

                    StepEvents.push_back(l_Event);
                }
                else if (l_Before > 0 && l_After == 0)
                {
                    StepEvents.push_back({ trigger ? PhysicsEventType::TriggerExit : PhysicsEventType::ContactEnd, a_Found->second.First, a_Found->second.Second, glm::vec2(0.0f), glm::vec2(0.0f), index });
                    pairs.erase(a_Found);
                }
                else if (a_Found != pairs.end())
                {
                    a_Found->second.Count = l_After;
                }
            }

            Raw.clear();
        };

        // Contacts by their bodies' records in index order, with the normal turned to match
        const b2ContactEvents l_Contacts = b2World_GetContactEvents(l_World);
        for (int it_Event = 0; it_Event < l_Contacts.beginCount; ++it_Event)
        {
            const b2ContactBeginTouchEvent& l_Begin = l_Contacts.beginEvents[it_Event];
            std::size_t l_A = GetIndex(l_Begin.shapeIdA);
            std::size_t l_B = GetIndex(l_Begin.shapeIdB);
            glm::vec2 l_Point{ 0.0f };
            for (int it_Point = 0; it_Point < l_Begin.manifold.pointCount; ++it_Point)
            {
                l_Point += FromBox2D(l_Begin.manifold.points[it_Point].point);
            }

            l_Point /= static_cast<float>(std::max(l_Begin.manifold.pointCount, 1));
            glm::vec2 l_Normal = FromBox2D(l_Begin.manifold.normal);
            if (l_A > l_B)
            {
                std::swap(l_A, l_B);
                l_Normal = -l_Normal;
            }

            Raw.push_back({ l_A, l_B, true, l_Point, l_Normal });
        }

        for (int it_Event = 0; it_Event < l_Contacts.endCount; ++it_Event)
        {
            const b2ContactEndTouchEvent& l_End = l_Contacts.endEvents[it_Event];
            if (!b2Shape_IsValid(l_End.shapeIdA) || !b2Shape_IsValid(l_End.shapeIdB))
            {
                continue;
            }

            const std::size_t l_A = GetIndex(l_End.shapeIdA);
            const std::size_t l_B = GetIndex(l_End.shapeIdB);
            Raw.push_back({ std::min(l_A, l_B), std::max(l_A, l_B), false, glm::vec2(0.0f), glm::vec2(0.0f) });
        }

        a_Count(Contacts, false);

        // Triggers by the trigger's body first, never its own body
        const b2SensorEvents l_Sensors = b2World_GetSensorEvents(l_World);
        for (int it_Event = 0; it_Event < l_Sensors.beginCount; ++it_Event)
        {
            const std::size_t l_Sensor = GetIndex(l_Sensors.beginEvents[it_Event].sensorShapeId);
            const std::size_t l_Visitor = GetIndex(l_Sensors.beginEvents[it_Event].visitorShapeId);
            if (l_Sensor != l_Visitor)
            {
                Raw.push_back({ l_Sensor, l_Visitor, true, glm::vec2(0.0f), glm::vec2(0.0f) });
            }
        }

        for (int it_Event = 0; it_Event < l_Sensors.endCount; ++it_Event)
        {
            const b2SensorEndTouchEvent& l_End = l_Sensors.endEvents[it_Event];
            if (!b2Shape_IsValid(l_End.sensorShapeId) || !b2Shape_IsValid(l_End.visitorShapeId))
            {
                continue;
            }

            const std::size_t l_Sensor = GetIndex(l_End.sensorShapeId);
            const std::size_t l_Visitor = GetIndex(l_End.visitorShapeId);
            if (l_Sensor != l_Visitor)
            {
                Raw.push_back({ l_Sensor, l_Visitor, false, glm::vec2(0.0f), glm::vec2(0.0f) });
            }
        }

        a_Count(Triggers, true);

        for (const auto& [it_Key, it_Pair] : Contacts)
        {
            if (it_Pair.Begun != StepCount && (b2Body_IsAwake(Records[it_Pair.FirstRecord].Body) || b2Body_IsAwake(Records[it_Pair.SecondRecord].Body)))
            {
                StepEvents.push_back({ PhysicsEventType::ContactStay, it_Pair.First, it_Pair.Second, glm::vec2(0.0f), glm::vec2(0.0f), index });
            }
        }

        std::ranges::stable_sort(StepEvents, [](const PhysicsEvent2D& left, const PhysicsEvent2D& right) { return std::tie(left.First, left.Second, left.Type) < std::tie(right.First, right.Second, right.Type); });
        Events.insert(Events.end(), StepEvents.begin(), StepEvents.end());
    }

    // A ray of no length, or with no direction, meets nothing. Box2D passes over a solid the ray starts in, so a point-sized test finds those first, which meet it at once. Each other hit is found again along a short piece of the ray around it, against its own shape, as in 3D
    std::vector<PhysicsHit2D> PhysicsScene2D::State::CastRay(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter, bool all) const
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's physics is queried on the main thread.");

        std::vector<PhysicsHit2D> l_Hits;
        const float l_Length = glm::length(direction);
        if (!(l_Length > 0.0f) || !(maxDistance > 0.0f) || !std::isfinite(l_Length) || !std::isfinite(maxDistance))
        {
            return l_Hits;
        }

        const glm::vec2 l_Direction = direction / l_Length;
        const b2QueryFilter l_Filter = MakeQueryFilter(filter);

        // Solids the ray starts in
        OverlapContext l_Inside{ &filter, {} };
        const b2Vec2 l_Origin = ToBox2D(origin);
        const b2ShapeProxy l_Point = b2MakeProxy(&l_Origin, 1, 0.0f);
        b2World_OverlapShape(WorldID, &l_Point, l_Filter, &GatherOverlap, &l_Inside);

        for (PhysicsHit2D& it_Hit : l_Inside.Hits)
        {
            it_Hit.Point = origin;
            it_Hit.Normal = -l_Direction;
            it_Hit.Distance = 0.0f;
            l_Hits.push_back(it_Hit);
        }

        if (!all && !l_Hits.empty())
        {
            SortHits(l_Hits);

            return l_Hits;
        }

        CastContext l_Context{ &filter, all, {} };
        b2World_CastRay(WorldID, l_Origin, ToBox2D(l_Direction * maxDistance), l_Filter, &GatherCast, &l_Context);
        if (!all && !l_Context.Hits.empty())
        {
            const auto a_Nearest = std::ranges::min_element(l_Context.Hits, {}, [](const auto& hit) { return hit.first.Distance; });
            l_Context.Hits = { *a_Nearest };
        }

        for (auto& [it_Hit, it_Shape] : l_Context.Hits)
        {
            if (std::ranges::find(l_Hits, it_Hit.Entity, &PhysicsHit2D::Entity) != l_Hits.end())
            {
                continue;
            }

            float l_Distance = it_Hit.Distance * maxDistance;
            const float l_Margin = c_RefineMargin * (l_Distance + 1.0f);
            const float l_Start = std::max(l_Distance - l_Margin, 0.0f);
            const b2RayCastInput l_Input{ ToBox2D(origin + l_Direction * l_Start), ToBox2D(l_Direction * (l_Margin * 2.0f)), 1.0f };
            const b2CastOutput l_Near = b2Shape_RayCast(it_Shape, &l_Input);
            if (l_Near.hit)
            {
                l_Distance = l_Start + l_Near.fraction * l_Margin * 2.0f;
                it_Hit.Normal = FromBox2D(l_Near.normal);
            }

            it_Hit.Distance = l_Distance;
            it_Hit.Point = origin + l_Direction * l_Distance;
            l_Hits.push_back(it_Hit);
        }

        SortHits(l_Hits);

        return l_Hits;
    }

    // As a ray, with a hit's point and normal where the shape first touched the collider
    std::vector<PhysicsHit2D> PhysicsScene2D::State::CastShape(const PhysicsQueryShape2D& shape, const glm::vec2& position, float angle, const glm::vec2& direction, float maxDistance, const PhysicsQueryFilter2D& filter, bool all) const
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's physics is queried on the main thread.");

        std::vector<PhysicsHit2D> l_Hits;
        const float l_Length = glm::length(direction);
        if (!(l_Length > 0.0f) || !(maxDistance > 0.0f) || !std::isfinite(l_Length) || !std::isfinite(maxDistance))
        {
            return l_Hits;
        }

        const glm::vec2 l_Direction = direction / l_Length;
        const b2ShapeProxy l_Proxy = MakeProxy(shape, position, angle);
        CastContext l_Context{ &filter, all, {} };
        b2World_CastShape(WorldID, &l_Proxy, ToBox2D(l_Direction * maxDistance), MakeQueryFilter(filter), &GatherCast, &l_Context);
        if (!all && !l_Context.Hits.empty())
        {
            const auto a_Nearest = std::ranges::min_element(l_Context.Hits, {}, [](const auto& hit) { return hit.first.Distance; });
            l_Context.Hits = { *a_Nearest };
        }

        for (auto& [it_Hit, it_Shape] : l_Context.Hits)
        {
            it_Hit.Distance *= maxDistance;
            if (it_Hit.Distance <= 0.0f)
            {
                it_Hit.Point = position;
                it_Hit.Normal = -l_Direction;
            }
            else
            {
                SettleCast(it_Hit, it_Shape, shape, position, angle, l_Direction, maxDistance);
            }

            l_Hits.push_back(it_Hit);
        }

        SortHits(l_Hits);

        return l_Hits;
    }
}