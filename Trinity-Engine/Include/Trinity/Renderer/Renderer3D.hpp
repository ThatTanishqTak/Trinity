#pragma once

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/EnvironmentAsset.hpp"
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

    // Where a scene is seen from: the view and the projection, with reversed depth, and the eye, which lighting needs
    struct TRINITY_API RenderView
    {
        glm::mat4 View{ 1.0f };
        glm::mat4 Projection{ 1.0f };
        glm::mat4 ViewProjection{ 1.0f };
        glm::vec3 Position{ 0.0f };
        // The way the view looks, which is the way every eye ray goes for an orthographic view
        glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
        bool Orthographic = false;

        // Through a camera on an entity with this world transform, for a target of this aspect ratio
        [[nodiscard]] static RenderView FromCamera(const CameraComponent& camera, const glm::mat4& world, float aspectRatio);
        // The eye and the way it looks are those of the view's inverse
        [[nodiscard]] static RenderView FromMatrices(const glm::mat4& view, const glm::mat4& projection, bool orthographic);
    };

    // The clusters point and spot lights are sorted into: a fixed grid of screen tiles, each cut into slices of view depth, logarithmically spaced for a perspective view and evenly for an orthographic one, between depths that hold every light's reach. Lighting.slang holds the same sizes
    struct TRINITY_API ClusterGrid
    {
        static constexpr std::uint32_t c_TilesX = 16;
        static constexpr std::uint32_t c_TilesY = 9;
        static constexpr std::uint32_t c_Slices = 24;
        static constexpr std::uint32_t c_Count = c_TilesX * c_TilesY * c_Slices;
        // A cluster keeps the first lights it is reached by up to this many, in light order, and counts the rest
        static constexpr std::uint32_t c_MaxLights = 128;

        // View depths, the distance in front of the eye, between which the slices lie
        float Near = 0.1f;
        float Far = 1.0f;
        // Perspective, sliced logarithmically, or orthographic, sliced evenly
        bool Perspective = true;
        // The projection's x and y scale, and for an orthographic one its x and y offset, which is all a symmetric perspective or an orthographic projection needs to map a tile back into view space
        glm::vec2 Scale{ 1.0f };
        glm::vec2 Offset{ 0.0f };

        [[nodiscard]] static constexpr std::uint32_t GetIndex(std::uint32_t x, std::uint32_t y, std::uint32_t slice) { return x + c_TilesX * (y + c_TilesY * slice); }

        // Where a slice begins, slice c_Slices being where the last ends
        [[nodiscard]] float GetSliceDepth(std::uint32_t slice) const;
        // A cluster's box in view space, where the view looks down -Z
        void GetBounds(std::uint32_t x, std::uint32_t y, std::uint32_t slice, glm::vec3& minimum, glm::vec3& maximum) const;
    };

    // From a point to a box, 0 inside it, which against a light's squared radius decides whether the light reaches a cluster, in the cluster pass and its reference alike
    [[nodiscard]] TRINITY_API float GetSquaredDistance(const glm::vec3& point, const glm::vec3& minimum, const glm::vec3& maximum);

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

    // Towards the light, and its colour times its illuminance in lux
    struct DirectionalLight
    {
        glm::vec3 Direction{ 0.0f, 1.0f, 0.0f };
        glm::vec3 Radiance{ 0.0f };
    };

    // A point or spot light as the shaders read it, in world space: where it is and its range, the way it shines and its colour times its intensity in candela, with the cone terms glTF gives, which leave a point light at full strength every way
    struct PunctualLight
    {
        glm::vec3 Position{ 0.0f };
        float Range = 0.0f;
        glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };
        float ConeScale = 0.0f;
        glm::vec3 Radiance{ 0.0f };
        float ConeOffset = 1.0f;
    };

    static_assert(sizeof(PunctualLight) == 48);

    // What a view of a scene draws, in the order it draws it, and the lights it is lit by
    struct TRINITY_API SceneDrawList
    {
        static constexpr std::uint32_t c_MaxDirectionalLights = 4;
        static constexpr std::uint32_t c_MaxPunctualLights = 4096;

        RenderView View;
        std::vector<MeshDraw, TaggedAllocator<MeshDraw, MemoryTag::Renderer>> Draws;
        std::array<DirectionalLight, c_MaxDirectionalLights> Directional{};
        std::uint32_t DirectionalCount = 0;
        // Point and spot lights in hierarchy order, and for each the sphere in view space that holds all it lights, which is what clusters test
        std::vector<PunctualLight, TaggedAllocator<PunctualLight, MemoryTag::Renderer>> Lights;
        std::vector<glm::vec4, TaggedAllocator<glm::vec4, MemoryTag::Renderer>> LightBounds;
        ClusterGrid Clusters;
        // The scene's environment, from its first Environment component, once it has loaded, its intensity, and its turn about +Y in degrees
        UUID EnvironmentID;
        const EnvironmentAsset* Environment = nullptr;
        float EnvironmentIntensity = 1.0f;
        float EnvironmentRotation = 90.0f;
        // Whether the scene has an Environment component, which stands in for the ambient light even while its environment loads
        bool HasEnvironment = false;
        // Lit by the default sun, as a scene with no lights and no environment is
        bool DefaultSun = false;
        // Lights past c_MaxDirectionalLights or c_MaxPunctualLights, which light nothing
        std::uint32_t DroppedLights = 0;
        std::uint32_t Submeshes = 0;
        std::uint32_t Culled = 0;
        // Mesh renderers whose mesh is still loading, which draw nothing until it is ready
        std::uint32_t Pending = 0;

        void Clear();
    };

    // How one view of a scene is shaded
    struct SceneOptions
    {
        // Every pixel loops over every point and spot light instead of its cluster's, which clustering is checked against
        bool ShadeAllLights = false;
        // Each pixel shows how many lights its cluster holds, from blue for none to red at the cap and magenta past it
        bool LightHeatmap = false;
    };

    // Draws a scene's MeshRenderers: collected after the transform pass, culled against the view's frustum on the job system, sorted by pipeline, material and mesh, then drawn in a depth pre-pass and an opaque pass that shades each pixel once, with glTF's metallic-roughness BRDF lit by the scene's environment, up to four directional lights and the point and spot lights of the cluster the pixel lies in
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
        // The light a scene with no lights at all is lit by, so a model dropped into a new scene shows, which is white at an illuminance that lights a white surface facing it to 1
        static constexpr float c_DefaultSunIntensity = 3.14159265f;
        // In a scene with no environment, every surface also takes this fraction of the directional lights' light from all around
        static constexpr float c_AmbientFraction = 0.03f;

        // What AddPasses leaves in the graph: the depth, and each cluster's light count then its lights, c_MaxLights to a cluster. The buffers are invalid when no clusters were built
        struct Passes
        {
            FrameGraphTexture Depth;
            FrameGraphBuffer ClusterCounts;
            FrameGraphBuffer ClusterLights;
        };

        struct Statistics
        {
            std::uint32_t Submeshes = 0;
            std::uint32_t Visible = 0;
            std::uint32_t Pending = 0;
            std::uint32_t PrePassDraws = 0;
            std::uint32_t OpaqueDraws = 0;
            std::uint32_t PipelineChanges = 0;
            // From the first view drawn in a frame, read back c_FramesInFlight frames later
            std::uint32_t Lights = 0;
            std::uint32_t MostLightsInCluster = 0;
            std::uint32_t OverfullClusters = 0;
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
        // Each cluster's light count, which may pass c_MaxLights, then its first c_MaxLights lights, as the cluster pass sorts them, for checking it against
        static void BuildClustersReference(const SceneDrawList& list, std::vector<std::uint32_t>& counts, std::vector<std::uint32_t>& lights);

        // A depth pre-pass into a depth texture the size of the target, then, when there are point or spot lights, a compute pass that sorts them into clusters, then the opaque pass, which clears the target and draws again with an equal depth test. The list must last until the graph has run
        Passes AddPasses(FrameGraph& graph, const SceneDrawList& list, FrameGraphTexture target, const RHI::TextureDescription& targetDescription, const std::array<float, 4>& clearColor, const SceneOptions& options = {});

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
        using EnvironmentCache = std::unordered_map<UUID, Cached<EnvironmentAsset>, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, Cached<EnvironmentAsset>>, MemoryTag::Renderer>>;

        struct PipelineEntry
        {
            RHI::Format Format = RHI::Format::Unknown;
            PipelineKind Kind = PipelineKind::Front;
            bool DepthOnly = false;
            RHI::PipelineHandle Pipeline;
        };

        [[nodiscard]] const MeshAsset* ResolveMesh(UUID id);
        [[nodiscard]] const MaterialAsset* ResolveMaterial(UUID id);
        [[nodiscard]] const EnvironmentAsset* ResolveEnvironment(UUID id);
        // Where this view's lights and clusters are for the opaque pass, and how it shades
        struct LightInputs
        {
            std::uint32_t Lights = RHI::c_NoBindlessIndex;
            std::uint32_t LightsOffset = 0;
            std::uint32_t ClusterCounts = RHI::c_NoBindlessIndex;
            std::uint32_t ClusterLights = RHI::c_NoBindlessIndex;
            std::uint32_t Flags = 0;
        };

        [[nodiscard]] RHI::PipelineHandle GetPipeline(RHI::Format colorFormat, PipelineKind kind, bool depthOnly);
        void RecordDraws(RHI::CommandList& commands, const SceneDrawList& list, RHI::Format colorFormat, bool depthOnly, std::uint32_t width, std::uint32_t height, const LightInputs& lights);
        void ReadClusterStatistics();

        RHI::Device& m_Device;
        const MaterialLoader& m_Materials;
        FileBuffer m_VertexShader;
        FileBuffer m_PixelShader;
        FileBuffer m_DepthPixelShader;
        RHI::PipelineHandle m_ClusterPipeline;
        std::array<RHI::BufferHandle, RHI::c_FramesInFlight> m_ClusterReadbacks{};
        std::array<bool, RHI::c_FramesInFlight> m_ClusterReadbackWritten{};
        std::uint64_t m_ClusterReadbackFrame = 0;
        RHI::SamplerHandle m_LinearSampler;
        RHI::SamplerHandle m_NearestSampler;
        // For the BRDF lookup table, whose edges must not wrap into each other
        RHI::SamplerHandle m_ClampSampler;
        std::vector<PipelineEntry, TaggedAllocator<PipelineEntry, MemoryTag::Renderer>> m_Pipelines;
        MeshCache m_Meshes;
        MaterialCache m_MaterialCache;
        EnvironmentCache m_Environments;
        std::vector<MeshDraw, TaggedAllocator<MeshDraw, MemoryTag::Renderer>> m_Candidates;
        std::vector<std::uint8_t, TaggedAllocator<std::uint8_t, MemoryTag::Renderer>> m_Visible;
        Statistics m_Statistics;
        std::uint64_t m_Frame = 0;
        bool m_ReportedNoShaders = false;
    };
}