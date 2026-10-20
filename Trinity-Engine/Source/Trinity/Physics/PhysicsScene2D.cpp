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
#include <cfloat>
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
        // Box2D's linear slop, within which a contact point counts as touching
        constexpr float c_TouchingSlop = 0.005f;
        // Box2D's own bounds on a revolute joint's limits
        constexpr float c_MaxLimitAngle = 0.99f * std::numbers::pi_v<float>;
        // A 2D character rising slower than this, against what it stands on, stays on it, as a 3D one does
        constexpr float c_RisingSpeed = 0.1f;
        // A 2D character's MaxSlope is held between these
        constexpr float c_MinimumSlope = std::numbers::pi_v<float> / 180.0f;
        constexpr float c_MaximumSlope = std::numbers::pi_v<float> *0.5f;
        // A 2D character's move meets shapes this near, and a shape this near touches it. Box2D's plane solver and casts leave a mover its linear slop inside what it rests on, so it keeps touching
        constexpr float c_ContactDistance = 0.05f;
        constexpr float c_SupportDistance = 0.01f;
        // The tries a 2D character's move takes to reach where it goes, as Box2D's mover sample takes, stopping once a try moves less than this
        constexpr int c_MoveIterations = 5;
        constexpr float c_MoveTolerance = 1e-4f;
        // A one-way collider holds a 2D character whose contact with it faces within about 45 degrees of the collider's up, while the character is no deeper in it than this
        constexpr float c_OneWayCosine = 0.7f;
        constexpr float c_OneWayDepth = 0.02f;
        // Jolt's own settings for walking up stairs, which a 2D character takes as a 3D one does: the least it steps forward, how far ahead it looks for a floor past an edge, and the most a wall it walks into may face away from where it goes, 75 degrees
        constexpr float c_StairsMinStepForward = 0.02f;
        constexpr float c_StairsStepForwardTest = 0.15f;
        constexpr float c_StairsCosAngleForwardContact = 0.258819f;

        enum class ColliderKind2D : std::uint8_t
        {
            Box,
            Circle,
            Capsule,
            Polygon
        };

        // A 2D collider as it was last read: a box's half extents, a circle's radius, a capsule's radius and height, or a polygon's points and rounding, and its entity's UUID, which events and queries report
        struct ColliderSource2D
        {
            entt::entity Entity = entt::null;
            UUID ID;
            ColliderKind2D Kind = ColliderKind2D::Box;
            Collider2DSettings Settings;
            glm::vec2 Size{ 0.0f };
            PhysicsVector<glm::vec2> Points;

            [[nodiscard]] bool operator==(const ColliderSource2D&) const = default;
        };

        // A place in the XY plane and a turn about Z, counter-clockwise in radians
        struct Pose2D
        {
            glm::vec2 Position{ 0.0f };
            float Angle = 0.0f;
        };

        // A shape a 2D character met: its body, the point on its surface nearest the character, the normal from it to the character, the gap between the two, below 0 while they overlap, and how fast the shape moves there
        struct CharacterContact2D
        {
            b2ShapeId Shape = b2_nullShapeId;
            b2BodyId Body = b2_nullBodyId;
            b2BodyType Type = b2_staticBody;
            glm::vec2 Point{ 0.0f };
            glm::vec2 Normal{ 0.0f };
            float Distance = 0.0f;
            glm::vec2 Velocity{ 0.0f };
        };

        // As Jolt has a 3D character stand: on ground it can stand on, on a slope too steep, touching what holds none of it up, or touching nothing
        enum class GroundState2D : std::uint8_t
        {
            OnGround,
            OnSteepGround,
            NotSupported,
            InAir
        };

        // A body carrying a 2D character over a step, and where it was as the step began
        struct CarryingBody2D
        {
            b2BodyId Body = b2_nullBodyId;
            b2Transform Before = b2Transform_identity;
        };

        // A dynamic body a 2D character pushes in a step, at the contact it goes into fastest
        struct BodyPush2D
        {
            const CharacterContact2D* Contact = nullptr;
            float Wanted = 0.0f;
        };

        // A body's owner and what it was made from. Each record is an allocation of its own, which never moves, since its body points at it, and records come and go as the scene changes while it runs. Its shapes point at its colliders, so they are made again whenever the colliders change
        struct BodyRecord2D
        {
            entt::entity Entity = entt::null;
            UUID ID;
            // Its place in the order records were made, which as the scene starts is hierarchy order, and which pairs of bodies are counted and sorted by
            std::uint32_t Serial = 0;
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
            // The entity's local position and rotation as the body was made or last wrote them, so a transform set from code is seen and the body teleported there
            glm::vec3 WrittenPosition{ 0.0f };
            glm::quat WrittenRotation = glm::identity<glm::quat>();
            // A character's controller as it was last read, and what its capsule shape reports to events and queries. Its body is kinematic, holding its capsule and the colliders that follow it, and is placed where the character moves
            bool HasCharacter = false;
            CharacterController2DComponent Controller;
            ColliderSource2D Capsule;
            b2ShapeId CapsuleShape = b2_nullShapeId;
            // Its capsule's cap centres in its body's space, its radius, and its step height, in the world, and the cosine of its steepest slope
            std::array<b2Vec2, 2> CapsuleEnds{};
            float Radius = 0.0f;
            bool Round = false;
            float StepHeight = 0.0f;
            float CosMaxSlope = 0.0f;
            // The velocity of its own, which lasts until changed, and how its last move left it standing, with the shapes it met there
            glm::vec2 Velocity{ 0.0f };
            GroundState2D Ground = GroundState2D::InAir;
            glm::vec2 GroundNormal{ 0.0f };
            b2BodyId GroundBody = b2_nullBodyId;
            PhysicsVector<CharacterContact2D> Contacts;
            // The body a character stood on as its step began, which carries it over the step
            CarryingBody2D Carrier;
        };

        // A joint as it was last read: its entity, the entity it names and the records of its two bodies, the second none for the world, and its Box2D joint once made. A joint that cannot be made is dropped, one whose body went with its last collider waits for it to come back, and one that breaks is let go
        struct JointRecord2D
        {
            entt::entity Entity = entt::null;
            UUID ID;
            entt::entity OtherEntity = entt::null;
            UUID Other;
            Joint2DComponent Settings;
            BodyRecord2D* First = nullptr;
            BodyRecord2D* Second = nullptr;
            b2JointId Joint = b2_nullJointId;
            // A prismatic joint's axis on its first Box2D body, which the motor's force is along
            b2Vec2 LocalAxis{ 0.0f, 1.0f };
            bool NeedsBody = false;
            bool Broken = false;
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
            const BodyRecord2D* FirstRecord = nullptr;
            const BodyRecord2D* SecondRecord = nullptr;
        };

        using ContactPairs2D = std::map<std::uint64_t, ContactPair2D, std::less<>, TaggedAllocator<std::pair<const std::uint64_t, ContactPair2D>, MemoryTag::Physics>>;

        // A shape pair Box2D began or ended touching in a step, by the serials of their bodies' records, with a contact's first point and normal from the first record to the second. A beginning has the records too, since its shapes are there
        struct RawContact2D
        {
            std::uint32_t First = 0;
            std::uint32_t Second = 0;
            bool Added = false;
            glm::vec2 Point{ 0.0f };
            glm::vec2 Normal{ 0.0f };
            const BodyRecord2D* FirstRecord = nullptr;
            const BodyRecord2D* SecondRecord = nullptr;
        };

        // A shape's record: its serial, the record itself until the shape is taken out, and for a one-way collider its up in its body's space, otherwise none
        struct ShapeOwner2D
        {
            std::uint32_t Serial = 0;
            const BodyRecord2D* Record = nullptr;
            b2Vec2 OneWay{ 0.0f, 0.0f };
        };

        std::uint64_t GetPairKey(std::uint32_t first, std::uint32_t second)
        {
            return (static_cast<std::uint64_t>(first) << 32) | static_cast<std::uint64_t>(second);
        }

        // A shape's slot, generation and world, which no other shape has while it is known, since a slot used again has a new generation
        std::uint64_t GetShapeKey(b2ShapeId shape)
        {
            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(shape.index1)) | (static_cast<std::uint64_t>(shape.generation) << 32) | (static_cast<std::uint64_t>(shape.world0) << 48);
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

        // Box2D's SetTargetTransform leaves a body's velocities as they were when the move is slower than its sleep threshold, so a kinematic body slowing to a stop would run on past its transform and back for good, and one moved slowly would stop and start. One awake, or whose transform moves this frame, is woken and given the velocities that take it there over the step, reckoned as SetTargetTransform does. One asleep and left still is left to SetTargetTransform, which wakes it only for a move that is not sleepy, so a body left still goes to sleep
        void MoveToTarget(b2BodyId body, b2Transform target, float seconds, bool moving)
        {
            if (!(seconds > 0.0f))
            {
                return;
            }

            if (!moving && !b2Body_IsAwake(body))
            {
                b2Body_SetTargetTransform(body, target, seconds);

                return;
            }

            b2Body_SetAwake(body, true);
            const float l_Inverse = 1.0f / seconds;
            b2Body_SetLinearVelocity(body, b2MulSV(l_Inverse, b2Sub(b2TransformPoint(target, b2Body_GetLocalCenterOfMass(body)), b2Body_GetWorldCenterOfMass(body))));
            if (!b2Body_IsFixedRotation(body))
            {
                b2Body_SetAngularVelocity(body, l_Inverse * b2RelativeAngle(target.q, b2Body_GetRotation(body)));
            }
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
                AddCollider(record, { entity, l_ID, ColliderKind2D::Box, *l_Box, l_Box->HalfExtents,{} });
            }

            if (const CircleCollider2DComponent* l_Circle = registry.try_get<CircleCollider2DComponent>(entity))
            {
                AddCollider(record, { entity, l_ID, ColliderKind2D::Circle, *l_Circle, glm::vec2(l_Circle->Radius, 0.0f),{} });
            }

            if (const CapsuleCollider2DComponent* l_Capsule = registry.try_get<CapsuleCollider2DComponent>(entity))
            {
                AddCollider(record, { entity, l_ID, ColliderKind2D::Capsule, *l_Capsule, glm::vec2(l_Capsule->Radius, l_Capsule->Height),{} });
            }

            if (const PolygonCollider2DComponent* l_Polygon = registry.try_get<PolygonCollider2DComponent>(entity))
            {
                AddCollider(record, { entity, l_ID, ColliderKind2D::Polygon, *l_Polygon, glm::vec2(l_Polygon->Radius, 0.0f), PhysicsVector<glm::vec2>(l_Polygon->Points.begin(), l_Polygon->Points.end()) });
            }
        }

        // The entity whose body an entity's 2D colliders join: the nearest at or above it with a RigidBody2D or a CharacterController2D, none when a RigidBody or a CharacterController without one comes first, since that is the 3D world's, and otherwise the highest at or above it with a 2D collider, which is a static body. As the scene starts it finds them walking down, and this walks up to the same answer
        entt::entity FindOwner(const SceneRegistry& registry, entt::entity entity)
        {
            entt::entity l_Highest = entt::null;
            for (entt::entity it_Entity = entity; it_Entity != entt::null; it_Entity = registry.get<RelationshipComponent>(it_Entity).Parent)
            {
                if (registry.any_of<RigidBodyComponent, CharacterControllerComponent>(it_Entity))
                {
                    return entt::null;
                }

                if (registry.any_of<RigidBody2DComponent, CharacterController2DComponent>(it_Entity))
                {
                    return it_Entity;
                }

                if (Has2DCollider(registry, it_Entity))
                {
                    l_Highest = it_Entity;
                }
            }

            return l_Highest;
        }

        // What an owner's record takes from its entity: its CharacterController2D, or its RigidBody2D, or neither for a static body. A character is kinematic, and a RigidBody2D beside it is left out
        void ReadOwner(const SceneRegistry& registry, entt::entity owner, BodyRecord2D& record)
        {
            const CharacterController2DComponent* l_Character = registry.try_get<CharacterController2DComponent>(owner);
            const RigidBody2DComponent* l_RigidBody = l_Character == nullptr ? registry.try_get<RigidBody2DComponent>(owner) : nullptr;
            record.Entity = owner;
            record.ID = registry.get<IDComponent>(owner).ID;
            record.HasRigidBody = l_RigidBody != nullptr;
            record.Motion = l_Character != nullptr ? BodyMotion::Kinematic : (l_RigidBody != nullptr ? l_RigidBody->Motion : BodyMotion::Static);
            record.Settings = l_RigidBody != nullptr ? *l_RigidBody : RigidBody2DComponent{};
            record.HasCharacter = l_Character != nullptr;
            record.Controller = l_Character != nullptr ? *l_Character : CharacterController2DComponent{};
        }

        // Each shape's body, gathered while a query runs and woken once it is done
        bool GatherBody(b2ShapeId shape, void* context)
        {
            static_cast<PhysicsVector<b2BodyId>*>(context)->push_back(b2Shape_GetBody(shape));

            return true;
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
                    const std::array<b2Vec2, 4> l_Corners{ { { -l_Half.x, -l_Half.y },{ l_Half.x, -l_Half.y },{ l_Half.x, l_Half.y },{ -l_Half.x, l_Half.y } } };

                    return b2MakeOffsetProxy(l_Corners.data(), 4, 0.0f, l_Position, l_Rotation);
                }
                case PhysicsQueryShapeType2D::Capsule:
                {
                    const float l_HalfSegment = std::max(shape.Height * 0.5f - shape.Radius, 0.0f);
                    const std::array<b2Vec2, 2> l_Ends{ { { 0.0f, -l_HalfSegment },{ 0.0f, l_HalfSegment } } };

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

        // The records the bodies point at go while the world still holds the bodies, which goes last
        ~State()
        {
            for (BodyRecord2D* it_Record : Records)
            {
                Memory::Delete(it_Record);
            }
        }

        State(const State&) = delete;
        State& operator=(const State&) = delete;

        // Signal listeners, which only note what changed, since nothing may be made or taken out while the registry is in the middle of a change
        void OnBodyChanged([[maybe_unused]] SceneRegistry& registry, entt::entity entity)
        {
            Dirty.push_back(entity);
        }

        void OnJointChanged([[maybe_unused]] SceneRegistry& registry, entt::entity entity)
        {
            DirtyJoints.push_back(entity);
        }

        [[nodiscard]] static PhysicsWorldSettings2D GetWorldSettings(const CollisionLayers& layers)
        {
            PhysicsWorldSettings2D l_Settings;
            l_Settings.Layers = layers;

            return l_Settings;
        }

        void MakeBody(BodyRecord2D& record);
        void MakeShapes(BodyRecord2D& record);
        void ApplyMass(const BodyRecord2D& record) const;
        void MakeJoint(JointRecord2D& joint);
        void CreateJointRecord(entt::entity entity);
        void DestroyShapes(b2BodyId body, bool destroy);
        void DestroyBody(BodyRecord2D& record);
        void WakeAround(b2BodyId body);
        void EndPairs(const BodyRecord2D& record);
        void Gather(entt::entity owner, BodyRecord2D& record) const;
        void Index(BodyRecord2D& record);
        void Unindex(const BodyRecord2D& record);
        [[nodiscard]] BodyRecord2D* FindRecord(entt::entity owner) const;
        void RemoveRecord(BodyRecord2D& record);
        [[nodiscard]] bool UpdateRecord(BodyRecord2D& record, BodyRecord2D& gathered);
        void Sync();
        void ResolveJoints(bool bodiesChanged, const PhysicsVector<entt::entity>& dirtyJoints);
        [[nodiscard]] bool Tune(JointRecord2D& joint);
        void FollowTransforms();
        void PlaceAtTransform(BodyRecord2D& record);
        [[nodiscard]] BodyRecord2D* FindBody(UUID entity);
        void PairEvents(std::uint32_t index);
        void BreakJoints(std::uint32_t index);
        void GatherContactPoints();
        void WakeAround(const b2AABB& box);

        void MakeCharacterShape(BodyRecord2D& record);
        void MoveCharacters(float seconds);
        void CarryCharacters();
        void PlaceCharacter(BodyRecord2D& record, const glm::vec2& position);
        [[nodiscard]] glm::vec2 GetUp() const;
        [[nodiscard]] b2ShapeProxy GetMover(const BodyRecord2D& record, const glm::vec2& position) const;
        [[nodiscard]] glm::vec2 GetFoot(const BodyRecord2D& record, const glm::vec2& position) const;
        [[nodiscard]] bool IsTooSteep(const BodyRecord2D& record, const glm::vec2& normal) const;
        void GatherCharacterShapes(const BodyRecord2D& record, const b2AABB& box);
        [[nodiscard]] glm::vec2 GetOneWayUp(b2ShapeId shape) const;
        void Collide(const BodyRecord2D& record, const glm::vec2& position, PhysicsVector<CharacterContact2D>& contacts);
        [[nodiscard]] float Cast(const BodyRecord2D& record, const glm::vec2& position, const glm::vec2& translation, CharacterContact2D* hit);
        void MakePlanes(const BodyRecord2D& record, const PhysicsVector<CharacterContact2D>& contacts, float reach);
        [[nodiscard]] glm::vec2 MoveShape(const BodyRecord2D& record, const glm::vec2& start, const glm::vec2& displacement);
        void UpdateGround(BodyRecord2D& record, const glm::vec2& position, const glm::vec2& velocity, float seconds, bool skipVelocityCheck);
        [[nodiscard]] glm::vec2 CancelVelocityTowardsSteepSlopes(const BodyRecord2D& record, const glm::vec2& velocity) const;
        [[nodiscard]] bool CanWalkStairs(const BodyRecord2D& record, const glm::vec2& velocity) const;
        bool WalkStairs(BodyRecord2D& record, glm::vec2& position, float seconds, const glm::vec2& stepUp, const glm::vec2& stepForward, const glm::vec2& stepForwardTest, const glm::vec2& velocity);
        bool StickToFloor(BodyRecord2D& record, glm::vec2& position, const glm::vec2& stepDown, const glm::vec2& velocity, float seconds);
        void Settle(BodyRecord2D& record, glm::vec2& position, const glm::vec2& velocity, float seconds);
        void MoveCharacter(BodyRecord2D& record, glm::vec2& position, const glm::vec2& velocity, float seconds);
        void PushBodies(const BodyRecord2D& record, const glm::vec2& velocity, float seconds);
        [[nodiscard]] glm::vec2 KeepVelocity(const BodyRecord2D& record, const glm::vec2& velocity) const;
        [[nodiscard]] glm::vec2 Predict(const CarryingBody2D& carrier, const glm::vec2& position, float seconds) const;
        [[nodiscard]] glm::vec2 GetGroundVelocity(const BodyRecord2D& record, const glm::vec2& position) const;
        [[nodiscard]] glm::vec2 GetContactVelocity(const BodyRecord2D& record, b2ShapeId shape, b2BodyId body, const glm::vec2& point) const;

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

        [[nodiscard]] const ShapeOwner2D* FindShape(b2ShapeId shape) const
        {
            const auto a_Found = ShapeOwners.find(GetShapeKey(shape));

            return a_Found != ShapeOwners.end() ? &a_Found->second : nullptr;
        }

        Scene& Owner;
        PhysicsWorld2D World;
        // The world's Box2D world and layers, which the scene, a friend of the world, takes when it is made
        b2WorldId WorldID = b2_nullWorldId;
        CollisionLayers Layers;
        // In the order they were made, which as the scene starts is hierarchy order
        PhysicsVector<BodyRecord2D*> Records;
        PhysicsVector<JointRecord2D> Joints;
        std::uint32_t NextSerial = 0;
        // Each entity whose colliders a record holds, its owner's included, and the record
        std::unordered_map<entt::entity, BodyRecord2D*, std::hash<entt::entity>, std::equal_to<entt::entity>, TaggedAllocator<std::pair<const entt::entity, BodyRecord2D*>, MemoryTag::Physics>> Members;
        // Entities whose RigidBody2D, CharacterController2D, 2D colliders or Joint2D were added, changed through Patch or Replace, or removed since the last frame with steps, and RigidBody and CharacterController ones, which take entities from the 2D world
        PhysicsVector<entt::entity> Dirty;
        PhysicsVector<entt::entity> DirtyJoints;
        // Joints let go with a body that went, to be found again once the scene's changes are all taken in
        PhysicsVector<entt::entity> RetryJoints;
        // Every shape made, by its key. Box2D tells of a shape taken out between steps ending its touches with the next step, so a shape taken out is known until then, as Retired lists
        std::unordered_map<std::uint64_t, ShapeOwner2D, std::hash<std::uint64_t>, std::equal_to<std::uint64_t>, TaggedAllocator<std::pair<const std::uint64_t, ShapeOwner2D>, MemoryTag::Physics>> ShapeOwners;
        PhysicsVector<std::uint64_t> Retired;
        PhysicsVector<b2ShapeId> ShapeIDs;
        PhysicsVector<b2BodyId> Woken;
        b2BodyId Ground = b2_nullBodyId;
        ContactPairs2D Contacts;
        ContactPairs2D Triggers;
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
        // What characters move with, kept so a step allocates nothing once they have room: the shapes a query found, the contacts and planes of a move, the normals stairs are walked against, the bodies a character pushes, and the last step's length, which a character made between steps takes
        PhysicsVector<b2ShapeId> CharacterShapes;
        PhysicsVector<CharacterContact2D> MoveContacts;
        PhysicsVector<b2CollisionPlane> Planes;
        PhysicsVector<glm::vec2> SteepNormals;
        PhysicsVector<BodyPush2D> Pushes;
        float LastSeconds = 1.0f / 60.0f;
    };

    // Bodies are found in hierarchy order, as in 3D. An entity with a RigidBody or a CharacterController, and everything below it, belongs to the 3D world, so 2D components there are reported and left out
    PhysicsScene2D::PhysicsScene2D(Scene& scene, const CollisionLayers& layers)
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's physics is made on the main thread.");

        m_State = Memory::New<State>(MemoryTag::Physics, scene, layers);
        State& l_State = *m_State;
        l_State.WorldID = l_State.World.m_State->World;
        l_State.Layers = layers;

        SceneRegistry& l_Registry = scene.GetRegistry();
        std::unordered_map<entt::entity, std::size_t> l_Owners;
        std::vector<entt::entity> l_Joined;
        constexpr std::size_t c_NoOwner = SIZE_MAX;
        // An entity with a RigidBody or a CharacterController, and everything below it, belongs to the 3D world    
        constexpr std::size_t c_Owned3D = SIZE_MAX - 1;
        for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            const entt::entity l_Handle = it_Entity.GetHandle();
            const entt::entity l_Parent = l_Registry.get<RelationshipComponent>(l_Handle).Parent;
            const std::size_t l_Inherited = l_Parent != entt::null ? l_Owners[l_Parent] : c_NoOwner;
            const bool l_Character3D = l_Registry.all_of<CharacterControllerComponent>(l_Handle);
            const bool l_Body3D = l_Character3D || l_Registry.all_of<RigidBodyComponent>(l_Handle);
            const bool l_Moves = l_Registry.any_of<RigidBody2DComponent, CharacterController2DComponent>(l_Handle);

            std::size_t l_Owner = l_Inherited;
            if (l_Body3D)
            {
                if (l_Moves && l_Character3D)
                {
                    TR_CORE_WARN("Physics: {} has both a CharacterController and a {}, so it is a 3D character and the 2D one is left out", GetName(scene, l_Handle), l_Registry.all_of<CharacterController2DComponent>(l_Handle) ? "CharacterController2D" : "RigidBody2D");
                }
                else if (l_Moves)
                {
                    TR_CORE_WARN("Physics: {} has both a RigidBody and a {}, so it keeps its 3D body and the 2D one is left out", GetName(scene, l_Handle), l_Registry.all_of<CharacterController2DComponent>(l_Handle) ? "CharacterController2D" : "RigidBody2D");
                }

                l_Owner = c_Owned3D;
            }
            else if (l_Moves || (l_Inherited == c_NoOwner && Has2DCollider(l_Registry, l_Handle)))
            {
                BodyRecord2D& l_Record = *l_State.Records.emplace_back(Memory::New<BodyRecord2D>(MemoryTag::Physics));
                ReadOwner(l_Registry, l_Handle, l_Record);
                l_Record.Serial = l_State.NextSerial++;
                l_Owner = l_State.Records.size() - 1;
            }

            l_Owners[l_Handle] = l_Owner;
            if (l_Owner == c_Owned3D && Has2DCollider(l_Registry, l_Handle))
            {
                TR_CORE_WARN("Physics: {}'s 2D colliders are part of a 3D body or character, so they are left out", GetName(scene, l_Handle));
            }
            else if (l_Owner != c_NoOwner && l_Owner != c_Owned3D)
            {
                AddColliders(l_Registry, l_Handle, *l_State.Records[l_Owner]);
            }

            if (l_Registry.all_of<Joint2DComponent>(l_Handle))
            {
                l_Joined.push_back(l_Handle);
            }
        }

        for (BodyRecord2D* it_Record : l_State.Records)
        {
            l_State.Index(*it_Record);
            if (it_Record->HasRigidBody && it_Record->Colliders.empty() && !it_Record->HasCharacter)
            {
                TR_CORE_WARN("Physics: {} has a RigidBody2D and no 2D colliders on it or below it, so it has no body", GetName(scene, it_Record->Entity));

                continue;
            }

            l_State.MakeBody(*it_Record);
        }

        for (const entt::entity it_Entity : l_Joined)
        {
            l_State.CreateJointRecord(it_Entity);
        }

        for (JointRecord2D& it_Joint : l_State.Joints)
        {
            l_State.MakeJoint(it_Joint);
        }

        // From here on, 2D bodies, characters, colliders and joints added, changed through Patch or Replace, or removed are taken in at the next frame with steps, and so are RigidBodies and CharacterControllers, which take entities from the 2D world or give them back
        const auto a_Watch = [&l_Registry, &l_State]<Component T>()
        {
            l_Registry.on_construct<T>().template connect<&State::OnBodyChanged>(l_State);
            l_Registry.on_update<T>().template connect<&State::OnBodyChanged>(l_State);
            l_Registry.on_destroy<T>().template connect<&State::OnBodyChanged>(l_State);
        };

        a_Watch.template operator() < RigidBody2DComponent > ();
        a_Watch.template operator() < CharacterController2DComponent > ();
        a_Watch.template operator() < RigidBodyComponent > ();
        a_Watch.template operator() < CharacterControllerComponent > ();
        a_Watch.template operator() < BoxCollider2DComponent > ();
        a_Watch.template operator() < CircleCollider2DComponent > ();
        a_Watch.template operator() < CapsuleCollider2DComponent > ();
        a_Watch.template operator() < PolygonCollider2DComponent > ();
        l_Registry.on_construct<Joint2DComponent>().connect<&State::OnJointChanged>(l_State);
        l_Registry.on_update<Joint2DComponent>().connect<&State::OnJointChanged>(l_State);
        l_Registry.on_destroy<Joint2DComponent>().connect<&State::OnJointChanged>(l_State);

        TR_CORE_INFO("Physics: the 2D scene starts with {} bod(ies)", GetBodyCount());
    }

    // The world goes first, and the drawn poses with it, so the scene draws from its transforms again
    PhysicsScene2D::~PhysicsScene2D()
    {
        Scene& l_Scene = m_State->Owner;
        SceneRegistry& l_Registry = l_Scene.GetRegistry();
        const auto a_Unwatch = [&l_Registry, this]<Component T>()
        {
            l_Registry.on_construct<T>().disconnect(m_State);
            l_Registry.on_update<T>().disconnect(m_State);
            l_Registry.on_destroy<T>().disconnect(m_State);
        };

        a_Unwatch.template operator() < RigidBody2DComponent > ();
        a_Unwatch.template operator() < CharacterController2DComponent > ();
        a_Unwatch.template operator() < RigidBodyComponent > ();
        a_Unwatch.template operator() < CharacterControllerComponent > ();
        a_Unwatch.template operator() < BoxCollider2DComponent > ();
        a_Unwatch.template operator() < CircleCollider2DComponent > ();
        a_Unwatch.template operator() < CapsuleCollider2DComponent > ();
        a_Unwatch.template operator() < PolygonCollider2DComponent > ();
        a_Unwatch.template operator() < Joint2DComponent > ();

        Memory::Delete(m_State);
        l_Scene.GetRegistry().clear<PhysicsPoseComponent>();
    }

    bool PhysicsScene2D::IsNeeded(Scene& scene)
    {
        SceneRegistry& l_Registry = scene.GetRegistry();

        return !l_Registry.storage<RigidBody2DComponent>().empty() || !l_Registry.storage<CharacterController2DComponent>().empty() || !l_Registry.storage<BoxCollider2DComponent>().empty() || !l_Registry.storage<CircleCollider2DComponent>().empty() || !l_Registry.storage<CapsuleCollider2DComponent>().empty() || !l_Registry.storage<PolygonCollider2DComponent>().empty();
    }

    // What changed in the scene since the last frame with steps is taken in, so a frame without steps changes nothing and the next one with steps takes it all. A frame without steps leaves the kinematic targets where they were, so the next frame with steps moves from there. A kinematic body takes its entity's depth as it goes
    void PhysicsScene2D::BeginFrame(std::uint32_t steps)
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        l_State.Events.clear();
        if (steps == 0)
        {
            return;
        }

        l_State.Sync();
        l_State.FollowTransforms();

        l_State.Steps = steps;
        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (BodyRecord2D* it_Record : l_State.Records)
        {
            if (it_Record->Motion != BodyMotion::Kinematic || it_Record->HasCharacter || B2_IS_NULL(it_Record->Body) || !l_Registry.valid(it_Record->Entity))
            {
                continue;
            }

            const glm::mat4 l_World = l_State.Owner.ComputeWorldMatrix(Entity(it_Record->Entity, &l_State.Owner));
            it_Record->TargetFrom = it_Record->TargetTo;
            it_Record->TargetTo = GetPose(l_World);
            it_Record->Depth = l_World[3][2];
        }
    }

    // What the scene's scripts changed since the frame began is taken in, kinematic bodies are moved for the step, then characters, the world steps, what carries a character takes it along, events are paired, and dynamic bodies and characters that moved are written back in hierarchy order, each at its depth and turned about Z alone
    void PhysicsScene2D::Step(Timestep step, std::uint32_t index)
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        l_State.Sync();
        const float l_Factor = static_cast<float>(std::min(index + 1, l_State.Steps)) / static_cast<float>(l_State.Steps);
        for (BodyRecord2D* it_Record : l_State.Records)
        {
            if (it_Record->Motion == BodyMotion::Kinematic && !it_Record->HasCharacter && B2_IS_NON_NULL(it_Record->Body))
            {
                const bool l_Moving = it_Record->TargetFrom.Position != it_Record->TargetTo.Position || it_Record->TargetFrom.Angle != it_Record->TargetTo.Angle;
                MoveToTarget(it_Record->Body, ToBox2D(Interpolate(it_Record->TargetFrom, it_Record->TargetTo, l_Factor)), step.GetSeconds(), l_Moving);
            }
        }

        l_State.MoveCharacters(step.GetSeconds());
        l_State.World.Step(step);
        l_State.CarryCharacters();
        ++l_State.StepCount;
        l_State.PairEvents(index);
        l_State.BreakJoints(index);
        l_State.GatherContactPoints();

        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (BodyRecord2D* it_Record : l_State.Records)
        {
            if (it_Record->Motion == BodyMotion::Static || B2_IS_NULL(it_Record->Body))
            {
                continue;
            }

            it_Record->Previous = it_Record->Current;
            it_Record->Current = ReadPose(it_Record->Body);
            if ((it_Record->Motion != BodyMotion::Dynamic && !it_Record->HasCharacter) || !l_Registry.valid(it_Record->Entity) || (it_Record->Previous.Position == it_Record->Current.Position && it_Record->Previous.Angle == it_Record->Current.Angle))
            {
                continue;
            }

            TransformComponent& l_Transform = l_Registry.get<TransformComponent>(it_Record->Entity);
            const glm::vec3 l_Position(it_Record->Current.Position, it_Record->Depth);
            const glm::quat l_Rotation = glm::angleAxis(it_Record->Current.Angle, glm::vec3(0.0f, 0.0f, 1.0f));
            const entt::entity l_Parent = l_Registry.get<RelationshipComponent>(it_Record->Entity).Parent;
            if (l_Parent == entt::null)
            {
                l_Transform.Position = l_Position;
                l_Transform.Rotation = l_Rotation;
            }
            else
            {
                const glm::mat4 l_ParentWorld = l_State.Owner.ComputeWorldMatrix(Entity(l_Parent, &l_State.Owner));
                l_Transform.Position = glm::vec3(glm::inverse(l_ParentWorld) * glm::vec4(l_Position, 1.0f));
                l_Transform.Rotation = glm::normalize(glm::inverse(GetRotation(l_ParentWorld)) * l_Rotation);
            }

            it_Record->WrittenPosition = l_Transform.Position;
            it_Record->WrittenRotation = l_Transform.Rotation;
        }
    }

    // Moving bodies and characters are drawn between their last two steps, at their depth
    void PhysicsScene2D::EndFrame(float interpolation)
    {
        State& l_State = *m_State;
        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (const BodyRecord2D* it_Record : l_State.Records)
        {
            if (it_Record->Motion == BodyMotion::Static || B2_IS_NULL(it_Record->Body) || !l_Registry.valid(it_Record->Entity))
            {
                continue;
            }

            const Pose2D l_Pose = Interpolate(it_Record->Previous, it_Record->Current, interpolation);
            l_Registry.emplace_or_replace<PhysicsPoseComponent>(it_Record->Entity, glm::vec3(l_Pose.Position, it_Record->Depth), glm::angleAxis(l_Pose.Angle, glm::vec3(0.0f, 0.0f, 1.0f)));
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
        const std::uint32_t l_Moving = static_cast<std::uint32_t>(std::ranges::count_if(m_State->Records, [](const BodyRecord2D* record) { return record->Motion != BodyMotion::Static && B2_IS_NON_NULL(record->Body); }));
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

        OverlapContext l_Context{ &filter,{} };
        const b2ShapeProxy l_Proxy = MakeProxy(shape, position, angle);
        b2World_OverlapShape(m_State->WorldID, &l_Proxy, MakeQueryFilter(filter), &GatherOverlap, &l_Context);

        std::ranges::sort(l_Context.Hits, [](const PhysicsHit2D& left, const PhysicsHit2D& right) { return left.Entity < right.Entity; });
        const auto [a_First, a_Last] = std::ranges::unique(l_Context.Hits, {}, &PhysicsHit2D::Entity);
        l_Context.Hits.erase(a_First, a_Last);

        return std::move(l_Context.Hits);
    }

    // A static body is still, and reads so. A character's velocity is read with GetCharacterVelocity
    PhysicsBodyResult PhysicsScene2D::GetVelocity(UUID entity, glm::vec2& linear, float& angular)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's bodies are reached on the main thread.");

        const BodyRecord2D* l_Record = m_State->FindBody(entity);
        if (l_Record == nullptr)
        {
            return PhysicsBodyResult::NoBody;
        }

        if (l_Record->HasCharacter)
        {
            return PhysicsBodyResult::WrongMotion;
        }

        linear = l_Record->Motion != BodyMotion::Static ? FromBox2D(b2Body_GetLinearVelocity(l_Record->Body)) : glm::vec2(0.0f);
        angular = l_Record->Motion != BodyMotion::Static ? b2Body_GetAngularVelocity(l_Record->Body) : 0.0f;

        return PhysicsBodyResult::Done;
    }

    namespace
    {
        // A dynamic 2D body, or why there is none, for each call that pushes or sets a velocity. A character is kinematic, so it is no dynamic body
        template<typename F>
        PhysicsBodyResult OnDynamic(const BodyRecord2D* record, F call)
        {
            if (record == nullptr)
            {
                return PhysicsBodyResult::NoBody;
            }

            if (record->Motion != BodyMotion::Dynamic)
            {
                return PhysicsBodyResult::WrongMotion;
            }

            call(record->Body);

            return PhysicsBodyResult::Done;
        }
    }

    PhysicsBodyResult PhysicsScene2D::SetLinearVelocity(UUID entity, const glm::vec2& velocity)
    {
        return OnDynamic(m_State->FindBody(entity), [&](b2BodyId body) { b2Body_SetLinearVelocity(body, ToBox2D(velocity)); b2Body_SetAwake(body, true); });
    }

    PhysicsBodyResult PhysicsScene2D::SetAngularVelocity(UUID entity, float velocity)
    {
        return OnDynamic(m_State->FindBody(entity), [&](b2BodyId body) { b2Body_SetAngularVelocity(body, velocity); b2Body_SetAwake(body, true); });
    }

    PhysicsBodyResult PhysicsScene2D::AddForce(UUID entity, const glm::vec2& force)
    {
        return OnDynamic(m_State->FindBody(entity), [&](b2BodyId body) { b2Body_ApplyForceToCenter(body, ToBox2D(force), true); });
    }

    PhysicsBodyResult PhysicsScene2D::AddForceAtPoint(UUID entity, const glm::vec2& force, const glm::vec2& point)
    {
        return OnDynamic(m_State->FindBody(entity), [&](b2BodyId body) { b2Body_ApplyForce(body, ToBox2D(force), ToBox2D(point), true); });
    }

    PhysicsBodyResult PhysicsScene2D::AddTorque(UUID entity, float torque)
    {
        return OnDynamic(m_State->FindBody(entity), [&](b2BodyId body) { b2Body_ApplyTorque(body, torque, true); });
    }

    PhysicsBodyResult PhysicsScene2D::AddImpulse(UUID entity, const glm::vec2& impulse)
    {
        return OnDynamic(m_State->FindBody(entity), [&](b2BodyId body) { b2Body_ApplyLinearImpulseToCenter(body, ToBox2D(impulse), true); });
    }

    PhysicsBodyResult PhysicsScene2D::AddImpulseAtPoint(UUID entity, const glm::vec2& impulse, const glm::vec2& point)
    {
        return OnDynamic(m_State->FindBody(entity), [&](b2BodyId body) { b2Body_ApplyLinearImpulse(body, ToBox2D(impulse), ToBox2D(point), true); });
    }

    PhysicsBodyResult PhysicsScene2D::AddAngularImpulse(UUID entity, float impulse)
    {
        return OnDynamic(m_State->FindBody(entity), [&](b2BodyId body) { b2Body_ApplyAngularImpulse(body, impulse, true); });
    }

    // The entity's transform is set first, at its depth and turned about Z alone, so the body or character is placed where the transform puts it, and the next frame does not move it again
    PhysicsBodyResult PhysicsScene2D::Teleport(UUID entity, const glm::vec2& position, float angle)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's bodies are reached on the main thread.");

        State& l_State = *m_State;
        BodyRecord2D* l_Record = l_State.FindBody(entity);
        if (l_Record == nullptr)
        {
            return PhysicsBodyResult::NoBody;
        }

        const Entity l_Owner(l_Record->Entity, &l_State.Owner);
        TransformComponent l_World = l_State.Owner.ComputeWorldTransform(l_Owner);
        l_World.Position = glm::vec3(position, l_World.Position.z);
        l_World.Rotation = glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f));
        l_State.Owner.SetWorldTransform(l_Owner, l_World);
        l_State.PlaceAtTransform(*l_Record);

        return PhysicsBodyResult::Done;
    }

    PhysicsBodyResult PhysicsScene2D::MoveKinematic(UUID entity, const glm::vec2& position, float angle)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's bodies are reached on the main thread.");

        State& l_State = *m_State;
        const BodyRecord2D* l_Record = l_State.FindBody(entity);
        if (l_Record == nullptr)
        {
            return PhysicsBodyResult::NoBody;
        }

        if (l_Record->Motion != BodyMotion::Kinematic || l_Record->HasCharacter)
        {
            return PhysicsBodyResult::WrongMotion;
        }

        const Entity l_Owner(l_Record->Entity, &l_State.Owner);
        TransformComponent l_World = l_State.Owner.ComputeWorldTransform(l_Owner);
        l_World.Position = glm::vec3(position, l_World.Position.z);
        l_World.Rotation = glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f));
        l_State.Owner.SetWorldTransform(l_Owner, l_World);

        return PhysicsBodyResult::Done;
    }

    PhysicsBodyResult PhysicsScene2D::SetAwake(UUID entity, bool awake)
    {
        const BodyRecord2D* l_Record = m_State->FindBody(entity);
        if (l_Record == nullptr)
        {
            return PhysicsBodyResult::NoBody;
        }

        if (l_Record->Motion == BodyMotion::Static || l_Record->HasCharacter)
        {
            return PhysicsBodyResult::WrongMotion;
        }

        b2Body_SetAwake(l_Record->Body, awake);

        return PhysicsBodyResult::Done;
    }

    PhysicsBodyResult PhysicsScene2D::IsAwake(UUID entity, bool& awake)
    {
        const BodyRecord2D* l_Record = m_State->FindBody(entity);
        if (l_Record == nullptr)
        {
            return PhysicsBodyResult::NoBody;
        }

        if (l_Record->Motion == BodyMotion::Static || l_Record->HasCharacter)
        {
            return PhysicsBodyResult::WrongMotion;
        }

        awake = b2Body_IsAwake(l_Record->Body);

        return PhysicsBodyResult::Done;
    }

    std::optional<bool> PhysicsScene2D::IsJointBroken(UUID entity) const
    {
        const auto a_Found = std::ranges::find(m_State->Joints, entity, &JointRecord2D::ID);

        return a_Found != m_State->Joints.end() ? std::optional(a_Found->Broken) : std::nullopt;
    }

    namespace
    {
        // A character's record, or why there is none, for each call on a character
        template<typename F>
        PhysicsBodyResult OnCharacter(BodyRecord2D* record, F call)
        {
            if (record == nullptr || !record->HasCharacter)
            {
                return record == nullptr ? PhysicsBodyResult::NoBody : PhysicsBodyResult::WrongMotion;
            }

            call(*record);

            return PhysicsBodyResult::Done;
        }
    }

    PhysicsBodyResult PhysicsScene2D::SetCharacterVelocity(UUID entity, const glm::vec2& velocity)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's characters are reached on the main thread.");

        return OnCharacter(m_State->FindBody(entity), [&velocity](BodyRecord2D& record) { record.Velocity = velocity; });
    }

    PhysicsBodyResult PhysicsScene2D::GetCharacterVelocity(UUID entity, glm::vec2& velocity)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's characters are reached on the main thread.");

        return OnCharacter(m_State->FindBody(entity), [&velocity](const BodyRecord2D& record) { velocity = record.Velocity; });
    }

    // The ground the character's last move left it on, the body it is part of found from its user data, and how fast that body moves under the character's feet
    PhysicsBodyResult PhysicsScene2D::GetCharacterGround(UUID entity, CharacterGround2D& ground)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's characters are reached on the main thread.");

        const State& l_State = *m_State;

        return OnCharacter(m_State->FindBody(entity), [&ground, &l_State](const BodyRecord2D& record)
        {
            ground = {};
            ground.Grounded = record.Ground == GroundState2D::OnGround;
            if (record.Ground != GroundState2D::OnGround && record.Ground != GroundState2D::OnSteepGround)
            {
                return;
            }

            ground.Normal = record.GroundNormal;
            ground.Velocity = l_State.GetGroundVelocity(record, l_State.GetFoot(record, FromBox2D(b2Body_GetPosition(record.Body))));
            const BodyRecord2D* l_Ground = B2_IS_NON_NULL(record.GroundBody) && b2Body_IsValid(record.GroundBody) ? GetRecord(record.GroundBody) : nullptr;
            ground.Entity = l_Ground != nullptr ? l_Ground->ID : UUID();
        });
    }

    // The body sits at its entity's place and turn about Z, and keeps its depth in Z for write-back, with its colliders. A record with no colliders has no body, unless it is a character, whose kinematic body holds its capsule and never sleeps, so it stays where triggers and other bodies see it. A character made starts still, with the shapes about it already met
    void PhysicsScene2D::State::MakeBody(BodyRecord2D& record)
    {
        Scene& l_Scene = Owner;
        if (!l_Scene.GetRegistry().valid(record.Entity) || (record.Colliders.empty() && !record.HasCharacter))
        {
            return;
        }

        if (record.HasCharacter && l_Scene.GetRegistry().all_of<RigidBody2DComponent>(record.Entity))
        {
            TR_CORE_WARN("Physics: {} has a CharacterController2D and a RigidBody2D, so it is a character and the RigidBody2D is left out", GetName(l_Scene, record.Entity));
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
        l_Body.enableSleep = !record.HasCharacter;
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
        MakeShapes(record);
        MakeCharacterShape(record);
        ApplyMass(record);

        const TransformComponent& l_Transform = l_Scene.GetRegistry().get<TransformComponent>(record.Entity);
        record.Previous = l_Pose;
        record.Current = l_Pose;
        record.TargetFrom = l_Pose;
        record.TargetTo = l_Pose;
        record.WrittenPosition = l_Transform.Position;
        record.WrittenRotation = l_Transform.Rotation;
        if (record.HasCharacter)
        {
            record.Velocity = glm::vec2(0.0f);
            record.Carrier = {};
            UpdateGround(record, l_Pose.Position, record.Velocity, LastSeconds, true);
        }
    }

    // Each collider placed on the body from its entity's world transform with the offset and turn, by the whole of the matrix in X and Y, so a box or polygon takes any scale, mirror or skew exactly, a circle the larger of its scales and a capsule its scale across for its radius. Placed against the body's entity as it is now, which is where the body is or is going. Every shape reports contacts and triggers, and a trigger weighs nothing
    void PhysicsScene2D::State::MakeShapes(BodyRecord2D& record)
    {
        Scene& l_Scene = Owner;
        const Pose2D l_Pose = GetPose(l_Scene.ComputeWorldMatrix(Entity(record.Entity, &l_Scene)));
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
            // A one-way collider's up, along its own Y axis, in the body's space
            const b2Vec2 l_OneWay = it_Collider.Settings.OneWay && glm::length(l_AxisY) > 1e-6f ? ToBox2D(Rotate(glm::normalize(l_AxisY), -l_Pose.Angle)) : b2Vec2{ 0.0f, 0.0f };
            const auto a_Known = [this, &record, l_OneWay](b2ShapeId shape) { ShapeOwners[GetShapeKey(shape)] = { record.Serial, &record, l_OneWay }; };

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
                    a_Known(b2CreatePolygonShape(record.Body, &l_Shape, &l_Polygon));

                    break;
                }
                case ColliderKind2D::Circle:
                {
                    if (std::abs(l_ScaleX - l_ScaleY) > c_ScaleTolerance * std::max({ l_ScaleX, l_ScaleY, 1.0f }) && !std::exchange(WarnedRoundScale, true))
                    {
                        TR_CORE_WARN("Physics: {}'s round 2D collider is scaled unevenly, so it takes the larger scale. Later ones scaled so are not reported", l_Name);
                    }

                    const b2Circle l_Circle{ a_ToBody(glm::vec2(0.0f)), std::max(it_Collider.Size.x * std::max(l_ScaleX, l_ScaleY), c_MinimumSize) };
                    a_Known(b2CreateCircleShape(record.Body, &l_Shape, &l_Circle));

                    break;
                }
                case ColliderKind2D::Capsule:
                {
                    const float l_HalfSegment = std::max(it_Collider.Size.y * 0.5f - it_Collider.Size.x, 0.0f);
                    const float l_Radius = std::max(it_Collider.Size.x * l_ScaleX, c_MinimumSize);
                    if (l_HalfSegment * l_ScaleY < c_MinimumSize)
                    {
                        const b2Circle l_Circle{ a_ToBody(glm::vec2(0.0f)), l_Radius };
                        a_Known(b2CreateCircleShape(record.Body, &l_Shape, &l_Circle));

                        break;
                    }

                    const b2Capsule l_Capsule{ a_ToBody(glm::vec2(0.0f, -l_HalfSegment)), a_ToBody(glm::vec2(0.0f, l_HalfSegment)), l_Radius };
                    a_Known(b2CreateCapsuleShape(record.Body, &l_Shape, &l_Capsule));

                    break;
                }
            }
        }
    }

    // A dynamic body's mass and inertia from its colliders, scaled to the mass asked for
    void PhysicsScene2D::State::ApplyMass(const BodyRecord2D& record) const
    {
        if (record.Motion != BodyMotion::Dynamic || B2_IS_NULL(record.Body))
        {
            return;
        }

        b2Body_ApplyMassFromShapes(record.Body);
        if (record.Settings.Mass > 0.0f)
        {
            b2MassData l_Mass = b2Body_GetMassData(record.Body);
            const float l_Scale = l_Mass.mass > 0.0f ? record.Settings.Mass / l_Mass.mass : 1.0f;
            l_Mass.mass = record.Settings.Mass;
            l_Mass.rotationalInertia *= l_Scale;
            b2Body_SetMassData(record.Body, l_Mass);
        }
    }

    // Its frame is its entity's world transform in the XY plane as it is made, with the anchor as an offset and the axis turned with it, and where it meets each body is taken from where the bodies are then. Box2D holds it on its bodies' own frames, which a change of their colliders, mass or motion leaves where they are, so it keeps the pose it was made in. The joint's own body is Box2D's second, and the body it joins, or the ground, its first, so a revolute joint's turn, a prismatic joint's travel and a motor's drive count as its own body's against the other's. A joint that holds no dynamic body is reported and left out, and one that joins a body with no 2D colliders is reported once and made when the body has some again
    void PhysicsScene2D::State::MakeJoint(JointRecord2D& joint)
    {
        Scene& l_Scene = Owner;
        SceneRegistry& l_Registry = l_Scene.GetRegistry();
        if (!l_Registry.valid(joint.Entity))
        {
            return;
        }

        const std::string l_Name = GetName(l_Scene, joint.Entity);
        const BodyRecord2D& l_Own = *joint.First;
        const BodyRecord2D* l_Other = joint.Second;
        if (B2_IS_NULL(l_Own.Body) || (l_Other != nullptr && B2_IS_NULL(l_Other->Body)))
        {
            if (!std::exchange(joint.NeedsBody, true))
            {
                TR_CORE_WARN("Physics: {}'s Joint2D joins a body with no 2D colliders, so it waits until the body has some", l_Name);
            }

            return;
        }

        joint.NeedsBody = false;

        if (l_Own.HasCharacter || (l_Other != nullptr && l_Other->HasCharacter))
        {
            TR_CORE_WARN("Physics: {}'s Joint2D joins a character now, which a joint cannot hold, so it is left out", l_Name);

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
            it_Joint.Broken = true;
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

        for (const BodyRecord2D* it_Record : Records)
        {
            if (it_Record->Motion == BodyMotion::Static || B2_IS_NULL(it_Record->Body))
            {
                continue;
            }

            ContactData.resize(static_cast<std::size_t>(b2Body_GetContactCapacity(it_Record->Body)));
            const int l_Count = b2Body_GetContactData(it_Record->Body, ContactData.data(), static_cast<int>(ContactData.size()));
            for (int it_Contact = 0; it_Contact < l_Count; ++it_Contact)
            {
                const b2ContactData& l_Data = ContactData[static_cast<std::size_t>(it_Contact)];
                const BodyRecord2D* l_First = GetRecord(b2Shape_GetBody(l_Data.shapeIdA));
                const BodyRecord2D* l_Second = GetRecord(b2Shape_GetBody(l_Data.shapeIdB));
                const BodyRecord2D* l_Other = l_First == it_Record ? l_Second : l_First;
                if (l_Other != nullptr && l_Other->Motion != BodyMotion::Static && l_Other->Serial < it_Record->Serial)
                {
                    continue;
                }

                for (int it_Point = 0; it_Point < l_Data.manifold.pointCount; ++it_Point)
                {
                    const b2ManifoldPoint& l_Point = l_Data.manifold.points[it_Point];
                    if (l_Point.separation <= c_TouchingSlop)
                    {
                        ContactPoints.push_back({ glm::vec3(FromBox2D(l_Point.point), it_Record->Depth), glm::vec3(FromBox2D(l_Data.manifold.normal), 0.0f) });
                    }
                }
            }
        }
    }

    // Box2D's begin and end events, for contacts and for triggers, are counted by body pair. A pair's first shape pair begins it and its last ends it, so a body whose colliders were made again keeps the pairs it still touches. A contact pair touching through the step with one body awake stays. Each step's events go out sorted by their entities. Shapes taken out before the step are forgotten after it, their last touches counted
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
                    // Only a beginning adds, so there is one, and its shapes and records are there
                    const BodyRecord2D& l_FirstRecord = *l_Added->FirstRecord;
                    const BodyRecord2D& l_SecondRecord = *l_Added->SecondRecord;
                    // A trigger's own body comes first, and a contact's bodies in UUID order
                    const bool l_Swapped = !trigger && l_SecondRecord.ID < l_FirstRecord.ID;
                    ContactPair2D l_Pair;
                    l_Pair.Count = l_After;
                    l_Pair.Begun = StepCount;
                    l_Pair.First = l_Swapped ? l_SecondRecord.ID : l_FirstRecord.ID;
                    l_Pair.Second = l_Swapped ? l_FirstRecord.ID : l_SecondRecord.ID;
                    l_Pair.FirstRecord = &l_FirstRecord;
                    l_Pair.SecondRecord = &l_SecondRecord;
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

        // Contacts by their bodies' records in serial order, with the normal turned to match
        const b2ContactEvents l_Contacts = b2World_GetContactEvents(l_World);
        for (int it_Event = 0; it_Event < l_Contacts.beginCount; ++it_Event)
        {
            const b2ContactBeginTouchEvent& l_Begin = l_Contacts.beginEvents[it_Event];
            const ShapeOwner2D* l_A = FindShape(l_Begin.shapeIdA);
            const ShapeOwner2D* l_B = FindShape(l_Begin.shapeIdB);
            if (l_A == nullptr || l_B == nullptr)
            {
                continue;
            }

            glm::vec2 l_Point{ 0.0f };
            for (int it_Point = 0; it_Point < l_Begin.manifold.pointCount; ++it_Point)
            {
                l_Point += FromBox2D(l_Begin.manifold.points[it_Point].point);
            }

            l_Point /= static_cast<float>(std::max(l_Begin.manifold.pointCount, 1));
            glm::vec2 l_Normal = FromBox2D(l_Begin.manifold.normal);
            if (l_A->Serial > l_B->Serial)
            {
                std::swap(l_A, l_B);
                l_Normal = -l_Normal;
            }

            Raw.push_back({ l_A->Serial, l_B->Serial, true, l_Point, l_Normal, l_A->Record, l_B->Record });
        }

        for (int it_Event = 0; it_Event < l_Contacts.endCount; ++it_Event)
        {
            const b2ContactEndTouchEvent& l_End = l_Contacts.endEvents[it_Event];
            const ShapeOwner2D* l_A = FindShape(l_End.shapeIdA);
            const ShapeOwner2D* l_B = FindShape(l_End.shapeIdB);
            if (l_A != nullptr && l_B != nullptr)
            {
                Raw.push_back({ std::min(l_A->Serial, l_B->Serial), std::max(l_A->Serial, l_B->Serial), false, glm::vec2(0.0f), glm::vec2(0.0f), nullptr, nullptr });
            }
        }

        a_Count(Contacts, false);

        // Triggers by the trigger's body first, never its own body
        const b2SensorEvents l_Sensors = b2World_GetSensorEvents(l_World);
        for (int it_Event = 0; it_Event < l_Sensors.beginCount; ++it_Event)
        {
            const ShapeOwner2D* l_Sensor = FindShape(l_Sensors.beginEvents[it_Event].sensorShapeId);
            const ShapeOwner2D* l_Visitor = FindShape(l_Sensors.beginEvents[it_Event].visitorShapeId);
            if (l_Sensor != nullptr && l_Visitor != nullptr && l_Sensor->Serial != l_Visitor->Serial)
            {
                Raw.push_back({ l_Sensor->Serial, l_Visitor->Serial, true, glm::vec2(0.0f), glm::vec2(0.0f), l_Sensor->Record, l_Visitor->Record });
            }
        }

        for (int it_Event = 0; it_Event < l_Sensors.endCount; ++it_Event)
        {
            const ShapeOwner2D* l_Sensor = FindShape(l_Sensors.endEvents[it_Event].sensorShapeId);
            const ShapeOwner2D* l_Visitor = FindShape(l_Sensors.endEvents[it_Event].visitorShapeId);
            if (l_Sensor != nullptr && l_Visitor != nullptr && l_Sensor->Serial != l_Visitor->Serial)
            {
                Raw.push_back({ l_Sensor->Serial, l_Visitor->Serial, false, glm::vec2(0.0f), glm::vec2(0.0f), nullptr, nullptr });
            }
        }

        a_Count(Triggers, true);

        for (const std::uint64_t it_Key : Retired)
        {
            ShapeOwners.erase(it_Key);
        }

        Retired.clear();
        for (const auto& [it_Key, it_Pair] : Contacts)
        {
            if (it_Pair.Begun != StepCount && (b2Body_IsAwake(it_Pair.FirstRecord->Body) || b2Body_IsAwake(it_Pair.SecondRecord->Body)))
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
        OverlapContext l_Inside{ &filter,{} };
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

        CastContext l_Context{ &filter, all,{} };
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
        CastContext l_Context{ &filter, all,{} };
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

    // A joint is on the body its entity is part of, and names the body of the entity it joins. One on no 2D body, or naming an entity that is not in the scene, or one that is part of no 2D body or of its own, or a joint with a character on either side, is reported and left out
    void PhysicsScene2D::State::CreateJointRecord(entt::entity entity)
    {
        Scene& l_Scene = Owner;
        const SceneRegistry& l_Registry = l_Scene.GetRegistry();
        const Joint2DComponent& l_Joint = l_Registry.get<Joint2DComponent>(entity);
        const std::string l_Name = GetName(l_Scene, entity);
        BodyRecord2D* l_First = FindRecord(FindOwner(l_Registry, entity));
        if (l_First == nullptr)
        {
            TR_CORE_WARN("Physics: {}'s Joint2D is on no 2D body, since nothing on it or above it has a RigidBody2D or a 2D collider first, so it is left out", l_Name);

            return;
        }

        if (l_First->HasCharacter)
        {
            TR_CORE_WARN("Physics: {}'s Joint2D is on {}'s character, which a joint cannot hold, so it is left out", l_Name, GetName(l_Scene, l_First->Entity));

            return;
        }

        BodyRecord2D* l_Second = nullptr;
        entt::entity l_OtherEntity = entt::null;
        if (l_Joint.ConnectedBody)
        {
            const Entity l_Connected = l_Scene.FindEntityByUUID(l_Joint.ConnectedBody);
            if (!l_Connected)
            {
                TR_CORE_WARN("Physics: {}'s Joint2D names entity {}, which is not in the scene, so it is left out", l_Name, l_Joint.ConnectedBody);

                return;
            }

            l_Second = FindRecord(FindOwner(l_Registry, l_Connected.GetHandle()));
            if (l_Second == nullptr || l_Second == l_First || l_Second->HasCharacter)
            {
                TR_CORE_WARN("Physics: {}'s Joint2D names {}, which is {}, so it is left out", l_Name, GetName(l_Scene, l_Connected.GetHandle()), l_Second == l_First ? "part of its own body" : (l_Second != nullptr ? "part of a character, which a joint cannot hold" : "part of no 2D body"));

                return;
            }

            l_OtherEntity = l_Connected.GetHandle();
        }

        JointRecord2D& l_Record = Joints.emplace_back();
        l_Record.Entity = entity;
        l_Record.ID = l_Registry.get<IDComponent>(entity).ID;
        l_Record.OtherEntity = l_OtherEntity;
        l_Record.Other = l_Second != nullptr ? l_Joint.ConnectedBody : UUID();
        l_Record.Settings = l_Joint;
        l_Record.First = l_First;
        l_Record.Second = l_Second;
    }

    // Each of the body's shapes is retired, so the touches Box2D ends with the next step are still counted by its record, and taken out unless its body goes with it
    void PhysicsScene2D::State::DestroyShapes(b2BodyId body, bool destroy)
    {
        ShapeIDs.resize(static_cast<std::size_t>(b2Body_GetShapeCount(body)));
        const int l_Count = b2Body_GetShapes(body, ShapeIDs.data(), static_cast<int>(ShapeIDs.size()));
        for (int it_Shape = 0; it_Shape < l_Count; ++it_Shape)
        {
            const b2ShapeId l_Shape = ShapeIDs[static_cast<std::size_t>(it_Shape)];
            const std::uint64_t l_Key = GetShapeKey(l_Shape);
            if (const auto a_Found = ShapeOwners.find(l_Key); a_Found != ShapeOwners.end())
            {
                a_Found->second.Record = nullptr;
                Retired.push_back(l_Key);
            }

            if (destroy)
            {
                b2DestroyShape(l_Shape, false);
            }
        }
    }

    // Out of the world, its joints let go first to wait for a body again. Box2D wakes the bodies it touched
    void PhysicsScene2D::State::DestroyBody(BodyRecord2D& record)
    {
        if (B2_IS_NULL(record.Body))
        {
            return;
        }

        for (JointRecord2D& it_Joint : Joints)
        {
            if ((it_Joint.First == &record || it_Joint.Second == &record) && B2_IS_NON_NULL(it_Joint.Joint))
            {
                b2DestroyJoint(it_Joint.Joint);
                it_Joint.Joint = b2_nullJointId;
                it_Joint.NeedsBody = true;
            }
        }

        WakeAround(record.Body);
        DestroyShapes(record.Body, false);
        b2DestroyBody(record.Body);
        record.Body = b2_nullBodyId;
        record.CapsuleShape = b2_nullShapeId;
        record.Contacts.clear();
        record.GroundBody = b2_nullBodyId;
        record.Carrier = {};
    }

    // Box2D leaves bodies asleep when one near them is moved, or a shape near them made smaller, so everything within its bounds is woken to find out
    void PhysicsScene2D::State::WakeAround(b2BodyId body)
    {
        if (B2_IS_NULL(body) || b2Body_GetShapeCount(body) == 0)
        {
            return;
        }

        WakeAround(b2Body_ComputeAABB(body));
    }

    void PhysicsScene2D::State::WakeAround(const b2AABB& box)
    {
        Woken.clear();
        b2World_OverlapAABB(WorldID, box, { Box2DLayers::c_QueryBit, c_AllCollisionLayers }, &GatherBody, &Woken);
        for (const b2BodyId it_Body : Woken)
        {
            b2Body_SetAwake(it_Body, true);
        }
    }

    // Each pair the record is part of ends with the frame's first step, contacts first, each in the order of its pairs' bodies. Box2D's own word of their shapes parting comes with that step and finds them gone
    void PhysicsScene2D::State::EndPairs(const BodyRecord2D& record)
    {
        for (ContactPairs2D* it_Pairs : { &Contacts, &Triggers })
        {
            for (auto a_Pair = it_Pairs->begin(); a_Pair != it_Pairs->end();)
            {
                const ContactPair2D& l_Pair = a_Pair->second;
                if (l_Pair.FirstRecord != &record && l_Pair.SecondRecord != &record)
                {
                    ++a_Pair;

                    continue;
                }

                Events.push_back({ it_Pairs == &Triggers ? PhysicsEventType::TriggerExit : PhysicsEventType::ContactEnd, l_Pair.First, l_Pair.Second, glm::vec2(0.0f), glm::vec2(0.0f), 0 });
                a_Pair = it_Pairs->erase(a_Pair);
            }
        }
    }

    // An owner's record as the scene has it now: its CharacterController2D or RigidBody2D, or neither for a static body, and the 2D colliders on it and below it, in hierarchy order, that no RigidBody2D or character below takes, nor a 3D body or character
    void PhysicsScene2D::State::Gather(entt::entity owner, BodyRecord2D& record) const
    {
        const SceneRegistry& l_Registry = Owner.GetRegistry();
        ReadOwner(l_Registry, owner, record);
        record.Colliders.clear();

        const auto a_Visit = [&l_Registry, &record](const auto& self, entt::entity entity) -> void
        {
            AddColliders(l_Registry, entity, record);
            for (entt::entity it_Child = l_Registry.get<RelationshipComponent>(entity).FirstChild; it_Child != entt::null; it_Child = l_Registry.get<RelationshipComponent>(it_Child).NextSibling)
            {
                if (!l_Registry.any_of<RigidBody2DComponent, CharacterController2DComponent, RigidBodyComponent, CharacterControllerComponent>(it_Child))
                {
                    self(self, it_Child);
                }
            }
        };

        a_Visit(a_Visit, owner);
    }

    void PhysicsScene2D::State::Index(BodyRecord2D& record)
    {
        Members[record.Entity] = &record;
        for (const ColliderSource2D& it_Collider : record.Colliders)
        {
            Members[it_Collider.Entity] = &record;
        }
    }

    // Only the entries that are still this record's, since another may have taken an entity over
    void PhysicsScene2D::State::Unindex(const BodyRecord2D& record)
    {
        const auto a_Erase = [this, &record](entt::entity entity)
        {
            const auto a_Found = Members.find(entity);
            if (a_Found != Members.end() && a_Found->second == &record)
            {
                Members.erase(a_Found);
            }
        };

        a_Erase(record.Entity);
        for (const ColliderSource2D& it_Collider : record.Colliders)
        {
            a_Erase(it_Collider.Entity);
        }
    }

    // The record an owner entity has, none for an entity that owns no record
    BodyRecord2D* PhysicsScene2D::State::FindRecord(entt::entity owner) const
    {
        const auto a_Found = owner != entt::null ? Members.find(owner) : Members.end();

        return a_Found != Members.end() && a_Found->second->Entity == owner ? a_Found->second : nullptr;
    }

    // Its joints go with it, to be found again later, then its pairs and its body, and the pose it was drawn at, so its entity, when it stays, draws from its transform again
    void PhysicsScene2D::State::RemoveRecord(BodyRecord2D& record)
    {
        SceneRegistry& l_Registry = Owner.GetRegistry();
        for (std::size_t it_Joint = 0; it_Joint < Joints.size();)
        {
            JointRecord2D& l_Joint = Joints[it_Joint];
            if (l_Joint.First != &record && l_Joint.Second != &record)
            {
                ++it_Joint;

                continue;
            }

            if (B2_IS_NON_NULL(l_Joint.Joint))
            {
                b2DestroyJoint(l_Joint.Joint);
            }

            if (!l_Joint.Broken && l_Registry.valid(l_Joint.Entity))
            {
                RetryJoints.push_back(l_Joint.Entity);
            }

            Joints.erase(Joints.begin() + static_cast<std::ptrdiff_t>(it_Joint));
        }

        EndPairs(record);
        DestroyBody(record);
        if (l_Registry.valid(record.Entity))
        {
            l_Registry.remove<PhysicsPoseComponent>(record.Entity);
        }

        Unindex(record);
        std::erase(Records, &record);
        Memory::Delete(&record);
    }

    // A record gathered again is left alone when it is as it was. Otherwise its body is changed where it is, so its velocity, its contacts and its joints carry over: its motion, its RigidBody2D's settings, and, when its colliders or its character's controller changed, its shapes, all made again. One that becomes dynamic takes its RigidBody2D's starting velocities, as in 3D. One that becomes a character, or stops being one, gets a body made again, its pairs ending first. A record that had no body gets one, and one left with no colliders, and no character, loses its body, its pairs ending and its joints waiting for it. True when a body came or went
    bool PhysicsScene2D::State::UpdateRecord(BodyRecord2D& record, BodyRecord2D& gathered)
    {
        if (record.HasRigidBody == gathered.HasRigidBody && record.Settings == gathered.Settings && record.HasCharacter == gathered.HasCharacter && record.Controller == gathered.Controller && record.Colliders == gathered.Colliders)
        {
            return false;
        }

        const bool l_Remake = record.HasCharacter != gathered.HasCharacter;
        const bool l_Reshape = record.Colliders != gathered.Colliders || record.Controller != gathered.Controller;
        const BodyMotion l_WasMotion = record.Motion;
        const RigidBody2DComponent l_Was = record.Settings;
        Unindex(record);
        record.HasRigidBody = gathered.HasRigidBody;
        record.Settings = gathered.Settings;
        record.Motion = gathered.Motion;
        record.HasCharacter = gathered.HasCharacter;
        record.Controller = gathered.Controller;
        record.Colliders = std::move(gathered.Colliders);
        Index(record);

        SceneRegistry& l_Registry = Owner.GetRegistry();
        if (l_Remake && B2_IS_NON_NULL(record.Body))
        {
            EndPairs(record);
            DestroyBody(record);
            l_Registry.remove<PhysicsPoseComponent>(record.Entity);
            MakeBody(record);

            return true;
        }

        if (B2_IS_NULL(record.Body))
        {
            MakeBody(record);

            return B2_IS_NON_NULL(record.Body);
        }

        if (record.Colliders.empty() && !record.HasCharacter)
        {
            EndPairs(record);
            DestroyBody(record);
            l_Registry.remove<PhysicsPoseComponent>(record.Entity);

            return true;
        }

        const b2BodyId l_Body = record.Body;
        const RigidBody2DComponent& l_RigidBody = record.Settings;
        if (record.Motion != l_WasMotion)
        {
            b2Body_SetType(l_Body, record.Motion == BodyMotion::Static ? b2_staticBody : (record.Motion == BodyMotion::Kinematic ? b2_kinematicBody : b2_dynamicBody));
            if (record.Motion == BodyMotion::Dynamic)
            {
                b2Body_SetLinearVelocity(l_Body, ToBox2D(l_RigidBody.InitialLinearVelocity));
                b2Body_SetAngularVelocity(l_Body, l_RigidBody.InitialAngularVelocity);
            }

            // It moves on from where it is, and a static body is drawn from its transform again
            const Pose2D l_Pose = ReadPose(l_Body);
            record.Previous = l_Pose;
            record.Current = l_Pose;
            record.TargetFrom = l_Pose;
            record.TargetTo = l_Pose;
            if (record.Motion == BodyMotion::Static)
            {
                l_Registry.remove<PhysicsPoseComponent>(record.Entity);
            }
        }

        if (record.Motion == BodyMotion::Dynamic)
        {
            b2Body_SetLinearDamping(l_Body, l_RigidBody.LinearDamping);
            b2Body_SetAngularDamping(l_Body, l_RigidBody.AngularDamping);
            b2Body_SetGravityScale(l_Body, l_RigidBody.GravityFactor);
            b2Body_EnableSleep(l_Body, l_RigidBody.CanSleep);
            if (l_RigidBody.ContinuousCollision != l_Was.ContinuousCollision || l_WasMotion != BodyMotion::Dynamic)
            {
                b2Body_SetBullet(l_Body, l_RigidBody.ContinuousCollision);
            }

            if (l_RigidBody.LockRotation != l_Was.LockRotation || l_WasMotion != BodyMotion::Dynamic)
            {
                b2Body_SetFixedRotation(l_Body, l_RigidBody.LockRotation);
            }
        }

        WakeAround(l_Body);
        if (l_Reshape)
        {
            DestroyShapes(l_Body, true);
            record.CapsuleShape = b2_nullShapeId;
            MakeShapes(record);
            MakeCharacterShape(record);
        }

        ApplyMass(record);
        if (record.Motion == BodyMotion::Dynamic)
        {
            b2Body_SetAwake(l_Body, true);
        }

        WakeAround(l_Body);

        return false;
    }

    // Everything changed since the last frame with steps is taken in. The owners whose records may differ are those of each changed entity as they were and as it, and everything below it, has them now. Each is gathered again: a record no longer owned goes, one newly owned is made, and the rest are updated. Then the joints follow
    void PhysicsScene2D::State::Sync()
    {
        if (Dirty.empty() && DirtyJoints.empty())
        {
            return;
        }

        TR_PROFILE_SCOPE("PhysicsScene2D::Sync");
        const SceneRegistry& l_Registry = Owner.GetRegistry();
        PhysicsVector<entt::entity> l_Dirty;
        PhysicsVector<entt::entity> l_DirtyJoints;
        l_Dirty.swap(Dirty);
        l_DirtyJoints.swap(DirtyJoints);

        PhysicsVector<entt::entity> l_Owners;
        const auto a_Note = [this, &l_Owners](entt::entity entity)
        {
            if (const auto a_Found = Members.find(entity); a_Found != Members.end())
            {
                l_Owners.push_back(a_Found->second->Entity);
            }
        };

        const auto a_Visit = [&l_Registry, &l_Owners, &a_Note](const auto& self, entt::entity entity) -> void
        {
            a_Note(entity);
            if (l_Registry.any_of<RigidBody2DComponent, CharacterController2DComponent>(entity) || Has2DCollider(l_Registry, entity))
            {
                if (const entt::entity l_Owner = FindOwner(l_Registry, entity); l_Owner != entt::null)
                {
                    l_Owners.push_back(l_Owner);
                }
            }

            for (entt::entity it_Child = l_Registry.get<RelationshipComponent>(entity).FirstChild; it_Child != entt::null; it_Child = l_Registry.get<RelationshipComponent>(it_Child).NextSibling)
            {
                self(self, it_Child);
            }
        };

        for (const entt::entity it_Entity : l_Dirty)
        {
            a_Note(it_Entity);
            if (l_Registry.valid(it_Entity))
            {
                a_Visit(a_Visit, it_Entity);
            }
        }

        std::ranges::sort(l_Owners);
        l_Owners.erase(std::ranges::unique(l_Owners).begin(), l_Owners.end());

        bool l_BodiesChanged = false;
        for (const entt::entity it_Owner : l_Owners)
        {
            BodyRecord2D* l_Record = FindRecord(it_Owner);
            if (!l_Registry.valid(it_Owner) || FindOwner(l_Registry, it_Owner) != it_Owner)
            {
                if (l_Record != nullptr)
                {
                    RemoveRecord(*l_Record);
                    l_BodiesChanged = true;
                }

                continue;
            }

            if (l_Record == nullptr)
            {
                BodyRecord2D& l_New = *Records.emplace_back(Memory::New<BodyRecord2D>(MemoryTag::Physics));
                Gather(it_Owner, l_New);
                l_New.Serial = NextSerial++;
                Index(l_New);
                MakeBody(l_New);
                l_BodiesChanged = true;

                continue;
            }

            BodyRecord2D l_Gathered;
            Gather(it_Owner, l_Gathered);
            l_BodiesChanged = UpdateRecord(*l_Record, l_Gathered) || l_BodiesChanged;
        }

        ResolveJoints(l_BodiesChanged, l_DirtyJoints);
    }

    // A joint whose entity changed is made again from where its bodies are now, and so is one whose bodies are other records now, or one let go with a body that went. One waiting for a body is made once its bodies have theirs. One whose entity or Joint2D went goes with them. A broken joint stays broken until its Joint2D changes
    void PhysicsScene2D::State::ResolveJoints(bool bodiesChanged, const PhysicsVector<entt::entity>& dirtyJoints)
    {
        const SceneRegistry& l_Registry = Owner.GetRegistry();
        PhysicsVector<entt::entity> l_Make;
        const auto a_Make = [&l_Make, &l_Registry](entt::entity entity)
        {
            if (l_Registry.valid(entity) && l_Registry.all_of<Joint2DComponent>(entity) && std::ranges::find(l_Make, entity) == l_Make.end())
            {
                l_Make.push_back(entity);
            }
        };

        for (std::size_t it_Joint = 0; it_Joint < Joints.size();)
        {
            JointRecord2D& l_Joint = Joints[it_Joint];
            const bool l_Dirty = std::ranges::find(dirtyJoints, l_Joint.Entity) != dirtyJoints.end();
            if ((!l_Dirty && (!bodiesChanged || l_Joint.Broken)) || (l_Dirty && Tune(l_Joint)))
            {
                ++it_Joint;

                continue;
            }

            if (!l_Dirty && l_Registry.valid(l_Joint.Entity))
            {
                const BodyRecord2D* l_First = FindRecord(FindOwner(l_Registry, l_Joint.Entity));
                const BodyRecord2D* l_Second = l_Registry.valid(l_Joint.OtherEntity) ? FindRecord(FindOwner(l_Registry, l_Joint.OtherEntity)) : nullptr;
                if (l_First == l_Joint.First && l_Second == l_Joint.Second)
                {
                    if (l_Joint.NeedsBody)
                    {
                        MakeJoint(l_Joint);
                    }

                    ++it_Joint;

                    continue;
                }
            }

            if (B2_IS_NON_NULL(l_Joint.Joint))
            {
                b2DestroyJoint(l_Joint.Joint);
            }

            a_Make(l_Joint.Entity);
            Joints.erase(Joints.begin() + static_cast<std::ptrdiff_t>(it_Joint));
        }

        for (const entt::entity it_Entity : dirtyJoints)
        {
            if (std::ranges::find(Joints, it_Entity, &JointRecord2D::Entity) == Joints.end())
            {
                a_Make(it_Entity);
            }
        }

        for (const entt::entity it_Entity : std::exchange(RetryJoints, {}))
        {
            if (std::ranges::find(Joints, it_Entity, &JointRecord2D::Entity) == Joints.end())
            {
                a_Make(it_Entity);
            }
        }

        for (const entt::entity it_Entity : l_Make)
        {
            const std::size_t l_Count = Joints.size();
            CreateJointRecord(it_Entity);
            if (Joints.size() > l_Count)
            {
                MakeJoint(Joints.back());
            }
        }
    }

    // A joint whose motor or break limits alone changed, or nothing at all, takes them on in place and holds where it is, and a broken one stays broken. False when anything else changed, or its bodies are other records now, so it is made again
    bool PhysicsScene2D::State::Tune(JointRecord2D& joint)
    {
        const SceneRegistry& l_Registry = Owner.GetRegistry();
        if (!l_Registry.valid(joint.Entity) || !l_Registry.all_of<Joint2DComponent>(joint.Entity))
        {
            return false;
        }

        const Joint2DComponent& l_Now = l_Registry.get<Joint2DComponent>(joint.Entity);
        Joint2DComponent l_Kept = l_Now;
        l_Kept.UseMotor = joint.Settings.UseMotor;
        l_Kept.MotorSpeed = joint.Settings.MotorSpeed;
        l_Kept.MotorMaxForce = joint.Settings.MotorMaxForce;
        l_Kept.BreakForce = joint.Settings.BreakForce;
        l_Kept.BreakTorque = joint.Settings.BreakTorque;
        const BodyRecord2D* l_First = FindRecord(FindOwner(l_Registry, joint.Entity));
        const BodyRecord2D* l_Second = l_Registry.valid(joint.OtherEntity) ? FindRecord(FindOwner(l_Registry, joint.OtherEntity)) : nullptr;
        if (!(l_Kept == joint.Settings) || l_First != joint.First || l_Second != joint.Second)
        {
            return false;
        }

        joint.Settings = l_Now;
        if (B2_IS_NULL(joint.Joint))
        {
            return true;
        }

        const float l_MotorMax = std::max(l_Now.MotorMaxForce, 0.0f);
        switch (l_Now.Type)
        {
            case Joint2DType::Revolute:
            {
                b2RevoluteJoint_EnableMotor(joint.Joint, l_Now.UseMotor);
                b2RevoluteJoint_SetMotorSpeed(joint.Joint, l_Now.MotorSpeed);
                b2RevoluteJoint_SetMaxMotorTorque(joint.Joint, l_MotorMax);

                break;
            }
            case Joint2DType::Prismatic:
            {
                b2PrismaticJoint_EnableMotor(joint.Joint, l_Now.UseMotor);
                b2PrismaticJoint_SetMotorSpeed(joint.Joint, l_Now.MotorSpeed);
                b2PrismaticJoint_SetMaxMotorForce(joint.Joint, l_MotorMax);

                break;
            }
            case Joint2DType::Wheel:
            {
                b2WheelJoint_EnableMotor(joint.Joint, l_Now.UseMotor);
                b2WheelJoint_SetMotorSpeed(joint.Joint, l_Now.MotorSpeed);
                b2WheelJoint_SetMaxMotorTorque(joint.Joint, l_MotorMax);

                break;
            }
            case Joint2DType::Distance:
            case Joint2DType::Weld:
            {
                break;
            }
        }

        // A motor turned on or sped up moves bodies that had fallen asleep
        b2Joint_WakeBodies(joint.Joint);

        return true;
    }

    // A body whose entity's transform was set from code since the body made or last wrote it is taken there at once, with the entity's depth, and drawn there with nothing between. Kinematic bodies follow their transforms already
    void PhysicsScene2D::State::FollowTransforms()
    {
        SceneRegistry& l_Registry = Owner.GetRegistry();
        for (BodyRecord2D* it_Record : Records)
        {
            if (it_Record->Motion == BodyMotion::Kinematic || B2_IS_NULL(it_Record->Body) || !l_Registry.valid(it_Record->Entity))
            {
                continue;
            }

            const TransformComponent& l_Transform = l_Registry.get<TransformComponent>(it_Record->Entity);
            if (l_Transform.Position != it_Record->WrittenPosition || l_Transform.Rotation != it_Record->WrittenRotation)
            {
                PlaceAtTransform(*it_Record);
            }
        }
    }

    // The body where its entity's transform puts it, with the entity's depth and nothing between to draw, and a kinematic one's targets there too, so it stays until its transform moves again. A character keeps its velocity, nothing carries it over the step, and it finds what it stands on there at once
    void PhysicsScene2D::State::PlaceAtTransform(BodyRecord2D& record)
    {
        const TransformComponent& l_Transform = Owner.GetRegistry().get<TransformComponent>(record.Entity);
        record.WrittenPosition = l_Transform.Position;
        record.WrittenRotation = l_Transform.Rotation;
        const glm::mat4 l_World = Owner.ComputeWorldMatrix(Entity(record.Entity, &Owner));
        const Pose2D l_Pose = GetPose(l_World);
        record.Depth = l_World[3][2];
        WakeAround(record.Body);
        b2Body_SetTransform(record.Body, ToBox2D(l_Pose.Position), Box2DConvert::ToRotation(l_Pose.Angle));
        WakeAround(record.Body);
        if (record.Motion == BodyMotion::Dynamic)
        {
            b2Body_SetAwake(record.Body, true);
        }

        record.Previous = l_Pose;
        record.Current = l_Pose;
        record.TargetFrom = l_Pose;
        record.TargetTo = l_Pose;
        if (record.HasCharacter)
        {
            record.Carrier = {};
            UpdateGround(record, l_Pose.Position, record.Velocity, LastSeconds, true);
        }
    }

    namespace
    {
        // What a character's query keeps: every shape the box meets but its own body's and triggers
        struct CharacterQuery2D
        {
            b2BodyId Self = b2_nullBodyId;
            PhysicsVector<b2ShapeId>* Shapes = nullptr;
        };

        bool GatherCharacterShape(b2ShapeId shape, void* context)
        {
            const CharacterQuery2D& l_Query = *static_cast<const CharacterQuery2D*>(context);
            if (!B2_ID_EQUALS(b2Shape_GetBody(shape), l_Query.Self) && !b2Shape_IsSensor(shape))
            {
                l_Query.Shapes->push_back(shape);
            }

            return true;
        }

        // The bounds of a proxy's points, grown by its radius and the margin
        b2AABB GetBounds(const b2ShapeProxy& proxy, float margin)
        {
            b2AABB l_Box{ proxy.points[0], proxy.points[0] };
            for (int it_Point = 1; it_Point < proxy.count; ++it_Point)
            {
                l_Box.lowerBound = b2Min(l_Box.lowerBound, proxy.points[it_Point]);
                l_Box.upperBound = b2Max(l_Box.upperBound, proxy.points[it_Point]);
            }

            const b2Vec2 l_Grown{ proxy.radius + margin, proxy.radius + margin };

            return { b2Sub(l_Box.lowerBound, l_Grown), b2Add(l_Box.upperBound, l_Grown) };
        }
    }

    // A character's capsule on its kinematic body, beside the colliders that follow it, placed as a 2D capsule collider is: along its entity's Y axis from where its offset puts it, its length by the Y scale and its radius by the X scale, a circle when its caps meet. On its layer, it meets triggers, bodies and queries, and reports itself as its entity. Its step height takes the Y scale
    void PhysicsScene2D::State::MakeCharacterShape(BodyRecord2D& record)
    {
        if (!record.HasCharacter || B2_IS_NULL(record.Body))
        {
            return;
        }

        Scene& l_Scene = Owner;
        const CharacterController2DComponent& l_Controller = record.Controller;
        const glm::mat4 l_World = l_Scene.ComputeWorldMatrix(Entity(record.Entity, &l_Scene));
        const Pose2D l_Pose = GetPose(l_World);
        const glm::vec2 l_AxisY(l_World[1]);
        const float l_ScaleX = glm::length(glm::vec3(l_World[0]));
        const float l_ScaleY = glm::length(glm::vec3(l_World[1]));
        const float l_HalfSegment = l_Controller.GetHalfSegment() * l_ScaleY;
        const glm::vec2 l_Middle = glm::vec2(l_World * glm::vec4(l_Controller.Offset, 0.0f, 1.0f)) - l_Pose.Position;
        const glm::vec2 l_Axis = glm::length(l_AxisY) > 1e-6f ? glm::normalize(l_AxisY) : glm::vec2(0.0f, 1.0f);
        const std::uint32_t l_Layer = l_Controller.Layer < c_MaxCollisionLayers ? l_Controller.Layer : 0;
        record.Radius = std::max(l_Controller.Radius * l_ScaleX, c_MinimumSize);
        record.Round = l_HalfSegment < c_MinimumSize;
        const float l_Reach = record.Round ? 0.0f : l_HalfSegment;
        record.CapsuleEnds = { ToBox2D(Rotate(l_Middle - l_Axis * l_Reach, -l_Pose.Angle)), ToBox2D(Rotate(l_Middle + l_Axis * l_Reach, -l_Pose.Angle)) };
        record.StepHeight = std::max(l_Controller.StepHeight, 0.0f) * l_ScaleY;
        record.CosMaxSlope = std::cos(std::clamp(l_Controller.MaxSlope, c_MinimumSlope, c_MaximumSlope));

        record.Capsule = ColliderSource2D{};
        record.Capsule.Entity = record.Entity;
        record.Capsule.ID = record.ID;
        record.Capsule.Kind = ColliderKind2D::Capsule;
        record.Capsule.Settings.Layer = l_Layer;
        record.Capsule.Size = glm::vec2(l_Controller.Radius, l_Controller.Height);

        b2ShapeDef l_Shape = b2DefaultShapeDef();
        l_Shape.userData = &record.Capsule;
        l_Shape.material.friction = record.Capsule.Settings.Friction;
        l_Shape.material.restitution = record.Capsule.Settings.Restitution;
        l_Shape.filter.categoryBits = 1ull << l_Layer;
        l_Shape.filter.maskBits = Layers.Rows[l_Layer] | Box2DLayers::c_QueryBit;
        l_Shape.density = 0.0f;
        l_Shape.enableContactEvents = true;
        l_Shape.enableSensorEvents = true;
        if (record.Round)
        {
            const b2Circle l_Circle{ record.CapsuleEnds[0], record.Radius };
            record.CapsuleShape = b2CreateCircleShape(record.Body, &l_Shape, &l_Circle);
        }
        else
        {
            const b2Capsule l_Capsule{ record.CapsuleEnds[0], record.CapsuleEnds[1], record.Radius };
            record.CapsuleShape = b2CreateCapsuleShape(record.Body, &l_Shape, &l_Capsule);
        }

        ShapeOwners[GetShapeKey(record.CapsuleShape)] = { record.Serial, &record, b2Vec2{ 0.0f, 0.0f } };
    }

    // Against the 2D world's gravity, or up the Y axis without any
    glm::vec2 PhysicsScene2D::State::GetUp() const
    {
        const glm::vec2 l_Gravity = World.GetGravity();
        const float l_Strength = glm::length(l_Gravity);

        return l_Strength > 0.0f ? -l_Gravity / l_Strength : glm::vec2(0.0f, 1.0f);
    }

    // The character's capsule placed at a position, turned as its body is, as a proxy for Box2D's queries: its two cap centres in the world, or one for a circle, and its radius
    b2ShapeProxy PhysicsScene2D::State::GetMover(const BodyRecord2D& record, const glm::vec2& position) const
    {
        const b2Transform l_Transform{ ToBox2D(position), b2Body_GetRotation(record.Body) };
        const std::array<b2Vec2, 2> l_Ends{ { b2TransformPoint(l_Transform, record.CapsuleEnds[0]), b2TransformPoint(l_Transform, record.CapsuleEnds[1]) } };

        return b2MakeProxy(l_Ends.data(), record.Round ? 1 : 2, record.Radius);
    }

    // The middle of the character's lower end cap placed there, lower along up, which is what stands on the ground
    glm::vec2 PhysicsScene2D::State::GetFoot(const BodyRecord2D& record, const glm::vec2& position) const
    {
        const b2ShapeProxy l_Mover = GetMover(record, position);
        const b2Vec2 l_Up = ToBox2D(GetUp());

        return FromBox2D(l_Mover.count > 1 && b2Dot(l_Mover.points[1], l_Up) < b2Dot(l_Mover.points[0], l_Up) ? l_Mover.points[1] : l_Mover.points[0]);
    }

    bool PhysicsScene2D::State::IsTooSteep(const BodyRecord2D& record, const glm::vec2& normal) const
    {
        return glm::dot(normal, GetUp()) < record.CosMaxSlope;
    }

    // Every shape within the box a character's move meets: on the layers its own collides with, and none of its own body's or a trigger
    void PhysicsScene2D::State::GatherCharacterShapes(const BodyRecord2D& record, const b2AABB& box)
    {
        CharacterShapes.clear();
        CharacterQuery2D l_Query{ record.Body, &CharacterShapes };
        b2World_OverlapAABB(WorldID, box, { Box2DLayers::c_QueryBit, static_cast<std::uint64_t>(Layers.Rows[record.Capsule.Settings.Layer]) }, &GatherCharacterShape, &l_Query);
    }

    // A one-way collider's up in the world, as its body turns it, or none for any other shape
    glm::vec2 PhysicsScene2D::State::GetOneWayUp(b2ShapeId shape) const
    {
        const ColliderSource2D* l_Collider = GetCollider(shape);
        if (l_Collider == nullptr || !l_Collider->Settings.OneWay)
        {
            return glm::vec2(0.0f);
        }

        const ShapeOwner2D* l_Owner = FindShape(shape);

        return l_Owner != nullptr ? FromBox2D(b2RotateVector(b2Body_GetRotation(b2Shape_GetBody(shape)), l_Owner->OneWay)) : glm::vec2(0.0f);
    }

    // The shapes within reach of the character placed there, each with its nearest point and normal from Box2D's distance between it and the capsule's core, as Box2D's own mover finds them, and a shape the core meets passed over. A one-way collider is met only from above, while the character is no deeper in it than a little
    void PhysicsScene2D::State::Collide(const BodyRecord2D& record, const glm::vec2& position, PhysicsVector<CharacterContact2D>& contacts)
    {
        contacts.clear();
        const b2ShapeProxy l_Mover = GetMover(record, position);
        GatherCharacterShapes(record, GetBounds(l_Mover, c_ContactDistance));
        for (const b2ShapeId it_Shape : CharacterShapes)
        {
            b2DistanceInput l_Input{};
            l_Input.proxyA = MakeProxy(it_Shape);
            l_Input.proxyB = l_Mover;
            l_Input.transformA = b2Transform_identity;
            l_Input.transformB = b2Transform_identity;
            l_Input.useRadii = false;
            b2SimplexCache l_Cache{};
            const b2DistanceOutput l_Output = b2ShapeDistance(&l_Input, &l_Cache, nullptr, 0);
            const float l_Gap = l_Output.distance - l_Input.proxyA.radius - l_Mover.radius;
            if (!(l_Output.distance > 0.0f) || l_Gap > c_ContactDistance)
            {
                continue;
            }

            CharacterContact2D l_Contact;
            l_Contact.Shape = it_Shape;
            l_Contact.Body = b2Shape_GetBody(it_Shape);
            l_Contact.Type = b2Body_GetType(l_Contact.Body);
            l_Contact.Normal = FromBox2D(l_Output.normal);
            l_Contact.Point = FromBox2D(l_Output.pointA) + l_Contact.Normal * l_Input.proxyA.radius;
            l_Contact.Distance = l_Gap;
            l_Contact.Velocity = GetContactVelocity(record, it_Shape, l_Contact.Body, l_Contact.Point);
            const glm::vec2 l_OneWay = GetOneWayUp(it_Shape);
            if (l_OneWay != glm::vec2(0.0f) && (glm::dot(l_Contact.Normal, l_OneWay) < c_OneWayCosine || l_Gap < -c_OneWayDepth))
            {
                continue;
            }

            contacts.push_back(l_Contact);
        }
    }

    // How far along the translation the character's capsule goes, from 0 to 1, before it meets a shape, as Box2D's mover cast goes: a shape whose core it starts in is passed over. Box2D lets a cast go a slop further into a shape it starts touching, which would leave it deeper than the planes rest it, so going into one stops it at once. A one-way collider is met only by a capsule coming down onto it from above. The shape it met, its point and its normal, when asked for
    float PhysicsScene2D::State::Cast(const BodyRecord2D& record, const glm::vec2& position, const glm::vec2& translation, CharacterContact2D* hit)
    {
        if (hit != nullptr)
        {
            *hit = {};
        }

        if (!(glm::dot(translation, translation) > 0.0f))
        {
            return 1.0f;
        }

        const b2ShapeProxy l_Mover = GetMover(record, position);
        const b2ShapeProxy l_Moved = GetMover(record, position + translation);
        GatherCharacterShapes(record, b2AABB_Union(GetBounds(l_Mover, c_SupportDistance), GetBounds(l_Moved, c_SupportDistance)));
        float l_Fraction = 1.0f;
        for (const b2ShapeId it_Shape : CharacterShapes)
        {
            b2ShapeCastPairInput l_Input{};
            l_Input.proxyA = MakeProxy(it_Shape);
            l_Input.proxyB = l_Mover;
            l_Input.transformA = b2Transform_identity;
            l_Input.transformB = b2Transform_identity;
            l_Input.translationB = ToBox2D(translation);
            l_Input.maxFraction = l_Fraction;
            l_Input.canEncroach = true;
            const b2CastOutput l_Output = b2ShapeCast(&l_Input);
            if (!l_Output.hit || !(l_Output.fraction > 0.0f) || l_Output.fraction >= l_Fraction)
            {
                continue;
            }

            const glm::vec2 l_Normal = FromBox2D(l_Output.normal);
            const glm::vec2 l_OneWay = GetOneWayUp(it_Shape);
            if (l_OneWay != glm::vec2(0.0f) && (glm::dot(translation, l_OneWay) >= 0.0f || glm::dot(l_Normal, l_OneWay) < c_OneWayCosine))
            {
                continue;
            }

            // Touching it at the start, as Box2D's cast counts touching
            b2DistanceInput l_Start{};
            l_Start.proxyA = l_Input.proxyA;
            l_Start.proxyB = l_Mover;
            l_Start.transformA = b2Transform_identity;
            l_Start.transformB = b2Transform_identity;
            l_Start.useRadii = false;
            b2SimplexCache l_Cache{};
            const float l_Touching = std::max(c_TouchingSlop, l_Input.proxyA.radius + l_Mover.radius - c_TouchingSlop) + 0.25f * c_TouchingSlop;
            l_Fraction = b2ShapeDistance(&l_Start, &l_Cache, nullptr, 0).distance < l_Touching ? 0.0f : l_Output.fraction;
            if (hit != nullptr)
            {
                hit->Shape = it_Shape;
                hit->Body = b2Shape_GetBody(it_Shape);
                hit->Type = b2Body_GetType(hit->Body);
                hit->Point = FromBox2D(l_Output.point);
                hit->Normal = l_Normal;
            }

            if (!(l_Fraction > 0.0f))
            {
                break;
            }
        }

        return l_Fraction;
    }

    // The planes Box2D's solver holds the character out of, one for each contact within reach, no nearer than its gap less Box2D's slop. As Jolt has it, a slope too steep to stand on that faces up is also held as a wall, its part across up alone, so walking into it does not climb it. The wall goes first, since the solver stops once its pushes are within a slop, and the slope solved first would have pushed the character up that much
    void PhysicsScene2D::State::MakePlanes(const BodyRecord2D& record, const PhysicsVector<CharacterContact2D>& contacts, float reach)
    {
        Planes.clear();
        const glm::vec2 l_Up = GetUp();
        for (const CharacterContact2D& it_Contact : contacts)
        {
            if (it_Contact.Distance > reach)
            {
                continue;
            }

            const float l_Rise = glm::dot(it_Contact.Normal, l_Up);
            const glm::vec2 l_Across = it_Contact.Normal - l_Rise * l_Up;
            if (l_Rise > 1e-3f && glm::length(l_Across) > 1e-6f && IsTooSteep(record, it_Contact.Normal))
            {
                const glm::vec2 l_Wall = glm::normalize(l_Across);
                Planes.push_back({ { ToBox2D(l_Wall), -it_Contact.Distance / glm::dot(l_Wall, it_Contact.Normal) }, FLT_MAX, 0.0f, true });
            }

            Planes.push_back({ { ToBox2D(it_Contact.Normal), -it_Contact.Distance }, FLT_MAX, 0.0f, true });
        }
    }

    // Where Box2D's planes and casts take the character from the start towards the displacement, as its mover sample does: each try gathers the planes about it, solves where it may go, and casts there, stopping where it meets something, and the next try goes on from there towards the same place
    glm::vec2 PhysicsScene2D::State::MoveShape(const BodyRecord2D& record, const glm::vec2& start, const glm::vec2& displacement)
    {
        const glm::vec2 l_Target = start + displacement;
        glm::vec2 l_Position = start;
        for (int it_Try = 0; it_Try < c_MoveIterations; ++it_Try)
        {
            Collide(record, l_Position, MoveContacts);
            MakePlanes(record, MoveContacts, c_ContactDistance);
            const b2PlaneSolverResult l_Solved = b2SolvePlanes(ToBox2D(l_Target - l_Position), Planes.data(), static_cast<int>(Planes.size()));
            const glm::vec2 l_Translation = FromBox2D(l_Solved.translation);
            const glm::vec2 l_Moved = l_Translation * Cast(record, l_Position, l_Translation, nullptr);
            l_Position += l_Moved;
            if (glm::dot(l_Moved, l_Moved) < c_MoveTolerance * c_MoveTolerance)
            {
                break;
            }
        }

        return l_Position;
    }

    // How the character stands where it is, from the shapes it touches and does not move away from, as Jolt's UpdateSupportingContact has a 3D one stand: on ground when one below the middle of its lower end cap holds it up, on a steep slope when such contacts are all too steep, unless they hold it from sliding between them, and otherwise touching what holds none of it up, or nothing. The ground's normal averages those of the contacts below it facing within 85 degrees of up, and its body is the most upward facing of them, or the deepest contact. A move that put it there, such as stepping up, is not asked whether it moves away
    void PhysicsScene2D::State::UpdateGround(BodyRecord2D& record, const glm::vec2& position, const glm::vec2& velocity, float seconds, bool skipVelocityCheck)
    {
        Collide(record, position, record.Contacts);
        const glm::vec2 l_Up = GetUp();
        const glm::vec2 l_Lower = GetFoot(record, position);
        int l_Supported = 0;
        int l_Sliding = 0;
        int l_Averaged = 0;
        glm::vec2 l_Normal(0.0f);
        const CharacterContact2D* l_Supporting = nullptr;
        float l_MostUp = -FLT_MAX;
        const CharacterContact2D* l_Deepest = nullptr;
        float l_Smallest = FLT_MAX;
        for (const CharacterContact2D& it_Contact : record.Contacts)
        {
            if (it_Contact.Distance > c_SupportDistance || (!skipVelocityCheck && glm::dot(it_Contact.Normal, velocity - it_Contact.Velocity) > 1e-4f))
            {
                continue;
            }

            if (it_Contact.Distance < l_Smallest)
            {
                l_Deepest = &it_Contact;
                l_Smallest = it_Contact.Distance;
            }

            // A contact above the middle of its lower end cap holds none of it up
            if (glm::dot(it_Contact.Point - l_Lower, l_Up) > 0.0f)
            {
                continue;
            }

            const float l_Cosine = glm::dot(it_Contact.Normal, l_Up);
            if (l_Cosine > l_MostUp)
            {
                l_Supporting = &it_Contact;
                l_MostUp = l_Cosine;
            }

            ++(l_Cosine >= record.CosMaxSlope ? l_Supported : l_Sliding);
            if (l_Cosine >= 0.08f)
            {
                l_Normal += it_Contact.Normal;
                ++l_Averaged;
            }
        }

        const CharacterContact2D* l_Best = l_Supporting != nullptr ? l_Supporting : l_Deepest;
        record.GroundNormal = l_Averaged > 0 ? glm::normalize(l_Normal) : (l_Best != nullptr ? l_Best->Normal : glm::vec2(0.0f));
        record.GroundBody = l_Best != nullptr ? l_Best->Body : b2_nullBodyId;
        if (l_Supported > 0)
        {
            record.Ground = GroundState2D::OnGround;
        }
        else if (l_Sliding > 0)
        {
            if (glm::dot(velocity - l_Deepest->Velocity, l_Up) > 1e-4f)
            {
                record.Ground = GroundState2D::OnSteepGround;
            }
            else
            {
                // Between slopes too steep that keep it from sliding down any, it stands
                MakePlanes(record, record.Contacts, c_SupportDistance);
                const b2PlaneSolverResult l_Fall = b2SolvePlanes(ToBox2D(-l_Up), Planes.data(), static_cast<int>(Planes.size()));
                const float l_Least = 0.6f * seconds;
                record.Ground = b2LengthSquared(l_Fall.translation) < l_Least * l_Least ? GroundState2D::OnGround : GroundState2D::OnSteepGround;
            }
        }
        else
        {
            record.Ground = l_Best != nullptr ? GroundState2D::NotSupported : GroundState2D::InAir;
        }
    }

    // As Jolt does before a 3D character moves: walking on ground, or touching nothing, the velocity is as given, and otherwise its part going into each slope too steep it touches is taken off, across up alone
    glm::vec2 PhysicsScene2D::State::CancelVelocityTowardsSteepSlopes(const BodyRecord2D& record, const glm::vec2& velocity) const
    {
        if (record.Ground == GroundState2D::OnGround || record.Ground == GroundState2D::InAir)
        {
            return velocity;
        }

        const glm::vec2 l_Up = GetUp();
        glm::vec2 l_Velocity = velocity;
        for (const CharacterContact2D& it_Contact : record.Contacts)
        {
            if (it_Contact.Distance > c_SupportDistance || !IsTooSteep(record, it_Contact.Normal))
            {
                continue;
            }

            const glm::vec2 l_Across = it_Contact.Normal - glm::dot(it_Contact.Normal, l_Up) * l_Up;
            const float l_Into = glm::dot(l_Across, l_Velocity);
            if (l_Into < 0.0f && glm::dot(l_Across, l_Across) > 1e-12f)
            {
                l_Velocity -= l_Into * l_Across / glm::dot(l_Across, l_Across);
            }
        }

        return l_Velocity;
    }

    // Standing on something and walking across up into a slope too steep, which a step's side is
    bool PhysicsScene2D::State::CanWalkStairs(const BodyRecord2D& record, const glm::vec2& velocity) const
    {
        if (record.Ground != GroundState2D::OnGround && record.Ground != GroundState2D::OnSteepGround)
        {
            return false;
        }

        const glm::vec2 l_Up = GetUp();
        const glm::vec2 l_Across = velocity - glm::dot(velocity, l_Up) * l_Up;
        if (glm::dot(l_Across, l_Across) <= 1e-6f)
        {
            return false;
        }

        return std::ranges::any_of(record.Contacts, [&](const CharacterContact2D& contact) { return contact.Distance <= c_SupportDistance && glm::dot(contact.Normal, l_Across - contact.Velocity) < 0.0f && IsTooSteep(record, contact.Normal); });
    }

    // Jolt's WalkStairs for a 2D character: up by the step height, or as far as a ceiling lets it, forward as it walks, then down onto the step, which takes it there when it lands on ground it can stand on, higher than it was. Landing on a step's edge, which faces it too steeply, it looks further forward for a floor there. Nothing changes when it meets no step, makes no way towards the slope it walked into, or gains nothing
    bool PhysicsScene2D::State::WalkStairs(BodyRecord2D& record, glm::vec2& position, float seconds, const glm::vec2& stepUp, const glm::vec2& stepForward, const glm::vec2& stepForwardTest, const glm::vec2& velocity)
    {
        CharacterContact2D l_Hit;
        glm::vec2 l_Up = stepUp;
        const float l_Rise = Cast(record, position, stepUp, &l_Hit);
        if (B2_IS_NON_NULL(l_Hit.Shape))
        {
            if (l_Rise < 1e-6f)
            {
                return false;
            }

            l_Up *= l_Rise;
        }

        const glm::vec2 l_UpPosition = position + l_Up;
        const glm::vec2 l_UpDirection = GetUp();

        // The slopes too steep it walks into, before the forward move finds others
        const glm::vec2 l_Walking = stepForward / seconds;
        SteepNormals.clear();
        for (const CharacterContact2D& it_Contact : record.Contacts)
        {
            if (it_Contact.Distance <= c_SupportDistance && glm::dot(it_Contact.Normal, l_Walking - it_Contact.Velocity) < 0.0f && IsTooSteep(record, it_Contact.Normal))
            {
                SteepNormals.push_back(it_Contact.Normal);
            }
        }

        if (SteepNormals.empty())
        {
            return false;
        }

        const glm::vec2 l_Moved = MoveShape(record, l_UpPosition, stepForward);
        const glm::vec2 l_Across = l_Moved - l_UpPosition;
        const float l_AcrossSquared = glm::dot(l_Across, l_Across);
        if (l_AcrossSquared < 1e-8f)
        {
            return false;
        }

        // Way made towards a slope it walked into, rather than sliding along it
        const float l_ForwardLength = glm::length(stepForward);
        const glm::vec2 l_Forward = stepForward / l_ForwardLength;
        const bool l_Progress = std::ranges::any_of(SteepNormals, [&](const glm::vec2& normal)
        {
            const glm::vec2 l_Direction = glm::dot(normal, l_UpDirection) * l_UpDirection - normal;
            const float l_Length = glm::length(l_Direction);

            return l_Length > 1e-6f && glm::dot(l_Direction, l_Forward) / l_Length > 0.707107f && glm::dot(l_Direction, l_Across) / l_Length > 0.05f * l_ForwardLength;
        });

        if (!l_Progress)
        {
            return false;
        }

        const glm::vec2 l_Down = -l_Up;
        const float l_Fall = Cast(record, l_Moved, l_Down, &l_Hit);
        if (B2_IS_NULL(l_Hit.Shape))
        {
            return false;
        }

        const glm::vec2 l_Final = l_Moved + l_Down * l_Fall;
        glm::vec2 l_Surface = l_Hit.Normal;
        if (IsTooSteep(record, l_Surface))
        {
            if (glm::dot(stepForwardTest, stepForwardTest) <= 0.0f)
            {
                return false;
            }

            const glm::vec2 l_Tested = MoveShape(record, l_UpPosition, stepForwardTest);
            const glm::vec2 l_TestAcross = l_Tested - l_UpPosition;
            if (glm::dot(l_TestAcross, l_TestAcross) <= l_AcrossSquared + 1e-8f)
            {
                return false;
            }

            static_cast<void>(Cast(record, l_Tested, l_Down, &l_Hit));
            if (B2_IS_NULL(l_Hit.Shape) || IsTooSteep(record, l_Hit.Normal))
            {
                return false;
            }

            l_Surface = l_Hit.Normal;
        }

        if (glm::dot(l_Final - position, l_Surface) < 1e-4f)
        {
            return false;
        }

        position = l_Final;
        UpdateGround(record, position, velocity, seconds, true);
        record.Ground = GroundState2D::OnGround;

        return true;
    }

    // Down by up to the step, onto whatever is there
    bool PhysicsScene2D::State::StickToFloor(BodyRecord2D& record, glm::vec2& position, const glm::vec2& stepDown, const glm::vec2& velocity, float seconds)
    {
        CharacterContact2D l_Hit;
        const float l_Fraction = Cast(record, position, stepDown, &l_Hit);
        if (B2_IS_NULL(l_Hit.Shape))
        {
            return false;
        }

        position += stepDown * l_Fraction;
        UpdateGround(record, position, velocity, seconds, true);

        return true;
    }

    // Standing a little above the ground, as a slow landing leaves it, a character settles straight down onto it, into Box2D's slop as a move pressed against it rests, so it stands at one height however it came down. Going straight down, it slides down no slope, and it is placed there as StickToFloor places it, on ground it may be leaving
    void PhysicsScene2D::State::Settle(BodyRecord2D& record, glm::vec2& position, const glm::vec2& velocity, float seconds)
    {
        if (record.Ground != GroundState2D::OnGround)
        {
            return;
        }

        const glm::vec2 l_Up = GetUp();
        const glm::vec2 l_Foot = GetFoot(record, position);
        const CharacterContact2D* l_Nearest = nullptr;
        for (const CharacterContact2D& it_Contact : record.Contacts)
        {
            if (it_Contact.Distance <= c_SupportDistance && glm::dot(it_Contact.Point - l_Foot, l_Up) <= 0.0f && !IsTooSteep(record, it_Contact.Normal) && (l_Nearest == nullptr || it_Contact.Distance < l_Nearest->Distance))
            {
                l_Nearest = &it_Contact;
            }
        }

        // Within half the slop of resting, where a cast would go a slop deeper
        if (l_Nearest == nullptr || l_Nearest->Distance <= -0.5f * c_TouchingSlop)
        {
            return;
        }

        const glm::vec2 l_Down = -l_Up * (l_Nearest->Distance + 2.0f * c_TouchingSlop) / std::max(glm::dot(l_Nearest->Normal, l_Up), 0.1f);
        CharacterContact2D l_Hit;
        const float l_Fraction = Cast(record, position, l_Down, &l_Hit);
        if (B2_IS_NULL(l_Hit.Shape))
        {
            return;
        }

        position += l_Down * l_Fraction;
        UpdateGround(record, position, velocity, seconds, true);
    }

    // Jolt's ExtendedUpdate for a 2D character: its velocity held back from slopes too steep it is pressed against, then its move, then, having just left the ground without rising, down to a floor within its step height, and, having walked into a step's side short of where it was going, up the step, in the way of the wall it walked into most squarely, by at least Jolt's least step and looking past an edge by at least its radius
    void PhysicsScene2D::State::MoveCharacter(BodyRecord2D& record, glm::vec2& position, const glm::vec2& velocity, float seconds)
    {
        const glm::vec2 l_Up = GetUp();
        const glm::vec2 l_Velocity = CancelVelocityTowardsSteepSlopes(record, velocity);
        const glm::vec2 l_Old = position;
        const bool l_WasSupported = record.Ground == GroundState2D::OnGround || record.Ground == GroundState2D::OnSteepGround;
        position = MoveShape(record, position, l_Velocity * seconds);
        UpdateGround(record, position, l_Velocity, seconds, false);
        const bool l_Supported = record.Ground == GroundState2D::OnGround || record.Ground == GroundState2D::OnSteepGround;
        if (l_WasSupported && !l_Supported && record.StepHeight > 0.0f && glm::dot(position - l_Old, l_Up) / seconds <= 1e-6f)
        {
            static_cast<void>(StickToFloor(record, position, -l_Up * record.StepHeight, l_Velocity, seconds));
        }

        if (record.StepHeight <= 0.0f)
        {
            return;
        }

        glm::vec2 l_Desired = velocity * seconds;
        l_Desired -= glm::dot(l_Desired, l_Up) * l_Up;
        const float l_DesiredLength = glm::length(l_Desired);
        if (!(l_DesiredLength > 0.0f))
        {
            return;
        }

        const glm::vec2 l_Forward = l_Desired / l_DesiredLength;
        glm::vec2 l_Achieved = position - l_Old;
        l_Achieved -= glm::dot(l_Achieved, l_Up) * l_Up;
        const float l_AchievedLength = std::max(glm::dot(l_Achieved, l_Forward), 0.0f);
        if (l_AchievedLength + 1e-4f >= l_DesiredLength || !CanWalkStairs(record, velocity))
        {
            return;
        }

        glm::vec2 l_Direction = l_Forward;
        float l_MostAlong = c_StairsCosAngleForwardContact;
        for (const CharacterContact2D& it_Contact : record.Contacts)
        {
            if (it_Contact.Distance > c_SupportDistance || glm::dot(it_Contact.Normal, velocity - it_Contact.Velocity) >= 0.0f || !IsTooSteep(record, it_Contact.Normal))
            {
                continue;
            }

            glm::vec2 l_Test = glm::dot(it_Contact.Normal, l_Up) * l_Up - it_Contact.Normal;
            const float l_Length = glm::length(l_Test);
            if (l_Length > 1e-6f)
            {
                l_Test /= l_Length;
                const float l_Along = glm::dot(l_Test, l_Forward);
                if (l_Along > l_MostAlong)
                {
                    l_Direction = l_Test;
                    l_MostAlong = l_Along;
                }
            }
        }

        const glm::vec2 l_StepForward = l_Direction * std::max(c_StairsMinStepForward, l_DesiredLength - l_AchievedLength);
        const glm::vec2 l_StepForwardTest = l_Direction * std::max(c_StairsStepForwardTest, record.Radius);
        static_cast<void>(WalkStairs(record, position, seconds, l_Up * record.StepHeight, l_StepForward, l_StepForwardTest, l_Velocity));
    }

    // Once a step, each dynamic body the character touches and goes into, at the contact it goes into fastest, as Jolt pushes one for a 3D character: the impulse that brings the body to the character's speed there, up to the push strength over the step, and none downwards, since the character's weight on what it stands on is added apart. Box2D leaves the character its slop inside what it rests against, which pushes nothing
    void PhysicsScene2D::State::PushBodies(const BodyRecord2D& record, const glm::vec2& velocity, float seconds)
    {
        Pushes.clear();
        for (const CharacterContact2D& it_Contact : record.Contacts)
        {
            if (it_Contact.Distance > c_SupportDistance || it_Contact.Type != b2_dynamicBody || !b2Body_IsValid(it_Contact.Body))
            {
                continue;
            }

            constexpr float c_Damping = 0.9f;
            constexpr float c_PenetrationResolution = 0.4f;
            const float l_Wanted = -glm::dot(velocity - it_Contact.Velocity, it_Contact.Normal) * c_Damping - std::min(it_Contact.Distance + c_TouchingSlop, 0.0f) * c_PenetrationResolution / seconds;
            if (l_Wanted <= 0.0f)
            {
                continue;
            }

            const auto a_Found = std::ranges::find_if(Pushes, [&it_Contact](const BodyPush2D& push) { return B2_ID_EQUALS(push.Contact->Body, it_Contact.Body); });
            if (a_Found == Pushes.end())
            {
                Pushes.push_back({ &it_Contact, l_Wanted });
            }
            else if (l_Wanted > a_Found->Wanted)
            {
                *a_Found = { &it_Contact, l_Wanted };
            }
        }

        const glm::vec2 l_Up = GetUp();
        const float l_Strength = std::max(record.Controller.PushStrength, 0.0f);
        for (const BodyPush2D& it_Push : Pushes)
        {
            const CharacterContact2D& l_Contact = *it_Push.Contact;
            const float l_Mass = b2Body_GetMass(l_Contact.Body);
            const float l_Inertia = b2Body_IsFixedRotation(l_Contact.Body) ? 0.0f : b2Body_GetRotationalInertia(l_Contact.Body);
            const glm::vec2 l_Arm = l_Contact.Point - FromBox2D(b2Body_GetWorldCenterOfMass(l_Contact.Body));
            const float l_Turn = l_Arm.x * l_Contact.Normal.y - l_Arm.y * l_Contact.Normal.x;
            const float l_InverseMass = (l_Mass > 0.0f ? 1.0f / l_Mass : 0.0f) + (l_Inertia > 0.0f ? l_Turn * l_Turn / l_Inertia : 0.0f);
            if (!(l_InverseMass > 0.0f))
            {
                continue;
            }

            const float l_Impulse = std::min(it_Push.Wanted / l_InverseMass, l_Strength * seconds);
            glm::vec2 l_World = -l_Impulse * l_Contact.Normal;
            const float l_Down = glm::dot(l_World, l_Up);
            l_World -= l_Down < 0.0f ? l_Down * l_Up : glm::vec2(0.0f);
            b2Body_ApplyLinearImpulse(l_Contact.Body, ToBox2D(l_World), ToBox2D(l_Contact.Point), true);
        }
    }

    // What is left of the velocity once the walls, ceilings and slopes too steep to stand on that the character touches take the part going into them, as each moves, in a few passes for corners. As for a 3D character, one facing up first stops it as a wall would, so walking into it does not lift the character, and what falls onto it slides off
    glm::vec2 PhysicsScene2D::State::KeepVelocity(const BodyRecord2D& record, const glm::vec2& velocity) const
    {
        constexpr int c_Passes = 4;
        const glm::vec2 l_Up = GetUp();
        glm::vec2 l_Velocity = velocity;
        for (int it_Pass = 0; it_Pass < c_Passes; ++it_Pass)
        {
            bool l_Changed = false;
            const auto a_Hold = [&l_Velocity, &l_Changed](const glm::vec2& normal, const glm::vec2& moving)
            {
                const float l_Into = glm::dot(l_Velocity - moving, normal);
                if (l_Into < 0.0f)
                {
                    l_Velocity -= l_Into * normal;
                    l_Changed = true;
                }
            };

            for (const CharacterContact2D& it_Contact : record.Contacts)
            {
                if (it_Contact.Distance > c_SupportDistance || !IsTooSteep(record, it_Contact.Normal))
                {
                    continue;
                }

                const float l_Rise = glm::dot(it_Contact.Normal, l_Up);
                const glm::vec2 l_Across = it_Contact.Normal - l_Rise * l_Up;
                if (l_Rise > 1e-3f && glm::length(l_Across) > 1e-6f)
                {
                    const glm::vec2 l_Wall = glm::normalize(l_Across);
                    a_Hold(l_Wall, glm::dot(it_Contact.Velocity, l_Wall) * l_Wall);
                }

                a_Hold(it_Contact.Normal, it_Contact.Velocity);
            }

            if (!l_Changed)
            {
                break;
            }
        }

        return l_Velocity;
    }

    // Where a body's motion over the next step takes a point, as Box2D moves the body: its centre of mass along its velocity, and turned about it. The velocity that takes the point there, none for no body
    glm::vec2 PhysicsScene2D::State::Predict(const CarryingBody2D& carrier, const glm::vec2& position, float seconds) const
    {
        if (B2_IS_NULL(carrier.Body) || !b2Body_IsValid(carrier.Body) || !(seconds > 0.0f))
        {
            return glm::vec2(0.0f);
        }

        const glm::vec2 l_Arm = position - FromBox2D(b2Body_GetWorldCenterOfMass(carrier.Body));
        const glm::vec2 l_Moved = FromBox2D(b2Body_GetLinearVelocity(carrier.Body)) * seconds + Rotate(l_Arm, b2Body_GetAngularVelocity(carrier.Body) * seconds) - l_Arm;

        return l_Moved / seconds;
    }

    // How fast the ground under a character moves at a point, its feet's, from its body's own velocities, none for still ground
    glm::vec2 PhysicsScene2D::State::GetGroundVelocity(const BodyRecord2D& record, const glm::vec2& position) const
    {
        if (B2_IS_NULL(record.GroundBody) || !b2Body_IsValid(record.GroundBody) || b2Body_GetType(record.GroundBody) == b2_staticBody)
        {
            return glm::vec2(0.0f);
        }

        return FromBox2D(b2Body_GetWorldPointVelocity(record.GroundBody, ToBox2D(position)));
    }

    // How fast a shape a character meets moves at a point, as a 3D character sees it: against the body carrying the character over the step, which happens apart, so that body is still, what rides it moves only as it moves on it, and the world goes past. Still ground and other characters, which are placed rather than moved and ride along, move at none
    glm::vec2 PhysicsScene2D::State::GetContactVelocity(const BodyRecord2D& record, b2ShapeId shape, b2BodyId body, const glm::vec2& point) const
    {
        const ShapeOwner2D* l_Owner = FindShape(shape);
        if (b2Body_GetType(body) == b2_staticBody || (l_Owner != nullptr && l_Owner->Record != nullptr && l_Owner->Record->HasCharacter))
        {
            return glm::vec2(0.0f);
        }

        glm::vec2 l_Velocity = FromBox2D(b2Body_GetWorldPointVelocity(body, ToBox2D(point)));
        if (B2_IS_NON_NULL(record.Carrier.Body) && b2Body_IsValid(record.Carrier.Body))
        {
            l_Velocity -= FromBox2D(b2Body_GetWorldPointVelocity(record.Carrier.Body, ToBox2D(point)));
        }

        return l_Velocity;
    }

    // The character's body taken to where it moved, turned as it was, waking what it left and what it reached, since Box2D wakes neither
    void PhysicsScene2D::State::PlaceCharacter(BodyRecord2D& record, const glm::vec2& position)
    {
        const b2Vec2 l_Position = ToBox2D(position);
        const b2Vec2 l_Was = b2Body_GetPosition(record.Body);
        if (l_Was.x == l_Position.x && l_Was.y == l_Position.y)
        {
            return;
        }

        const b2AABB l_Before = b2Body_ComputeAABB(record.Body);
        b2Body_SetTransform(record.Body, l_Position, b2Body_GetRotation(record.Body));
        WakeAround(b2AABB_Union(l_Before, b2Body_ComputeAABB(record.Body)));
    }

    // Each character, in the records' order, moved for the step by its own velocity before the world steps, as a 3D one is. One whose transform was set from code is taken there first. Standing on ground it can stand on, it loses the vertical part of its velocity, and off the ground gravity adds to it. MoveCharacter moves it, climbing and keeping to the floor, it settles onto ground it stands just above, it pushes, and the walls, ceilings and slopes too steep to stand on it touches take from its velocity the part that went into them. A body it stands on then carries it over the step, which CarryCharacters does once the world has stepped. Leaving the ground it keeps what carried it, and landing it gives back what it lands on moves at, so its velocity is its own, against what it stands on. Its body is placed where it moved
    void PhysicsScene2D::State::MoveCharacters(float seconds)
    {
        LastSeconds = seconds;
        SceneRegistry& l_Registry = Owner.GetRegistry();
        const glm::vec2 l_Gravity = World.GetGravity();
        const glm::vec2 l_Up = GetUp();
        for (BodyRecord2D* it_Record : Records)
        {
            if (!it_Record->HasCharacter || B2_IS_NULL(it_Record->Body) || !l_Registry.valid(it_Record->Entity))
            {
                continue;
            }

            BodyRecord2D& l_Record = *it_Record;
            const TransformComponent& l_Transform = l_Registry.get<TransformComponent>(l_Record.Entity);
            if (l_Transform.Position != l_Record.WrittenPosition || l_Transform.Rotation != l_Record.WrittenRotation)
            {
                PlaceAtTransform(l_Record);
            }

            const bool l_Stood = l_Record.Ground == GroundState2D::OnGround;
            l_Record.Carrier = {};
            if (l_Stood && B2_IS_NON_NULL(l_Record.GroundBody) && b2Body_IsValid(l_Record.GroundBody) && b2Body_GetType(l_Record.GroundBody) != b2_staticBody)
            {
                l_Record.Carrier = { l_Record.GroundBody, b2Body_GetTransform(l_Record.GroundBody) };
            }

            glm::vec2 l_Velocity = l_Record.Velocity;
            const float l_Rising = glm::dot(l_Velocity, l_Up);
            if (l_Stood && l_Rising < c_RisingSpeed)
            {
                l_Velocity -= l_Rising * l_Up;
            }
            else
            {
                l_Velocity += l_Gravity * seconds;
            }

            glm::vec2 l_Position = FromBox2D(b2Body_GetPosition(l_Record.Body));
            MoveCharacter(l_Record, l_Position, l_Velocity, seconds);
            Settle(l_Record, l_Position, l_Velocity, seconds);
            PushBodies(l_Record, l_Velocity, seconds);
            l_Velocity = KeepVelocity(l_Record, l_Velocity);

            // Walked off onto other ground, so nothing carries it this step
            const bool l_Stands = l_Record.Ground == GroundState2D::OnGround;
            if (l_Stands && !B2_ID_EQUALS(l_Record.GroundBody, l_Record.Carrier.Body))
            {
                l_Record.Carrier = {};
            }

            const glm::vec2 l_Foot = GetFoot(l_Record, l_Position);
            const glm::vec2 l_Carried = Predict(l_Record.Carrier, l_Foot, seconds);
            if (l_Stands)
            {
                l_Velocity -= l_Stood ? glm::vec2(0.0f) : GetGroundVelocity(l_Record, l_Foot);
                const float l_Left = glm::dot(l_Velocity, l_Up);
                l_Velocity -= l_Left < c_RisingSpeed ? l_Left * l_Up : glm::vec2(0.0f);
            }
            else if (l_Stood)
            {
                l_Velocity += l_Carried;
            }

            l_Record.Velocity = l_Velocity;
            PlaceCharacter(l_Record, l_Position);
        }
    }

    // Each character a body carried over the step taken there, as the body's pose before the step and after it move the middle of its lower end cap, so its feet keep their place on the body. It keeps its own turn, standing upright as the body turns under it
    void PhysicsScene2D::State::CarryCharacters()
    {
        for (BodyRecord2D* it_Record : Records)
        {
            if (!it_Record->HasCharacter || B2_IS_NULL(it_Record->Body) || B2_IS_NULL(it_Record->Carrier.Body))
            {
                continue;
            }

            const CarryingBody2D l_Carrier = std::exchange(it_Record->Carrier, CarryingBody2D{});
            if (!b2Body_IsValid(l_Carrier.Body))
            {
                continue;
            }

            const glm::vec2 l_Position = FromBox2D(b2Body_GetPosition(it_Record->Body));
            const glm::vec2 l_Foot = GetFoot(*it_Record, l_Position);
            const b2Vec2 l_Local = b2InvTransformPoint(l_Carrier.Before, ToBox2D(l_Foot));
            PlaceCharacter(*it_Record, l_Position + FromBox2D(b2TransformPoint(b2Body_GetTransform(l_Carrier.Body), l_Local)) - l_Foot);
        }
    }

    // The record of the 2D body made for the entity, or for the body one of its colliders is part of, with the scene's changes taken in
    BodyRecord2D* PhysicsScene2D::State::FindBody(UUID entity)
    {
        Sync();
        const Entity l_Entity = Owner.FindEntityByUUID(entity);
        const auto a_Found = l_Entity ? Members.find(l_Entity.GetHandle()) : Members.end();
        if (a_Found == Members.end() || B2_IS_NULL(a_Found->second->Body) || !Owner.GetRegistry().valid(a_Found->second->Entity))
        {
            return nullptr;
        }

        return a_Found->second;
    }
}