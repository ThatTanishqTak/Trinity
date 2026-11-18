#pragma once

#include "Trinity/Audio/AudioSettings.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Renderer/ToneMapping.hpp"
#include "Trinity/Scene/ComponentType.hpp"
#include "Trinity/Scripting/ScriptField.hpp"
#include "Trinity/UI/UIPanelSettings.hpp"
#include "Trinity/UI/UIWorldSettings.hpp"

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

        // Shear, which a non-uniformly scaled parent and a rotated child can produce, is dropped
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

    // Written by Scene::UpdateWorldTransforms, parents first. Every entity has one. Never saved
    struct WorldTransformComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.WorldTransform";

        glm::mat4 Matrix{ 1.0f };
    };

    // A moving body's world pose between its last two physics steps, while a scene runs. Never saved
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

        // Scenes saved before these existed load with no curve, as they were drawn then
        float ExposureEV100 = c_NeutralEV100;
        Tonemapper Tonemap = Tonemapper::PBRNeutral;

        [[nodiscard]] ToneMapping GetToneMapping() const { return { ExposureEV100, Tonemap }; }

        // Reversed depth: near maps to 1, and a perspective view's depth falls towards 0 at infinity
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
        // By material slot. A slot past the end, or an invalid UUID, draws with the default material
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

    // As KHR_lights_punctual: lux for a directional light, candela for point and spot. Shines along -Z
    struct LightComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Light";
        // The illuminance in lux at which a point or spot light with no range of its own stops
        static constexpr float c_AutomaticRangeCutoff = 0.01f;

        LightType Type = LightType::Directional;
        glm::vec3 Color{ 1.0f };
        float Intensity = 3.14159265f;
        // Point and spot: where the light has faded to nothing, or 0 to work it out from the intensity
        float Range = 0.0f;
        // Spot: degrees from the -Z axis to where the light starts to fade, and to where it ends
        float InnerConeAngle = 0.0f;
        float OuterConeAngle = 45.0f;
        // The first directional light that casts shadows is the sun. Point lights cast none yet
        bool CastShadows = true;

        // The range set, or the distance at which the brightest channel falls to c_AutomaticRangeCutoff
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

    // From the first entity in hierarchy order that has one. Rotation is about +Y in degrees
    struct EnvironmentComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Environment";

        UUID Environment;
        float Intensity = 1.0f;
        float Rotation = 90.0f;

        [[nodiscard]] bool operator==(const EnvironmentComponent&) const = default;
    };

    // In metres, kilograms and seconds. An entity with colliders but no rigid body is static
    struct RigidBodyComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.RigidBody";

        BodyMotion Motion = BodyMotion::Dynamic;
        // In kilograms. 0 takes it from the colliders, at the density of water
        float Mass = 1.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.05f;
        float GravityFactor = 1.0f;
        // Sweeps the body along its motion each step, so a fast one cannot pass through a thin one
        bool ContinuousCollision = false;
        glm::bvec3 LockPosition{ false };
        glm::bvec3 LockRotation{ false };
        // As the scene starts running: metres per second, and radians per second about each world axis
        glm::vec3 InitialLinearVelocity{ 0.0f };
        glm::vec3 InitialAngularVelocity{ 0.0f };
        bool StartAsleep = false;
        // Sleeps after half a second within a few centimetres, which a swinging lamp should not
        bool CanSleep = true;

        [[nodiscard]] bool operator==(const RigidBodyComponent&) const = default;
    };

    // Offsets are relative to the entity's transform. A collider takes its entity's world scale
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

    // A ConvexHullAsset's UUID, in the mesh's own space. An invalid UUID collides with nothing
    struct ConvexHullColliderComponent : ColliderSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.ConvexHullCollider";

        UUID Shape;

        [[nodiscard]] bool operator==(const ConvexHullColliderComponent&) const = default;
    };

    // A CollisionMeshAsset's UUID. Triangles have no inside, so for static and kinematic bodies only
    struct MeshColliderComponent : ColliderSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.MeshCollider";

        UUID Shape;

        [[nodiscard]] bool operator==(const MeshColliderComponent&) const = default;
    };

    // A capsule along the entity's Y axis that climbs steps and slides down slopes past MaxSlope
    struct CharacterControllerComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.CharacterController";

        CharacterMode Mode = CharacterMode::Kinematic;
        float Radius = 0.5f;
        // The whole capsule's, end caps included
        float Height = 2.0f;
        // Where the capsule's middle sits on its entity, relative to the entity's transform
        glm::vec3 Offset{ 0.0f };
        // The steepest slope it stands on, held between 1 and 90 degrees
        float MaxSlope = glm::radians(45.0f);
        // How far it lifts itself to climb a step, and how far down it keeps to the floor. 0 for neither
        float StepHeight = 0.3f;
        // Below c_MaxCollisionLayers
        std::uint32_t Layer = 0;
        // The most force a kinematic one pushes a dynamic body with, in newtons
        float PushStrength = 100.0f;
        // Its weight on what it stands on, and a physical one's mass, in kilograms, at least 0.1
        float Mass = 70.0f;

        [[nodiscard]] float GetHalfSegment() const { return std::max(Height * 0.5f - Radius, 0.0f); }
        [[nodiscard]] bool operator==(const CharacterControllerComponent&) const = default;
    };

    // In the XY plane, keeping the entity's depth in Z. 2D and 3D bodies live in worlds of their own
    struct RigidBody2DComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.RigidBody2D";

        BodyMotion Motion = BodyMotion::Dynamic;
        // In kilograms. 0 takes it from the colliders, at 1 kg per square metre
        float Mass = 1.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.0f;
        float GravityFactor = 1.0f;
        // Sweeps the body against every other dynamic body too, not only static ones
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

    // As a 3D collider's, in the entity's XY plane, with its turn counter-clockwise in radians
    struct Collider2DSettings
    {
        glm::vec2 Offset{ 0.0f };
        float OffsetAngle = 0.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool Trigger = false;
        // A 2D character meets it only from above, along the collider's Y axis. Bodies meet it as usual
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

    // The convex hull of 3 to c_MaxPoints points, with its corners rounded by the radius
    struct PolygonCollider2DComponent : Collider2DSettings
    {
        static constexpr std::string_view c_TypeName = "Trinity.PolygonCollider2D";
        // Box2D's most
        static constexpr std::size_t c_MaxPoints = 8;

        std::vector<glm::vec2, TaggedAllocator<glm::vec2, MemoryTag::Scene>> Points{ glm::vec2(0.0f, 0.5f), glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, -0.5f) };
        float Radius = 0.0f;

        [[nodiscard]] bool operator==(const PolygonCollider2DComponent&) const = default;
    };

    // As a CharacterController in 3D, in the entity's XY plane
    struct CharacterController2DComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.CharacterController2D";

        CharacterMode Mode = CharacterMode::Kinematic;
        float Radius = 0.5f;
        // The whole capsule's, end caps included
        float Height = 2.0f;
        // Where the capsule's middle sits in the entity's XY plane, relative to its transform
        glm::vec2 Offset{ 0.0f };
        // The steepest slope it stands on, held between 1 and 90 degrees
        float MaxSlope = glm::radians(45.0f);
        // How far it lifts itself to climb a step, and how far down it keeps to the floor. 0 for neither
        float StepHeight = 0.3f;
        // Below c_MaxCollisionLayers
        std::uint32_t Layer = 0;
        // The most force a kinematic one pushes a dynamic 2D body with, in newtons
        float PushStrength = 100.0f;
        // Its weight on what it stands on, and a physical one's mass, in kilograms, at least 0.1
        float Mass = 70.0f;

        [[nodiscard]] float GetHalfSegment() const { return std::max(Height * 0.5f - Radius, 0.0f); }
        [[nodiscard]] bool operator==(const CharacterController2DComponent&) const = default;
    };

    // Welded fast, turning about an axis, sliding along it, kept within a distance, or turning freely
    enum class JointType : std::uint8_t
    {
        Fixed,
        Hinge,
        Slider,
        Distance,
        Point
    };

    // Its frame is the entity's transform plus the anchor. It holds the pose the scene starts in
    struct JointComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Joint";

        JointType Type = JointType::Hinge;
        // May be one of the body's collider entities. None joins the world
        UUID ConnectedBody;
        glm::vec3 Anchor{ 0.0f };
        glm::vec3 Axis{ 0.0f, 1.0f, 0.0f };
        // A distance joint's other end, in the connected entity's space, or in the world with none
        glm::vec3 ConnectedAnchor{ 0.0f };
        // Whether the two bodies still collide with each other
        bool CollideConnected = false;
        // The force and torque on it, averaged over a step, that break it. 0 never breaks
        float BreakForce = 0.0f;
        float BreakTorque = 0.0f;
        // A hinge's turn in radians within -pi to pi, or a slider's travel in metres, from its start
        bool UseLimits = false;
        float LowerLimit = -0.5f;
        float UpperLimit = 0.5f;
        // A hinge's speed in radians per second, or a slider's in metres per second, up to the limit
        bool UseMotor = false;
        float MotorSpeed = 0.0f;
        float MotorMaxForce = 1000.0f;
        // Negative takes the distance as the scene starts. A spring frequency of 0 is no spring
        float MinDistance = -1.0f;
        float MaxDistance = -1.0f;
        float SpringFrequency = 0.0f;
        float SpringDamping = 0.0f;

        [[nodiscard]] bool operator==(const JointComponent&) const = default;
    };

    // Turning about an anchor, sliding along an axis, kept within a distance, welded, or a wheel
    enum class Joint2DType : std::uint8_t
    {
        Revolute,
        Prismatic,
        Distance,
        Weld,
        Wheel
    };

    // As a 3D Joint, in the XY plane. In metres, radians, newtons and seconds
    struct Joint2DComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Joint2D";

        Joint2DType Type = Joint2DType::Revolute;
        // May be one of the body's collider entities. None joins the world
        UUID ConnectedBody;
        glm::vec2 Anchor{ 0.0f };
        // What a prismatic joint slides along and a wheel's suspension travels along
        glm::vec2 Axis{ 0.0f, 1.0f };
        // A distance joint's other end, in the connected entity's space, or in the world with none
        glm::vec2 ConnectedAnchor{ 0.0f };
        // Whether the two bodies still collide with each other
        bool CollideConnected = false;
        // The force and torque on it that break it, its motor's left out. 0 never breaks
        float BreakForce = 0.0f;
        float BreakTorque = 0.0f;
        // A revolute joint's turn in radians within -pi to pi, or a prismatic or wheel's travel in metres
        bool UseLimits = false;
        float LowerLimit = -0.5f;
        float UpperLimit = 0.5f;
        // Radians per second for a revolute or wheel joint, metres per second for a prismatic one
        bool UseMotor = false;
        float MotorSpeed = 0.0f;
        float MotorMaxForce = 1000.0f;
        // Negative takes the distance as the scene starts
        float MinDistance = -1.0f;
        float MaxDistance = -1.0f;
        // A frequency of 0 is no spring. A wheel's spring holds it along its axis
        float SpringFrequency = 0.0f;
        float SpringDamping = 0.0f;

        [[nodiscard]] bool operator==(const Joint2DComponent&) const = default;
    };

    // As Unity's AudioSource. Only the project's real voices are mixed, the most important first
    struct AudioSourceComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.AudioSource";
        static constexpr std::uint32_t c_LowestPriority = 256;

        // An AudioClip or a Sound Event, as Unity 6's AudioResource is either
        UUID Resource;
        // A mixer group's ID. None, or a group the mixer lacks, plays into Master
        UUID Output;
        // Plays on, silent, keeping its time
        bool Mute = false;
        // Plays as the scene starts, or as the source first appears while it runs
        bool PlayOnAwake = true;
        bool Loop = false;
        // From 0, the most important, to 256, the least, as in Unity
        std::uint32_t Priority = 128;
        // From 0 to 1
        float Volume = 1.0f;
        // Playback speed, which changes pitch as a tape's does, above 0 and up to 3
        float Pitch = 1.0f;
        // From -1, full left, to 1, full right
        float StereoPan = 0.0f;
        // Heard from its entity in 3D rather than as it is
        bool Spatialize = false;
        // How much movement shifts its pitch, from 0, none, to 5
        float DopplerLevel = 1.0f;
        // In degrees: 0 pans it fully towards where it is, and 180 or more leaves it centred
        float Spread = 0.0f;
        // In metres: full volume inside the minimum, and no further falloff beyond the maximum
        float MinDistance = 1.0f;
        float MaxDistance = 500.0f;
        AudioRolloff Rolloff = AudioRolloff::Logarithmic;
        // Around its -Z, in degrees: full volume inside the inner angle. 360 is no cone
        float ConeInnerAngle = 360.0f;
        float ConeOuterAngle = 360.0f;
        float ConeOuterVolume = 0.0f;
        // Muffled while physics stands between it and the listener. Only a spatialized source is occluded
        bool Occlusion = false;
        float OcclusionVolume = 0.5f;
        float OcclusionCutoff = 1500.0f;

        [[nodiscard]] bool operator==(const AudioSourceComponent&) const = default;
    };

    // The first in hierarchy order is heard through, and a second gives a warning. It holds nothing
    struct AudioListenerComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.AudioListener";
        // Stored as any other component, a byte an entity, as KeepAcrossScenes is
        static constexpr std::size_t page_size = ENTT_PACKED_PAGE;

        [[nodiscard]] bool operator==(const AudioListenerComponent&) const = default;
    };

    // Moves, with its subtree, into each scene SceneManager switches to. It holds nothing
    struct KeepAcrossScenesComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.KeepAcrossScenes";
        // One byte an entity, where EnTT would keep nothing for an empty type, so Add and Get return it
        static constexpr std::size_t page_size = ENTT_PACKED_PAGE;

        [[nodiscard]] bool operator==(const KeepAcrossScenesComponent&) const = default;
    };

    // As Unity's UIDocument: over the game view by sort order, or in World mode on a quad in the scene
    struct UIDocumentComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.UIDocument";

        // A UIDocument asset, an .rml file. Without one it shows nothing
        UUID Document;
        // Over screen documents of a lower order. In World mode, its order in its sorting layer
        std::int32_t SortOrder = 0;
        bool Visible = true;
        // The mixer group its elements' sounds play into. None plays a clip into Master
        UUID SoundOutput;
        UIPanelSettings Panel;
        UIRenderMode RenderMode = UIRenderMode::Screen;
        UIWorldSettings World;

        [[nodiscard]] bool operator==(const UIDocumentComponent&) const = default;
    };

    // The values belong to the scene, not the instance, so they save, undo and outlive a reload
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
        // Never dropped for a field the class lacks, so a field taken out and put back keeps its value
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

    // Run in list order, each in C++ or C#. One class can be on an entity more than once
    struct ScriptComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.Script";

        std::vector<ScriptEntry, TaggedAllocator<ScriptEntry, MemoryTag::Scene>> Scripts;

        [[nodiscard]] bool operator==(const ScriptComponent&) const = default;
    };

    // Saved as the prefab and what the instance changes, never as a component
    struct PrefabInstanceComponent
    {
        static constexpr std::string_view c_TypeName = "Trinity.PrefabInstance";

        UUID Prefab;
        UUID Instance;
        TaggedString<MemoryTag::Scene> Kept;

        [[nodiscard]] bool IsMissing() const { return !Kept.empty(); }

        [[nodiscard]] bool operator==(const PrefabInstanceComponent&) const = default;
    };

    // Components a scene file named that no loaded code knows, kept as YAML and written back on save
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