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

    // One submesh to draw, with the pipeline and the material table record it is drawn with, its material's alpha mode, and whether its MeshRenderer casts shadows
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

    // A shadow map in its tile of the atlas: the light's view-projection, with reversed depth as the camera's has, the tile's top-left texel, and how wide a texel is in world space, anywhere in a cascade or at 1 m in front of a spot light. Its casters are a range of SceneDrawList::ShadowDraws
    struct ShadowView
    {
        glm::mat4 ViewProjection{ 1.0f };
        glm::uvec2 Origin{ 0 };
        float TexelSize = 0.0f;
        bool Perspective = false;
        std::uint32_t FirstDraw = 0;
        std::uint32_t DrawCount = 0;
    };

    // The one depth texture every shadow map is drawn into, a grid of tiles: the sun's cascades along the top row, nearest first, and spot lights' in the rows below. Mesh.slang holds the same sizes
    struct TRINITY_API ShadowAtlas
    {
        static constexpr RHI::Format c_Format = RHI::Format::D32Float;
        static constexpr std::uint32_t c_Size = 4096;
        static constexpr std::uint32_t c_TileSize = 1024;
        static constexpr std::uint32_t c_Cascades = 4;
        static constexpr std::uint32_t c_MaxSpotShadows = 12;
        static constexpr std::uint32_t c_MaxViews = c_Cascades + c_MaxSpotShadows;
        static constexpr std::uint32_t c_NoShadow = 0xFFFFFFFFu;
        // How far in front of the eye the sun's shadows reach, with the far tenth of the last cascade fading out, and how much the cascade splits lean towards logarithmic spacing over even spacing
        static constexpr float c_Distance = 100.0f;
        static constexpr float c_FadeFraction = 0.1f;
        static constexpr float c_SplitBlend = 0.8f;
        // A spot light's map starts this far in front of it and covers its cone up to this half angle in degrees, beyond which it casts no shadow
        static constexpr float c_SpotNear = 0.05f;
        static constexpr float c_MaxSpotAngle = 80.0f;
        // Bias: the raster's depth bias in multiples of a triangle's depth slope, negative since with reversed depth that moves a caster away from the light. A receiver then moves along its face's normal by c_NormalOffset texels at that point, and each PCF tap is compared with the depth of the receiver's own plane at that tap, nearer the light by c_DepthBias texels, so a surface never shadows itself however it slopes and a caster just above a surface still shadows it
        static constexpr float c_SlopeBias = -0.5f;
        static constexpr float c_NormalOffset = 0.5f;
        static constexpr float c_DepthBias = 0.1f;

        // Tile by tile along each row, cascades first
        [[nodiscard]] static glm::uvec2 GetTileOrigin(std::uint32_t view);
        // The sun's cascades for a view. Each holds a slice of the view's depth in the smallest sphere around it, which keeps its size however the view turns, seen down the light's direction with its centre snapped to whole texels, so a view that moves less than a texel leaves its map as it was. Splits gets each cascade's far view depth
        static void BuildCascades(const RenderView& view, const glm::vec3& toLight, std::span<ShadowView, c_Cascades> cascades, std::array<float, c_Cascades>& splits);
        // A spot light's map, which looks down its direction out to its range
        [[nodiscard]] static ShadowView BuildSpot(const glm::vec3& position, const glm::vec3& direction, float range, float outerAngle, std::uint32_t view);
    };

    // Towards the light, and its colour times its illuminance in lux
    struct DirectionalLight
    {
        glm::vec3 Direction{ 0.0f, 1.0f, 0.0f };
        glm::vec3 Radiance{ 0.0f };
    };

    // A point or spot light as the shaders read it, in world space: where it is and its range, the way it shines and its colour times its intensity in candela, with the cone terms glTF gives, which leave a point light at full strength every way, and the shadow view it casts shadows from
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

    // What a view of a scene draws, in the order it draws it, and the lights it is lit by: opaque and alpha-masked submeshes sorted by state, then alpha-blended ones from the farthest to the nearest
    struct TRINITY_API SceneDrawList
    {
        static constexpr std::uint32_t c_MaxDirectionalLights = 4;
        static constexpr std::uint32_t c_MaxPunctualLights = 4096;

        RenderView View;
        std::vector<MeshDraw, TaggedAllocator<MeshDraw, MemoryTag::Renderer>> Draws;
        std::vector<MeshDraw, TaggedAllocator<MeshDraw, MemoryTag::Renderer>> Transparent;
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
        // The shadows: the directional light that is the sun, whose cascades take the first c_Cascades views, and spot lights' views after them, each named by its light. Every view draws a range of ShadowDraws, the casters in it sorted as Draws is
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

    // How one view of a scene is shaded
    struct SceneOptions
    {
        // Every pixel loops over every point and spot light instead of its cluster's, which clustering is checked against
        bool ShadeAllLights = false;
        // Each pixel shows how many lights its cluster holds, from blue for none to red at the cap and magenta past it
        bool LightHeatmap = false;
        // Samples per pixel in the forward passes, 1 or Renderer3D::c_SampleCount, resolved into the target before anything else draws on it
        std::uint32_t SampleCount = 1;
        // Drawn after the scene, before the resolve, over its colour and tested against its depth, which it must not write: an editor's grid, say. It sets its own pipeline, built for the target's colour format, Renderer3D::c_DepthFormat and the sample count it is given
        std::function<void(RHI::CommandList& commands, const RenderView& view, RHI::Format colorFormat, std::uint32_t sampleCount)> Overlay;
        // Each pixel's entity, as ToPickID gives it, drawn into ID targets beside the scene, for picking and outlines: by the meshes in the forward passes, and by the sprites after them. An editor's view wants them, and a game's does not
        bool EntityIDs = false;
        // With EntityIDs, a pixel of the target whose entity Renderer::TakePickResult hands back once the GPU has finished with this frame
        std::optional<glm::uvec2> PickPixel;
        // With EntityIDs, entities to outline over the tonemapped image, as ToPickID gives them, in any order
        std::vector<std::uint32_t> Outlined;
        // The scene's sprites, drawn by the Renderer through the view after its meshes and before layers draw, for a view no layer draws sprites into, such as a game's. The main view's layers draw their own, so it leaves this off
        bool Sprites = false;
    };

    // Draws a scene's MeshRenderers: collected after the transform pass, culled against the view's frustum on the job system, sorted by pipeline, material and mesh, then drawn in a depth pre-pass and an opaque pass that shades each pixel once, with glTF's metallic-roughness BRDF lit by the scene's environment, up to four directional lights and the point and spot lights of the cluster the pixel lies in. Alpha-masked submeshes are cut out in the pre-pass, and alpha-blended ones drawn after, from back to front. The sun and spot lights cast shadows from maps in one atlas, filtered with PCF. The forward passes can be multisampled, and resolve into the target
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
        static constexpr RHI::Format c_EntityIDFormat = RHI::Format::R32Uint;
        // Reversed depth: the pre-pass keeps the nearest surface, and the opaque pass then draws only where it matches it exactly
        static constexpr RHI::CompareOp c_PrePassCompare = RHI::CompareOp::GreaterOrEqual;
        static constexpr RHI::CompareOp c_OpaqueCompare = RHI::CompareOp::Equal;
        // Blended surfaces are tested against the opaque depth and write none of their own
        static constexpr RHI::CompareOp c_TransparentCompare = RHI::CompareOp::GreaterOrEqual;
        // The multisampling SceneOptions asks for, which every backend supports for the scene's colour and depth formats
        static constexpr std::uint32_t c_SampleCount = 4;
        // The light a scene with no lights at all is lit by, so a model dropped into a new scene shows, which is white at an illuminance that lights a white surface facing it to 1
        static constexpr float c_DefaultSunIntensity = 3.14159265f;
        // In a scene with no environment, every surface also takes this fraction of the directional lights' light from all around
        static constexpr float c_AmbientFraction = 0.03f;

        // What AddPasses leaves in the graph: the depth and the meshes' entity IDs, multisampled when the passes are, with that many samples, each cluster's light count then its lights, c_MaxLights to a cluster, and the shadow atlas. The IDs are invalid unless SceneOptions asks for them, the buffers when no clusters were built, and the atlas when nothing casts a shadow
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

        // On the main thread, after the transform pass. Meshes and materials are kept loaded while views use them, and a submesh is drawn once its mesh is ready
        void Collect(Scene& scene, const RenderView& view, SceneDrawList& list);
        // Every submesh tested in turn on this thread, for checking Collect against. It reads only meshes Collect has already loaded
        static void CollectReference(Scene& scene, const RenderView& view, SceneDrawList& list);
        // Each cluster's light count, which may pass c_MaxLights, then its first c_MaxLights lights, as the cluster pass sorts them, for checking it against
        static void BuildClustersReference(const SceneDrawList& list, std::vector<std::uint32_t>& counts, std::vector<std::uint32_t>& lights);

        // When anything casts a shadow, a pass that draws every shadow map into the atlas, then a depth pre-pass into a depth texture the size of the target, then, when there are point or spot lights, a compute pass that sorts them into clusters, then the opaque pass, which clears the target and draws again with an equal depth test, then the blended submeshes over it, then any overlay. Multisampled, they draw into textures of their own, resolved into the target by the last. The list must last until the graph has run
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

        // The pass a pipeline draws in: the depth pre-pass, the opaque pass, the blended pass, or a shadow map, which writes depth alone with a bias and clamps it instead of clipping
        enum class MeshPass : std::uint8_t
        {
            PrePass,
            Opaque,
            Transparent,
            Shadow
        };

        // An alpha-masked submesh writes depth through a pixel shader that cuts it out, in the pre-pass and shadow maps, and a forward pass can write entity IDs beside its colour
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
        // Each draw's record into the upload ring, then the draws with the pipeline each needs, which reports false when the mesh shaders are missing
        [[nodiscard]] bool DrawMeshes(RHI::CommandList& commands, std::span<const MeshDraw> draws, std::uint32_t frame, std::uint32_t frameOffset, RHI::Format colorFormat, MeshPass pass, bool entityIDs, std::uint32_t sampleCount);
        void ReadClusterStatistics();

        RHI::Device& m_Device;
        const MaterialLoader& m_Materials;
        FileBuffer m_VertexShader;
        FileBuffer m_PixelShader;
        FileBuffer m_DepthPixelShader;
        FileBuffer m_MaskPixelShader;
        FileBuffer m_PickPixelShader;
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
        // Whether the device can draw entity IDs and read them, single-sampled then with c_SampleCount samples
        std::array<bool, 2> m_EntityIDSupport{};
        bool m_ReportedNoEntityIDs = false;
    };
}