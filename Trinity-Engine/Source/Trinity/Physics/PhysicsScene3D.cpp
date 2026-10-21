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
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/Character.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/Physics/Collision/SimShapeFilter.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>

#include <algorithm>
#include <bit>
#include <cfloat>
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
        // A body whose shapes weigh less than this, such as one flat hull or a triangle mesh, is given a box's inertia and this mass, unless a dynamic body's RigidBody gives one. Jolt asks a kinematic body for one too, though nothing pushes it
        constexpr float c_MinimumMass = 1e-6f;
        constexpr float c_FallbackMass = 1.0f;
        // Set in a body's user data, beside the pointer to its record, when it is a record's sensor, or a character's inner body
        constexpr std::uint64_t c_SensorBit = 1;
        constexpr std::uint64_t c_CharacterBit = 2;
        constexpr std::uint64_t c_RecordBits = c_SensorBit | c_CharacterBit;
        // A character on ground it can stand on that moves up slower than this, against the ground, stays on it, and a faster one leaves it, as one jumping does
        constexpr float c_RisingSpeed = 0.1f;
        // The slopes a character's MaxSlope is held between, since Jolt takes one under about a degree as no limit at all
        constexpr float c_MinimumSlope = 0.0174533f;
        constexpr float c_MaximumSlope = 1.5707963f;
        // A ray's hit is found again from this fraction of its distance, and a metre's, before it
        constexpr float c_RefineMargin = 1e-3f;
        // The least a character weighs, in kilograms
        constexpr float c_MinimumCharacterMass = 0.1f;
        // A physical character finds the ground this far below its capsule, as Jolt's Character is used to, and is held back by walls and steep slopes this close to it
        constexpr float c_GroundDistance = 0.05f;
        constexpr float c_TouchDistance = 0.01f;
        // A physical character stepping up looks for the step's top this far past the front of its capsule, from this far above its step height
        constexpr float c_StepReach = 0.05f;
        // It climbs a step this high or more, and is lifted this far above it
        constexpr float c_StepSkin = 1e-3f;
        // It fits on the step sunk this deep or less into what is about it, as the solver leaves a body resting against another
        constexpr float c_StepAllowance = 0.03f;

        enum class ColliderKind : std::uint8_t
        {
            Box,
            Sphere,
            Capsule,
            Cylinder,
            ConvexHull,
            Mesh
        };

        // A collider as it was last read: a box's half extents, or a round shape's radius and height, and its entity's UUID, which queries report
        struct ColliderSource
        {
            entt::entity Entity = entt::null;
            ColliderKind Kind = ColliderKind::Box;
            ColliderSettings Settings;
            glm::vec3 Size{ 0.0f };
            UUID Shape;
            UUID ID;

            [[nodiscard]] bool operator==(const ColliderSource&) const = default;
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

        // A body carrying a character over a step: where it was as the step began, and how it moved then, about its centre of mass
        struct CarryingBody
        {
            JPH::BodyID Body;
            JPH::RVec3 Position = JPH::RVec3::sZero();
            JPH::Quat Rotation = JPH::Quat::sIdentity();
            JPH::RVec3 Center = JPH::RVec3::sZero();
            JPH::Vec3 Linear = JPH::Vec3::sZero();
            JPH::Vec3 Angular = JPH::Vec3::sZero();
        };

        // A wall, ceiling or slope too steep to stand on that a character touches: its normal out towards the character, and how fast it moves there
        struct HeldContact
        {
            JPH::Vec3 Normal = JPH::Vec3::sZero();
            JPH::Vec3 Velocity = JPH::Vec3::sZero();
        };

        // What a physical character's capsule touches or nearly does: the body and the part of it, where, the normal out towards the capsule, and how deep the capsule is in it, less than nothing for a gap
        struct CapsuleTouch
        {
            JPH::BodyID Body;
            JPH::SubShapeID Part;
            JPH::RVec3 Point = JPH::RVec3::sZero();
            JPH::Vec3 Normal = JPH::Vec3::sZero();
            float Depth = 0.0f;
        };

        // What a character's capsule touches or nearly does, by the entity of the body it is part of: where, the normal there out towards the capsule, and how far apart they are, less than nothing for a capsule in it
        struct CharacterTouch
        {
            UUID Entity;
            glm::vec3 Point{ 0.0f };
            glm::vec3 Normal{ 0.0f };
            float Distance = 0.0f;
        };

        // Jolt's Character, told what it stands on by PhysicsScene3D, which finds that with the character's capsule alone, since its body holds the solids that join it besides
        class PhysicalCharacter final : public JPH::Character
        {
        public:
            using JPH::Character::Character;

            void SetGround(EGroundState state, const JPH::BodyID& body, const JPH::SubShapeID& part, JPH::RVec3Arg position, JPH::Vec3Arg normal, JPH::Vec3Arg velocity, std::uint64_t userData)
            {
                mGroundState = state;
                mGroundBodyID = body;
                mGroundBodySubShapeID = part;
                mGroundPosition = position;
                mGroundNormal = normal;
                mGroundVelocity = velocity;
                mGroundUserData = userData;
            }
        };

        // A body's owner and what it was made from. Each record is an allocation of its own, which never moves, since bodies point at theirs, and records come and go as the scene changes while it runs
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
            // A compound's parts carry their collider's index, and a single shape is the collider here, for the solids and the sensor's triggers
            bool Compound = false;
            std::uint32_t SingleSolid = 0;
            bool SensorCompound = false;
            std::uint32_t SensorSingle = 0;
            JPH::BodyID Body;
            JPH::BodyID Sensor;
            bool Waiting = true;
            BodyPose Previous;
            BodyPose Current;
            // Where its entity's transform puts it, last frame and this one, which a kinematic body and a sensor that follows its entity move between over the frame's steps
            BodyPose TargetFrom;
            BodyPose TargetTo;
            // The entity's local position and rotation as the body was made or last wrote them, so a transform set from code is seen and the body teleported there
            glm::vec3 WrittenPosition{ 0.0f };
            glm::quat WrittenRotation = glm::identity<glm::quat>();
            // A character's controller as it was last read, its Jolt character once made, its step height and radius in the world, and the velocity of its own, which lasts until changed. A kinematic one's colliders' bodies follow it
            bool HasCharacter = false;
            CharacterControllerComponent Controller;
            JPH::Ref<JPH::CharacterVirtual> Character;
            float StepHeight = 0.0f;
            float Radius = 0.0f;
            glm::vec3 Velocity{ 0.0f };
            // The displacement scripts asked a character for since its last step, which its next step covers besides its velocity, and for a physical one the velocity of what of it was covered over the step
            glm::vec3 Move{ 0.0f };
            glm::vec3 Moved{ 0.0f };
            // The bodies a character touches, by their entities' UUIDs in order, kept until it moves away from them, so it hits each once as it begins touching it
            PhysicsVector<UUID> Touching;
            // The body a character stood on as its step began, and where that body was, which carries it over the step
            CarryingBody Carrier;
            // A physical character's Jolt character in place of the kinematic one, whose body is the record's own Body, and its capsule alone, where it sits on the body and its half segment, which find the ground and step
            JPH::Ref<PhysicalCharacter> Physical;
            JPH::RefConst<JPH::Shape> Capsule;
            glm::vec3 CapsuleOffset{ 0.0f };
            float HalfSegment = 0.0f;
            // What a physical character's last step left it touching: what is too steep to stand on, the normals of ground it can stand on that holds it up, and how deep it was in the ground, less than nothing above it
            PhysicsVector<HeldContact> Held;
            PhysicsVector<JPH::Vec3> Supports;
            float GroundDepth = 0.0f;
            // As a physical character's step began: whether it stood on ground it can stand on, and walked along it rather than rising off it, the velocity its body was given, and the velocity and turn the body it stood on gives it over the step. Then the velocity the world's step left its body with, which tells apart what is done to the body between steps, such as a kinematic character's push
            bool Stood = false;
            bool Walking = false;
            glm::vec3 Given{ 0.0f };
            glm::vec3 Carried{ 0.0f };
            JPH::Quat Turn = JPH::Quat::sIdentity();
            glm::vec3 Stepped{ 0.0f };
            // A physical character that walked off ground, which is set down onto ground until it has fallen its step height, and how high it walked off
            bool WalkedOff = false;
            float OffHeight = 0.0f;
            // A physical character lifted onto a step it walks into: the step's top, which it stands on until its capsule is over it, how much further it walks that way to get there, and the way
            CapsuleTouch Step;
            float Climb = 0.0f;
            glm::vec3 ClimbWay{ 0.0f };
        };

        // A dynamic body's velocities, carried over when it is made again
        struct BodyVelocity
        {
            JPH::Vec3 Linear = JPH::Vec3::sZero();
            JPH::Vec3 Angular = JPH::Vec3::sZero();
        };

        // A joint as it was last read: its entity, the entity it names and the records of its two bodies, the second none for the world, and its constraint once made. A joint that cannot be made is dropped, and one that breaks is let go
        struct JointRecord
        {
            entt::entity Entity = entt::null;
            UUID ID;
            entt::entity OtherEntity = entt::null;
            UUID Other;
            JointComponent Settings;
            BodyRecord* First = nullptr;
            BodyRecord* Second = nullptr;
            JPH::Ref<JPH::TwoBodyConstraint> Constraint;
            // While a body it joins is changed, its constraint in each body's own space, Jolt's first body the other and its second its own, and the centres of mass that space was measured from, so it is made again holding the pose it was first made in
            JPH::Ref<JPH::TwoBodyConstraintSettings> Saved;
            JPH::Vec3 SavedCenter1 = JPH::Vec3::sZero();
            JPH::Vec3 SavedCenter2 = JPH::Vec3::sZero();
            // Its bodies' pair, kept from colliding while it holds
            std::uint64_t Pair = 0;
            bool KeptApart = false;
            bool Waiting = true;
            bool Broken = false;
        };

        std::string_view GetJointTypeName(JointType type)
        {
            switch (type)
            {
                case JointType::Fixed:
                    return "fixed";
                case JointType::Hinge:
                    return "hinge";
                case JointType::Slider:
                    return "slider";
                case JointType::Distance:
                    return "distance";
                case JointType::Point:
                    return "point";
            }

            return "unknown";
        }

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
            return reinterpret_cast<const BodyRecord*>(body.GetUserData() & ~c_RecordBits);
        }

        // The material of the solid collider a part of the body came from
        const ColliderMaterial* GetMaterial(const JPH::Body& body, const JPH::SubShapeID& part)
        {
            const BodyRecord* l_Record = GetRecord(body);
            if (l_Record == nullptr || (body.GetUserData() & c_RecordBits) != 0 || l_Record->Materials.empty())
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

        // Which of its record's solids or triggers a part of a body came from, found from the part's sub-shape ID, which Jolt gives for the shape that part is or, in a query's hit, for a triangle or other piece of it. A compound as a whole, which Jolt asks about before its parts, is none of them, and nor is a character's inner body. On a physical character's body the one past its solids is its capsule
        std::optional<std::uint32_t> FindPart(const JPH::Body& body, const JPH::Shape* shape, const JPH::SubShapeID& part)
        {
            const BodyRecord* l_Record = GetRecord(body);
            if (l_Record == nullptr || (body.GetUserData() & c_CharacterBit) != 0)
            {
                return std::nullopt;
            }

            const bool l_Sensor = (body.GetUserData() & c_SensorBit) != 0;
            if (!(l_Sensor ? l_Record->SensorCompound : l_Record->Compound))
            {
                return l_Sensor ? l_Record->SensorSingle : l_Record->SingleSolid;
            }

            if (shape == body.GetShape())
            {
                return std::nullopt;
            }

            const JPH::CompoundShape& l_Compound = static_cast<const JPH::CompoundShape&>(*body.GetShape());
            JPH::SubShapeID l_Remainder;

            return l_Compound.GetCompoundUserData(l_Compound.GetSubShapeIndexFromID(part, l_Remainder));
        }

        // The collider a part of a body came from, none for a part no one collider made
        const ColliderSource* FindCollider(const JPH::Body& body, const JPH::Shape* shape, const JPH::SubShapeID& part)
        {
            const std::optional<std::uint32_t> l_Index = FindPart(body, shape, part);
            if (!l_Index)
            {
                return nullptr;
            }

            const BodyRecord& l_Record = *GetRecord(body);
            const PhysicsVector<ColliderSource>& l_Colliders = (body.GetUserData() & c_SensorBit) != 0 ? l_Record.Triggers : l_Record.Solids;

            return *l_Index < l_Colliders.size() ? &l_Colliders[*l_Index] : nullptr;
        }

        // Whether a part of a body is a physical character's capsule
        bool IsCapsule(const JPH::Body& body, const JPH::Shape* shape, const JPH::SubShapeID& part)
        {
            const BodyRecord* l_Record = GetRecord(body);
            if (l_Record == nullptr || l_Record->Physical == nullptr || (body.GetUserData() & c_RecordBits) != 0)
            {
                return false;
            }

            const std::optional<std::uint32_t> l_Index = FindPart(body, shape, part);

            return l_Index && *l_Index == l_Record->Solids.size();
        }

        // A character's collision layer, one past the last there can be taken as the first, as a collider's is
        std::uint32_t GetCharacterLayer(const BodyRecord& record)
        {
            return record.Controller.Layer < c_MaxCollisionLayers ? record.Controller.Layer : 0;
        }

        // A body whose colliders are all on one layer was let through by its object layer alone. One with colliders on several has each of its parts held to the matrix, and a compound as a whole by all its layers, until Jolt reaches its parts
        class LayerShapeFilter final : public JPH::SimShapeFilter
        {
        public:
            explicit LayerShapeFilter(const CollisionLayers& layers) : m_Layers(layers)
            {

            }

            [[nodiscard]] bool ShouldCollide(const JPH::Body& first, const JPH::Shape* firstShape, const JPH::SubShapeID& firstPart, const JPH::Body& second, const JPH::Shape* secondShape, const JPH::SubShapeID& secondPart) const override
            {
                return (m_Layers.GetCollidingLayers(GetPartLayers(first, firstShape, firstPart)) & GetPartLayers(second, secondShape, secondPart)) != 0;
            }

        private:
            [[nodiscard]] static std::uint32_t GetPartLayers(const JPH::Body& body, const JPH::Shape* shape, const JPH::SubShapeID& part)
            {
                const std::uint32_t l_Layers = PhysicsLayers3D::GetLayers(body.GetObjectLayer());
                if (std::has_single_bit(l_Layers))
                {
                    return l_Layers;
                }

                if (const ColliderSource* l_Collider = FindCollider(body, shape, part))
                {
                    return 1u << l_Collider->Settings.Layer;
                }

                return IsCapsule(body, shape, part) ? 1u << GetCharacterLayer(*GetRecord(body)) : l_Layers;
            }

            const CollisionLayers& m_Layers;
        };

        // A query's filters, from the broadphase down: bodies on none of its layers are passed over, then the ignored entity's body and sensors it did not ask for, then the ignored entity's colliders and those on other layers
        class QueryLayerFilter final : public JPH::ObjectLayerFilter
        {
        public:
            explicit QueryLayerFilter(std::uint32_t layers) : m_Layers(layers)
            {

            }

            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer) const override
            {
                return (PhysicsLayers3D::GetLayers(layer) & m_Layers) != 0;
            }

        private:
            std::uint32_t m_Layers;
        };

        class QueryBodyFilter final : public JPH::BodyFilter
        {
        public:
            explicit QueryBodyFilter(const PhysicsQueryFilter3D& filter) : m_Filter(filter)
            {

            }

            [[nodiscard]] bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                const BodyRecord* l_Record = GetRecord(body);

                return l_Record != nullptr && l_Record->ID != m_Filter.Ignore && (m_Filter.HitTriggers || (body.GetUserData() & c_SensorBit) == 0);
            }

        private:
            const PhysicsQueryFilter3D& m_Filter;
        };

        class QueryShapeFilter final : public JPH::ShapeFilter
        {
        public:
            QueryShapeFilter(const JPH::BodyLockInterface& bodies, const PhysicsQueryFilter3D& filter) : m_Bodies(bodies), m_Filter(filter)
            {

            }

            [[nodiscard]] bool ShouldCollide(const JPH::Shape* shape, const JPH::SubShapeID& part) const override
            {
                const JPH::Body* l_Body = m_Bodies.TryGetBody(mBodyID2);
                if (l_Body == nullptr)
                {
                    return true;
                }

                if (const ColliderSource* l_Collider = FindCollider(*l_Body, shape, part))
                {
                    return (m_Filter.Layers & (1u << l_Collider->Settings.Layer)) != 0 && l_Collider->ID != m_Filter.Ignore;
                }

                return !IsCapsule(*l_Body, shape, part) || (m_Filter.Layers & (1u << GetCharacterLayer(*GetRecord(*l_Body)))) != 0;
            }

            // A shape cast or overlap names its own shape first
            [[nodiscard]] bool ShouldCollide([[maybe_unused]] const JPH::Shape* queryShape, [[maybe_unused]] const JPH::SubShapeID& queryPart, const JPH::Shape* shape, const JPH::SubShapeID& part) const override
            {
                return ShouldCollide(shape, part);
            }

        private:
            const JPH::BodyLockInterface& m_Bodies;
            const PhysicsQueryFilter3D& m_Filter;
        };

        // What a character meets as it moves and looks about: every body but its own record's, and no trigger, since a trigger finds a kinematic character through its inner body and a physical one through its body instead. Jolt leaves out a kinematic one's inner body
        class CharacterBodyFilter final : public JPH::BodyFilter
        {
        public:
            explicit CharacterBodyFilter(const BodyRecord& record) : m_Record(record)
            {

            }

            [[nodiscard]] bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                return !body.IsSensor() && GetRecord(body) != &m_Record;
            }

        private:
            const BodyRecord& m_Record;
        };

        // A body whose colliders are all on one layer was let through by its object layer, and one with colliders on several has each part held to the matrix against the character's layer
        class CharacterShapeFilter final : public JPH::ShapeFilter
        {
        public:
            CharacterShapeFilter(const JPH::BodyLockInterface& bodies, const CollisionLayers& layers, std::uint32_t layer) : m_Bodies(bodies), m_Layers(layers), m_Layer(layer)
            {

            }

            [[nodiscard]] bool ShouldCollide(const JPH::Shape* shape, const JPH::SubShapeID& part) const override
            {
                const JPH::Body* l_Body = m_Bodies.TryGetBody(mBodyID2);
                if (l_Body == nullptr || std::has_single_bit(PhysicsLayers3D::GetLayers(l_Body->GetObjectLayer())))
                {
                    return true;
                }

                if (const ColliderSource* l_Collider = FindCollider(*l_Body, shape, part))
                {
                    return m_Layers.Collides(m_Layer, l_Collider->Settings.Layer);
                }

                return !IsCapsule(*l_Body, shape, part) || m_Layers.Collides(m_Layer, GetCharacterLayer(*GetRecord(*l_Body)));
            }

            // A cast names the character's own shape first
            [[nodiscard]] bool ShouldCollide([[maybe_unused]] const JPH::Shape* characterShape, [[maybe_unused]] const JPH::SubShapeID& characterPart, const JPH::Shape* shape, const JPH::SubShapeID& part) const override
            {
                return ShouldCollide(shape, part);
            }

        private:
            const JPH::BodyLockInterface& m_Bodies;
            const CollisionLayers& m_Layers;
            std::uint32_t m_Layer;
        };

        // Turns off Jolt's own pushes, which PhysicsScene3D gives once a step itself. A character a body carries, which happens apart, sees every body as moving against that body, so the body is still, what rides it moves only as it moves on it, and the world goes past. Other characters ride along with it, since their inner bodies are placed rather than moved
        class CharacterListener final : public JPH::CharacterContactListener
        {
        public:
            void OnAdjustBodyVelocity(const JPH::CharacterVirtual* character, const JPH::Body& body, JPH::Vec3& linear, JPH::Vec3& angular) override
            {
                const BodyRecord* l_Record = reinterpret_cast<const BodyRecord*>(character->GetUserData() & ~c_RecordBits);
                if (l_Record == nullptr || l_Record->Carrier.Body.IsInvalid())
                {
                    return;
                }

                const CarryingBody& l_Carrier = l_Record->Carrier;
                if ((body.GetUserData() & c_CharacterBit) != 0)
                {
                    linear = JPH::Vec3::sZero();
                    angular = JPH::Vec3::sZero();

                    return;
                }

                linear -= l_Carrier.Linear + l_Carrier.Angular.Cross(JPH::Vec3(body.GetCenterOfMassPosition() - l_Carrier.Center));
                angular -= l_Carrier.Angular;
            }

            void OnContactAdded([[maybe_unused]] const JPH::CharacterVirtual* character, [[maybe_unused]] const JPH::CharacterContact& contact, JPH::CharacterContactSettings& settings) override
            {
                settings.mCanReceiveImpulses = false;
            }

            void OnContactPersisted([[maybe_unused]] const JPH::CharacterVirtual* character, [[maybe_unused]] const JPH::CharacterContact& contact, JPH::CharacterContactSettings& settings) override
            {
                settings.mCanReceiveImpulses = false;
            }
        };

        // A dynamic body a character pushes in a step: the contact it went into fastest, and the speed it would bring the body to there
        struct BodyPush
        {
            const JPH::CharacterContact* Contact = nullptr;
            float Wanted = 0.0f;
        };

        // The filters a character moves and looks about with, from its inner body's object layer
        struct CharacterFilters
        {
            CharacterFilters(const JPH::PhysicsSystem& system, const CollisionLayers& layers, const BodyRecord& record) : BroadPhase(system.GetDefaultBroadPhaseLayerFilter(GetLayer(record))), Objects(system.GetDefaultLayerFilter(GetLayer(record))), Bodies(record), Shapes(system.GetBodyLockInterfaceNoLock(), layers, record.Controller.Layer)
            {

            }

            [[nodiscard]] static JPH::ObjectLayer GetLayer(const BodyRecord& record)
            {
                return PhysicsLayers3D::Get(BodyMotion::Kinematic, 1u << GetCharacterLayer(record));
            }

            JPH::DefaultBroadPhaseLayerFilter BroadPhase;
            JPH::DefaultObjectLayerFilter Objects;
            CharacterBodyFilter Bodies;
            CharacterShapeFilter Shapes;
        };

        // A character's capsule as its entity's world scale sizes it, as a capsule collider is: across by the larger of X and Z and along by Y, a sphere when it has no length, and where its middle sits on its entity
        struct CharacterCapsule
        {
            JPH::RefConst<JPH::Shape> Shape;
            glm::vec3 Offset{ 0.0f };
            float Radius = 0.0f;
            float HalfSegment = 0.0f;
        };

        CharacterCapsule MakeCapsule(const CharacterControllerComponent& controller, const glm::vec3& scale)
        {
            const glm::vec3 l_Size = glm::abs(scale);
            CharacterCapsule l_Capsule;
            l_Capsule.Radius = std::max(controller.Radius * std::max(l_Size.x, l_Size.z), c_MinimumSize);
            l_Capsule.HalfSegment = controller.GetHalfSegment() * l_Size.y;
            l_Capsule.Shape = l_Capsule.HalfSegment < c_MinimumSize ? JPH::RefConst<JPH::Shape>(new JPH::SphereShape(l_Capsule.Radius)) : JPH::RefConst<JPH::Shape>(new JPH::CapsuleShape(l_Capsule.HalfSegment, l_Capsule.Radius));
            l_Capsule.Offset = controller.Offset * scale;

            return l_Capsule;
        }

        float GetCharacterMass(const CharacterControllerComponent& controller)
        {
            return std::max(controller.Mass, c_MinimumCharacterMass);
        }

        // The walls, ceilings and slopes too steep to stand on that a kinematic character met as it moved, in the order Jolt keeps them
        void GatherHeld(const JPH::CharacterVirtual& character, PhysicsVector<HeldContact>& held)
        {
            held.clear();
            for (const JPH::CharacterContact& it_Contact : character.GetActiveContacts())
            {
                if (it_Contact.mHadCollision && !it_Contact.mWasDiscarded && !it_Contact.mIsSensorB && character.IsSlopeTooSteep(it_Contact.mSurfaceNormal))
                {
                    held.push_back({ it_Contact.mContactNormal, it_Contact.mLinearVelocity });
                }
            }
        }

        // What a kinematic character's last move left it touching or nearly touching, of Jolt's contacts, but for sensors and what it let pass, each counted as the entity of the body or the other character's
        void GatherNearby(const JPH::CharacterVirtual& character, PhysicsVector<CharacterTouch>& nearby)
        {
            nearby.clear();
            for (const JPH::CharacterContact& it_Contact : character.GetActiveContacts())
            {
                const BodyRecord* l_Other = reinterpret_cast<const BodyRecord*>(it_Contact.mUserData & ~c_RecordBits);
                if (!it_Contact.mIsSensorB && !it_Contact.mWasDiscarded && l_Other != nullptr)
                {
                    nearby.push_back({ l_Other->ID, FromJolt(JPH::Vec3(it_Contact.mPosition)), FromJolt(it_Contact.mSurfaceNormal), it_Contact.mDistance });
                }
            }
        }

        // What is left of the velocity once the walls, ceilings and slopes too steep to stand on that a character touches take the part going into them, as each moves, in a few passes for corners. As Jolt holds a character back from a steep slope, one facing up first stops it as a wall would, so walking into it does not lift the character, and what falls onto it slides off
        glm::vec3 HoldVelocity(std::span<const HeldContact> held, JPH::Vec3Arg up, const glm::vec3& velocity)
        {
            constexpr int c_Passes = 4;
            glm::vec3 l_Velocity = velocity;
            for (int it_Pass = 0; it_Pass < c_Passes; ++it_Pass)
            {
                bool l_Changed = false;
                for (const HeldContact& it_Contact : held)
                {
                    const auto a_Hold = [&l_Velocity, &l_Changed](JPH::Vec3Arg normal, JPH::Vec3Arg moving)
                    {
                        const glm::vec3 l_Normal = FromJolt(normal);
                        const float l_Into = glm::dot(l_Velocity - FromJolt(moving), l_Normal);
                        if (l_Into < 0.0f)
                        {
                            l_Velocity -= l_Into * l_Normal;
                            l_Changed = true;
                        }
                    };

                    const float l_Up = it_Contact.Normal.Dot(up);
                    if (l_Up > 1.0e-3f)
                    {
                        const JPH::Vec3 l_Wall = (it_Contact.Normal - l_Up * up).Normalized();
                        a_Hold(l_Wall, it_Contact.Velocity.Dot(l_Wall) * l_Wall);
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

        // Into a list kept from step to step, so finding the ground allocates nothing once it has room
        class TouchCollector final : public JPH::CollideShapeCollector
        {
        public:
            TouchCollector(PhysicsVector<CapsuleTouch>& touches, JPH::RVec3Arg base) : m_Touches(touches), m_Base(base)
            {

            }

            void AddHit(const JPH::CollideShapeResult& result) override
            {
                m_Touches.push_back({ result.mBodyID2, result.mSubShapeID2, m_Base + result.mContactPointOn2, -result.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()), result.mPenetrationDepth });
            }

        private:
            PhysicsVector<CapsuleTouch>& m_Touches;
            JPH::RVec3 m_Base;
        };

        // Where a physical character's capsule swept along a motion first meets something going against the motion: how far along it, the normal out towards the capsule, and the point
        struct CapsuleHit
        {
            float Fraction = 0.0f;
            JPH::Vec3 Normal = JPH::Vec3::sZero();
            JPH::RVec3 Point = JPH::RVec3::sZero();
        };

        // The nearest hit going against the motion, so what the capsule rests on or slides along does not stop it, and with a character given, the nearest ground it can stand on, passing walls and edges in the way
        class AgainstCollector final : public JPH::CastShapeCollector
        {
        public:
            AgainstCollector(JPH::Vec3Arg motion, JPH::RVec3Arg base, const PhysicalCharacter* standing) : m_Direction(motion.NormalizedOr(JPH::Vec3::sZero())), m_Base(base), m_Standing(standing)
            {

            }

            void AddHit(const JPH::ShapeCastResult& result) override
            {
                const JPH::Vec3 l_Normal = -result.mPenetrationAxis.NormalizedOr(-m_Direction);
                if (l_Normal.Dot(m_Direction) >= -1.0e-3f || (Hit && result.mFraction >= Hit->Fraction) || (m_Standing != nullptr && m_Standing->IsSlopeTooSteep(l_Normal)))
                {
                    return;
                }

                Hit = CapsuleHit{ result.mFraction, l_Normal, m_Base + result.mContactPointOn2 };
                UpdateEarlyOutFraction(result.mFraction);
            }

            std::optional<CapsuleHit> Hit;

        private:
            JPH::Vec3 m_Direction;
            JPH::RVec3 m_Base;
            const PhysicalCharacter* m_Standing = nullptr;
        };

        // Sized as the colliders are made
        JPH::RefConst<JPH::Shape> MakeQueryShape(const PhysicsQueryShape3D& shape)
        {
            switch (shape.Type)
            {
                case PhysicsQueryShapeType3D::Box:
                {
                    const glm::vec3 l_HalfExtents = glm::max(glm::abs(shape.HalfExtents), glm::vec3(c_MinimumSize));
                    const float l_ConvexRadius = std::min(JPH::cDefaultConvexRadius, std::min({ l_HalfExtents.x, l_HalfExtents.y, l_HalfExtents.z }) * 0.5f);

                    return new JPH::BoxShape(ToJolt(l_HalfExtents), l_ConvexRadius);
                }
                case PhysicsQueryShapeType3D::Capsule:
                {
                    const float l_Radius = std::max(shape.Radius, c_MinimumSize);
                    const float l_HalfSegment = std::max(shape.Height * 0.5f - shape.Radius, 0.0f);
                    if (l_HalfSegment < c_MinimumSize)
                    {
                        return new JPH::SphereShape(l_Radius);
                    }

                    return new JPH::CapsuleShape(l_HalfSegment, l_Radius);
                }
                case PhysicsQueryShapeType3D::Sphere:
                    break;
            }

            return new JPH::SphereShape(std::max(shape.Radius, c_MinimumSize));
        }

        // Nearest first, then by UUID, with only the nearest hit of each entity kept
        void SortHits(std::vector<PhysicsHit3D>& hits)
        {
            std::ranges::sort(hits, [](const PhysicsHit3D& left, const PhysicsHit3D& right) { return std::tie(left.Distance, left.Entity) < std::tie(right.Distance, right.Entity); });

            std::vector<PhysicsHit3D> l_Kept;
            l_Kept.reserve(hits.size());
            for (const PhysicsHit3D& it_Hit : hits)
            {
                if (std::ranges::find(l_Kept, it_Hit.Entity, &PhysicsHit3D::Entity) == l_Kept.end())
                {
                    l_Kept.push_back(it_Hit);
                }
            }

            hits = std::move(l_Kept);
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

        // A layer past the last there can be is taken as the first, as the scene's file reads it
        void AddCollider(BodyRecord& record, ColliderSource collider)
        {
            collider.Settings.Layer = collider.Settings.Layer < c_MaxCollisionLayers ? collider.Settings.Layer : 0;
            (collider.Settings.Trigger ? record.Triggers : record.Solids).push_back(std::move(collider));
        }

        void AddColliders(const SceneRegistry& registry, entt::entity entity, BodyRecord& record)
        {
            const UUID l_ID = registry.get<IDComponent>(entity).ID;
            if (const BoxColliderComponent* l_Box = registry.try_get<BoxColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::Box, *l_Box, l_Box->HalfExtents,{}, l_ID });
            }

            if (const SphereColliderComponent* l_Sphere = registry.try_get<SphereColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::Sphere, *l_Sphere, glm::vec3(l_Sphere->Radius, 0.0f, 0.0f),{}, l_ID });
            }

            if (const CapsuleColliderComponent* l_Capsule = registry.try_get<CapsuleColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::Capsule, *l_Capsule, glm::vec3(l_Capsule->Radius, l_Capsule->Height, 0.0f),{}, l_ID });
            }

            if (const CylinderColliderComponent* l_Cylinder = registry.try_get<CylinderColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::Cylinder, *l_Cylinder, glm::vec3(l_Cylinder->Radius, l_Cylinder->Height, 0.0f),{}, l_ID });
            }

            if (const ConvexHullColliderComponent* l_Hull = registry.try_get<ConvexHullColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::ConvexHull, *l_Hull, glm::vec3(0.0f), l_Hull->Shape, l_ID });
            }

            if (const MeshColliderComponent* l_Mesh = registry.try_get<MeshColliderComponent>(entity))
            {
                AddCollider(record, { entity, ColliderKind::Mesh, *l_Mesh, glm::vec3(0.0f), l_Mesh->Shape, l_ID });
            }
        }

        bool IsCooked(ColliderKind kind)
        {
            return kind == ColliderKind::ConvexHull || kind == ColliderKind::Mesh;
        }

        void GiveBoxMass(JPH::BodyCreationSettings& settings, const JPH::Shape& shape, float mass)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride.SetMassAndInertiaOfSolidBox(JPH::Vec3::sMax(shape.GetLocalBounds().GetSize(), JPH::Vec3::sReplicate(c_MinimumSize)), 1000.0f);
            settings.mMassPropertiesOverride.ScaleToMass(mass);
        }

        bool IsWeightless(const JPH::Shape& shape)
        {
            return shape.GetMassProperties().mMass < c_MinimumMass;
        }

        // The mass and inertia a body's shape is given: a box's around a shape with no volume to weigh, at the mass given or the fallback, and otherwise the shape's own, at the mass given when there is one
        JPH::MassProperties GetMassProperties(const JPH::Shape& shape, float mass)
        {
            JPH::MassProperties l_Properties;
            if (IsWeightless(shape))
            {
                l_Properties.SetMassAndInertiaOfSolidBox(JPH::Vec3::sMax(shape.GetLocalBounds().GetSize(), JPH::Vec3::sReplicate(c_MinimumSize)), 1000.0f);
                l_Properties.ScaleToMass(mass > 0.0f ? mass : c_FallbackMass);

                return l_Properties;
            }

            l_Properties = shape.GetMassProperties();
            if (mass > 0.0f)
            {
                l_Properties.ScaleToMass(mass);
            }

            return l_Properties;
        }

        // The entity whose body an entity's colliders join: the nearest at or above it with a RigidBody or a CharacterController, none when a RigidBody2D or a CharacterController2D without one comes first, since that is the 2D world's, and otherwise the highest at or above it with a collider, which is a static body. As the scene starts it finds them walking down, and this walks up to the same answer
        entt::entity FindOwner(const SceneRegistry& registry, entt::entity entity)
        {
            entt::entity l_Highest = entt::null;
            for (entt::entity it_Entity = entity; it_Entity != entt::null; it_Entity = registry.get<RelationshipComponent>(it_Entity).Parent)
            {
                if (registry.any_of<RigidBodyComponent, CharacterControllerComponent>(it_Entity))
                {
                    return it_Entity;
                }

                if (registry.any_of<RigidBody2DComponent, CharacterController2DComponent>(it_Entity))
                {
                    return entt::null;
                }

                if (HasCollider(registry, it_Entity))
                {
                    l_Highest = it_Entity;
                }
            }

            return l_Highest;
        }

        // What an owner's record takes from its entity: its CharacterController, or its RigidBody, or neither for a static body. A character is kinematic, as the bodies of the colliders that follow it are, and a RigidBody beside it is left out
        void ReadOwner(const SceneRegistry& registry, entt::entity owner, BodyRecord& record)
        {
            const CharacterControllerComponent* l_Character = registry.try_get<CharacterControllerComponent>(owner);
            const RigidBodyComponent* l_RigidBody = l_Character == nullptr ? registry.try_get<RigidBodyComponent>(owner) : nullptr;
            record.Entity = owner;
            record.ID = registry.get<IDComponent>(owner).ID;
            record.HasRigidBody = l_RigidBody != nullptr;
            record.Motion = l_Character != nullptr ? BodyMotion::Kinematic : (l_RigidBody != nullptr ? l_RigidBody->Motion : BodyMotion::Static);
            record.Settings = l_RigidBody != nullptr ? *l_RigidBody : RigidBodyComponent{};
            record.HasCharacter = l_Character != nullptr;
            record.Controller = l_Character != nullptr ? *l_Character : CharacterControllerComponent{};
        }

        // Jolt's first body of a joint is the other one, and its second the joint's own, so each point moves by its own body's change of centre of mass
        template<typename T>
        void MovePoints(JPH::TwoBodyConstraintSettings& settings, JPH::Vec3Arg first, JPH::Vec3Arg second)
        {
            T& l_Settings = static_cast<T&>(settings);
            l_Settings.mPoint1 += JPH::RVec3(first);
            l_Settings.mPoint2 += JPH::RVec3(second);
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
            {
                return "contact begin";
            }
            case PhysicsEventType::ContactStay:
            {
                return "contact stay";
            }
            case PhysicsEventType::ContactEnd:
            {
                return "contact end";
            }
            case PhysicsEventType::TriggerEnter:
            {
                return "trigger enter";
            }
            case PhysicsEventType::TriggerExit:
            {
                return "trigger exit";
            }
            case PhysicsEventType::JointBreak:
            {
                return "joint break";
            }
            case PhysicsEventType::CharacterHit:
            {
                return "character hit";
            }
        }

        return "unknown";
    }

    // Jolt calls the listener on its workers during a step, so what it hears is only queued, under a lock, for the main thread to sort and pair after the step
    struct PhysicsScene3D::State final : public JPH::ContactListener
    {
        State(Scene& scene, const CollisionLayers& layers) : Owner(scene), Layers(layers), World(GetWorldSettings(layers)), Parts(Layers)
        {

        }

        // The joints let go first, then the records the bodies point at, while the world still holds the bodies, which goes last. Jolt destroys a physical character's body with it, once it is out of the world
        ~State() override
        {
            Joints.clear();
            for (BodyRecord* it_Record : Records)
            {
                if (it_Record->Physical != nullptr)
                {
                    it_Record->Physical->RemoveFromPhysicsSystem();
                }

                Memory::Delete(it_Record);
            }
        }

        // Signal listeners, which only note what changed, since nothing may be made or taken out while the registry is in the middle of a change
        void OnBodyChanged([[maybe_unused]] SceneRegistry& registry, entt::entity entity)
        {
            Dirty.push_back(entity);
        }

        void OnJointChanged([[maybe_unused]] SceneRegistry& registry, entt::entity entity)
        {
            DirtyJoints.push_back(entity);
        }

        [[nodiscard]] static PhysicsWorldSettings3D GetWorldSettings(const CollisionLayers& layers)
        {
            PhysicsWorldSettings3D l_Settings;
            l_Settings.Layers = layers;

            return l_Settings;
        }

        // A body's own sensor never reports touching the body, and two bodies a joint keeps apart never touch. The joints' pairs change only on the main thread between steps
        [[nodiscard]] JPH::ValidateResult OnContactValidate(const JPH::Body& first, const JPH::Body& second, [[maybe_unused]] JPH::RVec3Arg baseOffset, [[maybe_unused]] const JPH::CollideShapeResult& result) override
        {
            if (GetRecord(first) != nullptr && GetRecord(first) == GetRecord(second))
            {
                return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
            }

            const std::uint32_t l_First = first.GetID().GetIndexAndSequenceNumber();
            const std::uint32_t l_Second = second.GetID().GetIndexAndSequenceNumber();
            if (!KeptApart.empty() && std::ranges::binary_search(KeptApart, GetPairKey(std::min(l_First, l_Second), std::max(l_First, l_Second))))
            {
                return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
            }

            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
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
            Gather(first, second, manifold);
        }

        void OnContactPersisted(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold, [[maybe_unused]] JPH::ContactSettings& settings) override
        {
            Gather(first, second, manifold);
        }

        // Each point halfway between the two surfaces
        void Gather(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold)
        {
            if (!GatherContacts || first.IsSensor() || second.IsSensor())
            {
                return;
            }

            const glm::vec3 l_Normal = FromJolt(manifold.mWorldSpaceNormal);
            const std::scoped_lock l_Lock(RawMutex);
            for (JPH::uint it_Point = 0; it_Point < manifold.mRelativeContactPointsOn1.size(); ++it_Point)
            {
                const glm::vec3 l_First = FromJolt(JPH::Vec3(manifold.GetWorldSpaceContactPointOn1(it_Point)));
                const glm::vec3 l_Second = FromJolt(JPH::Vec3(manifold.GetWorldSpaceContactPointOn2(it_Point)));
                Contacts.push_back({ (l_First + l_Second) * 0.5f, l_Normal });
            }
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

        [[nodiscard]] bool MakeBody(BodyRecord& record, const BodyVelocity* carried = nullptr, bool keepBodies = false);
        void MakeCharacter(BodyRecord& record);
        void MakePhysical(BodyRecord& record, const JPH::RefConst<JPH::Shape>& shape, std::uint32_t layers, const DecomposedMatrix& world, const CharacterCapsule& capsule);
        void DestroyCharacter(BodyRecord& record);
        void MoveCharacters(float seconds);
        void MovePhysical(BodyRecord& record, float seconds, const glm::vec3& gravity, const glm::vec3& up);
        [[nodiscard]] glm::vec3 Predict(JPH::BodyID body, float seconds, const glm::vec3& up, JPH::RVec3& position, JPH::Quat& rotation) const;
        [[nodiscard]] glm::vec3 GetGroundVelocity(const JPH::CharacterBase& character, JPH::RVec3Arg position) const;
        void CarryCharacters();
        void SettleCharacters(float seconds);
        [[nodiscard]] glm::vec3 CoverMove(BodyRecord& record, const glm::vec3& displacement, const glm::vec3& up);
        void NoteTouches(BodyRecord& record);
        void HandOutHits(std::uint32_t index);
        void FindGround(BodyRecord& record);
        [[nodiscard]] std::optional<CapsuleHit> CastCapsule(const BodyRecord& record, JPH::RVec3Arg position, JPH::QuatArg rotation, JPH::Vec3Arg motion, bool standing = false) const;
        [[nodiscard]] bool Fits(const BodyRecord& record, JPH::RVec3Arg position, JPH::QuatArg rotation);
        [[nodiscard]] bool StepUp(BodyRecord& record, const glm::vec3& velocity, float seconds, const glm::vec3& up);
        void StandOnStep(BodyRecord& record);
        [[nodiscard]] bool StickToFloor(BodyRecord& record, const glm::vec3& up, float distance);
        void PushBodies(const BodyRecord& record, const glm::vec3& velocity, float seconds);
        void TurnToTransform(BodyRecord& record);
        [[nodiscard]] bool MakeJoint(JointRecord& joint);
        [[nodiscard]] bool RestoreJoint(JointRecord& joint);
        void AddConstraint(JointRecord& joint, const JPH::TwoBodyConstraintSettings& settings, JPH::Body& own, JPH::Body& other);
        void RemoveConstraint(JointRecord& joint);
        void SaveJoints(const BodyRecord& record);
        void RestoreJoints(const BodyRecord& record);
        void CreateJointRecord(entt::entity entity);
        void DestroyBody(JPH::BodyID& body);
        void WakeAround(JPH::BodyID body);
        void EndPairs(JPH::BodyID body);
        void Gather(entt::entity owner, BodyRecord& record) const;
        void Index(BodyRecord& record);
        void Unindex(const BodyRecord& record);
        [[nodiscard]] BodyRecord* FindRecord(entt::entity owner) const;
        void AcquireShapes(const BodyRecord& record);
        void RemoveRecord(BodyRecord& record);
        void UpdateRecord(BodyRecord& record, BodyRecord& gathered);
        void Sync();
        void ResolveJoints(bool bodiesChanged, const PhysicsVector<entt::entity>& dirtyJoints);
        [[nodiscard]] bool Tune(JointRecord& joint);
        void FollowTransforms();
        void PlaceAtTransform(BodyRecord& record);
        [[nodiscard]] BodyRecord* FindBody(UUID entity);
        void PairContacts(std::uint32_t index);
        void BreakJoints(float seconds, std::uint32_t index);
        [[nodiscard]] PhysicsHit3D MakeHit(JPH::BodyID body, const JPH::SubShapeID& part) const;
        [[nodiscard]] std::vector<PhysicsHit3D> CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter, bool all) const;
        [[nodiscard]] std::vector<PhysicsHit3D> CastShape(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter, bool all) const;

        Scene& Owner;
        CollisionLayers Layers;
        PhysicsWorld3D World;
        LayerShapeFilter Parts;
        JPH::PhysicsSystem* System = nullptr;
        // The world's scratch memory, which characters move in between its steps
        JPH::TempAllocator* StepMemory = nullptr;
        CharacterListener Characters;
        // The bodies a character pushes in a step, the walls it is held back by, and what a physical one's capsule touches, kept so a step allocates nothing once they have room
        PhysicsVector<BodyPush> Pushes;
        PhysicsVector<HeldContact> Held;
        PhysicsVector<CapsuleTouch> Touches;
        // What a character touches or nearly does, gathered for each in turn, the bodies it keeps touching, and the step's hits, handed out after its contacts, kept so a step allocates nothing once they have room
        PhysicsVector<CharacterTouch> Nearby;
        PhysicsVector<UUID> Kept;
        PhysicsVector<PhysicsEvent3D> Hits;
        // In the order they were made, which as the scene starts is hierarchy order
        PhysicsVector<BodyRecord*> Records;
        PhysicsVector<JointRecord> Joints;
        // Each entity whose colliders a record holds, its owner's included, and the record
        std::unordered_map<entt::entity, BodyRecord*, std::hash<entt::entity>, std::equal_to<entt::entity>, TaggedAllocator<std::pair<const entt::entity, BodyRecord*>, MemoryTag::Physics>> Members;
        // Entities whose RigidBody, colliders or joint were added, changed through Patch or Replace, or removed since the last frame with steps
        PhysicsVector<entt::entity> Dirty;
        PhysicsVector<entt::entity> DirtyJoints;
        // Joints let go with a body that went, to be found again once the scene's changes are all taken in
        PhysicsVector<entt::entity> RetryJoints;
        // The body pairs of joints that keep them from colliding, sorted, once for each joint
        PhysicsVector<std::uint64_t> KeptApart;
        std::uint32_t WaitingJoints = 0;
        PhysicsVector<UUID> Shapes;
        std::mutex RawMutex;
        PhysicsVector<RawContact> Raw;
        // The last step's contact points, kept only while asked for
        PhysicsVector<PhysicsContactPoint> Contacts;
        bool GatherContacts = false;
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
    PhysicsScene3D::PhysicsScene3D(Scene& scene, const CollisionLayers& layers)
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's physics is made on the main thread.");

        m_State = Memory::New<State>(MemoryTag::Physics, scene, layers);
        State& l_State = *m_State;
        l_State.System = &l_State.World.m_State->System;
        l_State.StepMemory = &l_State.World.m_State->StepMemory;
        l_State.System->SetContactListener(&l_State);
        l_State.System->SetCombineFriction(&CombineFriction);
        l_State.System->SetCombineRestitution(&CombineRestitution);

        SceneRegistry& l_Registry = scene.GetRegistry();
        std::unordered_map<entt::entity, std::size_t> l_Owners;
        std::vector<entt::entity> l_Joined;
        constexpr std::size_t c_NoOwner = SIZE_MAX;
        // An entity with a RigidBody2D or a CharacterController2D and no RigidBody or CharacterController, and everything below it, belongs to the 2D world
        constexpr std::size_t c_Owned2D = SIZE_MAX - 1;
        for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            const entt::entity l_Handle = it_Entity.GetHandle();
            const entt::entity l_Parent = l_Registry.get<RelationshipComponent>(l_Handle).Parent;
            const std::size_t l_Inherited = l_Parent != entt::null ? l_Owners[l_Parent] : c_NoOwner;

            std::size_t l_Owner = l_Inherited;
            const bool l_Moves = l_Registry.any_of<RigidBodyComponent, CharacterControllerComponent>(l_Handle);
            if (!l_Moves && l_Registry.any_of<RigidBody2DComponent, CharacterController2DComponent>(l_Handle))
            {
                l_Owner = c_Owned2D;
            }
            else if (l_Moves || (l_Inherited == c_NoOwner && HasCollider(l_Registry, l_Handle)))
            {
                BodyRecord& l_Record = *l_State.Records.emplace_back(Memory::New<BodyRecord>(MemoryTag::Physics));
                ReadOwner(l_Registry, l_Handle, l_Record);
                l_Owner = l_State.Records.size() - 1;
            }

            l_Owners[l_Handle] = l_Owner;
            if (l_Owner == c_Owned2D && HasCollider(l_Registry, l_Handle))
            {
                TR_CORE_WARN("Physics: {}'s 3D colliders are part of a 2D body or character, so they are left out", GetName(scene, l_Handle));
            }
            else if (l_Owner != c_NoOwner && l_Owner != c_Owned2D)
            {
                AddColliders(l_Registry, l_Handle, *l_State.Records[l_Owner]);
            }

            if (l_Registry.all_of<JointComponent>(l_Handle))
            {
                l_Joined.push_back(l_Handle);
            }
        }

        for (BodyRecord* it_Record : l_State.Records)
        {
            if (it_Record->HasRigidBody && it_Record->Solids.empty() && it_Record->Triggers.empty())
            {
                TR_CORE_WARN("Physics: {} has a RigidBody and no colliders on it or below it, so it has no body", GetName(scene, it_Record->Entity));
            }

            l_State.Index(*it_Record);
            l_State.AcquireShapes(*it_Record);
        }

        for (BodyRecord* it_Record : l_State.Records)
        {
            static_cast<void>(l_State.MakeBody(*it_Record));
        }

        for (const entt::entity it_Entity : l_Joined)
        {
            l_State.CreateJointRecord(it_Entity);
        }

        for (JointRecord& it_Joint : l_State.Joints)
        {
            static_cast<void>(l_State.MakeJoint(it_Joint));
        }

        l_State.Waiting = static_cast<std::uint32_t>(std::ranges::count_if(l_State.Records, [](const BodyRecord* record) { return record->Waiting; }));
        l_State.WaitingJoints = static_cast<std::uint32_t>(std::ranges::count_if(l_State.Joints, [](const JointRecord& joint) { return joint.Waiting; }));

        // From here on, bodies, colliders and joints added, changed through Patch or Replace, or removed are taken in at the next frame with steps
        const auto a_Watch = [&l_Registry, &l_State]<Component T>()
        {
            l_Registry.on_construct<T>().template connect<&State::OnBodyChanged>(l_State);
            l_Registry.on_update<T>().template connect<&State::OnBodyChanged>(l_State);
            l_Registry.on_destroy<T>().template connect<&State::OnBodyChanged>(l_State);
        };

        a_Watch.template operator() < RigidBodyComponent > ();
        a_Watch.template operator() < RigidBody2DComponent > ();
        a_Watch.template operator() < CharacterController2DComponent > ();
        a_Watch.template operator() < BoxColliderComponent > ();
        a_Watch.template operator() < SphereColliderComponent > ();
        a_Watch.template operator() < CapsuleColliderComponent > ();
        a_Watch.template operator() < CylinderColliderComponent > ();
        a_Watch.template operator() < ConvexHullColliderComponent > ();
        a_Watch.template operator() < MeshColliderComponent > ();
        a_Watch.template operator() < CharacterControllerComponent > ();
        l_Registry.on_construct<JointComponent>().connect<&State::OnJointChanged>(l_State);
        l_Registry.on_update<JointComponent>().connect<&State::OnJointChanged>(l_State);
        l_Registry.on_destroy<JointComponent>().connect<&State::OnJointChanged>(l_State);

        TR_CORE_INFO("Physics: the scene starts with {} bod(ies){}{}", GetBodyCount(), !l_State.Joints.empty() ? std::format(" and {} joint(s)", GetJointCount()) : std::string(), l_State.Waiting > 0 ? std::format(", and {} waiting for their collision shapes to load", l_State.Waiting) : std::string());
    }

    // The world goes first, and the shapes its bodies held with it, and the drawn poses with them, so the scene draws from its transforms again
    PhysicsScene3D::~PhysicsScene3D()
    {
        Scene& l_Scene = m_State->Owner;
        SceneRegistry& l_Registry = l_Scene.GetRegistry();
        const auto a_Unwatch = [&l_Registry, this]<Component T>()
        {
            l_Registry.on_construct<T>().disconnect(m_State);
            l_Registry.on_update<T>().disconnect(m_State);
            l_Registry.on_destroy<T>().disconnect(m_State);
        };

        a_Unwatch.template operator() < RigidBodyComponent > ();
        a_Unwatch.template operator() < RigidBody2DComponent > ();
        a_Unwatch.template operator() < CharacterController2DComponent > ();
        a_Unwatch.template operator() < BoxColliderComponent > ();
        a_Unwatch.template operator() < SphereColliderComponent > ();
        a_Unwatch.template operator() < CapsuleColliderComponent > ();
        a_Unwatch.template operator() < CylinderColliderComponent > ();
        a_Unwatch.template operator() < ConvexHullColliderComponent > ();
        a_Unwatch.template operator() < MeshColliderComponent > ();
        a_Unwatch.template operator() < CharacterControllerComponent > ();
        a_Unwatch.template operator() < JointComponent > ();

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

        return !l_Registry.storage<RigidBodyComponent>().empty() || !l_Registry.storage<BoxColliderComponent>().empty() || !l_Registry.storage<SphereColliderComponent>().empty() || !l_Registry.storage<CapsuleColliderComponent>().empty() || !l_Registry.storage<CylinderColliderComponent>().empty() || !l_Registry.storage<ConvexHullColliderComponent>().empty() || !l_Registry.storage<MeshColliderComponent>().empty() || !l_Registry.storage<CharacterControllerComponent>().empty();
    }

    // What changed in the scene since the last frame with steps is taken in, so a frame without steps changes nothing and the next one with steps takes it all. Bodies still waiting for their shapes are tried again. A frame without steps leaves the kinematic targets where they were, so the next frame with steps moves from there
    void PhysicsScene3D::BeginFrame(std::uint32_t steps)
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        l_State.Events.clear();
        if (steps > 0)
        {
            l_State.Sync();
            l_State.FollowTransforms();
        }

        if (l_State.Waiting > 0)
        {
            for (BodyRecord* it_Record : l_State.Records)
            {
                if (it_Record->Waiting && l_State.MakeBody(*it_Record))
                {
                    l_State.RestoreJoints(*it_Record);
                }
            }

            l_State.Waiting = static_cast<std::uint32_t>(std::ranges::count_if(l_State.Records, [](const BodyRecord* record) { return record->Waiting; }));
        }

        if (l_State.WaitingJoints > 0)
        {
            for (JointRecord& it_Joint : l_State.Joints)
            {
                if (it_Joint.Waiting)
                {
                    static_cast<void>(it_Joint.Saved != nullptr ? l_State.RestoreJoint(it_Joint) : l_State.MakeJoint(it_Joint));
                }
            }

            l_State.WaitingJoints = static_cast<std::uint32_t>(std::ranges::count_if(l_State.Joints, [](const JointRecord& joint) { return joint.Waiting; }));
        }

        if (steps == 0)
        {
            return;
        }

        l_State.Steps = steps;
        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (BodyRecord* it_Record : l_State.Records)
        {
            const bool l_Follows = (it_Record->Motion == BodyMotion::Kinematic && !it_Record->HasCharacter) || (it_Record->Motion == BodyMotion::Dynamic && it_Record->Body.IsInvalid() && !it_Record->Sensor.IsInvalid());
            if (it_Record->Waiting || !l_Follows || !l_Registry.valid(it_Record->Entity))
            {
                continue;
            }

            const DecomposedMatrix l_World = Decompose(l_State.Owner.ComputeWorldMatrix(Entity(it_Record->Entity, &l_State.Owner)));
            it_Record->TargetFrom = it_Record->TargetTo;
            it_Record->TargetTo = l_World.Pose;
        }
    }

    // What the scene's scripts changed since the frame began is taken in, kinematic bodies and sensors are moved for the step, then characters, the world steps, what carried a kinematic character takes it along and a physical one finds where the step left it, events are paired, and dynamic bodies and characters that moved are written back in hierarchy order, so each parent is placed before its children are made local to it
    void PhysicsScene3D::Step(Timestep step, std::uint32_t index)
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        l_State.Sync();
        JPH::BodyInterface& l_Bodies = l_State.System->GetBodyInterface();
        const float l_Seconds = step.GetSeconds();
        const float l_Factor = static_cast<float>(std::min(index + 1, l_State.Steps)) / static_cast<float>(l_State.Steps);
        for (BodyRecord* it_Record : l_State.Records)
        {
            if (it_Record->Waiting || it_Record->Motion == BodyMotion::Static || it_Record->HasCharacter)
            {
                continue;
            }

            const BodyPose l_Target = Interpolate(it_Record->TargetFrom, it_Record->TargetTo, l_Factor);
            if (it_Record->Motion == BodyMotion::Kinematic && !it_Record->Body.IsInvalid())
            {
                l_Bodies.MoveKinematic(it_Record->Body, JPH::RVec3(ToJolt(l_Target.Position)), ToJolt(l_Target.Rotation), l_Seconds);
            }

            if (!it_Record->Sensor.IsInvalid())
            {
                if (it_Record->Motion == BodyMotion::Dynamic && !it_Record->Body.IsInvalid())
                {
                    JPH::RVec3 l_Position;
                    JPH::Quat l_Rotation;
                    l_Bodies.GetPositionAndRotation(it_Record->Body, l_Position, l_Rotation);
                    l_Bodies.SetPositionAndRotation(it_Record->Sensor, l_Position, l_Rotation, JPH::EActivation::DontActivate);
                }
                else
                {
                    l_Bodies.MoveKinematic(it_Record->Sensor, JPH::RVec3(ToJolt(l_Target.Position)), ToJolt(l_Target.Rotation), l_Seconds);
                }
            }
        }

        l_State.Hits.clear();
        l_State.MoveCharacters(l_Seconds);
        l_State.Contacts.clear();
        l_State.World.Step(step);
        l_State.CarryCharacters();
        l_State.SettleCharacters(l_Seconds);
        ++l_State.StepCount;
        l_State.PairContacts(index);
        l_State.HandOutHits(index);
        l_State.BreakJoints(l_Seconds, index);

        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (BodyRecord* it_Record : l_State.Records)
        {
            const bool l_Character = it_Record->Character != nullptr || it_Record->Physical != nullptr;
            if (it_Record->Waiting || it_Record->Motion == BodyMotion::Static || (it_Record->Body.IsInvalid() && !l_Character))
            {
                continue;
            }

            it_Record->Previous = it_Record->Current;
            JPH::RVec3 l_Position;
            JPH::Quat l_Rotation;
            if (it_Record->Character != nullptr)
            {
                l_Position = it_Record->Character->GetPosition();
                l_Rotation = it_Record->Character->GetRotation();
            }
            else
            {
                l_Bodies.GetPositionAndRotation(it_Record->Body, l_Position, l_Rotation);
            }

            it_Record->Current = { FromJolt(JPH::Vec3(l_Position)), FromJolt(l_Rotation) };
            if ((it_Record->Motion != BodyMotion::Dynamic && !l_Character) || !l_Registry.valid(it_Record->Entity) || (it_Record->Previous.Position == it_Record->Current.Position && it_Record->Previous.Rotation == it_Record->Current.Rotation))
            {
                continue;
            }

            TransformComponent& l_Transform = l_Registry.get<TransformComponent>(it_Record->Entity);
            const entt::entity l_Parent = l_Registry.get<RelationshipComponent>(it_Record->Entity).Parent;
            if (l_Parent == entt::null)
            {
                l_Transform.Position = it_Record->Current.Position;
                l_Transform.Rotation = it_Record->Current.Rotation;
            }
            else
            {
                const glm::mat4 l_ParentWorld = l_State.Owner.ComputeWorldMatrix(Entity(l_Parent, &l_State.Owner));
                l_Transform.Position = glm::vec3(glm::inverse(l_ParentWorld) * glm::vec4(it_Record->Current.Position, 1.0f));
                l_Transform.Rotation = glm::normalize(glm::inverse(Decompose(l_ParentWorld).Pose.Rotation) * it_Record->Current.Rotation);
            }

            it_Record->WrittenPosition = l_Transform.Position;
            it_Record->WrittenRotation = l_Transform.Rotation;
        }
    }

    // Moving bodies and characters are drawn between their last two steps. A kinematic body's last step reached its transform, so it is drawn a little behind it, beside the bodies and characters it carries
    void PhysicsScene3D::EndFrame(float interpolation)
    {
        State& l_State = *m_State;
        SceneRegistry& l_Registry = l_State.Owner.GetRegistry();
        for (const BodyRecord* it_Record : l_State.Records)
        {
            if (it_Record->Waiting || it_Record->Motion == BodyMotion::Static || (it_Record->Body.IsInvalid() && it_Record->Character == nullptr) || !l_Registry.valid(it_Record->Entity))
            {
                continue;
            }

            const BodyPose l_Pose = Interpolate(it_Record->Previous, it_Record->Current, interpolation);
            l_Registry.emplace_or_replace<PhysicsPoseComponent>(it_Record->Entity, l_Pose.Position, l_Pose.Rotation);
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

    std::uint32_t PhysicsScene3D::GetJointCount() const
    {
        return static_cast<std::uint32_t>(std::ranges::count_if(m_State->Joints, [](const JointRecord& joint) { return joint.Constraint != nullptr; }));
    }

    const CollisionLayers& PhysicsScene3D::GetLayers() const
    {
        return m_State->Layers;
    }

    PhysicsBodyCounts PhysicsScene3D::GetBodyCounts() const
    {
        return m_State->World.GetBodyCounts();
    }

    // Between steps, so no worker is gathering
    void PhysicsScene3D::SetGatherContacts(bool gather)
    {
        m_State->GatherContacts = gather;
        if (!gather)
        {
            m_State->Contacts.clear();
        }
    }

    std::span<const PhysicsContactPoint> PhysicsScene3D::GetContactPoints() const
    {
        return m_State->Contacts;
    }

    std::optional<PhysicsHit3D> PhysicsScene3D::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter) const
    {
        const std::vector<PhysicsHit3D> l_Hits = m_State->CastRay(origin, direction, maxDistance, filter, false);

        return l_Hits.empty() ? std::nullopt : std::optional<PhysicsHit3D>(l_Hits.front());
    }

    std::vector<PhysicsHit3D> PhysicsScene3D::RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter) const
    {
        return m_State->CastRay(origin, direction, maxDistance, filter, true);
    }

    std::optional<PhysicsHit3D> PhysicsScene3D::ShapeCast(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter) const
    {
        const std::vector<PhysicsHit3D> l_Hits = m_State->CastShape(shape, position, rotation, direction, maxDistance, filter, false);

        return l_Hits.empty() ? std::nullopt : std::optional<PhysicsHit3D>(l_Hits.front());
    }

    std::vector<PhysicsHit3D> PhysicsScene3D::ShapeCastAll(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter) const
    {
        return m_State->CastShape(shape, position, rotation, direction, maxDistance, filter, true);
    }

    std::vector<PhysicsHit3D> PhysicsScene3D::Overlap(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const PhysicsQueryFilter3D& filter) const
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's physics is queried on the main thread.");

        const State& l_State = *m_State;
        const JPH::RefConst<JPH::Shape> l_Shape = MakeQueryShape(shape);
        const QueryLayerFilter l_Layers(filter.Layers);
        const QueryBodyFilter l_Bodies(filter);
        const QueryShapeFilter l_Shapes(l_State.System->GetBodyLockInterfaceNoLock(), filter);
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> l_Collector;
        l_State.System->GetNarrowPhaseQueryNoLock().CollideShape(l_Shape, JPH::Vec3::sOne(), JPH::RMat44::sRotationTranslation(ToJolt(rotation), JPH::RVec3(ToJolt(position))), JPH::CollideShapeSettings(), JPH::RVec3::sZero(), l_Collector, {}, l_Layers, l_Bodies, l_Shapes);

        std::vector<PhysicsHit3D> l_Hits;
        l_Hits.reserve(l_Collector.mHits.size());
        for (const JPH::CollideShapeResult& it_Result : l_Collector.mHits)
        {
            l_Hits.push_back(l_State.MakeHit(it_Result.mBodyID2, it_Result.mSubShapeID2));
        }

        std::erase_if(l_Hits, [](const PhysicsHit3D& hit) { return !hit.Entity; });
        SortHits(l_Hits);

        return l_Hits;
    }

    // A static body is still, and reads so. A character's velocity is read with GetCharacterVelocity
    PhysicsBodyResult PhysicsScene3D::GetVelocity(UUID entity, glm::vec3& linear, glm::vec3& angular)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's bodies are reached on the main thread.");

        const BodyRecord* l_Record = m_State->FindBody(entity);
        if (l_Record != nullptr && l_Record->HasCharacter)
        {
            return PhysicsBodyResult::WrongMotion;
        }

        if (l_Record == nullptr || l_Record->Body.IsInvalid())
        {
            return PhysicsBodyResult::NoBody;
        }

        const JPH::BodyInterface& l_Bodies = m_State->System->GetBodyInterface();
        linear = l_Record->Motion != BodyMotion::Static ? FromJolt(l_Bodies.GetLinearVelocity(l_Record->Body)) : glm::vec3(0.0f);
        angular = l_Record->Motion != BodyMotion::Static ? FromJolt(l_Bodies.GetAngularVelocity(l_Record->Body)) : glm::vec3(0.0f);

        return PhysicsBodyResult::Done;
    }

    namespace
    {
        // A dynamic body's record, or why there is none, for each call that pushes or sets a velocity. A character is no dynamic body
        template<typename F>
        PhysicsBodyResult OnDynamic(BodyRecord* record, F call)
        {
            if (record != nullptr && record->HasCharacter)
            {
                return PhysicsBodyResult::WrongMotion;
            }

            if (record == nullptr || record->Body.IsInvalid())
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

    PhysicsBodyResult PhysicsScene3D::SetLinearVelocity(UUID entity, const glm::vec3& velocity)
    {
        JPH::BodyInterface& l_Bodies = m_State->System->GetBodyInterface();

        return OnDynamic(m_State->FindBody(entity), [&](JPH::BodyID body) { l_Bodies.SetLinearVelocity(body, ToJolt(velocity)); l_Bodies.ActivateBody(body); });
    }

    PhysicsBodyResult PhysicsScene3D::SetAngularVelocity(UUID entity, const glm::vec3& velocity)
    {
        JPH::BodyInterface& l_Bodies = m_State->System->GetBodyInterface();

        return OnDynamic(m_State->FindBody(entity), [&](JPH::BodyID body) { l_Bodies.SetAngularVelocity(body, ToJolt(velocity)); l_Bodies.ActivateBody(body); });
    }

    PhysicsBodyResult PhysicsScene3D::AddForce(UUID entity, const glm::vec3& force)
    {
        JPH::BodyInterface& l_Bodies = m_State->System->GetBodyInterface();

        return OnDynamic(m_State->FindBody(entity), [&](JPH::BodyID body) { l_Bodies.AddForce(body, ToJolt(force)); });
    }

    PhysicsBodyResult PhysicsScene3D::AddForceAtPoint(UUID entity, const glm::vec3& force, const glm::vec3& point)
    {
        JPH::BodyInterface& l_Bodies = m_State->System->GetBodyInterface();

        return OnDynamic(m_State->FindBody(entity), [&](JPH::BodyID body) { l_Bodies.AddForce(body, ToJolt(force), JPH::RVec3(ToJolt(point))); });
    }

    PhysicsBodyResult PhysicsScene3D::AddTorque(UUID entity, const glm::vec3& torque)
    {
        JPH::BodyInterface& l_Bodies = m_State->System->GetBodyInterface();

        return OnDynamic(m_State->FindBody(entity), [&](JPH::BodyID body) { l_Bodies.AddTorque(body, ToJolt(torque)); });
    }

    PhysicsBodyResult PhysicsScene3D::AddImpulse(UUID entity, const glm::vec3& impulse)
    {
        JPH::BodyInterface& l_Bodies = m_State->System->GetBodyInterface();

        return OnDynamic(m_State->FindBody(entity), [&](JPH::BodyID body) { l_Bodies.AddImpulse(body, ToJolt(impulse)); });
    }

    PhysicsBodyResult PhysicsScene3D::AddImpulseAtPoint(UUID entity, const glm::vec3& impulse, const glm::vec3& point)
    {
        JPH::BodyInterface& l_Bodies = m_State->System->GetBodyInterface();

        return OnDynamic(m_State->FindBody(entity), [&](JPH::BodyID body) { l_Bodies.AddImpulse(body, ToJolt(impulse), JPH::RVec3(ToJolt(point))); });
    }

    PhysicsBodyResult PhysicsScene3D::AddAngularImpulse(UUID entity, const glm::vec3& impulse)
    {
        JPH::BodyInterface& l_Bodies = m_State->System->GetBodyInterface();

        return OnDynamic(m_State->FindBody(entity), [&](JPH::BodyID body) { l_Bodies.AddAngularImpulse(body, ToJolt(impulse)); });
    }

    // The entity's transform is set first, so the body or character is placed where the transform puts it, and the next frame does not move it again
    PhysicsBodyResult PhysicsScene3D::Teleport(UUID entity, const glm::vec3& position, const glm::quat& rotation)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's bodies are reached on the main thread.");

        State& l_State = *m_State;
        BodyRecord* l_Record = l_State.FindBody(entity);
        if (l_Record == nullptr || (l_Record->Body.IsInvalid() && l_Record->Sensor.IsInvalid() && l_Record->Character == nullptr))
        {
            return PhysicsBodyResult::NoBody;
        }

        const Entity l_Owner(l_Record->Entity, &l_State.Owner);
        TransformComponent l_World = l_State.Owner.ComputeWorldTransform(l_Owner);
        l_World.Position = position;
        l_World.Rotation = glm::normalize(rotation);
        l_State.Owner.SetWorldTransform(l_Owner, l_World);
        l_State.PlaceAtTransform(*l_Record);

        return PhysicsBodyResult::Done;
    }

    PhysicsBodyResult PhysicsScene3D::MoveKinematic(UUID entity, const glm::vec3& position, const glm::quat& rotation)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's bodies are reached on the main thread.");

        State& l_State = *m_State;
        const BodyRecord* l_Record = l_State.FindBody(entity);
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
        l_World.Position = position;
        l_World.Rotation = glm::normalize(rotation);
        l_State.Owner.SetWorldTransform(l_Owner, l_World);

        return PhysicsBodyResult::Done;
    }

    PhysicsBodyResult PhysicsScene3D::SetAwake(UUID entity, bool awake)
    {
        const BodyRecord* l_Record = m_State->FindBody(entity);
        if (l_Record != nullptr && l_Record->HasCharacter)
        {
            return PhysicsBodyResult::WrongMotion;
        }

        if (l_Record == nullptr || l_Record->Body.IsInvalid())
        {
            return PhysicsBodyResult::NoBody;
        }

        if (l_Record->Motion == BodyMotion::Static)
        {
            return PhysicsBodyResult::WrongMotion;
        }

        JPH::BodyInterface& l_Bodies = m_State->System->GetBodyInterface();
        awake ? l_Bodies.ActivateBody(l_Record->Body) : l_Bodies.DeactivateBody(l_Record->Body);

        return PhysicsBodyResult::Done;
    }

    PhysicsBodyResult PhysicsScene3D::IsAwake(UUID entity, bool& awake)
    {
        const BodyRecord* l_Record = m_State->FindBody(entity);
        if (l_Record != nullptr && l_Record->HasCharacter)
        {
            return PhysicsBodyResult::WrongMotion;
        }

        if (l_Record == nullptr || l_Record->Body.IsInvalid())
        {
            return PhysicsBodyResult::NoBody;
        }

        if (l_Record->Motion == BodyMotion::Static)
        {
            return PhysicsBodyResult::WrongMotion;
        }

        awake = m_State->System->GetBodyInterface().IsActive(l_Record->Body);

        return PhysicsBodyResult::Done;
    }

    namespace
    {
        // A character's record, or why there is none, for each call on a character
        template<typename F>
        PhysicsBodyResult OnCharacter(BodyRecord* record, F call)
        {
            if (record == nullptr || (record->Character == nullptr && record->Physical == nullptr))
            {
                return record == nullptr ? PhysicsBodyResult::NoBody : PhysicsBodyResult::WrongMotion;
            }

            call(*record);

            return PhysicsBodyResult::Done;
        }
    }

    PhysicsBodyResult PhysicsScene3D::SetCharacterVelocity(UUID entity, const glm::vec3& velocity)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's characters are reached on the main thread.");

        return OnCharacter(m_State->FindBody(entity), [&velocity](BodyRecord& record) { record.Velocity = velocity; });
    }

    PhysicsBodyResult PhysicsScene3D::GetCharacterVelocity(UUID entity, glm::vec3& velocity)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's characters are reached on the main thread.");

        return OnCharacter(m_State->FindBody(entity), [&velocity](const BodyRecord& record) { velocity = record.Velocity; });
    }

    // The ground Jolt found as the character last moved, or a physical one's last step left it, the body it is part of found from its user data, and how fast that body moves there
    PhysicsBodyResult PhysicsScene3D::GetCharacterGround(UUID entity, CharacterGround3D& ground)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's characters are reached on the main thread.");

        const State& l_State = *m_State;
        const JPH::BodyInterface& l_Bodies = l_State.System->GetBodyInterface();

        return OnCharacter(m_State->FindBody(entity), [&ground, &l_State, &l_Bodies](const BodyRecord& record)
        {
            const JPH::CharacterBase& l_Character = record.Character != nullptr ? static_cast<const JPH::CharacterBase&>(*record.Character) : *record.Physical;
            ground = {};
            ground.Grounded = l_Character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            if (!l_Character.IsSupported())
            {
                return;
            }

            ground.Normal = FromJolt(l_Character.GetGroundNormal());
            ground.Velocity = l_State.GetGroundVelocity(l_Character, record.Character != nullptr ? record.Character->GetPosition() : l_Bodies.GetPosition(record.Body));
            const JPH::BodyID l_Body = l_Character.GetGroundBodyID();
            const BodyRecord* l_Ground = !l_Body.IsInvalid() ? reinterpret_cast<const BodyRecord*>(l_Bodies.GetUserData(l_Body) & ~c_RecordBits) : nullptr;
            ground.Entity = l_Ground != nullptr ? l_Ground->ID : UUID();
        });
    }

    PhysicsBodyResult PhysicsScene3D::MoveCharacter(UUID entity, const glm::vec3& displacement)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's characters are reached on the main thread.");

        return OnCharacter(m_State->FindBody(entity), [&displacement](BodyRecord& record) { record.Move += displacement; });
    }

    std::optional<bool> PhysicsScene3D::IsJointBroken(UUID entity) const
    {
        const auto a_Found = std::ranges::find(m_State->Joints, entity, &JointRecord::ID);

        return a_Found != m_State->Joints.end() ? std::optional(a_Found->Broken) : std::nullopt;
    }

    // Once every cooked shape it uses has loaded or failed. Each collider is placed relative to the body's pose, its entity's world scale taken into its shape, and the solids made one shape and the triggers another. A physical character's solids and its capsule are made one shape, its body's. False while it waits. A body made again carries the velocities given in place of its RigidBody's starting ones. Keeping its bodies, it is changed where it is: each body still needed takes its new shape, layers, mass and RigidBody settings, so its velocity and contacts carry over, and one no longer needed goes
    bool PhysicsScene3D::State::MakeBody(BodyRecord& record, const BodyVelocity* carried, bool keepBodies)
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
        const bool l_Character = l_Registry.valid(l_Record.Entity) && l_Record.HasCharacter;
        const bool l_Physical = l_Character && l_Record.Controller.Mode == CharacterMode::Physical;
        if (l_Character && !l_Physical)
        {
            l_State.MakeCharacter(l_Record);
        }
        else if (!l_Physical)
        {
            l_State.DestroyCharacter(l_Record);
        }

        if (!l_Registry.valid(l_Record.Entity) || (l_Record.Solids.empty() && l_Record.Triggers.empty() && !l_Physical))
        {
            l_State.DestroyBody(l_Record.Body);
            l_State.DestroyBody(l_Record.Sensor);

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
                    // Triangles have no volume to weigh and Jolt cannot collide two meshes, so a dynamic body, a physical character's included, takes a hull. Nothing pushes a kinematic body, so its mesh only has to be met
                    const bool l_Hull = collider.Kind == ColliderKind::ConvexHull;
                    if (!l_Hull && (l_Motion == BodyMotion::Dynamic || l_Physical))
                    {
                        TR_CORE_WARN("Physics: {}'s MeshCollider is left out of its dynamic body, since triangle meshes are for static and kinematic bodies, and a dynamic one takes a convex hull", l_Name);

                        return nullptr;
                    }

                    if (!l_Hull && collider.Settings.Trigger)
                    {
                        TR_CORE_WARN("Physics: {}'s MeshCollider is a trigger, and triangles have no inside, so it reports only what crosses its surface. A ConvexHullCollider trigger reports all it holds", l_Name);
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

        // The parts placed on the body, then made one shape, with the layers of the parts made, and a physical character's capsule last, on its own layer. A single part needs no compound, and one with no offset nothing around it
        const auto a_Combine = [&](const PhysicsVector<ColliderSource>& colliders, bool& compound, std::uint32_t& single, std::uint32_t& layers, const ShapePart* capsule = nullptr) -> JPH::RefConst<JPH::Shape>
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
                    layers |= 1u << l_Collider.Settings.Layer;
                }
            }

            if (capsule != nullptr)
            {
                l_Parts.push_back(*capsule);
                layers |= 1u << GetCharacterLayer(l_Record);
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

        l_Record.Materials.clear();
        for (const ColliderSource& it_Solid : l_Record.Solids)
        {
            l_Record.Materials.push_back({ it_Solid.Settings.Friction, it_Solid.Settings.Restitution });
        }

        // Jolt asks the filter on the shapes about every pair of bodies once it has one, so it is given one only when a body has colliders on more than one layer
        const auto a_UseLayers = [&l_State](std::uint32_t layers)
        {
            if (!std::has_single_bit(layers))
            {
                l_State.System->SetSimShapeFilter(&l_State.Parts);
            }
        };

        bool l_Compound = false;
        std::uint32_t l_Single = 0;
        std::uint32_t l_Layers = 0;
        const bool l_HadBody = !l_Record.Body.IsInvalid();
        // A physical character's capsule comes after its solids, and slides along what it touches
        CharacterCapsule l_Capsule;
        ShapePart l_CapsulePart;
        if (l_Physical)
        {
            l_Capsule = MakeCapsule(l_Record.Controller, l_Body.Scale);
            l_CapsulePart = { ToJolt(l_Capsule.Offset), JPH::Quat::sIdentity(), l_Capsule.Shape, static_cast<std::uint32_t>(l_Record.Solids.size()) };
            l_Record.Materials.push_back({ 0.0f, 0.0f });
        }

        const JPH::RefConst<JPH::Shape> l_Solid = a_Combine(l_Record.Solids, l_Compound, l_Single, l_Layers, l_Physical ? &l_CapsulePart : nullptr);
        if (l_Physical)
        {
            l_Record.Compound = l_Compound;
            l_Record.SingleSolid = l_Single;
            a_UseLayers(l_Layers);
            l_State.MakePhysical(l_Record, l_Solid, l_Layers, l_Body, l_Capsule);
        }
        else if (l_Solid != nullptr && keepBodies && !l_Record.Body.IsInvalid())
        {
            l_Record.Compound = l_Compound;
            l_Record.SingleSolid = l_Single;
            a_UseLayers(l_Layers);

            const RigidBodyComponent& l_RigidBody = l_Record.Settings;
            const JPH::EActivation l_Activation = l_Motion == BodyMotion::Dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
            l_State.WakeAround(l_Record.Body);
            l_Bodies.SetShape(l_Record.Body, l_Solid, true, l_Activation);
            l_State.WakeAround(l_Record.Body);
            l_Bodies.SetObjectLayer(l_Record.Body, PhysicsLayers3D::Get(l_Motion, l_Layers));
            if (l_Motion == BodyMotion::Dynamic)
            {
                l_Bodies.SetMotionQuality(l_Record.Body, l_RigidBody.ContinuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete);
            }

            const JPH::BodyLockWrite l_Lock(l_State.System->GetBodyLockInterface(), l_Record.Body);
            if (l_Lock.Succeeded() && !l_Lock.GetBody().IsStatic())
            {
                JPH::Body& l_Changed = l_Lock.GetBody();
                JPH::MotionProperties& l_Properties = *l_Changed.GetMotionProperties();
                if (l_Motion == BodyMotion::Dynamic)
                {
                    l_Properties.SetLinearDamping(l_RigidBody.LinearDamping);
                    l_Properties.SetAngularDamping(l_RigidBody.AngularDamping);
                    l_Properties.SetGravityFactor(l_RigidBody.GravityFactor);
                    l_Changed.SetAllowSleeping(l_RigidBody.CanSleep);
                    if (IsWeightless(*l_Solid) || l_RigidBody.Mass > 0.0f)
                    {
                        l_Properties.SetMassProperties(l_Properties.GetAllowedDOFs(), GetMassProperties(*l_Solid, l_RigidBody.Mass));
                    }
                }
                else if (IsWeightless(*l_Solid))
                {
                    l_Properties.SetMassProperties(l_Properties.GetAllowedDOFs(), GetMassProperties(*l_Solid, c_FallbackMass));
                }
            }
        }
        else if (l_Solid == nullptr && keepBodies)
        {
            l_State.DestroyBody(l_Record.Body);
        }
        else if (const JPH::RefConst<JPH::Shape>& l_Shape = l_Solid)
        {
            l_Record.Compound = l_Compound;
            l_Record.SingleSolid = l_Single;
            a_UseLayers(l_Layers);

            JPH::BodyCreationSettings l_Settings(l_Shape, l_Position, l_Rotation, ToJolt(l_Motion), PhysicsLayers3D::Get(l_Motion, l_Layers));
            l_Settings.mUserData = l_UserData;
            if (l_Motion == BodyMotion::Dynamic)
            {
                const RigidBodyComponent& l_RigidBody = l_Record.Settings;
                l_Settings.mLinearDamping = l_RigidBody.LinearDamping;
                l_Settings.mAngularDamping = l_RigidBody.AngularDamping;
                l_Settings.mGravityFactor = l_RigidBody.GravityFactor;
                l_Settings.mMotionQuality = l_RigidBody.ContinuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
                l_Settings.mAllowedDOFs = l_Allowed;
                l_Settings.mLinearVelocity = carried != nullptr ? carried->Linear : ToJolt(l_RigidBody.InitialLinearVelocity);
                l_Settings.mAngularVelocity = carried != nullptr ? carried->Angular : ToJolt(l_RigidBody.InitialAngularVelocity);
                l_Settings.mAllowSleeping = l_RigidBody.CanSleep;

                // A shape with no volume to weigh, such as a flat hull, gets the inertia of a box around it
                if (IsWeightless(*l_Shape))
                {
                    TR_CORE_WARN("Physics: {}'s colliders have no volume to weigh, so it takes the inertia of a box around them and {} kg", GetName(l_Scene, l_Record.Entity), l_RigidBody.Mass > 0.0f ? l_RigidBody.Mass : c_FallbackMass);
                    GiveBoxMass(l_Settings, *l_Shape, l_RigidBody.Mass > 0.0f ? l_RigidBody.Mass : c_FallbackMass);
                }
                else if (l_RigidBody.Mass > 0.0f)
                {
                    l_Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                    l_Settings.mMassPropertiesOverride.mMass = l_RigidBody.Mass;
                }
            }
            else if (l_Motion == BodyMotion::Kinematic && IsWeightless(*l_Shape))
            {
                GiveBoxMass(l_Settings, *l_Shape, c_FallbackMass);
            }

            const JPH::EActivation l_Activation = l_Motion == BodyMotion::Dynamic && (carried != nullptr || !l_Record.Settings.StartAsleep) ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
            l_Record.Body = l_Bodies.CreateAndAddBody(l_Settings, l_Activation);
            if (l_Record.Body.IsInvalid())
            {
                TR_CORE_ERROR("Physics: the world is full, so {} has no body", GetName(l_Scene, l_Record.Entity));
            }
        }

        // Triggers on a static body stay with it, and the rest follow it as a kinematic sensor
        bool l_SensorCompound = false;
        std::uint32_t l_SensorSingle = 0;
        std::uint32_t l_SensorLayers = 0;
        const JPH::RefConst<JPH::Shape> l_Trigger = a_Combine(l_Record.Triggers, l_SensorCompound, l_SensorSingle, l_SensorLayers);
        const BodyMotion l_SensorMotion = l_Motion == BodyMotion::Static ? BodyMotion::Static : BodyMotion::Kinematic;
        if (l_Trigger != nullptr && keepBodies && !l_Record.Sensor.IsInvalid())
        {
            l_Record.SensorCompound = l_SensorCompound;
            l_Record.SensorSingle = l_SensorSingle;
            a_UseLayers(l_SensorLayers);
            l_Bodies.SetShape(l_Record.Sensor, l_Trigger, false, JPH::EActivation::DontActivate);
            l_Bodies.SetObjectLayer(l_Record.Sensor, PhysicsLayers3D::Get(l_SensorMotion, l_SensorLayers));
        }
        else if (l_Trigger == nullptr && keepBodies)
        {
            l_State.DestroyBody(l_Record.Sensor);
        }
        else if (const JPH::RefConst<JPH::Shape>& l_Shape = l_Trigger)
        {
            l_Record.SensorCompound = l_SensorCompound;
            l_Record.SensorSingle = l_SensorSingle;
            a_UseLayers(l_SensorLayers);

            JPH::BodyCreationSettings l_Settings(l_Shape, l_Position, l_Rotation, ToJolt(l_SensorMotion), PhysicsLayers3D::Get(l_SensorMotion, l_SensorLayers));
            l_Settings.mUserData = l_UserData | c_SensorBit;
            l_Settings.mIsSensor = true;
            if (l_SensorMotion == BodyMotion::Kinematic && IsWeightless(*l_Shape))
            {
                GiveBoxMass(l_Settings, *l_Shape, c_FallbackMass);
            }
            l_Record.Sensor = l_Bodies.CreateAndAddBody(l_Settings, JPH::EActivation::DontActivate);
            if (l_Record.Sensor.IsInvalid())
            {
                TR_CORE_ERROR("Physics: the world is full, so {}'s triggers have no body", GetName(l_Scene, l_Record.Entity));
            }
        }

        // A body changed where it is keeps where it was drawn and where it was heading, and a character keeps its own
        if (!l_Record.HasCharacter && (!keepBodies || (!l_HadBody && !l_Record.Body.IsInvalid())))
        {
            const TransformComponent& l_Transform = l_Registry.get<TransformComponent>(l_Record.Entity);
            l_Record.Previous = l_Body.Pose;
            l_Record.Current = l_Body.Pose;
            l_Record.TargetFrom = l_Body.Pose;
            l_Record.TargetTo = l_Body.Pose;
            l_Record.WrittenPosition = l_Transform.Position;
            l_Record.WrittenRotation = l_Transform.Rotation;
        }

        return true;
    }

    // Made where its entity is, or changed where it stands, from its controller and its entity's world scale. Its inner body is the same capsule, kinematic and on its layer, which raycasts, triggers, dynamic bodies and other characters meet, and carries the record with a bit of its own. Its mass is its weight on what it stands on. It is told what it stands on at once
    void PhysicsScene3D::State::MakeCharacter(BodyRecord& record)
    {
        Scene& l_Scene = Owner;
        const CharacterControllerComponent& l_Controller = record.Controller;
        const DecomposedMatrix l_World = Decompose(l_Scene.ComputeWorldMatrix(Entity(record.Entity, &l_Scene)));
        const CharacterCapsule l_Capsule = MakeCapsule(l_Controller, l_World.Scale);
        const JPH::RefConst<JPH::Shape>& l_Shape = l_Capsule.Shape;
        const glm::vec3& l_Offset = l_Capsule.Offset;
        const float l_MaxSlope = std::clamp(l_Controller.MaxSlope, c_MinimumSlope, c_MaximumSlope);
        // Contacts below the middle of its lower end cap hold it up, and the rest only stop it
        const JPH::Plane l_Supporting(JPH::Vec3::sAxisY(), l_Capsule.HalfSegment - l_Offset.y);
        const JPH::ObjectLayer l_Layer = CharacterFilters::GetLayer(record);
        record.StepHeight = std::max(l_Controller.StepHeight, 0.0f) * std::abs(l_World.Scale.y);
        record.Radius = l_Capsule.Radius;

        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        if (record.Character != nullptr)
        {
            JPH::CharacterVirtual& l_Character = *record.Character;
            const JPH::BodyID l_Inner = l_Character.GetInnerBodyID();
            static_cast<void>(l_Character.SetShape(l_Shape, FLT_MAX, {}, {}, {}, {}, *StepMemory));
            WakeAround(l_Inner);
            l_Character.SetInnerBodyShape(l_Shape);
            WakeAround(l_Inner);
            l_Bodies.SetObjectLayer(l_Inner, l_Layer);
            l_Character.SetShapeOffset(ToJolt(l_Offset));
            l_Character.SetMaxSlopeAngle(l_MaxSlope);
            l_Character.SetMaxStrength(std::max(l_Controller.PushStrength, 0.0f));
            l_Character.SetMass(GetCharacterMass(l_Controller));
            l_Character.SetSupportingVolume(l_Supporting);
        }
        else
        {
            if (Owner.GetRegistry().all_of<RigidBodyComponent>(record.Entity))
            {
                TR_CORE_WARN("Physics: {} has a CharacterController and a RigidBody, so it is a character and the RigidBody is left out", GetName(l_Scene, record.Entity));
            }

            JPH::Ref<JPH::CharacterVirtualSettings> l_Settings = new JPH::CharacterVirtualSettings();
            l_Settings->mShape = l_Shape;
            l_Settings->mInnerBodyShape = l_Shape;
            l_Settings->mInnerBodyLayer = l_Layer;
            l_Settings->mShapeOffset = ToJolt(l_Offset);
            l_Settings->mMaxSlopeAngle = l_MaxSlope;
            l_Settings->mMaxStrength = std::max(l_Controller.PushStrength, 0.0f);
            l_Settings->mMass = GetCharacterMass(l_Controller);
            l_Settings->mSupportingVolume = l_Supporting;
            record.Character = new JPH::CharacterVirtual(l_Settings, JPH::RVec3(ToJolt(l_World.Pose.Position)), ToJolt(l_World.Pose.Rotation), reinterpret_cast<std::uint64_t>(&record) | c_CharacterBit, System);
            record.Character->SetListener(&Characters);
            if (record.Character->GetInnerBodyID().IsInvalid())
            {
                TR_CORE_ERROR("Physics: the world is full, so {}'s character has no inner body, and raycasts, triggers and bodies do not meet it", GetName(l_Scene, record.Entity));
            }

            const TransformComponent& l_Transform = Owner.GetRegistry().get<TransformComponent>(record.Entity);
            record.Previous = l_World.Pose;
            record.Current = l_World.Pose;
            record.TargetFrom = l_World.Pose;
            record.TargetTo = l_World.Pose;
            record.WrittenPosition = l_Transform.Position;
            record.WrittenRotation = l_Transform.Rotation;
        }

        const glm::vec3 l_Gravity = FromJolt(System->GetGravity());
        record.Character->SetUp(glm::length(l_Gravity) > 0.0f ? ToJolt(-glm::normalize(l_Gravity)) : JPH::Vec3::sAxisY());
        const CharacterFilters l_Filters(*System, Layers, record);
        record.Character->RefreshContacts(l_Filters.BroadPhase, l_Filters.Objects, l_Filters.Bodies, l_Filters.Shapes, *StepMemory);
    }

    // Made where its entity is, or changed where it stands: Jolt's Character on a dynamic body that cannot turn, of the shape given, its solids and its capsule, on the layers given and of its mass. Its velocity is not damped, as its own lasts until changed, it never sleeps, and it is swept along its motion so a fall cannot take it through what it lands on. It is told what it stands on at once. With no shape, as when its colliders could not be made one, it has none
    void PhysicsScene3D::State::MakePhysical(BodyRecord& record, const JPH::RefConst<JPH::Shape>& shape, std::uint32_t layers, const DecomposedMatrix& world, const CharacterCapsule& capsule)
    {
        if (shape == nullptr)
        {
            DestroyCharacter(record);

            return;
        }

        const CharacterControllerComponent& l_Controller = record.Controller;
        const float l_MaxSlope = std::clamp(l_Controller.MaxSlope, c_MinimumSlope, c_MaximumSlope);
        const JPH::Plane l_Supporting(JPH::Vec3::sAxisY(), capsule.HalfSegment - capsule.Offset.y);
        const JPH::ObjectLayer l_Layer = PhysicsLayers3D::Get(BodyMotion::Dynamic, layers);
        const float l_Mass = GetCharacterMass(l_Controller);
        const glm::vec3 l_Gravity = FromJolt(System->GetGravity());
        const JPH::Vec3 l_Up = glm::length(l_Gravity) > 0.0f ? ToJolt(-glm::normalize(l_Gravity)) : JPH::Vec3::sAxisY();
        record.StepHeight = std::max(l_Controller.StepHeight, 0.0f) * std::abs(world.Scale.y);
        record.Radius = capsule.Radius;
        record.HalfSegment = capsule.HalfSegment;
        record.CapsuleOffset = capsule.Offset;
        record.Capsule = capsule.Shape;

        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        if (record.Physical != nullptr)
        {
            PhysicalCharacter& l_Character = *record.Physical;
            WakeAround(record.Body);
            static_cast<void>(l_Character.SetShape(shape, FLT_MAX));
            WakeAround(record.Body);
            l_Character.SetLayer(l_Layer);
            l_Character.SetMaxSlopeAngle(l_MaxSlope);
            l_Character.SetSupportingVolume(l_Supporting);
            const JPH::BodyLockWrite l_Lock(System->GetBodyLockInterface(), record.Body);
            if (l_Lock.Succeeded())
            {
                l_Lock.GetBody().GetMotionProperties()->SetInverseMass(1.0f / l_Mass);
            }
        }
        else
        {
            const std::string l_Name = GetName(Owner, record.Entity);
            if (Owner.GetRegistry().all_of<RigidBodyComponent>(record.Entity))
            {
                TR_CORE_WARN("Physics: {} has a CharacterController and a RigidBody, so it is a character and the RigidBody is left out", l_Name);
            }

            // Jolt's Character cannot be made without its body
            if (System->GetNumBodies() >= System->GetMaxBodies())
            {
                TR_CORE_ERROR("Physics: the world is full, so {}'s character has no body", l_Name);

                return;
            }

            JPH::Ref<JPH::CharacterSettings> l_Settings = new JPH::CharacterSettings();
            l_Settings->mShape = shape;
            l_Settings->mLayer = l_Layer;
            l_Settings->mMass = l_Mass;
            l_Settings->mFriction = 0.0f;
            l_Settings->mUp = l_Up;
            l_Settings->mMaxSlopeAngle = l_MaxSlope;
            l_Settings->mSupportingVolume = l_Supporting;
            record.Physical = new PhysicalCharacter(l_Settings, JPH::RVec3(ToJolt(world.Pose.Position)), ToJolt(world.Pose.Rotation), reinterpret_cast<std::uint64_t>(&record), System);
            record.Body = record.Physical->GetBodyID();
            record.Physical->AddToPhysicsSystem(JPH::EActivation::Activate);
            l_Bodies.SetMotionQuality(record.Body, JPH::EMotionQuality::LinearCast);
            {
                const JPH::BodyLockWrite l_Lock(System->GetBodyLockInterface(), record.Body);
                if (l_Lock.Succeeded())
                {
                    JPH::Body& l_Body = l_Lock.GetBody();
                    l_Body.SetAllowSleeping(false);
                    l_Body.GetMotionProperties()->SetLinearDamping(0.0f);
                    l_Body.GetMotionProperties()->SetAngularDamping(0.0f);
                }
            }

            const TransformComponent& l_Transform = Owner.GetRegistry().get<TransformComponent>(record.Entity);
            record.Previous = world.Pose;
            record.Current = world.Pose;
            record.TargetFrom = world.Pose;
            record.TargetTo = world.Pose;
            record.WrittenPosition = l_Transform.Position;
            record.WrittenRotation = l_Transform.Rotation;
            record.Stepped = glm::vec3(0.0f);
        }

        record.Physical->SetUp(l_Up);
        FindGround(record);
    }

    // Its inner body's pairs end, and what rested on it wakes, as for a body taken out, before Jolt takes the inner body out with the character. A physical one's body goes the same way, taken out of the world before Jolt destroys it with the character
    void PhysicsScene3D::State::DestroyCharacter(BodyRecord& record)
    {
        if (record.Physical != nullptr)
        {
            EndPairs(record.Body);
            WakeAround(record.Body);
            record.Physical->RemoveFromPhysicsSystem();
            record.Physical = nullptr;
            record.Body = JPH::BodyID();
            record.Held.clear();
            record.Supports.clear();
            record.Stood = false;
            record.Walking = false;
            record.Climb = 0.0f;
            record.WalkedOff = false;
        }

        if (record.Character == nullptr)
        {
            return;
        }

        const JPH::BodyID l_Inner = record.Character->GetInnerBodyID();
        if (!l_Inner.IsInvalid())
        {
            EndPairs(l_Inner);
            WakeAround(l_Inner);
        }

        record.Character = nullptr;
    }

    // Each character, in the records' order, moved for the step by its own velocity, and the move scripts asked for, before the world steps. One whose transform was set from code is taken there first. Standing on ground it can stand on, it loses the vertical part of its velocity, and off the ground gravity adds to it. Jolt moves it, climbing, keeping to the floor and pushing, against what it stands on, as the listener has it see the bodies, and the walls, ceilings and slopes too steep to stand on it met take from its velocity the part that went into them. A body it stands on then carries it over the step, which CarryCharacters does once the world has stepped, so its colliders' bodies are sent where that takes it. Leaving the ground it keeps what carried it, and landing it gives back what it lands on moves at, so its velocity is its own, against what it stands on
    void PhysicsScene3D::State::MoveCharacters(float seconds)
    {
        SceneRegistry& l_Registry = Owner.GetRegistry();
        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        const glm::vec3 l_Gravity = FromJolt(System->GetGravity());
        const float l_Strength = glm::length(l_Gravity);
        const glm::vec3 l_Up = l_Strength > 0.0f ? -l_Gravity / l_Strength : glm::vec3(0.0f, 1.0f, 0.0f);
        for (BodyRecord* it_Record : Records)
        {
            if (it_Record->Waiting || (it_Record->Character == nullptr && it_Record->Physical == nullptr) || !l_Registry.valid(it_Record->Entity))
            {
                continue;
            }

            BodyRecord& l_Record = *it_Record;
            if (l_Record.Physical != nullptr)
            {
                MovePhysical(l_Record, seconds, l_Gravity, l_Up);

                continue;
            }

            JPH::CharacterVirtual& l_Character = *l_Record.Character;
            const TransformComponent& l_Transform = l_Registry.get<TransformComponent>(l_Record.Entity);
            if (l_Transform.Position != l_Record.WrittenPosition)
            {
                PlaceAtTransform(l_Record);
            }
            else if (l_Transform.Rotation != l_Record.WrittenRotation)
            {
                TurnToTransform(l_Record);
            }

            l_Character.SetUp(ToJolt(l_Up));
            const bool l_Stood = l_Character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            l_Record.Carrier = {};
            if (l_Stood)
            {
                const JPH::BodyLockRead l_Lock(System->GetBodyLockInterface(), l_Character.GetGroundBodyID());
                if (l_Lock.SucceededAndIsInBroadPhase() && !l_Lock.GetBody().IsStatic())
                {
                    const JPH::Body& l_Ground = l_Lock.GetBody();
                    l_Record.Carrier = { l_Character.GetGroundBodyID(), l_Ground.GetPosition(), l_Ground.GetRotation(), l_Ground.GetCenterOfMassPosition(), l_Ground.GetLinearVelocity(), l_Ground.GetAngularVelocity() };
                }
            }

            glm::vec3 l_Velocity = l_Record.Velocity;
            const float l_Rising = glm::dot(l_Velocity, l_Up);
            if (l_Stood && l_Rising < c_RisingSpeed)
            {
                l_Velocity -= l_Rising * l_Up;
            }
            else
            {
                l_Velocity += l_Gravity * seconds;
            }

            // A move scripts asked for is covered over the step besides its velocity, which it leaves as it was
            const glm::vec3 l_Moving = l_Velocity + std::exchange(l_Record.Move, glm::vec3(0.0f)) / seconds;
            l_Character.SetLinearVelocity(ToJolt(l_Moving));
            JPH::CharacterVirtual::ExtendedUpdateSettings l_Settings;
            l_Settings.mStickToFloorStepDown = ToJolt(l_Up * -l_Record.StepHeight);
            l_Settings.mWalkStairsStepUp = ToJolt(l_Up * l_Record.StepHeight);
            l_Settings.mWalkStairsStepUp = ToJolt(l_Up * l_Record.StepHeight);
            // Having stepped up onto an edge, Jolt looks this far on for a floor it can stand on, which a round end cap reaches only past the edge by about its radius
            l_Settings.mWalkStairsStepForwardTest = std::max(l_Settings.mWalkStairsStepForwardTest, l_Record.Radius);
            const CharacterFilters l_Filters(*System, Layers, l_Record);
            l_Character.ExtendedUpdate(seconds, ToJolt(l_Gravity), l_Settings, l_Filters.BroadPhase, l_Filters.Objects, l_Filters.Bodies, l_Filters.Shapes, *StepMemory);
            PushBodies(l_Record, l_Moving, seconds);
            GatherHeld(l_Character, Held);
            l_Velocity = HoldVelocity(Held, l_Character.GetUp(), l_Velocity);
            GatherNearby(l_Character, Nearby);
            NoteTouches(l_Record);

            // Walked off onto other ground, so nothing carries it this step
            const bool l_Stands = l_Character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            if (l_Stands && l_Character.GetGroundBodyID() != l_Record.Carrier.Body)
            {
                l_Record.Carrier = {};
            }

            JPH::RVec3 l_Position = l_Character.GetPosition();
            JPH::Quat l_Rotation = l_Character.GetRotation();
            const glm::vec3 l_Carried = Predict(l_Record.Carrier.Body, seconds, l_Up, l_Position, l_Rotation);
            if (l_Stands)
            {
                l_Velocity -= l_Stood ? glm::vec3(0.0f) : GetGroundVelocity(l_Character, l_Character.GetPosition());
                const float l_Left = glm::dot(l_Velocity, l_Up);
                l_Velocity -= l_Left < c_RisingSpeed ? l_Left * l_Up : glm::vec3(0.0f);
            }
            else if (l_Stood)
            {
                l_Velocity += l_Carried;
            }

            l_Record.Velocity = l_Velocity;
            for (const JPH::BodyID it_Body : { l_Record.Body, l_Record.Sensor })
            {
                if (!it_Body.IsInvalid())
                {
                    l_Bodies.MoveKinematic(it_Body, l_Position, l_Rotation, seconds);
                }
            }
        }
    }

    // Where a body's motion over the next step takes a character's pose, as Jolt moves the body: its centre of mass along its velocity, and turned about it, the character turning with it only about up. The velocity that takes the position there, none for no body
    glm::vec3 PhysicsScene3D::State::Predict(JPH::BodyID body, float seconds, const glm::vec3& up, JPH::RVec3& position, JPH::Quat& rotation) const
    {
        if (body.IsInvalid())
        {
            return glm::vec3(0.0f);
        }

        const JPH::BodyLockRead l_Lock(System->GetBodyLockInterface(), body);
        if (!l_Lock.SucceededAndIsInBroadPhase())
        {
            return glm::vec3(0.0f);
        }

        const JPH::Body& l_Body = l_Lock.GetBody();
        const JPH::Vec3 l_Angular = l_Body.GetAngularVelocity() * seconds;
        const float l_Angle = l_Angular.Length();
        const JPH::Vec3 l_Arm(position - l_Body.GetCenterOfMassPosition());
        const JPH::Vec3 l_Moved = l_Body.GetLinearVelocity() * seconds + (l_Angle > 1.0e-6f ? JPH::Quat::sRotation(l_Angular / l_Angle, l_Angle) * l_Arm - l_Arm : JPH::Vec3::sZero());
        position += l_Moved;
        rotation = (JPH::Quat::sRotation(ToJolt(up), l_Angle > 1.0e-6f ? l_Angular.Dot(ToJolt(up)) : 0.0f) * rotation).Normalized();

        return FromJolt(l_Moved) / seconds;
    }

    // How fast the ground under a character moves where the character is, from the body's own velocities, which a kinematic character saw as still if it carries it
    glm::vec3 PhysicsScene3D::State::GetGroundVelocity(const JPH::CharacterBase& character, JPH::RVec3Arg position) const
    {
        const JPH::BodyLockRead l_Lock(System->GetBodyLockInterface(), character.GetGroundBodyID());
        if (!l_Lock.SucceededAndIsInBroadPhase() || l_Lock.GetBody().IsStatic())
        {
            return glm::vec3(0.0f);
        }

        const JPH::Body& l_Body = l_Lock.GetBody();

        return FromJolt(l_Body.GetPointVelocity(position));
    }

    // Each character a body carried over the step taken there, as the body's pose before the step and after it move its position, turning it about up as the body turned. A kinematic body moves as Predict reckoned, and a dynamic one as the step left it
    void PhysicsScene3D::State::CarryCharacters()
    {
        for (BodyRecord* it_Record : Records)
        {
            if (it_Record->Character == nullptr || it_Record->Carrier.Body.IsInvalid())
            {
                continue;
            }

            BodyRecord& l_Record = *it_Record;
            JPH::CharacterVirtual& l_Character = *l_Record.Character;
            const CarryingBody l_Before = std::exchange(l_Record.Carrier, CarryingBody{});
            JPH::RMat44 l_After = JPH::RMat44::sIdentity();
            JPH::Quat l_Turned = JPH::Quat::sIdentity();
            {
                const JPH::BodyLockRead l_Lock(System->GetBodyLockInterface(), l_Before.Body);
                if (!l_Lock.SucceededAndIsInBroadPhase())
                {
                    continue;
                }

                l_After = l_Lock.GetBody().GetWorldTransform();
                l_Turned = l_Lock.GetBody().GetRotation() * l_Before.Rotation.Conjugated();
            }

            const JPH::RVec3 l_Local = JPH::RMat44::sRotationTranslation(l_Before.Rotation, l_Before.Position).InversedRotationTranslation() * l_Character.GetPosition();
            // The body's turn about up alone, its twist
            const JPH::Vec3 l_Up = l_Character.GetUp();
            const float l_Twist = 2.0f * std::atan2(l_Turned.GetXYZ().Dot(l_Up), l_Turned.GetW());
            l_Character.SetPosition(l_After * l_Local);
            l_Character.SetRotation((JPH::Quat::sRotation(l_Up, l_Twist) * l_Character.GetRotation()).Normalized());
        }
    }

    // A physical character before the world steps, as a kinematic one's move begins: one whose transform was set from code is taken there first, and what was done to its body since the last step, such as a kinematic character's push or another's weight, adds to its velocity. A move scripts asked for is covered first, sliding along what it meets, and its velocity is left as it was. Standing on ground it can stand on and not rising off it, it walks: it loses the vertical part of its velocity, steps up onto what it walks into that its step height clears, and its body moves along the ground at its pace across it, kept on the ground's surface, neither above it nor in it. Gravity does not pull it then, and its weight presses on a dynamic body it stands on, as a kinematic one's does. Otherwise gravity pulls it as it does any body. The walls, ceilings and slopes too steep to stand on it touches take from its velocity the part going into them, and the body it stood on adds how it moves the character over the step. The world then steps its body, so what hits it, what it pushes and what it lands on change its body's velocity, which SettleCharacters reads back. Its sensor is placed where its body is, as a dynamic body's is
    void PhysicsScene3D::State::MovePhysical(BodyRecord& record, float seconds, const glm::vec3& gravity, const glm::vec3& up)
    {
        const TransformComponent& l_Transform = Owner.GetRegistry().get<TransformComponent>(record.Entity);
        if (l_Transform.Position != record.WrittenPosition)
        {
            PlaceAtTransform(record);
        }
        else if (l_Transform.Rotation != record.WrittenRotation)
        {
            TurnToTransform(record);
        }

        PhysicalCharacter& l_Character = *record.Physical;
        l_Character.SetUp(ToJolt(up));
        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        JPH::RVec3 l_Position;
        JPH::Quat l_Rotation;
        l_Bodies.GetPositionAndRotation(record.Body, l_Position, l_Rotation);

        // What was done to its body since the world last stepped is its own, as what the step does
        record.Velocity += FromJolt(l_Bodies.GetLinearVelocity(record.Body)) - record.Stepped;
        record.Stood = l_Character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
        record.Carrier = {};
        record.Turn = l_Rotation;
        const JPH::BodyID l_Ground = l_Character.GetGroundBodyID();
        if (record.Stood && !l_Ground.IsInvalid() && l_Bodies.IsAdded(l_Ground) && l_Bodies.GetMotionType(l_Ground) != JPH::EMotionType::Static)
        {
            record.Carrier.Body = l_Ground;
        }

        JPH::RVec3 l_Carried = l_Position;
        record.Carried = Predict(record.Carrier.Body, seconds, up, l_Carried, record.Turn);

        // A move scripts asked for counts with its velocity as it decides whether it walks and climbs, and is covered before the world steps, which leaves its velocity as it was
        glm::vec3 l_Velocity = record.Velocity;
        const glm::vec3 l_Asked = std::exchange(record.Move, glm::vec3(0.0f));
        const float l_Rising = glm::dot(l_Velocity + l_Asked / seconds, up);
        record.Walking = record.Stood && l_Rising < c_RisingSpeed;
        if (record.Walking)
        {
            l_Velocity -= glm::dot(l_Velocity, up) * up;
        }

        // Before the walls it touches hold it back, since a step it climbs is one of them
        if (record.Walking && record.StepHeight > 0.0f && StepUp(record, l_Velocity + l_Asked / seconds, seconds, up))
        {
            l_Bodies.GetPositionAndRotation(record.Body, l_Position, l_Rotation);
        }

        record.Moved = glm::vec3(0.0f);
        if (l_Asked != glm::vec3(0.0f))
        {
            record.Moved = CoverMove(record, l_Asked, up) / seconds;
            l_Bodies.GetPositionAndRotation(record.Body, l_Position, l_Rotation);
        }

        l_Velocity = HoldVelocity(record.Held, ToJolt(up), l_Velocity + record.Carried) - record.Carried;

        glm::vec3 l_Given = l_Velocity;
        if (record.Walking)
        {
            const glm::vec3 l_Normal = FromJolt(l_Character.GetGroundNormal());
            const float l_Mass = GetCharacterMass(record.Controller);
            l_Given -= up * (glm::dot(l_Velocity, l_Normal) / std::max(glm::dot(l_Normal, up), 1.0e-3f));
            l_Given += l_Normal * (record.GroundDepth / seconds);
            l_Bodies.AddForce(record.Body, ToJolt(-gravity * l_Mass));
            if (!l_Ground.IsInvalid() && l_Bodies.IsAdded(l_Ground) && l_Bodies.GetMotionType(l_Ground) == JPH::EMotionType::Dynamic)
            {
                l_Bodies.AddImpulse(l_Ground, ToJolt(gravity * (l_Mass * seconds)), l_Character.GetGroundPosition());
            }
        }

        l_Given += record.Carried;
        l_Bodies.SetLinearVelocity(record.Body, ToJolt(l_Given));
        record.Velocity = l_Velocity;
        record.Given = l_Given;
        if (!record.Sensor.IsInvalid())
        {
            l_Bodies.SetPositionAndRotation(record.Sensor, l_Position, l_Rotation, JPH::EActivation::DontActivate);
        }
    }

    // Each physical character once the world has stepped, as a kinematic one's move ends: what the step did to its body's velocity, beyond what it was given, is added to its own, so a hit knocks it back and a heavy body it walks into slows it, but not what the ground it walked on did to push it out, which only kept it there. It turns as what it stood on turned. Having walked off ground it stood on, it is set down onto ground below as soon as it can be, until it has fallen its step height. Standing, it loses the vertical part of its velocity, and landing it gives back what it lands on moves at. Leaving the ground it keeps what carried it
    void PhysicsScene3D::State::SettleCharacters(float seconds)
    {
        const SceneRegistry& l_Registry = Owner.GetRegistry();
        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        for (BodyRecord* it_Record : Records)
        {
            if (it_Record->Physical == nullptr || !l_Registry.valid(it_Record->Entity))
            {
                continue;
            }

            BodyRecord& l_Record = *it_Record;
            PhysicalCharacter& l_Character = *l_Record.Physical;
            const glm::vec3 l_Up = FromJolt(l_Character.GetUp());
            glm::vec3 l_Change = FromJolt(l_Bodies.GetLinearVelocity(l_Record.Body)) - l_Record.Given;
            for (const JPH::Vec3& it_Normal : l_Record.Walking ? std::span<const JPH::Vec3>(l_Record.Supports) : std::span<const JPH::Vec3>())
            {
                const glm::vec3 l_Normal = FromJolt(it_Normal);
                l_Change -= std::max(glm::dot(l_Change, l_Normal), 0.0f) * l_Normal;
            }

            glm::vec3 l_Velocity = l_Record.Velocity + l_Change;
            if (!l_Record.Carrier.Body.IsInvalid())
            {
                l_Bodies.SetRotation(l_Record.Body, l_Record.Turn, JPH::EActivation::DontActivate);
            }

            FindGround(l_Record);
            if (l_Record.Climb > 0.0f)
            {
                // Walked on along the step, or turned back from it
                const float l_On = glm::dot(l_Velocity + l_Record.Moved, l_Record.ClimbWay) * seconds;
                l_Record.Climb = l_On > 0.0f ? l_Record.Climb - l_On : 0.0f;
                if (l_Record.Climb > 0.0f)
                {
                    StandOnStep(l_Record);
                }
            }

            bool l_Stands = l_Character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            const float l_Height = glm::dot(FromJolt(JPH::Vec3(l_Bodies.GetPosition(l_Record.Body))), l_Up);
            if (l_Record.Stood && !l_Stands && !l_Record.WalkedOff)
            {
                l_Record.WalkedOff = true;
                l_Record.OffHeight = l_Height;
            }

            const float l_Below = l_Record.StepHeight - (l_Record.OffHeight - l_Height);
            l_Record.WalkedOff = l_Record.WalkedOff && !l_Stands && l_Below > 0.0f && glm::dot(l_Velocity + l_Record.Moved, l_Up) < c_RisingSpeed;
            if (l_Record.WalkedOff)
            {
                l_Stands = StickToFloor(l_Record, l_Up, l_Below);
                l_Record.WalkedOff = !l_Stands;
            }

            if (l_Stands)
            {
                l_Velocity -= l_Record.Stood ? glm::vec3(0.0f) : GetGroundVelocity(l_Character, l_Bodies.GetPosition(l_Record.Body));
                const float l_Left = glm::dot(l_Velocity, l_Up);
                l_Velocity -= l_Left < c_RisingSpeed ? l_Left * l_Up : glm::vec3(0.0f);
            }
            else if (l_Record.Stood)
            {
                l_Velocity += l_Record.Carried;
            }

            l_Record.Velocity = l_Velocity;
            l_Record.Stepped = FromJolt(l_Bodies.GetLinearVelocity(l_Record.Body));

            // What its capsule touches as the step left it, sensors left out
            Nearby.clear();
            for (const CapsuleTouch& it_Touch : Touches)
            {
                const std::uint64_t l_UserData = l_Bodies.GetUserData(it_Touch.Body);
                const BodyRecord* l_Other = reinterpret_cast<const BodyRecord*>(l_UserData & ~c_RecordBits);
                if (l_Other != nullptr && (l_UserData & c_SensorBit) == 0)
                {
                    Nearby.push_back({ l_Other->ID, FromJolt(JPH::Vec3(it_Touch.Point)), FromJolt(it_Touch.Normal), -it_Touch.Depth });
                }
            }

            NoteTouches(l_Record);
        }
    }

    // A physical character moved before the world steps by a displacement scripts asked for, as a kinematic one's move slides: its capsule swept along it to what it meets going against it, stopping a little short, then along that for what is left, a few times for corners. Walking, the move goes along the ground it stands on, as its velocity does, and a wall or a slope too steep to stand on is met as an upright wall, so sliding along it lifts the character nowhere. Its body is placed where that takes it, and what it covered is given back
    glm::vec3 PhysicsScene3D::State::CoverMove(BodyRecord& record, const glm::vec3& displacement, const glm::vec3& up)
    {
        constexpr int c_Passes = 4;
        const PhysicalCharacter& l_Character = *record.Physical;
        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        JPH::RVec3 l_Position;
        JPH::Quat l_Rotation;
        l_Bodies.GetPositionAndRotation(record.Body, l_Position, l_Rotation);
        const JPH::RVec3 l_Start = l_Position;
        glm::vec3 l_Left = displacement;
        if (record.Walking)
        {
            const glm::vec3 l_Ground = FromJolt(l_Character.GetGroundNormal());
            l_Left -= up * (glm::dot(l_Left, l_Ground) / std::max(glm::dot(l_Ground, up), 1.0e-3f));
        }

        for (int it_Pass = 0; it_Pass < c_Passes; ++it_Pass)
        {
            const float l_Length = glm::length(l_Left);
            if (!(l_Length > 0.0f))
            {
                break;
            }

            const std::optional<CapsuleHit> l_Hit = CastCapsule(record, l_Position, l_Rotation, ToJolt(l_Left));
            const float l_Reached = l_Hit ? std::max(l_Hit->Fraction - c_StepSkin / l_Length, 0.0f) : 1.0f;
            l_Position += JPH::RVec3(ToJolt(l_Left * l_Reached));
            if (!l_Hit)
            {
                break;
            }

            l_Left *= 1.0f - l_Reached;
            glm::vec3 l_Normal = FromJolt(l_Hit->Normal);
            const glm::vec3 l_Across = l_Normal - glm::dot(l_Normal, up) * up;
            if (record.Walking && l_Character.IsSlopeTooSteep(l_Hit->Normal) && glm::length(l_Across) > 1.0e-3f)
            {
                l_Normal = glm::normalize(l_Across);
            }

            l_Left -= std::min(glm::dot(l_Left, l_Normal), 0.0f) * l_Normal;
        }

        l_Bodies.SetPosition(record.Body, l_Position, JPH::EActivation::Activate);

        return FromJolt(JPH::Vec3(l_Position - l_Start));
    }

    // A character's hits from Nearby, what it touches or nearly does as its step left it: each body it touches that it was not touching, at the nearest place on it, and the bodies it touched that it still nearly touches stay touched, so a gap that opens and closes as it walks or rests hits once
    void PhysicsScene3D::State::NoteTouches(BodyRecord& record)
    {
        std::ranges::sort(Nearby, [](const CharacterTouch& left, const CharacterTouch& right) { return std::tie(left.Entity, left.Distance) < std::tie(right.Entity, right.Distance); });
        Kept.clear();
        for (std::size_t it_Touch = 0; it_Touch < Nearby.size(); ++it_Touch)
        {
            const CharacterTouch& l_Nearest = Nearby[it_Touch];
            if (it_Touch > 0 && Nearby[it_Touch - 1].Entity == l_Nearest.Entity)
            {
                continue;
            }

            const bool l_Was = std::ranges::binary_search(record.Touching, l_Nearest.Entity);
            const bool l_Touches = l_Nearest.Distance <= c_TouchDistance;
            if (l_Was || l_Touches)
            {
                Kept.push_back(l_Nearest.Entity);
            }

            if (!l_Was && l_Touches)
            {
                Hits.push_back({ PhysicsEventType::CharacterHit, record.ID, l_Nearest.Entity, l_Nearest.Point, l_Nearest.Normal, 0 });
            }
        }

        record.Touching.assign(Kept.begin(), Kept.end());
    }

    // The step's hits after its contacts, by the character's UUID and then the body's
    void PhysicsScene3D::State::HandOutHits(std::uint32_t index)
    {
        std::ranges::sort(Hits, [](const PhysicsEvent3D& left, const PhysicsEvent3D& right) { return std::tie(left.First, left.Second) < std::tie(right.First, right.Second); });
        for (PhysicsEvent3D& it_Hit : Hits)
        {
            it_Hit.Step = index;
        }

        Events.insert(Events.end(), Hits.begin(), Hits.end());
    }

    // What a physical character stands on, as Jolt's Character finds it after a step, but with its capsule alone and the scene's filters, from what the capsule touches or nearly does. What is below the middle of its lower end cap holds it up, on ground it can stand on or on a slope too steep, and above it the character is not held up. The ground it can stand on that holds it up is kept, to keep its velocity from being lifted off it, and so are the walls, ceilings and slopes too steep to stand on that it touches, but for dynamic bodies, to hold its velocity back from them. So is how deep it is in the ground
    void PhysicsScene3D::State::FindGround(BodyRecord& record)
    {
        PhysicalCharacter& l_Character = *record.Physical;
        const JPH::BodyInterface& l_Bodies = System->GetBodyInterfaceNoLock();
        JPH::RVec3 l_Position;
        JPH::Quat l_Rotation;
        l_Bodies.GetPositionAndRotation(record.Body, l_Position, l_Rotation);
        JPH::CollideShapeSettings l_Settings;
        l_Settings.mMaxSeparationDistance = c_GroundDistance;
        l_Settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideOnlyWithActive;
        l_Settings.mActiveEdgeMovementDirection = l_Bodies.GetLinearVelocity(record.Body);
        l_Settings.mBackFaceMode = JPH::EBackFaceMode::IgnoreBackFaces;
        const CharacterFilters l_Filters(*System, Layers, record);
        Touches.clear();
        TouchCollector l_Collector(Touches, l_Position);
        System->GetNarrowPhaseQueryNoLock().CollideShape(record.Capsule, JPH::Vec3::sOne(), JPH::RMat44::sRotationTranslation(l_Rotation, l_Position).PreTranslated(ToJolt(record.CapsuleOffset)), l_Settings, l_Position, l_Collector, l_Filters.BroadPhase, l_Filters.Objects, l_Filters.Bodies, l_Filters.Shapes);

        // How fast what a touch is on moves there, its user data, and whether it is dynamic
        const JPH::BodyLockInterface& l_Locks = System->GetBodyLockInterfaceNoLock();
        const auto a_Read = [&l_Locks](const CapsuleTouch& touch, JPH::Vec3& velocity, std::uint64_t& userData)
        {
            const JPH::BodyLockRead l_Lock(l_Locks, touch.Body);
            velocity = JPH::Vec3::sZero();
            userData = 0;
            if (!l_Lock.Succeeded())
            {
                return false;
            }

            const JPH::Body& l_Body = l_Lock.GetBody();
            velocity = l_Body.IsStatic() ? JPH::Vec3::sZero() : l_Body.GetPointVelocity(touch.Point);
            userData = l_Body.GetUserData();

            return l_Body.IsDynamic();
        };

        // Ground it can stand on before any other, the deepest of it, so walking from a floor onto a slope it touches takes the slope, and otherwise what faces most up
        const JPH::Vec3 l_Up = l_Character.GetUp();
        const JPH::RMat44 l_Inverse = JPH::RMat44::sInverseRotationTranslation(l_Rotation, l_Position);
        const CapsuleTouch* l_Ground = nullptr;
        bool l_Walkable = false;
        record.Held.clear();
        record.Supports.clear();
        for (const CapsuleTouch& it_Touch : Touches)
        {
            const bool l_Steep = l_Character.IsSlopeTooSteep(it_Touch.Normal);
            const bool l_Holds = !l_Steep && l_Character.GetSupportingVolume().SignedDistance(JPH::Vec3(l_Inverse * it_Touch.Point)) <= 0.0f;
            if (l_Holds)
            {
                record.Supports.push_back(it_Touch.Normal);
            }

            const bool l_Better = l_Ground == nullptr || (l_Holds && !l_Walkable) || (l_Holds == l_Walkable && (l_Holds ? it_Touch.Depth > l_Ground->Depth : it_Touch.Normal.Dot(l_Up) > l_Ground->Normal.Dot(l_Up)));
            if (l_Better)
            {
                l_Ground = &it_Touch;
                l_Walkable = l_Holds;
            }

            // A dynamic body is met by the solver instead, which pushes it
            JPH::Vec3 l_Moving;
            std::uint64_t l_UserData = 0;
            if (it_Touch.Depth > -c_TouchDistance && l_Steep && !a_Read(it_Touch, l_Moving, l_UserData))
            {
                record.Held.push_back({ it_Touch.Normal, l_Moving });
            }
        }

        record.GroundDepth = 0.0f;
        if (l_Ground == nullptr)
        {
            l_Character.SetGround(JPH::CharacterBase::EGroundState::InAir, JPH::BodyID(), JPH::SubShapeID(), JPH::RVec3::sZero(), JPH::Vec3::sZero(), JPH::Vec3::sZero(), 0);

            return;
        }

        JPH::CharacterBase::EGroundState l_State = JPH::CharacterBase::EGroundState::OnGround;
        if (!l_Walkable && l_Character.GetSupportingVolume().SignedDistance(JPH::Vec3(l_Inverse * l_Ground->Point)) > 0.0f)
        {
            l_State = JPH::CharacterBase::EGroundState::NotSupported;
        }
        else if (!l_Walkable)
        {
            l_State = JPH::CharacterBase::EGroundState::OnSteepGround;
        }

        record.GroundDepth = l_Ground->Depth;
        JPH::Vec3 l_Velocity;
        std::uint64_t l_UserData = 0;
        static_cast<void>(a_Read(*l_Ground, l_Velocity, l_UserData));
        l_Character.SetGround(l_State, l_Ground->Body, l_Ground->Part, l_Ground->Point, l_Ground->Normal, l_Velocity, l_UserData);
    }

    // A physical character's capsule swept from where it is placed along the motion, and the nearest of what it meets going against the motion, or of the ground it can stand on there, through the scene's filters, as a kinematic one looks about
    std::optional<CapsuleHit> PhysicsScene3D::State::CastCapsule(const BodyRecord& record, JPH::RVec3Arg position, JPH::QuatArg rotation, JPH::Vec3Arg motion, bool standing) const
    {
        const JPH::RShapeCast l_Cast(record.Capsule, JPH::Vec3::sOne(), JPH::RMat44::sRotationTranslation(rotation, position).PreTranslated(ToJolt(record.CapsuleOffset)), motion);
        JPH::ShapeCastSettings l_Settings;
        l_Settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideOnlyWithActive;
        l_Settings.mActiveEdgeMovementDirection = motion;
        const CharacterFilters l_Filters(*System, Layers, record);
        AgainstCollector l_Collector(motion, position, standing ? record.Physical.GetPtr() : nullptr);
        System->GetNarrowPhaseQueryNoLock().CastShape(l_Cast, l_Settings, position, l_Collector, l_Filters.BroadPhase, l_Filters.Objects, l_Filters.Bodies, l_Filters.Shapes);

        return l_Collector.Hit;
    }

    // A physical character walking into what its step height clears, such as a stair, lifted onto it before the world steps, as Jolt lifts a kinematic one: its capsule swept along its motion over the step meets a wall or a slope too steep to stand on, a ray down from its step height above its feet, just past the front of its capsule, finds ground it can stand on, and its capsule fits just above that, both where it is and over the step. Its pace across carries it on over the top. False when it does not climb
    bool PhysicsScene3D::State::StepUp(BodyRecord& record, const glm::vec3& velocity, float seconds, const glm::vec3& up)
    {
        const glm::vec3 l_Across = velocity - glm::dot(velocity, up) * up;
        const float l_Speed = glm::length(l_Across);
        if (l_Speed * seconds < c_StepSkin)
        {
            return false;
        }

        const PhysicalCharacter& l_Character = *record.Physical;
        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        JPH::RVec3 l_Position;
        JPH::Quat l_Rotation;
        l_Bodies.GetPositionAndRotation(record.Body, l_Position, l_Rotation);
        const std::optional<CapsuleHit> l_Wall = CastCapsule(record, l_Position, l_Rotation, ToJolt(l_Across * seconds));
        if (!l_Wall || !l_Character.IsSlopeTooSteep(l_Wall->Normal))
        {
            return false;
        }

        // Its feet are the lowest point of its capsule, and the ray starts a little higher than it climbs, so it finds a step as high as that
        const glm::vec3 l_Feet = FromJolt(JPH::Vec3(l_Position)) + FromJolt(l_Rotation * ToJolt(record.CapsuleOffset)) - up * (record.HalfSegment + record.Radius);
        const float l_Reach = record.StepHeight + c_StepReach;
        const glm::vec3 l_From = l_Feet + l_Across / l_Speed * (record.Radius + c_StepReach) + up * l_Reach;
        const JPH::RRayCast l_Ray(JPH::RVec3(ToJolt(l_From)), ToJolt(-up * l_Reach));
        const CharacterFilters l_Filters(*System, Layers, record);
        JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> l_Top;
        System->GetNarrowPhaseQueryNoLock().CastRay(l_Ray, JPH::RayCastSettings(), l_Top, l_Filters.BroadPhase, l_Filters.Objects, l_Filters.Bodies, l_Filters.Shapes);
        if (!l_Top.HadHit())
        {
            return false;
        }

        JPH::Vec3 l_Normal = JPH::Vec3::sZero();
        {
            const JPH::BodyLockRead l_Lock(System->GetBodyLockInterfaceNoLock(), l_Top.mHit.mBodyID);
            if (!l_Lock.Succeeded())
            {
                return false;
            }

            l_Normal = l_Lock.GetBody().GetWorldSpaceSurfaceNormal(l_Top.mHit.mSubShapeID2, l_Ray.GetPointOnRay(l_Top.mHit.mFraction));
        }

        // A ray that starts inside what it walks into finds it at once, higher than it climbs
        const float l_Rise = l_Reach * (1.0f - l_Top.mHit.mFraction);
        if (l_Rise < c_StepSkin || l_Rise > record.StepHeight + c_StepSkin || l_Character.IsSlopeTooSteep(l_Normal))
        {
            return false;
        }

        // Lifted, and as far on as it is held there, over the step
        const JPH::RVec3 l_Lifted = l_Position + JPH::RVec3(ToJolt(up * (l_Rise + c_StepSkin)));
        const float l_Climb = record.Radius + c_StepReach;
        if (!Fits(record, l_Lifted, l_Rotation) || !Fits(record, l_Lifted + JPH::RVec3(ToJolt(l_Across / l_Speed * l_Climb)), l_Rotation))
        {
            return false;
        }

        l_Bodies.SetPosition(record.Body, l_Lifted, JPH::EActivation::Activate);
        record.Step = { l_Top.mHit.mBodyID, l_Top.mHit.mSubShapeID2, l_Ray.GetPointOnRay(l_Top.mHit.mFraction), l_Normal, 0.0f };
        record.Climb = l_Climb;
        record.ClimbWay = l_Across / l_Speed;
        FindGround(record);
        StandOnStep(record);

        return true;
    }

    // A physical character lifted onto a step that its capsule is not over yet stands on the step's top, so it walks on at that height rather than down onto the step's edge, until it stands on the top itself
    void PhysicsScene3D::State::StandOnStep(BodyRecord& record)
    {
        PhysicalCharacter& l_Character = *record.Physical;
        if (l_Character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround && l_Character.GetGroundNormal().Dot(record.Step.Normal) > 0.999f)
        {
            record.Climb = 0.0f;

            return;
        }

        const CapsuleTouch& l_Step = record.Step;
        const JPH::BodyLockRead l_Lock(System->GetBodyLockInterfaceNoLock(), l_Step.Body);
        if (!l_Lock.Succeeded())
        {
            record.Climb = 0.0f;

            return;
        }

        const JPH::Body& l_Body = l_Lock.GetBody();
        l_Character.SetGround(JPH::CharacterBase::EGroundState::OnGround, l_Step.Body, l_Step.Part, l_Step.Point, l_Step.Normal, l_Body.IsStatic() ? JPH::Vec3::sZero() : l_Body.GetPointVelocity(l_Step.Point), l_Body.GetUserData());
        record.GroundDepth = 0.0f;
        record.Supports.clear();
        record.Supports.push_back(l_Step.Normal);
    }

    // Whether a physical character's capsule fits where it is placed, sunk into what is about it by no more than the solver leaves it
    bool PhysicsScene3D::State::Fits(const BodyRecord& record, JPH::RVec3Arg position, JPH::QuatArg rotation)
    {
        JPH::CollideShapeSettings l_Settings;
        l_Settings.mBackFaceMode = JPH::EBackFaceMode::IgnoreBackFaces;
        const CharacterFilters l_Filters(*System, Layers, record);
        Touches.clear();
        TouchCollector l_Collector(Touches, position);
        System->GetNarrowPhaseQueryNoLock().CollideShape(record.Capsule, JPH::Vec3::sOne(), JPH::RMat44::sRotationTranslation(rotation, position).PreTranslated(ToJolt(record.CapsuleOffset)), l_Settings, position, l_Collector, l_Filters.BroadPhase, l_Filters.Objects, l_Filters.Bodies, l_Filters.Shapes);

        return std::ranges::none_of(Touches, [](const CapsuleTouch& touch) { return touch.Depth > c_StepAllowance; });
    }

    // A physical character that walked off ground it stood on, set down onto ground it can stand on within the distance below, as Jolt keeps a kinematic one to the floor going down stairs: its capsule swept down meets it, and fits there, which it does not while the edge it walked off is in the way. False when it is not set down
    bool PhysicsScene3D::State::StickToFloor(BodyRecord& record, const glm::vec3& up, float distance)
    {
        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        JPH::RVec3 l_Position;
        JPH::Quat l_Rotation;
        l_Bodies.GetPositionAndRotation(record.Body, l_Position, l_Rotation);
        const JPH::Vec3 l_Down = ToJolt(-up * distance);
        const std::optional<CapsuleHit> l_Floor = CastCapsule(record, l_Position, l_Rotation, l_Down, true);
        const JPH::RVec3 l_There = l_Floor ? l_Position + JPH::RVec3(l_Down * l_Floor->Fraction) : l_Position;
        if (!l_Floor || !Fits(record, l_There, l_Rotation))
        {
            return false;
        }

        l_Bodies.SetPosition(record.Body, l_There, JPH::EActivation::Activate);
        FindGround(record);

        return record.Physical->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }

    // Once a step, each dynamic body the character walked into, at the contact it went into fastest, as Jolt reckons it: the impulse that brings the body to the character's speed there, up to the push strength over the step, and none downwards, since the character's weight on what it stands on is added apart. Jolt's own pushes are turned off by the listener, since Jolt pushes a body again each time a pass of the move meets it
    void PhysicsScene3D::State::PushBodies(const BodyRecord& record, const glm::vec3& velocity, float seconds)
    {
        const JPH::CharacterVirtual& l_Character = *record.Character;
        const JPH::Vec3 l_Velocity = ToJolt(velocity);
        Pushes.clear();
        for (const JPH::CharacterContact& it_Contact : l_Character.GetActiveContacts())
        {
            if (!it_Contact.mHadCollision || it_Contact.mWasDiscarded || it_Contact.mIsSensorB || it_Contact.mMotionTypeB != JPH::EMotionType::Dynamic)
            {
                continue;
            }

            constexpr float c_Damping = 0.9f;
            constexpr float c_PenetrationResolution = 0.4f;
            const float l_Wanted = -(l_Velocity - it_Contact.mLinearVelocity).Dot(it_Contact.mContactNormal) * c_Damping - std::min(it_Contact.mDistance, 0.0f) * c_PenetrationResolution / seconds;
            if (l_Wanted <= 0.0f)
            {
                continue;
            }

            const auto a_Found = std::ranges::find(Pushes, it_Contact.mBodyB, [](const BodyPush& push) { return push.Contact->mBodyB; });
            if (a_Found == Pushes.end())
            {
                Pushes.push_back({ &it_Contact, l_Wanted });
            }
            else if (l_Wanted > a_Found->Wanted)
            {
                *a_Found = { &it_Contact, l_Wanted };
            }
        }

        const JPH::Vec3 l_Up = l_Character.GetUp();
        for (const BodyPush& it_Push : Pushes)
        {
            const JPH::CharacterContact& l_Contact = *it_Push.Contact;
            const JPH::BodyLockWrite l_Lock(System->GetBodyLockInterface(), l_Contact.mBodyB);
            if (!l_Lock.SucceededAndIsInBroadPhase())
            {
                continue;
            }

            const JPH::Body& l_Body = l_Lock.GetBody();
            const JPH::Vec3 l_Arm = JPH::Vec3(l_Contact.mPosition - l_Body.GetCenterOfMassPosition()).Cross(l_Contact.mContactNormal);
            const float l_InverseMass = l_Body.GetInverseInertia().Multiply3x3(l_Arm).Dot(l_Arm) + l_Body.GetMotionProperties()->GetInverseMass();
            const float l_Impulse = std::min(it_Push.Wanted / l_InverseMass, l_Character.GetMaxStrength() * seconds);
            JPH::Vec3 l_World = -l_Impulse * l_Contact.mContactNormal;
            const float l_Down = l_World.Dot(l_Up);
            l_World -= l_Down < 0.0f ? l_Down * l_Up : JPH::Vec3::sZero();
            System->GetBodyInterfaceNoLock().AddImpulse(l_Contact.mBodyB, l_World, l_Contact.mPosition);
        }
    }

    // Turned from code alone, so it turns at once with its colliders' bodies, and moves on between its steps as it was
    void PhysicsScene3D::State::TurnToTransform(BodyRecord& record)
    {
        record.WrittenRotation = Owner.GetRegistry().get<TransformComponent>(record.Entity).Rotation;
        const glm::quat l_Rotation = Decompose(Owner.ComputeWorldMatrix(Entity(record.Entity, &Owner))).Pose.Rotation;
        if (record.Character != nullptr)
        {
            record.Character->SetRotation(ToJolt(l_Rotation));
        }

        record.Previous.Rotation = l_Rotation;
        record.Current.Rotation = l_Rotation;
        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        for (const JPH::BodyID it_Body : { record.Body, record.Sensor })
        {
            if (!it_Body.IsInvalid())
            {
                l_Bodies.SetRotation(it_Body, ToJolt(l_Rotation), JPH::EActivation::DontActivate);
            }
        }
    }

    // Once both its bodies are made, false until then. Its frame is its entity's world transform as it is then, which a body waiting for its shapes has not moved from, with the anchor as an offset and the axis turned with it. Given to Jolt in world space, so where it meets each body is taken from where the bodies are. Jolt measures a hinge's turn and a slider's travel as its second body's against its first, so the joint's own body goes second, and both count its own body's motion along or about the axis. A joint that holds no dynamic body, joins a body with only triggers, or joins a character, is reported and left out
    bool PhysicsScene3D::State::MakeJoint(JointRecord& joint)
    {
        const BodyRecord& l_First = *joint.First;
        const BodyRecord* l_Second = joint.Second;
        if (l_First.Waiting || (l_Second != nullptr && l_Second->Waiting))
        {
            return false;
        }

        joint.Waiting = false;
        Scene& l_Scene = Owner;
        SceneRegistry& l_Registry = l_Scene.GetRegistry();
        if (!l_Registry.valid(joint.Entity))
        {
            return true;
        }

        const std::string l_Name = GetName(l_Scene, joint.Entity);
        if (l_First.Body.IsInvalid() || (l_Second != nullptr && l_Second->Body.IsInvalid()))
        {
            TR_CORE_WARN("Physics: {}'s Joint joins a body with no solid colliders, so it is left out", l_Name);

            return true;
        }

        if (l_First.HasCharacter || (l_Second != nullptr && l_Second->HasCharacter))
        {
            TR_CORE_WARN("Physics: {}'s Joint joins a character, which a joint cannot hold, so it is left out", l_Name);

            return true;
        }

        if (l_First.Motion != BodyMotion::Dynamic && (l_Second == nullptr || l_Second->Motion != BodyMotion::Dynamic))
        {
            TR_CORE_WARN("Physics: {}'s Joint joins no dynamic body, so it would hold nothing and is left out", l_Name);

            return true;
        }

        const JPH::BodyLockInterface& l_Bodies = System->GetBodyLockInterfaceNoLock();
        JPH::Body* l_Own = l_Bodies.TryGetBody(l_First.Body);
        JPH::Body* l_Other = l_Second != nullptr ? l_Bodies.TryGetBody(l_Second->Body) : &JPH::Body::sFixedToWorld;
        if (l_Own == nullptr || l_Other == nullptr)
        {
            return true;
        }

        const JointComponent& l_Joint = joint.Settings;
        const glm::mat4 l_World = l_Scene.ComputeWorldMatrix(Entity(joint.Entity, &l_Scene));
        const glm::quat l_Rotation = Decompose(l_World).Pose.Rotation;
        const JPH::RVec3 l_Anchor(ToJolt(glm::vec3(l_World * glm::vec4(l_Joint.Anchor, 1.0f))));
        const JPH::Vec3 l_Axis = ToJolt(l_Rotation * (glm::length(l_Joint.Axis) > 1e-6f ? glm::normalize(l_Joint.Axis) : glm::vec3(0.0f, 1.0f, 0.0f))).Normalized();
        const JPH::Vec3 l_Normal = l_Axis.GetNormalizedPerpendicular();

        JPH::Ref<JPH::TwoBodyConstraintSettings> l_Settings;
        switch (l_Joint.Type)
        {
            case JointType::Fixed:
            {
                JPH::FixedConstraintSettings* l_Fixed = new JPH::FixedConstraintSettings();
                l_Fixed->mPoint1 = l_Fixed->mPoint2 = l_Anchor;
                l_Fixed->mAxisX1 = l_Fixed->mAxisX2 = ToJolt(l_Rotation * glm::vec3(1.0f, 0.0f, 0.0f));
                l_Fixed->mAxisY1 = l_Fixed->mAxisY2 = ToJolt(l_Rotation * glm::vec3(0.0f, 1.0f, 0.0f));
                l_Settings = l_Fixed;

                break;
            }
            case JointType::Hinge:
            {
                JPH::HingeConstraintSettings* l_Hinge = new JPH::HingeConstraintSettings();
                l_Hinge->mPoint1 = l_Hinge->mPoint2 = l_Anchor;
                l_Hinge->mHingeAxis1 = l_Hinge->mHingeAxis2 = l_Axis;
                l_Hinge->mNormalAxis1 = l_Hinge->mNormalAxis2 = l_Normal;
                if (l_Joint.UseLimits)
                {
                    l_Hinge->mLimitsMin = std::clamp(l_Joint.LowerLimit, -JPH::JPH_PI, 0.0f);
                    l_Hinge->mLimitsMax = std::clamp(l_Joint.UpperLimit, 0.0f, JPH::JPH_PI);
                }

                l_Hinge->mMotorSettings.SetTorqueLimit(std::max(l_Joint.MotorMaxForce, 0.0f));
                l_Settings = l_Hinge;

                break;
            }
            case JointType::Slider:
            {
                JPH::SliderConstraintSettings* l_Slider = new JPH::SliderConstraintSettings();
                l_Slider->mPoint1 = l_Slider->mPoint2 = l_Anchor;
                l_Slider->mSliderAxis1 = l_Slider->mSliderAxis2 = l_Axis;
                l_Slider->mNormalAxis1 = l_Slider->mNormalAxis2 = l_Normal;
                if (l_Joint.UseLimits)
                {
                    l_Slider->mLimitsMin = std::min(l_Joint.LowerLimit, 0.0f);
                    l_Slider->mLimitsMax = std::max(l_Joint.UpperLimit, 0.0f);
                }

                l_Slider->mMotorSettings.SetForceLimit(std::max(l_Joint.MotorMaxForce, 0.0f));
                l_Settings = l_Slider;

                break;
            }
            case JointType::Distance:
            {
                // The other end is on the entity the joint names, or in the world
                const bool l_OnEntity = l_Second != nullptr && l_Registry.valid(joint.OtherEntity);
                const glm::vec3 l_OtherEnd = l_OnEntity ? glm::vec3(l_Scene.ComputeWorldMatrix(Entity(joint.OtherEntity, &l_Scene)) * glm::vec4(l_Joint.ConnectedAnchor, 1.0f)) : l_Joint.ConnectedAnchor;
                const float l_Start = glm::distance(FromJolt(JPH::Vec3(l_Anchor)), l_OtherEnd);
                const float l_Min = l_Joint.MinDistance < 0.0f ? l_Start : l_Joint.MinDistance;

                JPH::DistanceConstraintSettings* l_Distance = new JPH::DistanceConstraintSettings();
                l_Distance->mPoint1 = JPH::RVec3(ToJolt(l_OtherEnd));
                l_Distance->mPoint2 = l_Anchor;
                l_Distance->mMinDistance = l_Min;
                l_Distance->mMaxDistance = std::max(l_Joint.MaxDistance < 0.0f ? l_Start : l_Joint.MaxDistance, l_Min);
                l_Distance->mLimitsSpringSettings.mFrequency = std::max(l_Joint.SpringFrequency, 0.0f);
                l_Distance->mLimitsSpringSettings.mDamping = std::max(l_Joint.SpringDamping, 0.0f);
                l_Settings = l_Distance;

                break;
            }
            case JointType::Point:
            {
                JPH::PointConstraintSettings* l_Point = new JPH::PointConstraintSettings();
                l_Point->mPoint1 = l_Point->mPoint2 = l_Anchor;
                l_Settings = l_Point;

                break;
            }
        }

        AddConstraint(joint, *l_Settings, *l_Own, *l_Other);

        return true;
    }

    // Made from settings given in world space, or in each body's own space, with its motor started and its bodies kept from colliding unless it lets them
    void PhysicsScene3D::State::AddConstraint(JointRecord& joint, const JPH::TwoBodyConstraintSettings& settings, JPH::Body& own, JPH::Body& other)
    {
        const JointComponent& l_Joint = joint.Settings;
        joint.Constraint = static_cast<JPH::TwoBodyConstraint*>(settings.Create(other, own));
        if (l_Joint.UseMotor && l_Joint.Type == JointType::Hinge)
        {
            JPH::HingeConstraint& l_Hinge = static_cast<JPH::HingeConstraint&>(*joint.Constraint);
            l_Hinge.SetMotorState(JPH::EMotorState::Velocity);
            l_Hinge.SetTargetAngularVelocity(l_Joint.MotorSpeed);
        }
        else if (l_Joint.UseMotor && l_Joint.Type == JointType::Slider)
        {
            JPH::SliderConstraint& l_Slider = static_cast<JPH::SliderConstraint&>(*joint.Constraint);
            l_Slider.SetMotorState(JPH::EMotorState::Velocity);
            l_Slider.SetTargetVelocity(l_Joint.MotorSpeed);
        }

        System->AddConstraint(joint.Constraint);
        if (!l_Joint.CollideConnected && joint.Second != nullptr)
        {
            const std::uint32_t l_FirstID = joint.First->Body.GetIndexAndSequenceNumber();
            const std::uint32_t l_SecondID = joint.Second->Body.GetIndexAndSequenceNumber();
            joint.Pair = GetPairKey(std::min(l_FirstID, l_SecondID), std::max(l_FirstID, l_SecondID));
            joint.KeptApart = true;
            KeptApart.insert(std::ranges::upper_bound(KeptApart, joint.Pair), joint.Pair);
        }
    }

    void PhysicsScene3D::State::RemoveConstraint(JointRecord& joint)
    {
        if (joint.Constraint != nullptr)
        {
            System->RemoveConstraint(joint.Constraint);
            joint.Constraint = nullptr;
        }

        if (std::exchange(joint.KeptApart, false))
        {
            KeptApart.erase(std::ranges::lower_bound(KeptApart, joint.Pair));
        }
    }

    // The force and the torque on each joint that can break, averaged over the step from the impulses its constraint's parts gave, its limits' included and its motor's not. One past its break force or torque is taken out of the world, and its bodies collide again, after it is logged and handed out as an event, in the joints' order
    void PhysicsScene3D::State::BreakJoints(float seconds, std::uint32_t index)
    {
        if (seconds <= 0.0f)
        {
            return;
        }

        for (JointRecord& it_Joint : Joints)
        {
            const JointComponent& l_Joint = it_Joint.Settings;
            if (it_Joint.Constraint == nullptr || (l_Joint.BreakForce <= 0.0f && l_Joint.BreakTorque <= 0.0f))
            {
                continue;
            }

            float l_Force = 0.0f;
            float l_Torque = 0.0f;
            JPH::TwoBodyConstraint& l_Constraint = *it_Joint.Constraint;
            switch (l_Joint.Type)
            {
                case JointType::Fixed:
                {
                    const JPH::FixedConstraint& l_Fixed = static_cast<const JPH::FixedConstraint&>(l_Constraint);
                    l_Force = l_Fixed.GetTotalLambdaPosition().Length();
                    l_Torque = l_Fixed.GetTotalLambdaRotation().Length();

                    break;
                }
                case JointType::Hinge:
                {
                    const JPH::HingeConstraint& l_Hinge = static_cast<const JPH::HingeConstraint&>(l_Constraint);
                    l_Force = l_Hinge.GetTotalLambdaPosition().Length();
                    l_Torque = std::hypot(l_Hinge.GetTotalLambdaRotation().Length(), l_Hinge.GetTotalLambdaRotationLimits());

                    break;
                }
                case JointType::Slider:
                {
                    const JPH::SliderConstraint& l_Slider = static_cast<const JPH::SliderConstraint&>(l_Constraint);
                    l_Force = std::hypot(l_Slider.GetTotalLambdaPosition().Length(), l_Slider.GetTotalLambdaPositionLimits());
                    l_Torque = l_Slider.GetTotalLambdaRotation().Length();

                    break;
                }
                case JointType::Distance:
                {
                    l_Force = std::abs(static_cast<const JPH::DistanceConstraint&>(l_Constraint).GetTotalLambdaPosition());

                    break;
                }
                case JointType::Point:
                {
                    l_Force = static_cast<const JPH::PointConstraint&>(l_Constraint).GetTotalLambdaPosition().Length();

                    break;
                }
            }

            l_Force /= seconds;
            l_Torque /= seconds;
            const bool l_ByForce = l_Joint.BreakForce > 0.0f && l_Force > l_Joint.BreakForce;
            const bool l_ByTorque = !l_ByForce && l_Joint.BreakTorque > 0.0f && l_Torque > l_Joint.BreakTorque;
            if (!l_ByForce && !l_ByTorque)
            {
                continue;
            }

            TR_CORE_INFO("Physics: {}'s {} joint broke under {}, past its break {}", GetName(Owner, it_Joint.Entity), GetJointTypeName(l_Joint.Type), l_ByForce ? std::format("{:.1f} N", l_Force) : std::format("{:.1f} N m", l_Torque), l_ByForce ? std::format("force of {:.1f} N", l_Joint.BreakForce) : std::format("torque of {:.1f} N m", l_Joint.BreakTorque));
            RemoveConstraint(it_Joint);
            it_Joint.Broken = true;

            Events.push_back({ PhysicsEventType::JointBreak, it_Joint.ID, it_Joint.Other, glm::vec3(0.0f), glm::vec3(0.0f), index });
        }
    }

    // Out of the world. Its pairs end now, as the bodies it touched see them, so Jolt's word of their parts parting, which comes with the next step, finds them gone
    void PhysicsScene3D::State::DestroyBody(JPH::BodyID& body)
    {
        if (body.IsInvalid())
        {
            return;
        }

        EndPairs(body);
        WakeAround(body);
        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        l_Bodies.RemoveBody(body);
        l_Bodies.DestroyBody(body);
        body = JPH::BodyID();
    }

    // Jolt leaves bodies asleep when one they rest on goes, shrinks or moves away, so everything within its bounds is woken to find out
    void PhysicsScene3D::State::WakeAround(JPH::BodyID body)
    {
        if (body.IsInvalid())
        {
            return;
        }

        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        l_Bodies.ActivateBodiesInAABox(l_Bodies.GetTransformedShape(body).GetWorldSpaceBounds(), JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter());
    }

    // Each pair the body is part of ends with the frame's first step, in the order of the pairs' bodies
    void PhysicsScene3D::State::EndPairs(JPH::BodyID body)
    {
        for (auto a_Pair = Pairs.begin(); a_Pair != Pairs.end();)
        {
            const ContactPair& l_Pair = a_Pair->second;
            if (l_Pair.FirstBody != body && l_Pair.SecondBody != body)
            {
                ++a_Pair;

                continue;
            }

            Events.push_back({ l_Pair.Trigger ? PhysicsEventType::TriggerExit : PhysicsEventType::ContactEnd, l_Pair.First, l_Pair.Second, glm::vec3(0.0f), glm::vec3(0.0f), 0 });
            a_Pair = Pairs.erase(a_Pair);
        }
    }

    // An owner's record as the scene has it now: its CharacterController or RigidBody, or neither for a static body, and the colliders on it and below it, in hierarchy order, that no RigidBody or character below takes, nor a 2D body or character
    void PhysicsScene3D::State::Gather(entt::entity owner, BodyRecord& record) const
    {
        const SceneRegistry& l_Registry = Owner.GetRegistry();
        ReadOwner(l_Registry, owner, record);
        record.Solids.clear();
        record.Triggers.clear();

        const auto a_Visit = [&l_Registry, &record](const auto& self, entt::entity entity) -> void
        {
            AddColliders(l_Registry, entity, record);
            for (entt::entity it_Child = l_Registry.get<RelationshipComponent>(entity).FirstChild; it_Child != entt::null; it_Child = l_Registry.get<RelationshipComponent>(it_Child).NextSibling)
            {
                if (!l_Registry.any_of<RigidBodyComponent, CharacterControllerComponent, RigidBody2DComponent, CharacterController2DComponent>(it_Child))
                {
                    self(self, it_Child);
                }
            }
        };

        a_Visit(a_Visit, owner);
    }

    void PhysicsScene3D::State::Index(BodyRecord& record)
    {
        Members[record.Entity] = &record;
        for (const PhysicsVector<ColliderSource>* it_List : { &record.Solids, &record.Triggers })
        {
            for (const ColliderSource& it_Collider : *it_List)
            {
                Members[it_Collider.Entity] = &record;
            }
        }
    }

    // Only the entries that are still this record's, since another may have taken an entity over
    void PhysicsScene3D::State::Unindex(const BodyRecord& record)
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
        for (const PhysicsVector<ColliderSource>* it_List : { &record.Solids, &record.Triggers })
        {
            for (const ColliderSource& it_Collider : *it_List)
            {
                a_Erase(it_Collider.Entity);
            }
        }
    }

    // The record an owner entity has, none for an entity that owns no record
    BodyRecord* PhysicsScene3D::State::FindRecord(entt::entity owner) const
    {
        const auto a_Found = owner != entt::null ? Members.find(owner) : Members.end();

        return a_Found != Members.end() && a_Found->second->Entity == owner ? a_Found->second : nullptr;
    }

    // The cooked shapes its colliders use, each held while the scene runs
    void PhysicsScene3D::State::AcquireShapes(const BodyRecord& record)
    {
        for (const PhysicsVector<ColliderSource>* it_List : { &record.Solids, &record.Triggers })
        {
            for (const ColliderSource& it_Collider : *it_List)
            {
                if (IsCooked(it_Collider.Kind) && it_Collider.Shape && std::ranges::find(Shapes, it_Collider.Shape) == Shapes.end())
                {
                    Shapes.push_back(it_Collider.Shape);
                    AssetManager::Acquire(it_Collider.Shape);
                }
            }
        }
    }

    // Its joints go with it, to be found again later, then its bodies, and the pose it was drawn at, so its entity, when it stays, draws from its transform again
    void PhysicsScene3D::State::RemoveRecord(BodyRecord& record)
    {
        SceneRegistry& l_Registry = Owner.GetRegistry();
        for (std::size_t it_Joint = 0; it_Joint < Joints.size();)
        {
            JointRecord& l_Joint = Joints[it_Joint];
            if (l_Joint.First != &record && l_Joint.Second != &record)
            {
                ++it_Joint;

                continue;
            }

            RemoveConstraint(l_Joint);
            if (!l_Joint.Broken && l_Registry.valid(l_Joint.Entity))
            {
                RetryJoints.push_back(l_Joint.Entity);
            }

            Joints.erase(Joints.begin() + static_cast<std::ptrdiff_t>(it_Joint));
        }

        DestroyCharacter(record);
        DestroyBody(record.Body);
        DestroyBody(record.Sensor);
        if (l_Registry.valid(record.Entity))
        {
            l_Registry.remove<PhysicsPoseComponent>(record.Entity);
        }

        Unindex(record);
        std::erase(Records, &record);
        Memory::Delete(&record);
    }

    // A record gathered again is left alone when it is as it was. A change of its RigidBody's motion or axis locks, of whether it is a character or of its character's mode, or of a body still waiting for its shapes, makes its bodies again, a dynamic one keeping its velocities and a character its own, and any other change is made to the bodies and the character where they are, so their contacts carry over. A character that is new starts still. Its joints are let go first and made again after, holding the pose they were first made in
    void PhysicsScene3D::State::UpdateRecord(BodyRecord& record, BodyRecord& gathered)
    {
        if (record.HasRigidBody == gathered.HasRigidBody && record.Settings == gathered.Settings && record.HasCharacter == gathered.HasCharacter && record.Controller == gathered.Controller && record.Solids == gathered.Solids && record.Triggers == gathered.Triggers)
        {
            return;
        }

        const bool l_Remake = record.Waiting || record.HasRigidBody != gathered.HasRigidBody || record.HasCharacter != gathered.HasCharacter || record.Controller.Mode != gathered.Controller.Mode || record.Settings.Motion != gathered.Settings.Motion || record.Settings.LockPosition != gathered.Settings.LockPosition || record.Settings.LockRotation != gathered.Settings.LockRotation;
        BodyVelocity l_Velocity;
        const bool l_Carry = l_Remake && record.Motion == BodyMotion::Dynamic && !record.Body.IsInvalid();
        if (l_Carry)
        {
            const JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
            l_Velocity = { l_Bodies.GetLinearVelocity(record.Body), l_Bodies.GetAngularVelocity(record.Body) };
        }

        SaveJoints(record);
        Unindex(record);
        // A character that is new starts still, touching nothing
        if (record.HasCharacter != gathered.HasCharacter)
        {
            record.Velocity = glm::vec3(0.0f);
            record.Move = glm::vec3(0.0f);
            record.Touching.clear();
        }

        record.HasRigidBody = gathered.HasRigidBody;
        record.Settings = gathered.Settings;
        record.Motion = gathered.Motion;
        record.HasCharacter = gathered.HasCharacter;
        record.Controller = gathered.Controller;
        record.Solids = std::move(gathered.Solids);
        record.Triggers = std::move(gathered.Triggers);
        Index(record);
        AcquireShapes(record);
        if (l_Remake)
        {
            DestroyCharacter(record);
            DestroyBody(record.Body);
            DestroyBody(record.Sensor);
        }

        if (MakeBody(record, l_Carry ? &l_Velocity : nullptr, !l_Remake))
        {
            RestoreJoints(record);
        }
    }

    // Each live joint on the record, as its constraint has it in each body's own space, let go until RestoreJoints
    void PhysicsScene3D::State::SaveJoints(const BodyRecord& record)
    {
        const JPH::BodyLockInterface& l_Bodies = System->GetBodyLockInterfaceNoLock();
        for (JointRecord& it_Joint : Joints)
        {
            if ((it_Joint.First != &record && it_Joint.Second != &record) || it_Joint.Constraint == nullptr)
            {
                continue;
            }

            const JPH::Ref<JPH::ConstraintSettings> l_Settings = it_Joint.Constraint->GetConstraintSettings();
            it_Joint.Saved = static_cast<JPH::TwoBodyConstraintSettings*>(l_Settings.GetPtr());
            const JPH::Body* l_Own = l_Bodies.TryGetBody(it_Joint.First->Body);
            const JPH::Body* l_Other = it_Joint.Second != nullptr ? l_Bodies.TryGetBody(it_Joint.Second->Body) : nullptr;
            it_Joint.SavedCenter2 = l_Own != nullptr ? l_Own->GetShape()->GetCenterOfMass() : JPH::Vec3::sZero();
            it_Joint.SavedCenter1 = l_Other != nullptr ? l_Other->GetShape()->GetCenterOfMass() : JPH::Vec3::sZero();
            RemoveConstraint(it_Joint);
            it_Joint.Waiting = true;
        }
    }

    void PhysicsScene3D::State::RestoreJoints(const BodyRecord& record)
    {
        for (JointRecord& it_Joint : Joints)
        {
            if (it_Joint.Waiting && (it_Joint.First == &record || it_Joint.Second == &record))
            {
                static_cast<void>(it_Joint.Saved != nullptr ? RestoreJoint(it_Joint) : MakeJoint(it_Joint));
            }
        }
    }

    // As it was saved, in each body's own space, each point moved by its body's change of centre of mass, so it holds the pose it was first made in. Once both its bodies are made, false until then. One left with no dynamic body, or joining a body that has no solid colliders now or is a character now, is reported and left out
    bool PhysicsScene3D::State::RestoreJoint(JointRecord& joint)
    {
        const BodyRecord& l_First = *joint.First;
        const BodyRecord* l_Second = joint.Second;
        if (l_First.Waiting || (l_Second != nullptr && l_Second->Waiting))
        {
            return false;
        }

        joint.Waiting = false;
        const JPH::Ref<JPH::TwoBodyConstraintSettings> l_Settings = std::exchange(joint.Saved, nullptr);
        const std::string l_Name = GetName(Owner, joint.Entity);
        if (l_First.Body.IsInvalid() || (l_Second != nullptr && l_Second->Body.IsInvalid()))
        {
            TR_CORE_WARN("Physics: {}'s Joint joins a body with no solid colliders now, so it is left out", l_Name);

            return true;
        }

        if (l_First.HasCharacter || (l_Second != nullptr && l_Second->HasCharacter))
        {
            TR_CORE_WARN("Physics: {}'s Joint joins a character now, which a joint cannot hold, so it is left out", l_Name);

            return true;
        }

        if (l_First.Motion != BodyMotion::Dynamic && (l_Second == nullptr || l_Second->Motion != BodyMotion::Dynamic))
        {
            TR_CORE_WARN("Physics: {}'s Joint joins no dynamic body now, so it would hold nothing and is left out", l_Name);

            return true;
        }

        const JPH::BodyLockInterface& l_Bodies = System->GetBodyLockInterfaceNoLock();
        JPH::Body* l_Own = l_Bodies.TryGetBody(l_First.Body);
        JPH::Body* l_Other = l_Second != nullptr ? l_Bodies.TryGetBody(l_Second->Body) : &JPH::Body::sFixedToWorld;
        if (l_Own == nullptr || l_Other == nullptr)
        {
            return true;
        }

        const JPH::Vec3 l_Move1 = joint.SavedCenter1 - (l_Second != nullptr ? l_Other->GetShape()->GetCenterOfMass() : JPH::Vec3::sZero());
        const JPH::Vec3 l_Move2 = joint.SavedCenter2 - l_Own->GetShape()->GetCenterOfMass();
        switch (joint.Settings.Type)
        {
            case JointType::Fixed: MovePoints<JPH::FixedConstraintSettings>(*l_Settings, l_Move1, l_Move2); break;
            case JointType::Hinge: MovePoints<JPH::HingeConstraintSettings>(*l_Settings, l_Move1, l_Move2); break;
            case JointType::Slider: MovePoints<JPH::SliderConstraintSettings>(*l_Settings, l_Move1, l_Move2); break;
            case JointType::Distance: MovePoints<JPH::DistanceConstraintSettings>(*l_Settings, l_Move1, l_Move2); break;
            case JointType::Point: MovePoints<JPH::PointConstraintSettings>(*l_Settings, l_Move1, l_Move2); break;
        }

        AddConstraint(joint, *l_Settings, *l_Own, *l_Other);

        return true;
    }

    // A joint is on the body its entity is part of, and names the body of the entity it joins. One on no 3D body, or naming an entity no longer in the scene, or one that is part of no body or of its own, or a joint with a character on either side, is reported and left out
    void PhysicsScene3D::State::CreateJointRecord(entt::entity entity)
    {
        Scene& l_Scene = Owner;
        const SceneRegistry& l_Registry = l_Scene.GetRegistry();
        const JointComponent& l_Joint = l_Registry.get<JointComponent>(entity);
        const std::string l_Name = GetName(l_Scene, entity);
        BodyRecord* l_First = FindRecord(FindOwner(l_Registry, entity));
        if (l_First == nullptr)
        {
            TR_CORE_WARN("Physics: {}'s Joint is on no 3D body, since nothing on it or above it has a RigidBody or a 3D collider first, so it is left out", l_Name);

            return;
        }

        if (l_First->HasCharacter)
        {
            TR_CORE_WARN("Physics: {}'s Joint is on {}'s character, which a joint cannot hold, so it is left out", l_Name, GetName(l_Scene, l_First->Entity));

            return;
        }

        BodyRecord* l_Second = nullptr;
        entt::entity l_OtherEntity = entt::null;
        if (l_Joint.ConnectedBody)
        {
            const Entity l_Connected = l_Scene.FindEntityByUUID(l_Joint.ConnectedBody);
            if (!l_Connected)
            {
                TR_CORE_WARN("Physics: {}'s Joint names entity {}, which is not in the scene, so it is left out", l_Name, l_Joint.ConnectedBody);

                return;
            }

            l_Second = FindRecord(FindOwner(l_Registry, l_Connected.GetHandle()));
            if (l_Second == nullptr || l_Second == l_First || l_Second->HasCharacter)
            {
                TR_CORE_WARN("Physics: {}'s Joint names {}, which is {}, so it is left out", l_Name, GetName(l_Scene, l_Connected.GetHandle()), l_Second == l_First ? "part of its own body" : (l_Second != nullptr ? "part of a character, which a joint cannot hold" : "part of no body"));

                return;
            }

            l_OtherEntity = l_Connected.GetHandle();
        }

        JointRecord& l_Record = Joints.emplace_back();
        l_Record.Entity = entity;
        l_Record.ID = l_Registry.get<IDComponent>(entity).ID;
        l_Record.OtherEntity = l_OtherEntity;
        l_Record.Other = l_Second != nullptr ? l_Joint.ConnectedBody : UUID();
        l_Record.Settings = l_Joint;
        l_Record.First = l_First;
        l_Record.Second = l_Second;
    }

    // Everything changed since the last frame with steps is taken in. The owners whose records may differ are those of each changed entity as they were and as it, and everything below it, has them now. Each is gathered again: a record no longer owned goes, one newly owned is made, and the rest are updated. Then the joints follow
    void PhysicsScene3D::State::Sync()
    {
        if (Dirty.empty() && DirtyJoints.empty())
        {
            return;
        }

        TR_PROFILE_SCOPE("PhysicsScene3D::Sync");
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
            if (l_Registry.any_of<RigidBodyComponent, CharacterControllerComponent>(entity) || HasCollider(l_Registry, entity))
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
            BodyRecord* l_Record = FindRecord(it_Owner);
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
                BodyRecord& l_New = *Records.emplace_back(Memory::New<BodyRecord>(MemoryTag::Physics));
                Gather(it_Owner, l_New);
                Index(l_New);
                AcquireShapes(l_New);
                static_cast<void>(MakeBody(l_New));
                l_BodiesChanged = true;

                continue;
            }

            BodyRecord l_Gathered;
            Gather(it_Owner, l_Gathered);
            UpdateRecord(*l_Record, l_Gathered);
        }

        ResolveJoints(l_BodiesChanged, l_DirtyJoints);
        Waiting = static_cast<std::uint32_t>(std::ranges::count_if(Records, [](const BodyRecord* record) { return record->Waiting; }));
        WaitingJoints = static_cast<std::uint32_t>(std::ranges::count_if(Joints, [](const JointRecord& joint) { return joint.Waiting; }));
    }

    // A joint whose entity changed is made again from where its bodies are now, and so is one whose bodies are other records now, or one let go with a body that went. One whose entity or Joint went goes with them. A broken joint stays broken until its Joint changes
    void PhysicsScene3D::State::ResolveJoints(bool bodiesChanged, const PhysicsVector<entt::entity>& dirtyJoints)
    {
        const SceneRegistry& l_Registry = Owner.GetRegistry();
        PhysicsVector<entt::entity> l_Make;
        const auto a_Make = [&l_Make, &l_Registry](entt::entity entity)
        {
            if (l_Registry.valid(entity) && l_Registry.all_of<JointComponent>(entity) && std::ranges::find(l_Make, entity) == l_Make.end())
            {
                l_Make.push_back(entity);
            }
        };

        for (std::size_t it_Joint = 0; it_Joint < Joints.size();)
        {
            JointRecord& l_Joint = Joints[it_Joint];
            const bool l_Dirty = std::ranges::find(dirtyJoints, l_Joint.Entity) != dirtyJoints.end();
            if ((!l_Dirty && (!bodiesChanged || l_Joint.Broken)) || (l_Dirty && Tune(l_Joint)))
            {
                ++it_Joint;

                continue;
            }

            if (!l_Dirty && l_Registry.valid(l_Joint.Entity))
            {
                const BodyRecord* l_First = FindRecord(FindOwner(l_Registry, l_Joint.Entity));
                const BodyRecord* l_Second = l_Registry.valid(l_Joint.OtherEntity) ? FindRecord(FindOwner(l_Registry, l_Joint.OtherEntity)) : nullptr;
                if (l_First == l_Joint.First && l_Second == l_Joint.Second)
                {
                    ++it_Joint;

                    continue;
                }
            }

            RemoveConstraint(l_Joint);
            a_Make(l_Joint.Entity);
            Joints.erase(Joints.begin() + static_cast<std::ptrdiff_t>(it_Joint));
        }

        for (const entt::entity it_Entity : dirtyJoints)
        {
            if (std::ranges::find(Joints, it_Entity, &JointRecord::Entity) == Joints.end())
            {
                a_Make(it_Entity);
            }
        }

        for (const entt::entity it_Entity : std::exchange(RetryJoints, {}))
        {
            if (std::ranges::find(Joints, it_Entity, &JointRecord::Entity) == Joints.end())
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
                static_cast<void>(MakeJoint(Joints.back()));
            }
        }
    }

    // A joint whose motor or break limits alone changed, or nothing at all, takes them on in place and holds where it is, and a broken one stays broken. False when anything else changed, or its bodies are other records now, so it is made again
    bool PhysicsScene3D::State::Tune(JointRecord& joint)
    {
        const SceneRegistry& l_Registry = Owner.GetRegistry();
        if (!l_Registry.valid(joint.Entity) || !l_Registry.all_of<JointComponent>(joint.Entity))
        {
            return false;
        }

        const JointComponent& l_Now = l_Registry.get<JointComponent>(joint.Entity);
        JointComponent l_Kept = l_Now;
        l_Kept.UseMotor = joint.Settings.UseMotor;
        l_Kept.MotorSpeed = joint.Settings.MotorSpeed;
        l_Kept.MotorMaxForce = joint.Settings.MotorMaxForce;
        l_Kept.BreakForce = joint.Settings.BreakForce;
        l_Kept.BreakTorque = joint.Settings.BreakTorque;
        const BodyRecord* l_First = FindRecord(FindOwner(l_Registry, joint.Entity));
        const BodyRecord* l_Second = l_Registry.valid(joint.OtherEntity) ? FindRecord(FindOwner(l_Registry, joint.OtherEntity)) : nullptr;
        if (!(l_Kept == joint.Settings) || l_First != joint.First || l_Second != joint.Second)
        {
            return false;
        }

        joint.Settings = l_Now;
        if (joint.Constraint == nullptr)
        {
            return true;
        }

        if (l_Now.Type == JointType::Hinge)
        {
            JPH::HingeConstraint& l_Hinge = static_cast<JPH::HingeConstraint&>(*joint.Constraint);
            l_Hinge.GetMotorSettings().SetTorqueLimit(std::max(l_Now.MotorMaxForce, 0.0f));
            l_Hinge.SetMotorState(l_Now.UseMotor ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
            l_Hinge.SetTargetAngularVelocity(l_Now.MotorSpeed);
        }
        else if (l_Now.Type == JointType::Slider)
        {
            JPH::SliderConstraint& l_Slider = static_cast<JPH::SliderConstraint&>(*joint.Constraint);
            l_Slider.GetMotorSettings().SetForceLimit(std::max(l_Now.MotorMaxForce, 0.0f));
            l_Slider.SetMotorState(l_Now.UseMotor ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
            l_Slider.SetTargetVelocity(l_Now.MotorSpeed);
        }

        // A motor turned on or sped up moves bodies that had fallen asleep
        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        for (const BodyRecord* it_Record : { joint.First, joint.Second })
        {
            if (it_Record != nullptr && it_Record->Motion == BodyMotion::Dynamic && !it_Record->Body.IsInvalid())
            {
                l_Bodies.ActivateBody(it_Record->Body);
            }
        }

        return true;
    }

    // A body whose entity's transform was set from code since the body made or last wrote it is taken there at once, and drawn there with nothing between. Kinematic bodies follow their transforms already, and characters look before each step
    void PhysicsScene3D::State::FollowTransforms()
    {
        SceneRegistry& l_Registry = Owner.GetRegistry();
        for (BodyRecord* it_Record : Records)
        {
            if (it_Record->Waiting || it_Record->Motion == BodyMotion::Kinematic || !l_Registry.valid(it_Record->Entity) || (it_Record->Body.IsInvalid() && it_Record->Sensor.IsInvalid()))
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

    // The body, its sensor and its character where its entity's transform puts it, with nothing between to draw, and a kinematic one's targets there too, so it stays until its transform moves again. A character keeps its velocity and finds what it stands on there, and a physical one's body is woken, and whatever step or ledge it was on is left behind
    void PhysicsScene3D::State::PlaceAtTransform(BodyRecord& record)
    {
        const TransformComponent& l_Transform = Owner.GetRegistry().get<TransformComponent>(record.Entity);
        record.WrittenPosition = l_Transform.Position;
        record.WrittenRotation = l_Transform.Rotation;
        const BodyPose l_Pose = Decompose(Owner.ComputeWorldMatrix(Entity(record.Entity, &Owner))).Pose;
        const JPH::RVec3 l_Position(ToJolt(l_Pose.Position));
        const JPH::Quat l_Rotation = ToJolt(l_Pose.Rotation);
        JPH::BodyInterface& l_Bodies = System->GetBodyInterface();
        if (record.Character != nullptr)
        {
            JPH::CharacterVirtual& l_Character = *record.Character;
            WakeAround(l_Character.GetInnerBodyID());
            l_Character.SetPosition(l_Position);
            l_Character.SetRotation(l_Rotation);
            WakeAround(l_Character.GetInnerBodyID());
            const CharacterFilters l_Filters(*System, Layers, record);
            l_Character.RefreshContacts(l_Filters.BroadPhase, l_Filters.Objects, l_Filters.Bodies, l_Filters.Shapes, *StepMemory);
        }

        if (!record.Body.IsInvalid())
        {
            WakeAround(record.Body);
            l_Bodies.SetPositionAndRotation(record.Body, l_Position, l_Rotation, record.Motion == BodyMotion::Dynamic || record.Physical != nullptr ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            WakeAround(record.Body);
        }

        if (!record.Sensor.IsInvalid())
        {
            l_Bodies.SetPositionAndRotation(record.Sensor, l_Position, l_Rotation, JPH::EActivation::DontActivate);
        }

        if (record.Physical != nullptr)
        {
            record.Climb = 0.0f;
            record.WalkedOff = false;
            FindGround(record);
        }

        record.Previous = l_Pose;
        record.Current = l_Pose;
        record.TargetFrom = l_Pose;
        record.TargetTo = l_Pose;
    }

    // The record of the body made for the entity, or for the body one of its colliders is part of, with the scene's changes taken in
    BodyRecord* PhysicsScene3D::State::FindBody(UUID entity)
    {
        Sync();
        const Entity l_Entity = Owner.FindEntityByUUID(entity);
        const auto a_Found = l_Entity ? Members.find(l_Entity.GetHandle()) : Members.end();
        if (a_Found == Members.end() || a_Found->second->Waiting || !Owner.GetRegistry().valid(a_Found->second->Entity))
        {
            return nullptr;
        }

        return a_Found->second;
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

            return { reinterpret_cast<const BodyRecord*>(l_UserData & ~c_RecordBits), (l_UserData & c_SensorBit) != 0 };
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

    // The entities of the collider a query met and of its body, which for a character's inner body, or a physical one's capsule, are both the character's. None for a body that is not the scene's
    PhysicsHit3D PhysicsScene3D::State::MakeHit(JPH::BodyID body, const JPH::SubShapeID& part) const
    {
        PhysicsHit3D l_Hit;
        const JPH::BodyLockRead l_Lock(System->GetBodyLockInterfaceNoLock(), body);
        if (!l_Lock.Succeeded())
        {
            return l_Hit;
        }

        const JPH::Body& l_Body = l_Lock.GetBody();
        const BodyRecord* l_Record = GetRecord(l_Body);
        if (l_Record != nullptr && ((l_Body.GetUserData() & c_CharacterBit) != 0 || IsCapsule(l_Body, nullptr, part)))
        {
            l_Hit.Entity = l_Record->ID;
            l_Hit.Body = l_Record->ID;

            return l_Hit;
        }

        const ColliderSource* l_Collider = FindCollider(l_Body, nullptr, part);
        if (l_Record == nullptr || l_Collider == nullptr)
        {
            return l_Hit;
        }

        l_Hit.Entity = l_Collider->ID;
        l_Hit.Body = l_Record->ID;
        l_Hit.Trigger = (l_Body.GetUserData() & c_SensorBit) != 0;

        return l_Hit;
    }

    // A ray of no length, or with no direction, meets nothing. Jolt treats convex shapes as solid, and passes the back faces of triangles. It finds a round shape along the whole ray in single precision, which tens of metres away can be out by a few tenths of a millimetre, so each hit is found again along a short piece of the ray around it, against the part it is on
    std::vector<PhysicsHit3D> PhysicsScene3D::State::CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter, bool all) const
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's physics is queried on the main thread.");

        std::vector<PhysicsHit3D> l_Hits;
        const float l_Length = glm::length(direction);
        if (!(l_Length > 0.0f) || !(maxDistance > 0.0f) || !std::isfinite(l_Length) || !std::isfinite(maxDistance))
        {
            return l_Hits;
        }

        const glm::vec3 l_Direction = direction / l_Length;
        const JPH::RRayCast l_Ray(JPH::RVec3(ToJolt(origin)), ToJolt(l_Direction * maxDistance));
        const QueryLayerFilter l_Layers(filter.Layers);
        const QueryBodyFilter l_Bodies(filter);
        const QueryShapeFilter l_Shapes(System->GetBodyLockInterfaceNoLock(), filter);
        const JPH::NarrowPhaseQuery& l_Query = System->GetNarrowPhaseQueryNoLock();

        const auto a_Add = [&](const JPH::RayCastResult& result)
        {
            PhysicsHit3D l_Hit = MakeHit(result.mBodyID, result.mSubShapeID2);
            if (!l_Hit.Entity)
            {
                return;
            }

            l_Hit.Distance = result.mFraction * maxDistance;
            l_Hit.Normal = -l_Direction;
            if (result.mFraction > 0.0f)
            {
                const JPH::BodyLockRead l_Lock(System->GetBodyLockInterfaceNoLock(), result.mBodyID);
                const JPH::Body& l_Body = l_Lock.GetBody();
                const float l_Margin = c_RefineMargin * (l_Hit.Distance + 1.0f);
                const float l_Start = std::max(l_Hit.Distance - l_Margin, 0.0f);
                JPH::SubShapeID l_Remainder;
                JPH::RayCastResult l_Near;
                if (l_Body.GetTransformedShape().GetSubShapeTransformedShape(result.mSubShapeID2, l_Remainder).CastRay(JPH::RRayCast(JPH::RVec3(ToJolt(origin + l_Direction * l_Start)), ToJolt(l_Direction * (l_Margin * 2.0f))), l_Near))
                {
                    l_Hit.Distance = l_Start + l_Near.mFraction * l_Margin * 2.0f;
                }

                l_Hit.Normal = FromJolt(l_Body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, JPH::RVec3(ToJolt(origin + l_Direction * l_Hit.Distance))));
            }

            l_Hit.Point = origin + l_Direction * l_Hit.Distance;

            l_Hits.push_back(l_Hit);
        };

        if (all)
        {
            JPH::AllHitCollisionCollector<JPH::CastRayCollector> l_Collector;
            l_Query.CastRay(l_Ray, JPH::RayCastSettings(), l_Collector, {}, l_Layers, l_Bodies, l_Shapes);
            for (const JPH::RayCastResult& it_Result : l_Collector.mHits)
            {
                a_Add(it_Result);
            }
        }
        else
        {
            JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> l_Collector;
            l_Query.CastRay(l_Ray, JPH::RayCastSettings(), l_Collector, {}, l_Layers, l_Bodies, l_Shapes);
            if (l_Collector.HadHit())
            {
                a_Add(l_Collector.mHit);
            }
        }

        SortHits(l_Hits);

        return l_Hits;
    }

    // As a ray, with a hit's point and normal where the shape first touched the collider
    std::vector<PhysicsHit3D> PhysicsScene3D::State::CastShape(const PhysicsQueryShape3D& shape, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& direction, float maxDistance, const PhysicsQueryFilter3D& filter, bool all) const
    {
        TR_PROFILE_FUNCTION();
        TR_CORE_ASSERT(MainThread::IsMainThread(), "A scene's physics is queried on the main thread.");

        std::vector<PhysicsHit3D> l_Hits;
        const float l_Length = glm::length(direction);
        if (!(l_Length > 0.0f) || !(maxDistance > 0.0f) || !std::isfinite(l_Length) || !std::isfinite(maxDistance))
        {
            return l_Hits;
        }

        const glm::vec3 l_Direction = direction / l_Length;
        const JPH::RefConst<JPH::Shape> l_Shape = MakeQueryShape(shape);
        const JPH::RShapeCast l_Cast = JPH::RShapeCast::sFromWorldTransform(l_Shape, JPH::Vec3::sOne(), JPH::RMat44::sRotationTranslation(ToJolt(rotation), JPH::RVec3(ToJolt(position))), ToJolt(l_Direction * maxDistance));
        const QueryLayerFilter l_Layers(filter.Layers);
        const QueryBodyFilter l_Bodies(filter);
        const QueryShapeFilter l_Shapes(System->GetBodyLockInterfaceNoLock(), filter);
        const JPH::NarrowPhaseQuery& l_Query = System->GetNarrowPhaseQueryNoLock();

        const auto a_Add = [&](const JPH::ShapeCastResult& result)
        {
            PhysicsHit3D l_Hit = MakeHit(result.mBodyID2, result.mSubShapeID2);
            if (!l_Hit.Entity)
            {
                return;
            }

            l_Hit.Distance = result.mFraction * maxDistance;
            l_Hit.Point = position;
            l_Hit.Normal = -l_Direction;
            if (result.mFraction > 0.0f)
            {
                l_Hit.Point = FromJolt(JPH::Vec3(result.mContactPointOn2));
                l_Hit.Normal = FromJolt(-result.mPenetrationAxis.NormalizedOr(-ToJolt(l_Direction)));
            }

            l_Hits.push_back(l_Hit);
        };

        if (all)
        {
            JPH::AllHitCollisionCollector<JPH::CastShapeCollector> l_Collector;
            l_Query.CastShape(l_Cast, JPH::ShapeCastSettings(), JPH::RVec3::sZero(), l_Collector, {}, l_Layers, l_Bodies, l_Shapes);
            for (const JPH::ShapeCastResult& it_Result : l_Collector.mHits)
            {
                a_Add(it_Result);
            }
        }
        else
        {
            JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> l_Collector;
            l_Query.CastShape(l_Cast, JPH::ShapeCastSettings(), JPH::RVec3::sZero(), l_Collector, {}, l_Layers, l_Bodies, l_Shapes);
            if (l_Collector.HadHit())
            {
                a_Add(l_Collector.mHit);
            }
        }

        SortHits(l_Hits);

        return l_Hits;
    }
}