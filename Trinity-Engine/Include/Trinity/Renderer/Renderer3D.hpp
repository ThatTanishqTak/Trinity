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
#include "Trinity/Renderer/PickID.hpp"
#include "Trinity/Renderer/WorldUI.hpp"
#include "Trinity/RHI/Device.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Trinity
{
    class Entity;
    class MaterialLoader;
    class Scene;
    struct CameraComponent;

    // The projection has reversed depth. Lighting needs the eye
    struct TRINITY_API RenderView
    {
        glm::mat4 View{ 1.0f };
        glm::mat4 Projection{ 1.0f };
        glm::mat4 ViewProjection{ 1.0f };
        glm::vec3 Position{ 0.0f };
        // Also the way every eye ray goes for an orthographic view
        glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
        bool Orthographic = false;

        // Through a camera on an entity with this world transform, for a target of this aspect ratio
        [[nodiscard]] static RenderView FromCamera(const CameraComponent& camera, const glm::mat4& world, float aspectRatio);
        // The eye and the way it looks are those of the view's inverse
        [[nodiscard]] static RenderView FromMatrices(const glm::mat4& view, const glm::mat4& projection, bool orthographic);
    };

    // A fixed grid of screen tiles cut into depth slices. Lighting.slang holds the same sizes
    struct TRINITY_API ClusterGrid
    {
        static constexpr std::uint32_t c_TilesX = 16;
        static constexpr std::uint32_t c_TilesY = 9;
        static constexpr std::uint32_t c_Slices = 24;
        static constexpr std::uint32_t c_Count = c_TilesX * c_TilesY * c_Slices;
        // A cluster keeps the first lights that reach it, up to this many, and counts the rest
        static constexpr std::uint32_t c_MaxLights = 128;

        // View depths, the distance in front of the eye, between which the slices lie
        float Near = 0.1f;
        float Far = 1.0f;
        // Perspective, sliced logarithmically, or orthographic, sliced evenly
        bool Perspective = true;
        // The projection's x and y scale, and for an orthographic one its x and y offset
        glm::vec2 Scale{ 1.0f };
        glm::vec2 Offset{ 0.0f };

        [[nodiscard]] static constexpr std::uint32_t GetIndex(std::uint32_t x, std::uint32_t y, std::uint32_t slice) { return x + c_TilesX * (y + c_TilesY * slice); }

        // Slice c_Slices is where the last slice ends
        [[nodiscard]] float GetSliceDepth(std::uint32_t slice) const;
        // A cluster's box in view space, where the view looks down -Z
        void GetBounds(std::uint32_t x, std::uint32_t y, std::uint32_t slice, glm::vec3& minimum, glm::vec3& maximum) const;
    };

    // 0 inside the box. Compared with a light's squared radius to tell if it reaches a cluster
    [[nodiscard]] TRINITY_API float GetSquaredDistance(const glm::vec3& point, const glm::vec3& minimum, const glm::vec3& maximum);

    // A point p is inside a plane where dot(plane.xyz, p) + plane.w >= 0
    struct TRINITY_API Frustum
    {
        std::array<glm::vec4, 6> Planes{};

        [[nodiscard]] static Frustum FromViewProjection(const glm::mat4& viewProjection);
        // Outside only when wholly outside one plane, so a box that is kept may still be just out of view
        [[nodiscard]] bool Intersects(const glm::vec3& center, const glm::vec3& extents) const;
    };

    // The world-space box that holds the local box under the transform
    TRINITY_API void GetWorldBounds(const MeshBounds& local, const glm::mat4& world, glm::vec3& center, glm::vec3& extents);

    struct MeshDraw
    {
        UUID Entity;
        // The entity as ToPickID gives it, which the ID target holds
        std::uint32_t PickID = 0;
        const MeshAsset* Mesh = nullptr;
        std::uint32_t Submesh = 0;
        std::uint32_t Material = 0;
        std::uint32_t Pipeline = 0;
        glm::mat4 World{ 1.0f };
        std::uint64_t Key = 0;
        MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
        bool CastShadows = true;

        [[nodiscard]] bool operator==(const MeshDraw&) const = default;
    };

    // A world UI document's quad, drawn with the blended submeshes and sorted among them by depth
    struct WorldUIDraw
    {
        std::uint32_t Document = 0;
        std::uint32_t PickID = 0;
        glm::mat4 World{ 1.0f };
        float Depth = 0.0f;
    };

    // A shadow map in its atlas tile. TexelSize is in world space, at 1 m for a spot light
    struct ShadowView
    {
        glm::mat4 ViewProjection{ 1.0f };
        glm::uvec2 Origin{ 0 };
        float TexelSize = 0.0f;
        bool Perspective = false;
        std::uint32_t FirstDraw = 0;
        std::uint32_t DrawCount = 0;
    };

    // Cascades on the top row, spot lights' maps below. Mesh.slang holds the same sizes
    struct TRINITY_API ShadowAtlas
    {
        static constexpr RHI::Format c_Format = RHI::Format::D32Float;
        static constexpr std::uint32_t c_Size = 4096;
        static constexpr std::uint32_t c_TileSize = 1024;
        static constexpr std::uint32_t c_Cascades = 4;
        static constexpr std::uint32_t c_MaxSpotShadows = 12;
        static constexpr std::uint32_t c_MaxViews = c_Cascades + c_MaxSpotShadows;
        static constexpr std::uint32_t c_NoShadow = 0xFFFFFFFFu;
        // How far the sun's shadows reach, and how far the splits lean towards logarithmic spacing
        static constexpr float c_Distance = 100.0f;
        static constexpr float c_FadeFraction = 0.1f;
        static constexpr float c_SplitBlend = 0.8f;
        // A spot light's map starts this far in front of it, and covers its cone up to this half angle
        static constexpr float c_SpotNear = 0.05f;
        static constexpr float c_MaxSpotAngle = 80.0f;
        // A negative slope bias: reversed depth moves a caster away from the light. Offsets are in texels
        static constexpr float c_SlopeBias = -0.5f;
        static constexpr float c_NormalOffset = 0.5f;
        static constexpr float c_DepthBias = 0.1f;

        // Tile by tile along each row, cascades first
        [[nodiscard]] static glm::uvec2 GetTileOrigin(std::uint32_t view);
        // Each cascade is a sphere around its depth slice, snapped to whole texels so it does not shimmer
        static void BuildCascades(const RenderView& view, const glm::vec3& toLight, std::span<ShadowView, c_Cascades> cascades, std::array<float, c_Cascades>& splits);
        // A spot light's map, which looks down its direction out to its range
        [[nodiscard]] static ShadowView BuildSpot(const glm::vec3& position, const glm::vec3& direction, float range, float outerAngle, std::uint32_t view);
    };

    // Towards the light. Colour times illuminance in lux
    struct DirectionalLight
    {
        glm::vec3 Direction{ 0.0f, 1.0f, 0.0f };
        glm::vec3 Radiance{ 0.0f };
    };

    // World space. Colour times intensity in candela, with glTF's cone terms
    struct PunctualLight
    {
        glm::vec3 Position{ 0.0f };
        float Range = 0.0f;
        glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };
        float ConeScale = 0.0f;
        glm::vec3 Radiance{ 0.0f };
        float ConeOffset = 1.0f;
        std::uint32_t Shadow = ShadowAtlas::c_NoShadow;
        std::array<std::uint32_t, 3> Padding{};
    };

    static_assert(sizeof(PunctualLight) == 64);

    // Opaque and masked submeshes sorted by state, then blended ones and world UI, far to near
    struct TRINITY_API SceneDrawList
    {
        static constexpr std::uint32_t c_MaxDirectionalLights = 4;
        static constexpr std::uint32_t c_MaxPunctualLights = 4096;

        RenderView View;
        std::vector<MeshDraw, TaggedAllocator<MeshDraw, MemoryTag::Renderer>> Draws;
        std::vector<MeshDraw, TaggedAllocator<MeshDraw, MemoryTag::Renderer>> Transparent;
        // A blended submesh no nearer than a document's quad is drawn before it
        std::vector<WorldUIDraw, TaggedAllocator<WorldUIDraw, MemoryTag::Renderer>> WorldUI;
        std::array<DirectionalLight, c_MaxDirectionalLights> Directional{};
        std::uint32_t DirectionalCount = 0;
        // In hierarchy order, each with the view-space sphere that holds all it lights
        std::vector<PunctualLight, TaggedAllocator<PunctualLight, MemoryTag::Renderer>> Lights;
        std::vector<glm::vec4, TaggedAllocator<glm::vec4, MemoryTag::Renderer>> LightBounds;
        ClusterGrid Clusters;
        // From the scene's first Environment component, once it has loaded
        UUID EnvironmentID;
        const EnvironmentAsset* Environment = nullptr;
        float EnvironmentIntensity = 1.0f;
        float EnvironmentRotation = 90.0f;
        // An Environment component stands in for the ambient light even while its environment loads
        bool HasEnvironment = false;
        // Lit by the default sun, as a scene with no lights and no environment is
        bool DefaultSun = false;
        // The sun's cascades take the first c_Cascades views, and spot lights' views follow
        std::uint32_t SunShadow = ShadowAtlas::c_NoShadow;
        std::array<float, ShadowAtlas::c_Cascades> CascadeSplits{};
        std::array<ShadowView, ShadowAtlas::c_MaxViews> ShadowViews{};
        std::uint32_t SpotShadows = 0;
        std::vector<MeshDraw, TaggedAllocator<MeshDraw, MemoryTag::Renderer>> ShadowDraws;
        // Lights past c_MaxDirectionalLights or c_MaxPunctualLights, which light nothing
        std::uint32_t DroppedLights = 0;
        std::uint32_t Submeshes = 0;
        std::uint32_t Culled = 0;
        // Mesh renderers whose mesh is still loading, which draw nothing until it is ready
        std::uint32_t Pending = 0;

        void Clear();
    };

    struct SceneOptions
    {
        // Every pixel loops over every light, not its cluster's: what clustering is checked against
        bool ShadeAllLights = false;
        // From blue for none to red at the cap, and magenta past it
        bool LightHeatmap = false;
        // 1 or Renderer3D::c_SampleCount, resolved before anything else draws on the target
        std::uint32_t SampleCount = 1;
        // Drawn after the scene, before the resolve. It tests against the depth and must not write it
        std::function<void(RHI::CommandList& commands, const RenderView& view, RHI::Format colorFormat, std::uint32_t sampleCount)> Overlay;
        // Each pixel's entity, for picking and outlines. An editor's view wants them, a game's does not
        bool EntityIDs = false;
        // With EntityIDs, the pixel whose entity Renderer::TakePickResult hands back
        std::optional<glm::uvec2> PickPixel;
        // With EntityIDs, entities to outline over the tonemapped image, in any order
        std::vector<std::uint32_t> Outlined;
        // For a view no layer draws sprites into, such as a game's. The main view's layers draw their own
        bool Sprites = false;
        // An editor's view wants them, a game's does not. Nothing is drawn in Distribution
        bool DebugLines = false;
    };

    // Culled on the job system, sorted, then drawn in a depth pre-pass and an opaque pass
    class TRINITY_API Renderer3D
    {
    public:
        // Counter-clockwise as in glTF, clockwise under a mirroring transform, both for double-sided
        enum class PipelineKind : std::uint32_t
        {
            Front,
            Mirrored,
            DoubleSided,

            Count
        };

        static constexpr RHI::Format c_DepthFormat = RHI::Format::D32Float;
        static constexpr RHI::Format c_EntityIDFormat = RHI::Format::R32Uint;
        // Reversed depth. The pre-pass keeps the nearest surface, and the opaque pass tests for equal
        static constexpr RHI::CompareOp c_PrePassCompare = RHI::CompareOp::GreaterOrEqual;
        static constexpr RHI::CompareOp c_OpaqueCompare = RHI::CompareOp::Equal;
        // Blended surfaces are tested against the opaque depth and write none of their own
        static constexpr RHI::CompareOp c_TransparentCompare = RHI::CompareOp::GreaterOrEqual;
        // Every backend supports it for the scene's colour and depth formats
        static constexpr std::uint32_t c_SampleCount = 4;
        // Lights a scene with no lights at all, so a model dropped into a new scene shows
        static constexpr float c_DefaultSunIntensity = 3.14159265f;
        // In a scene with no environment, the fraction of directional light that comes from all around
        static constexpr float c_AmbientFraction = 0.03f;

        // IDs are invalid unless asked for, the buffers without clusters, the atlas without shadows
        struct Passes
        {
            FrameGraphTexture Depth;
            FrameGraphTexture EntityIDs;
            std::uint32_t SampleCount = 1;
            FrameGraphBuffer ClusterCounts;
            FrameGraphBuffer ClusterLights;
            FrameGraphTexture ShadowAtlas;
        };

        struct Statistics
        {
            std::uint32_t Submeshes = 0;
            std::uint32_t Visible = 0;
            std::uint32_t Pending = 0;
            std::uint32_t PrePassDraws = 0;
            std::uint32_t OpaqueDraws = 0;
            std::uint32_t TransparentDraws = 0;
            std::uint32_t WorldUIDraws = 0;
            std::uint32_t PipelineChanges = 0;
            // Shadow maps drawn, and the casters drawn into them
            std::uint32_t ShadowMaps = 0;
            std::uint32_t ShadowDraws = 0;
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
        // None when world UI documents are not drawn
        void SetWorldUISource(const WorldUISource* source) { m_WorldUI = source; }

        // On the main thread, after the transform pass. Meshes stay loaded while views use them
        void Collect(Scene& scene, const RenderView& view, SceneDrawList& list);
        // Tests every submesh on this thread, for checking Collect against
        static void CollectReference(Scene& scene, const RenderView& view, SceneDrawList& list);
        // Each cluster's light count, then its first c_MaxLights lights, for checking the cluster pass
        static void BuildClustersReference(const SceneDrawList& list, std::vector<std::uint32_t>& counts, std::vector<std::uint32_t>& lights);

        // Shadows, depth pre-pass, clusters, opaque, sky, blended, overlay. The list must outlive the frame
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

        // A shadow map writes depth alone, with a bias, and clamps it instead of clipping
        enum class MeshPass : std::uint8_t
        {
            PrePass,
            Opaque,
            Transparent,
            Shadow
        };

        // A world UI quad or the sky, for one colour format and sample count
        struct TargetPipeline
        {
            RHI::Format Format = RHI::Format::Unknown;
            bool EntityIDs = false;
            std::uint32_t SampleCount = 1;
            RHI::PipelineHandle Pipeline;
        };

        // An alpha-masked submesh needs a pixel shader to cut it out, in the pre-pass and shadow maps
        struct PipelineEntry
        {
            RHI::Format Format = RHI::Format::Unknown;
            PipelineKind Kind = PipelineKind::Front;
            MeshPass Pass = MeshPass::Opaque;
            bool Masked = false;
            bool EntityIDs = false;
            std::uint32_t SampleCount = 1;
            RHI::PipelineHandle Pipeline;
        };

        [[nodiscard]] const MeshAsset* ResolveMesh(UUID id);
        [[nodiscard]] const MaterialAsset* ResolveMaterial(UUID id);
        [[nodiscard]] const EnvironmentAsset* ResolveEnvironment(UUID id);
        // Where this view's lights, clusters and shadows are for the opaque pass, and how it shades
        struct LightInputs
        {
            std::uint32_t Lights = RHI::c_NoBindlessIndex;
            std::uint32_t LightsOffset = 0;
            std::uint32_t ClusterCounts = RHI::c_NoBindlessIndex;
            std::uint32_t ClusterLights = RHI::c_NoBindlessIndex;
            std::uint32_t Flags = 0;
            std::uint32_t ShadowAtlas = RHI::c_NoBindlessIndex;
            std::uint32_t Shadows = RHI::c_NoBindlessIndex;
            std::uint32_t ShadowsOffset = 0;
        };

        // Every caster against every shadow view on the job system, then each view's casters in draw order
        void CollectShadowCasters(SceneDrawList& list);
        [[nodiscard]] RHI::PipelineHandle GetPipeline(RHI::Format colorFormat, PipelineKind kind, MeshPass pass, bool masked, bool entityIDs, std::uint32_t sampleCount);
        void RecordDraws(RHI::CommandList& commands, const SceneDrawList& list, std::span<const MeshDraw> draws, RHI::Format colorFormat, MeshPass pass, bool entityIDs, std::uint32_t sampleCount, std::uint32_t width, std::uint32_t height, const LightInputs& lights);
        void RecordShadowDraws(RHI::CommandList& commands, const SceneDrawList& list);
        // The entity's world UI document, when it has one sorted by depth that draws this frame
        void CollectWorldUI(Scene& scene, Entity entity, const RenderView& view, SceneDrawList& list);
        // The blended submeshes with the world UI documents' quads among them
        void RecordBlended(RHI::CommandList& commands, const SceneDrawList& list, RHI::Format colorFormat, bool entityIDs, std::uint32_t sampleCount, std::uint32_t width, std::uint32_t height, const LightInputs& lights);
        void DrawWorldUI(RHI::CommandList& commands, const SceneDrawList& list, const WorldUIDraw& draw, RHI::Format colorFormat, bool entityIDs, std::uint32_t sampleCount);
        [[nodiscard]] RHI::PipelineHandle GetWorldUIPipeline(RHI::Format colorFormat, bool entityIDs, std::uint32_t sampleCount);
        // The sky behind what the opaque pass drew, for a perspective view with a loaded environment
        void DrawSky(RHI::CommandList& commands, const SceneDrawList& list, RHI::Format colorFormat, bool entityIDs, std::uint32_t sampleCount, std::uint32_t width, std::uint32_t height);
        [[nodiscard]] RHI::PipelineHandle GetSkyPipeline(RHI::Format colorFormat, bool entityIDs, std::uint32_t sampleCount);
        // False when the mesh shaders are missing
        [[nodiscard]] bool DrawMeshes(RHI::CommandList& commands, std::span<const MeshDraw> draws, std::uint32_t frame, std::uint32_t frameOffset, RHI::Format colorFormat, MeshPass pass, bool entityIDs, std::uint32_t sampleCount);
        void ReadClusterStatistics();

        RHI::Device& m_Device;
        const MaterialLoader& m_Materials;
        FileBuffer m_VertexShader;
        FileBuffer m_PixelShader;
        FileBuffer m_DepthPixelShader;
        FileBuffer m_MaskPixelShader;
        FileBuffer m_PickPixelShader;
        FileBuffer m_WorldUIVertexShader;
        FileBuffer m_WorldUIPixelShader;
        FileBuffer m_WorldUIPickPixelShader;
        const WorldUISource* m_WorldUI = nullptr;
        std::vector<TargetPipeline, TaggedAllocator<TargetPipeline, MemoryTag::Renderer>> m_WorldUIPipelines;
        bool m_ReportedNoWorldUIShaders = false;
        FileBuffer m_SkyVertexShader;
        FileBuffer m_SkyPixelShader;
        FileBuffer m_SkyPickPixelShader;
        std::vector<TargetPipeline, TaggedAllocator<TargetPipeline, MemoryTag::Renderer>> m_SkyPipelines;
        bool m_ReportedNoSkyShaders = false;
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
        // For each candidate, a bit for each shadow view it casts into
        std::vector<std::uint16_t, TaggedAllocator<std::uint16_t, MemoryTag::Renderer>> m_ShadowMasks;
        Statistics m_Statistics;
        std::uint64_t m_Frame = 0;
        bool m_ReportedNoShaders = false;
        // Single-sampled, then with c_SampleCount samples
        std::array<bool, 2> m_EntityIDSupport{};
        bool m_ReportedNoEntityIDs = false;
    };
}