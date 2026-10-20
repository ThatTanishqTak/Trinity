#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Renderer/ToneMapping.hpp"
#include "Trinity/Scene/ComponentType.hpp"
#include "Trinity/Scripting/ScriptField.hpp"

#include <entt/entity/entity.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

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

        [[nodiscard]] bool operator==(const TagComponent&) const = default;
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

        [[nodiscard]] bool operator==(const TransformComponent&) const = default;
    };

    // Written by Scene::UpdateWorldTransforms, parents first. Every entity has one, and it is never saved
    struct WorldTransformComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.WorldTransform";

        glm::mat4 Matrix{ 1.0f };
    };

    // While a scene runs, a moving body's world position and turn between its last two physics steps, which the transform pass places its entity at in place of its own transform, keeping the entity's world scale. Never saved
    struct PhysicsPoseComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.PhysicsPose";

        glm::vec3 Position{ 0.0f };
        glm::quat Rotation = glm::identity<glm::quat>();
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

    enum class CameraProjection : std::uint8_t
    {
        Orthographic,
        Perspective
    };

    // Looks down its entity's -Z with +Y up. The view is the inverse of the entity's world transform
    struct CameraComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Camera";

        CameraProjection Projection = CameraProjection::Orthographic;
        float OrthographicSize = 10.0f;
        float Near = -1.0f;
        float Far = 1.0f;
        // The vertical field of view in degrees, and the near plane. There is no far plane
        float FieldOfView = 60.0f;
        float PerspectiveNear = 0.1f;
        bool Primary = true;

        // How the image this camera shows is exposed and tone mapped. Scenes saved before these existed load with no curve, as they were drawn then
        float ExposureEV100 = c_NeutralEV100;
        Tonemapper Tonemap = Tonemapper::PBRNeutral;

        [[nodiscard]] ToneMapping GetToneMapping() const { return { ExposureEV100, Tonemap }; }

        // Reversed depth, so near maps to 1 and far to 0. Orthographic: OrthographicSize is the visible height, and near and far are swapped. Perspective: depth falls towards 0 at infinity, which keeps precision evenly spread
        [[nodiscard]] glm::mat4 GetProjection(float aspectRatio) const
        {
            if (Projection == CameraProjection::Perspective)
            {
                const float l_Focal = 1.0f / std::tan(glm::radians(FieldOfView) * 0.5f);
                glm::mat4 l_Projection(0.0f);
                l_Projection[0][0] = l_Focal / aspectRatio;
                l_Projection[1][1] = l_Focal;
                l_Projection[2][3] = -1.0f;
                l_Projection[3][2] = PerspectiveNear;

                return l_Projection;
            }

            const float l_HalfHeight = OrthographicSize * 0.5f;
            const float l_HalfWidth = l_HalfHeight * aspectRatio;

            return glm::ortho(-l_HalfWidth, l_HalfWidth, -l_HalfHeight, l_HalfHeight, Far, Near);
        }

        [[nodiscard]] bool operator==(const CameraComponent&) const = default;
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

        [[nodiscard]] bool operator==(const SpriteRendererComponent&) const = default;
    };

    struct MeshRendererComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.MeshRenderer";

        // A mesh asset's UUID. An invalid one draws nothing
        UUID Mesh;
        // By material slot, which each submesh names. A slot past the end, or holding an invalid UUID, draws with the default material
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scene>> Materials;
        bool CastShadows = true;

        [[nodiscard]] bool operator==(const MeshRendererComponent&) const = default;
    };

    enum class LightType : std::uint8_t
    {
        Directional,
        Point,
        Spot
    };

    // A light in physical units, as glTF's KHR_lights_punctual has them: lux for a directional light, candela for a point or spot light. Directional and spot lights shine along their entity's -Z. Colour is linear
    struct LightComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Light";
        // The illuminance in lux below which a point or spot light with no range of its own stops, on a surface facing it
        static constexpr float c_AutomaticRangeCutoff = 0.01f;

        LightType Type = LightType::Directional;
        glm::vec3 Color{ 1.0f };
        float Intensity = 3.14159265f;
        // Point and spot: the distance at which the light has faded to nothing, or 0 to work it out from the intensity
        float Range = 0.0f;
        // Spot: from the -Z axis to where the light starts to fade and to where it ends, in degrees, as glTF's cone angles are in radians
        float InnerConeAngle = 0.0f;
        float OuterConeAngle = 45.0f;
        // Directional and spot: the first directional light that casts shadows is the sun, whose shadows reach the camera in cascades, and spot lights that do get a shadow map each while there is room. Point lights cast none yet
        bool CastShadows = true;

        // The range lighting and clustering use: the range set, or the distance at which the brightest channel falls to c_AutomaticRangeCutoff
        [[nodiscard]] float GetRange() const
        {
            if (Range > 0.0f)
            {
                return Range;
            }

            const float l_Brightest = std::max(Intensity * std::max({ Color.r, Color.g, Color.b }), 0.0f);

            return std::sqrt(l_Brightest / c_AutomaticRangeCutoff);
        }

        [[nodiscard]] bool operator==(const LightComponent&) const = default;
    };

    // Image-based lighting from an HDR environment, for the whole scene, from the first entity in hierarchy order that has one. Intensity scales its radiance, and Rotation turns it about +Y in degrees as glTF Sample Viewer does, which at 90 puts the middle of the panorama towards +Z
    struct EnvironmentComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Environment";

        UUID Environment;
        float Intensity = 1.0f;
        float Rotation = 90.0f;

        [[nodiscard]] bool operator==(const EnvironmentComponent&) const = default;
    };

    // How an entity's body moves once its scene runs, in metres, kilograms and seconds. The body's shape is the entity's colliders, and an entity with colliders but no rigid body is static. Damping is the fraction of velocity lost each second, and locked axes are in world space
    struct RigidBodyComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.RigidBody";

        BodyMotion Motion = BodyMotion::Dynamic;
        // In kilograms. 0 takes it from the colliders, at the density of water
        float Mass = 1.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.05f;
        float GravityFactor = 1.0f;
        // Sweeps the body along its motion each step, so a fast one cannot pass through a thin one, at some cost
        bool ContinuousCollision = false;
        glm::bvec3 LockPosition{ false };
        glm::bvec3 LockRotation{ false };
        // As the scene starts running: metres per second, and radians per second about each world axis
        glm::vec3 InitialLinearVelocity{ 0.0f };
        glm::vec3 InitialAngularVelocity{ 0.0f };
        bool StartAsleep = false;
        // Whether it may fall asleep once it has stayed within a few centimetres for half a second, which a slow, small swing does at each end, so a swinging lamp or a bobbing buoy should not
        bool CanSleep = true;

        [[nodiscard]] bool operator==(const RigidBodyComponent&) const = default;
    };

    // What every collider has: where it sits on its entity, relative to the entity's transform, how it rubs and bounces, whether it is a trigger, which reports overlaps without pushing, and its collision layer. A collider takes its entity's world scale
    struct ColliderSettings
    {
        glm::vec3 Offset{ 0.0f };
        glm::quat OffsetRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool Trigger = false;
        // Below c_MaxCollisionLayers
        std::uint32_t Layer = 0;

        [[nodiscard]] bool operator==(const ColliderSettings&) const = default;
    };

    struct BoxColliderComponent : ColliderSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.BoxCollider";

        glm::vec3 HalfExtents{ 0.5f };

        [[nodiscard]] bool operator==(const BoxColliderComponent&) const = default;
    };

    struct SphereColliderComponent : ColliderSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.SphereCollider";

        float Radius = 0.5f;

        [[nodiscard]] bool operator==(const SphereColliderComponent&) const = default;
    };

    // Along the collider's Y axis. The height is the whole capsule's, end caps included
    struct CapsuleColliderComponent : ColliderSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.CapsuleCollider";

        float Radius = 0.5f;
        float Height = 2.0f;

        [[nodiscard]] float GetHalfSegment() const { return std::max(Height * 0.5f - Radius, 0.0f); }
        [[nodiscard]] bool operator==(const CapsuleColliderComponent&) const = default;
    };

    // Along the collider's Y axis
    struct CylinderColliderComponent : ColliderSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.CylinderCollider";

        float Radius = 0.5f;
        float Height = 1.0f;

        [[nodiscard]] bool operator==(const CylinderColliderComponent&) const = default;
    };

    // The convex hull a model's import cooked around one of its meshes, a ConvexHullAsset's UUID. In the mesh's own space, so on the entity drawing that mesh it fits it. Any body can have one. An invalid UUID collides with nothing
    struct ConvexHullColliderComponent : ColliderSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.ConvexHullCollider";

        UUID Shape;

        [[nodiscard]] bool operator==(const ConvexHullColliderComponent&) const = default;
    };

    // A model's mesh as its triangles, a CollisionMeshAsset's UUID, in the mesh's own space. Triangles have no inside, so it is for static and kinematic bodies, and a dynamic one takes a convex hull
    struct MeshColliderComponent : ColliderSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.MeshCollider";

        UUID Shape;

        [[nodiscard]] bool operator==(const MeshColliderComponent&) const = default;
    };

    // Moves its entity as a character once its scene runs: a capsule standing along the entity's Y axis that goes where its velocity takes it, climbing steps up to its step height, keeping to the floor going down them, sliding down slopes steeper than its MaxSlope, riding what it stands on and pushing dynamic bodies by up to its push strength. The world's gravity pulls it while it is off the ground, and up is against gravity. An entity with this and a RigidBody is a character, and the RigidBody is left out. Colliders on it and below it, without a RigidBody of their own, follow it as a kinematic body, such as a trigger around it. It takes its entity's world scale as a capsule collider does. In metres, radians and newtons
    struct CharacterControllerComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.CharacterController";

        float Radius = 0.5f;
        // The whole capsule's, end caps included
        float Height = 2.0f;
        // Where the capsule's middle sits on its entity, relative to the entity's transform
        glm::vec3 Offset{ 0.0f };
        // The steepest slope it stands on, held between 1 and 90 degrees
        float MaxSlope = glm::radians(45.0f);
        // How far it lifts itself to climb a step, and as far down as it keeps to the floor, 0 for neither. Its round end cap climbs a little higher by itself, about 0.15 m at the default radius and slope
        float StepHeight = 0.3f;
        // Below c_MaxCollisionLayers
        std::uint32_t Layer = 0;
        // The most force it pushes a dynamic body with, in newtons
        float PushStrength = 100.0f;

        [[nodiscard]] float GetHalfSegment() const { return std::max(Height * 0.5f - Radius, 0.0f); }
        [[nodiscard]] bool operator==(const CharacterControllerComponent&) const = default;
    };

    // How an entity's 2D body moves once its scene runs, in the XY plane: it takes the entity's world X and Y and its turn about Z, and writes them back keeping the entity's depth in Z. Its shape is the entity's 2D colliders and those below it, and an entity with 2D colliders and no 2D rigid body above them is static. 2D and 3D bodies live in worlds of their own, so an entity with a RigidBody keeps that and not this, and one with a CharacterController2D is a character. In metres, kilograms and seconds, damping the fraction of velocity lost each second
    struct RigidBody2DComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.RigidBody2D";

        BodyMotion Motion = BodyMotion::Dynamic;
        // In kilograms. 0 takes it from the colliders, at 1 kg per square metre
        float Mass = 1.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.0f;
        float GravityFactor = 1.0f;
        // Sweeps the body along its motion against every other dynamic body too, not only static ones, so a fast one cannot pass through, at some cost
        bool ContinuousCollision = false;
        // Box2D locks the turn alone, not the slide along X or Y
        bool LockRotation = false;
        // As the scene starts running: metres per second, and radians per second counter-clockwise
        glm::vec2 InitialLinearVelocity{ 0.0f };
        float InitialAngularVelocity = 0.0f;
        bool StartAsleep = false;
        bool CanSleep = true;

        [[nodiscard]] bool operator==(const RigidBody2DComponent&) const = default;
    };

    // What every 2D collider has, as a 3D one does: where it sits on its entity in its XY plane, its turn there counter-clockwise in radians, how it rubs and bounces, whether it is a trigger, whether 2D characters meet it one way, and its collision layer from the project's. A 2D collider takes its entity's world scale in X and Y
    struct Collider2DSettings
    {
        glm::vec2 Offset{ 0.0f };
        float OffsetAngle = 0.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool Trigger = false;
        // A 2D character meets it only from above, along the collider's own Y axis, so it jumps up through it and lands on it, and walks through its sides. Bodies meet it as any other
        bool OneWay = false;
        // Below c_MaxCollisionLayers
        std::uint32_t Layer = 0;

        [[nodiscard]] bool operator==(const Collider2DSettings&) const = default;
    };

    struct BoxCollider2DComponent : Collider2DSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.BoxCollider2D";

        glm::vec2 HalfExtents{ 0.5f };

        [[nodiscard]] bool operator==(const BoxCollider2DComponent&) const = default;
    };

    struct CircleCollider2DComponent : Collider2DSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.CircleCollider2D";

        float Radius = 0.5f;

        [[nodiscard]] bool operator==(const CircleCollider2DComponent&) const = default;
    };

    // Along the collider's Y axis. The height is the whole capsule's, end caps included
    struct CapsuleCollider2DComponent : Collider2DSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.CapsuleCollider2D";

        float Radius = 0.5f;
        float Height = 2.0f;

        [[nodiscard]] float GetHalfSegment() const { return std::max(Height * 0.5f - Radius, 0.0f); }
        [[nodiscard]] bool operator==(const CapsuleCollider2DComponent&) const = default;
    };

    // The convex hull of its points, from 3 to c_MaxPoints, in the collider's own space, with its corners rounded by the radius
    struct PolygonCollider2DComponent : Collider2DSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.PolygonCollider2D";
        // Box2D's most
        static constexpr std::size_t c_MaxPoints = 8;

        std::vector<glm::vec2, TaggedAllocator<glm::vec2, MemoryTag::Scene>> Points{ glm::vec2(0.0f, 0.5f), glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, -0.5f) };
        float Radius = 0.0f;

        [[nodiscard]] bool operator==(const PolygonCollider2DComponent&) const = default;
    };

    // Moves its entity as a 2D character once its scene runs, as a CharacterController does in 3D: a capsule standing along the entity's Y axis in its XY plane that goes where its velocity takes it, climbing steps up to its step height, keeping to the floor going down them, sliding down slopes steeper than its MaxSlope, carried by what it stands on with its feet keeping their place on it while it keeps its own turn, and pushing dynamic 2D bodies by up to its push strength. The 2D world's gravity pulls it while it is off the ground, and up is against gravity. It takes its entity's world X and Y and writes them back keeping the entity's depth in Z. An entity with this and a RigidBody2D is a character, and the RigidBody2D is left out. 2D colliders on it and below it, without a RigidBody2D of their own, follow it, such as a trigger around it. It takes its entity's world scale as a capsule collider does. In metres, radians and newtons
    struct CharacterController2DComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.CharacterController2D";

        float Radius = 0.5f;
        // The whole capsule's, end caps included
        float Height = 2.0f;
        // Where the capsule's middle sits on its entity in its XY plane, relative to the entity's transform
        glm::vec2 Offset{ 0.0f };
        // The steepest slope it stands on, held between 1 and 90 degrees
        float MaxSlope = glm::radians(45.0f);
        // How far it lifts itself to climb a step, and as far down as it keeps to the floor, 0 for neither. Its round end cap climbs a little higher by itself, up to just under 0.15 m at the default radius and slope
        float StepHeight = 0.3f;
        // Below c_MaxCollisionLayers
        std::uint32_t Layer = 0;
        // The most force it pushes a dynamic 2D body with, in newtons
        float PushStrength = 100.0f;

        [[nodiscard]] float GetHalfSegment() const { return std::max(Height * 0.5f - Radius, 0.0f); }
        [[nodiscard]] bool operator==(const CharacterController2DComponent&) const = default;
    };

    // What a joint holds: its bodies welded fast, turning about its axis, sliding along it, kept within a range of distances, or turning every way about a point
    enum class JointType : std::uint8_t
    {
        Fixed,
        Hinge,
        Slider,
        Distance,
        Point
    };

    // Joins the body its entity is part of to another body, or to the world, once the scene runs. Its frame is its entity's transform with the anchor as an offset, and the axis a hinge turns about or a slider slides along is in that frame. Where it meets the other body is taken from where both are as the scene starts, so it holds the pose it was built in, and a hinge's turn and a slider's travel count from there. More joints on one body go on entities below it. In metres, radians, newtons and seconds
    struct JointComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Joint";

        JointType Type = JointType::Hinge;
        // The entity whose body it joins, which may be one of that body's collider entities, or none for the world
        UUID ConnectedBody;
        glm::vec3 Anchor{ 0.0f };
        glm::vec3 Axis{ 0.0f, 1.0f, 0.0f };
        // A distance joint's other end, in the connected entity's own space, or in the world for a joint to the world
        glm::vec3 ConnectedAnchor{ 0.0f };
        // Whether the two bodies still collide with each other
        bool CollideConnected = false;
        // What it takes to break it, the force on it and the torque on it averaged over a step. 0 never breaks it
        float BreakForce = 0.0f;
        float BreakTorque = 0.0f;
        // A hinge's turn in radians, from -pi to 0 and 0 to pi, or a slider's travel in metres, either side of where it started
        bool UseLimits = false;
        float LowerLimit = -0.5f;
        float UpperLimit = 0.5f;
        // A hinge's speed in radians per second, driven by up to the torque, or a slider's in metres per second, by up to the force
        bool UseMotor = false;
        float MotorSpeed = 0.0f;
        float MotorMaxForce = 1000.0f;
        // A distance joint's range, either negative for the distance as the scene starts, and a spring that holds it to the range, by its frequency in hertz, 0 for none, and its damping ratio
        float MinDistance = -1.0f;
        float MaxDistance = -1.0f;
        float SpringFrequency = 0.0f;
        float SpringDamping = 0.0f;

        [[nodiscard]] bool operator==(const JointComponent&) const = default;
    };

    // What a 2D joint holds: its bodies turning about its anchor, sliding along its axis, kept within a range of distances, welded fast, or a wheel turning freely about its anchor on a suspension along its axis
    enum class Joint2DType : std::uint8_t
    {
        Revolute,
        Prismatic,
        Distance,
        Weld,
        Wheel
    };

    // Joins the 2D body its entity is part of to another 2D body, or to the world, once the scene runs, as a 3D Joint does: its frame is its entity's transform in the XY plane with the anchor as an offset, the axis is in that frame, and a revolute joint's turn and a prismatic or wheel joint's travel count from where the bodies are as the scene starts. In metres, radians, newtons and seconds
    struct Joint2DComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Joint2D";

        Joint2DType Type = Joint2DType::Revolute;
        // The entity whose 2D body it joins, which may be one of that body's collider entities, or none for the world
        UUID ConnectedBody;
        glm::vec2 Anchor{ 0.0f };
        // What a prismatic joint slides along and a wheel's suspension travels along
        glm::vec2 Axis{ 0.0f, 1.0f };
        // A distance joint's other end, in the connected entity's own space, or in the world for a joint to the world
        glm::vec2 ConnectedAnchor{ 0.0f };
        // Whether the two bodies still collide with each other
        bool CollideConnected = false;
        // What it takes to break it, the force on it and the torque on it, its motor's left out. 0 never breaks it
        float BreakForce = 0.0f;
        float BreakTorque = 0.0f;
        // A revolute joint's turn in radians, from -pi to 0 and 0 to pi, or a prismatic or wheel joint's travel in metres, either side of where it started
        bool UseLimits = false;
        float LowerLimit = -0.5f;
        float UpperLimit = 0.5f;
        // A revolute or wheel joint's turning speed in radians per second, driven by up to the torque, or a prismatic joint's in metres per second, by up to the force
        bool UseMotor = false;
        float MotorSpeed = 0.0f;
        float MotorMaxForce = 1000.0f;
        // A distance joint's range, either negative for the distance as the scene starts
        float MinDistance = -1.0f;
        float MaxDistance = -1.0f;
        // A spring by its frequency in hertz, 0 for none, and its damping ratio. A distance joint's pulls it towards the middle of its range, and a wheel's holds the wheel along its axis
        float SpringFrequency = 0.0f;
        float SpringDamping = 0.0f;

        [[nodiscard]] bool operator==(const Joint2DComponent&) const = default;
    };

    // Marks an entity that moves, with its subtree, into each scene SceneManager switches to as the scene runs, its scripts keeping their instances, fields and waiting timers and coroutines. A scene that has an entity of the same UUID, as the scene it came from does, gives way to it. It holds nothing
    struct KeepAcrossScenesComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.KeepAcrossScenes";
        // Stored as any other component, a byte an entity, where EnTT would keep nothing for an empty type, so Add and Get return it, a copied entity keeps it and its pool hands out a value
        static constexpr std::size_t page_size = ENTT_PACKED_PAGE;

        [[nodiscard]] bool operator==(const KeepAcrossScenesComponent&) const = default;
    };

    // One script on an entity: the class it is an instance of, by its full name in its language, whether it runs, and its fields' values. The values belong to the scene, not to the instance, so they save, undo and outlive a reload, and a class no loaded code has keeps every one of them
    struct ScriptEntry
    {
        struct Field
        {
            TaggedString<MemoryTag::Scene> Name;
            ScriptFieldValue Value;

            [[nodiscard]] bool operator==(const Field&) const = default;
        };

        TaggedString<MemoryTag::Scene> ClassName;
        ScriptLanguage Language = ScriptLanguage::CSharp;
        bool Enabled = true;
        // In the order the class declares them, or the file had them. Never dropped for a field the class lacks, so a field taken out of a class and put back keeps its value
        std::vector<Field, TaggedAllocator<Field, MemoryTag::Scene>> Fields;

        [[nodiscard]] const Field* FindField(std::string_view name) const
        {
            const auto a_Found = std::ranges::find_if(Fields, [name](const Field& field) { return std::string_view(field.Name) == name; });

            return a_Found != Fields.end() ? &*a_Found : nullptr;
        }

        [[nodiscard]] Field* FindField(std::string_view name)
        {
            return const_cast<Field*>(std::as_const(*this).FindField(name));
        }

        // Replaces the field's value, type and all, or adds it last
        void SetField(std::string_view name, ScriptFieldValue value)
        {
            if (Field* l_Field = FindField(name))
            {
                l_Field->Value = std::move(value);

                return;
            }

            Fields.push_back({ TaggedString<MemoryTag::Scene>(name), std::move(value) });
        }

        [[nodiscard]] bool operator==(const ScriptEntry&) const = default;
    };

    // The scripts on an entity, run in list order, each in C++ or C#. One class can be on an entity more than once
    struct ScriptComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Script";

        std::vector<ScriptEntry, TaggedAllocator<ScriptEntry, MemoryTag::Scene>> Scripts;

        [[nodiscard]] bool operator==(const ScriptComponent&) const = default;
    };

    // Components a scene file named that no loaded code knows, kept as YAML text in file order and written back on save
    struct UnknownComponentsComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.UnknownComponents";

        struct Entry
        {
            TaggedString<MemoryTag::Scene> Name;
            TaggedString<MemoryTag::Scene> Yaml;

            [[nodiscard]] bool operator==(const Entry&) const = default;
        };

        std::vector<Entry, TaggedAllocator<Entry, MemoryTag::Scene>> Entries;
    };
}