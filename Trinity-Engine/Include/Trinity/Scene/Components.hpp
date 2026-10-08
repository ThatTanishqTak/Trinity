#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Renderer/ToneMapping.hpp"
#include "Trinity/Scene/ComponentType.hpp"

#include <entt/entity/entity.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
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