#pragma once

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/MaterialAsset.hpp"
#include "Trinity/Asset/MeshAsset.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Renderer/FrameGraph.hpp"
#include "Trinity/RHI/Device.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Trinity
{
    class MaterialLoader;
    class Scene;
    struct CameraComponent;

    // Where a scene is seen from: the view-projection, with reversed depth, and the eye, which lighting needs
    struct TRINITY_API RenderView
    {
        glm::mat4 ViewProjection{ 1.0f };
        glm::vec3 Position{ 0.0f };
        // The way the view looks, which is the way every eye ray goes for an orthographic view
        glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
        bool Orthographic = false;

        // Through a camera on an entity with this world transform, for a target of this aspect ratio
        [[nodiscard]] static RenderView FromCamera(const CameraComponent& camera, const glm::mat4& world, float aspectRatio);
    };

    // A view's six planes, a point p being inside one where dot(plane.xyz, p) + plane.w >= 0. A view without a far plane gets one every point is inside
    struct TRINITY_API Frustum
    {
        std::array<glm::vec4, 6> Planes{};

        [[nodiscard]] static Frustum FromViewProjection(const glm::mat4& viewProjection);
        // A box, by its centre and half extents, which is outside only when it lies wholly outside one plane, so a box that is kept may still be just out of view
        [[nodiscard]] bool Intersects(const glm::vec3& center, const glm::vec3& extents) const;
    };

    // The box in world space that holds the local box under the transform, by its centre and half extents
    TRINITY_API void GetWorldBounds(const MeshBounds& local, const glm::mat4& world, glm::vec3& center, glm::vec3& extents);

    // One submesh to draw, with the pipeline and the material table record it is drawn with
    struct MeshDraw
    {
        UUID Entity;
        const MeshAsset* Mesh = nullptr;
        std::uint32_t Submesh = 0;
        std::uint32_t Material = 0;
        std::uint32_t Pipeline = 0;
        glm::mat4 World{ 1.0f };
        std::uint64_t Key = 0;

        [[nodiscard]] bool operator==(const MeshDraw&) const = default;
    };

    // What a view of a scene draws, in the order it draws it, and the light it is lit by
    struct TRINITY_API SceneDrawList
    {
        RenderView View;
        std::vector<MeshDraw, TaggedAllocator<MeshDraw, MemoryTag::Renderer>> Draws;
        // Towards the light, and its colour times its illuminance in lux
        glm::vec3 SunDirection{ 0.0f, 1.0f, 0.0f };
        glm::vec3 SunRadiance{ 0.0f };
        bool DefaultSun = false;
        std::uint32_t Submeshes = 0;
        std::uint32_t Culled = 0;
        // Mesh renderers whose mesh is still loading, which draw nothing until it is ready
        std::uint32_t Pending = 0;

        void Clear();
    };

    // Draws a scene's MeshRenderers: collected after the transform pass, culled against the view's frustum on the job system, sorted by pipeline, material and mesh, then drawn in a depth pre-pass and an opaque pass that shades each pixel once, with the GGX BRDF lit by one directional light
    class TRINITY_API Renderer3D
    {
    public:
        // Which way a submesh's triangles face, which picks its pipeline: counter-clockwise as glTF has them, clockwise under a mirroring transform, or both ways for a double-sided material
        enum class PipelineKind : std::uint32_t
        {
            Front,
            Mirrored,
            DoubleSided,

            Count
        };

        static constexpr RHI::Format c_DepthFormat = RHI::Format::D32Float;
        // Reversed depth: the pre-pass keeps the nearest surface, and the opaque pass then draws only where it matches it exactly
        static constexpr RHI::CompareOp c_PrePassCompare = RHI::CompareOp::GreaterOrEqual;
        static constexpr RHI::CompareOp c_OpaqueCompare = RHI::CompareOp::Equal;
        // The light a scene with no directional light is lit by, so a model dropped into a new scene shows, which is white at an illuminance that lights a white surface facing it to 1
        static constexpr float c_DefaultSunIntensity = 3.14159265f;
        // Until image-based lighting arrives, every surface also takes this fraction of the sun's light from all around
        static constexpr float c_AmbientFraction = 0.03f;

        struct Statistics
        {
            std::uint32_t Submeshes = 0;
            std::uint32_t Visible = 0;
            std::uint32_t Pending = 0;
            std::uint32_t PrePassDraws = 0;
            std::uint32_t OpaqueDraws = 0;
            std::uint32_t PipelineChanges = 0;
        };

        Renderer3D(RHI::Device& device, const MaterialLoader& materials);
        ~Renderer3D();

        Renderer3D(const Renderer3D&) = delete;
        Renderer3D& operator=(const Renderer3D&) = delete;

        void BeginFrame();
        void ReleaseAssets();

        // On the main thread, after the transform pass. Meshes and materials are kept loaded while views use them, and a submesh is drawn once its mesh is ready
        void Collect(Scene& scene, const RenderView& view, SceneDrawList& list);
        // Every submesh tested in turn on this thread, for checking Collect against. It reads only meshes Collect has already loaded
        static void CollectReference(Scene& scene, const RenderView& view, SceneDrawList& list);

        // A depth pre-pass into a depth texture the size of the target, then the opaque pass, which clears the target and draws again with an equal depth test. The list must last until the graph has run. Returns the depth texture
        FrameGraphTexture AddPasses(FrameGraph& graph, const SceneDrawList& list, FrameGraphTexture target, const RHI::TextureDescription& targetDescription, const std::array<float, 4>& clearColor);

        [[nodiscard]] const Statistics& GetStatistics() const { return m_Statistics; }

    private:
        template<typename T>
        struct Cached
        {
            AssetRef<T> Asset;
            std::uint64_t LastUsedFrame = 0;
        };

        using MeshCache = std::unordered_map<UUID, Cached<MeshAsset>, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, Cached<MeshAsset>>, MemoryTag::Renderer>>;
        using MaterialCache = std::unordered_map<UUID, Cached<MaterialAsset>, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, Cached<MaterialAsset>>, MemoryTag::Renderer>>;

        struct PipelineEntry
        {
            RHI::Format Format = RHI::Format::Unknown;
            PipelineKind Kind = PipelineKind::Front;
            bool DepthOnly = false;
            RHI::PipelineHandle Pipeline;
        };

        [[nodiscard]] const MeshAsset* ResolveMesh(UUID id);
        [[nodiscard]] const MaterialAsset* ResolveMaterial(UUID id);
        [[nodiscard]] RHI::PipelineHandle GetPipeline(RHI::Format colorFormat, PipelineKind kind, bool depthOnly);
        void RecordDraws(RHI::CommandList& commands, const SceneDrawList& list, RHI::Format colorFormat, bool depthOnly, std::uint32_t width, std::uint32_t height);

        RHI::Device& m_Device;
        const MaterialLoader& m_Materials;
        FileBuffer m_VertexShader;
        FileBuffer m_PixelShader;
        FileBuffer m_DepthPixelShader;
        RHI::SamplerHandle m_LinearSampler;
        RHI::SamplerHandle m_NearestSampler;
        std::vector<PipelineEntry, TaggedAllocator<PipelineEntry, MemoryTag::Renderer>> m_Pipelines;
        MeshCache m_Meshes;
        MaterialCache m_MaterialCache;
        std::vector<MeshDraw, TaggedAllocator<MeshDraw, MemoryTag::Renderer>> m_Candidates;
        std::vector<std::uint8_t, TaggedAllocator<std::uint8_t, MemoryTag::Renderer>> m_Visible;
        Statistics m_Statistics;
        std::uint64_t m_Frame = 0;
        bool m_ReportedNoShaders = false;
    };
}