#include "Trinity/Renderer/Renderer3D.hpp"

#include "Trinity/Asset/MaterialLoader.hpp"
#include "Trinity/Core/JobSystem.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/RHI/Pipeline.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <span>
#include <utility>

namespace Trinity
{
    namespace
    {
        // Laid out as Mesh.slang reads them
        struct PushData
        {
            std::array<std::uint32_t, 2> Frame{};
            std::array<std::uint32_t, 2> Draws{};
            std::uint32_t FrameOffset = 0;
            std::uint32_t DrawOffset = 0;
            std::uint32_t DrawIndex = 0;
            std::uint32_t Padding = 0;
        };

        // The directional lights are a direction and a radiance each, and the counts are the directional lights, the point and spot lights, the flags and whether the view is a perspective one
        struct FrameData
        {
            std::array<glm::vec4, 4> ViewProjection{};
            glm::vec4 Eye{ 0.0f };
            glm::vec4 Forward{ 0.0f };
            glm::vec4 ViewDepth{ 0.0f };
            glm::vec4 Ambient{ 0.0f };
            std::array<glm::vec4, SceneDrawList::c_MaxDirectionalLights * 2> Directional{};
            std::array<std::uint32_t, 4> Counts{};
            glm::vec4 Slicing{ 0.0f };
            std::array<std::uint32_t, 2> MaterialTable{};
            std::array<std::uint32_t, 2> LinearSampler{};
            std::array<std::uint32_t, 2> NearestSampler{};
            std::array<std::uint32_t, 2> Lights{};
            std::array<std::uint32_t, 2> ClusterCounts{};
            std::array<std::uint32_t, 2> ClusterLights{};
            std::uint32_t LightsOffset = 0;
            std::array<std::uint32_t, 3> Padding{};
        };

        // Laid out as LightClusters.slang reads it
        struct ClusterPushData
        {
            std::array<std::uint32_t, 2> Bounds{};
            std::array<std::uint32_t, 2> Counts{};
            std::array<std::uint32_t, 2> Lights{};
            std::uint32_t BoundsOffset = 0;
            std::uint32_t LightCount = 0;
            float Near = 0.0f;
            float Far = 0.0f;
            float ScaleX = 1.0f;
            float ScaleY = 1.0f;
            float OffsetX = 0.0f;
            float OffsetY = 0.0f;
            std::uint32_t Perspective = 0;
            std::uint32_t Padding = 0;
        };

        struct DrawData
        {
            std::array<glm::vec4, 3> Rows{};
            std::uint32_t MeshBuffer = 0;
            std::uint32_t Material = 0;
            std::uint32_t Positions = 0;
            std::uint32_t NormalTangents = 0;
            std::uint32_t TexCoords0 = 0;
            std::uint32_t TexCoords1 = 0;
            std::uint32_t Colors = 0;
            std::uint32_t Padding = 0;
        };

        static_assert(sizeof(PushData) == 32);
        static_assert(sizeof(FrameData) == 352);
        static_assert(sizeof(ClusterPushData) == 64);
        static_assert(sizeof(DrawData) == 80);

        constexpr std::uint64_t c_UploadAlignment = 16;
        constexpr std::uint32_t c_Absent = 0xFFFFFFFFu;
        // Submeshes culled together on one worker
        constexpr std::size_t c_CullBatch = 256;
        // About two seconds at 60 Hz without a view drawing it, after which a mesh or material is released
        constexpr std::uint64_t c_AssetKeepFrames = 120;
        // From above, a little to the side and in front, when a scene has no light of its own
        constexpr glm::vec3 c_DefaultToSun{ 0.3f, 0.8f, 0.5f };
        // Clusters sorted by one workgroup, as LightClusters.slang has it
        constexpr std::uint32_t c_ClusterGroupSize = 64;
        constexpr std::uint64_t c_ClusterCountsSize = std::uint64_t{ ClusterGrid::c_Count } * sizeof(std::uint32_t);
        constexpr std::uint64_t c_ClusterLightsSize = c_ClusterCountsSize * ClusterGrid::c_MaxLights;
        // FrameData's flags, as Mesh.slang reads them
        constexpr std::uint32_t c_ShadeAllLightsFlag = 1u << 0;
        constexpr std::uint32_t c_LightHeatmapFlag = 1u << 1;

        std::uint32_t ToOffset(std::uint64_t offset)
        {
            return offset == MeshLayout::c_Absent ? c_Absent : static_cast<std::uint32_t>(offset);
        }

        RHI::SamplerHandle CreateMaterialSampler(RHI::Device& device, RHI::Filter filter, std::string_view name)
        {
            RHI::SamplerDescription l_Description;
            l_Description.MinFilter = filter;
            l_Description.MagFilter = filter;
            l_Description.MipFilter = filter;
            l_Description.DebugName = name;

            return device.CreateSampler(l_Description);
        }

        // Back-face culled for counter-clockwise fronts, the same with clockwise fronts, or not culled
        RHI::PipelineHandle CreatePipeline(RHI::Device& device, std::span<const std::byte> vertexShader, std::span<const std::byte> pixelShader, RHI::Format colorFormat, Renderer3D::PipelineKind kind, bool depthOnly)
        {
            const std::array<RHI::Format, 1> l_ColorFormats{ colorFormat };

            RHI::GraphicsPipelineDescription l_Description;
            l_Description.VertexShader = { vertexShader, "VertexMain" };
            l_Description.PixelShader = { pixelShader, depthOnly ? "DepthPixelMain" : "PixelMain" };
            l_Description.ColorFormats = depthOnly ? std::span<const RHI::Format>() : std::span<const RHI::Format>(l_ColorFormats);
            l_Description.DepthFormat = Renderer3D::c_DepthFormat;
            l_Description.Cull = kind == Renderer3D::PipelineKind::DoubleSided ? RHI::CullMode::None : RHI::CullMode::Back;
            l_Description.FrontCounterClockwise = kind != Renderer3D::PipelineKind::Mirrored;
            l_Description.DepthTest = true;
            l_Description.DepthWrite = depthOnly;
            l_Description.DepthCompare = depthOnly ? Renderer3D::c_PrePassCompare : Renderer3D::c_OpaqueCompare;
            l_Description.DebugName = depthOnly ? "Mesh depth pre-pass" : "Mesh opaque";

            return device.CreateGraphicsPipeline(l_Description);
        }

        // The pipeline a submesh needs, and its sort key: pipeline, then material, then mesh, so draws sharing state follow each other
        void Finish(MeshDraw& draw, bool doubleSided)
        {
            const glm::mat3 l_Basis(draw.World);
            draw.Pipeline = static_cast<std::uint32_t>(doubleSided ? Renderer3D::PipelineKind::DoubleSided : (glm::determinant(l_Basis) < 0.0f ? Renderer3D::PipelineKind::Mirrored : Renderer3D::PipelineKind::Front));
            draw.Key = (std::uint64_t{ draw.Pipeline } << 62) | (std::uint64_t{ draw.Material & 0x3FFFFFFFu } << 32) | std::uint64_t{ draw.Mesh->GetShaderResourceIndex() };
        }

        // The cone a spot light lights out to its range lies in this sphere, which is the smallest that holds it
        void GetConeBounds(const glm::vec3& apex, const glm::vec3& direction, float range, float outerAngle, glm::vec3& center, float& radius)
        {
            const float l_Cosine = std::cos(outerAngle);
            if (outerAngle > glm::quarter_pi<float>())
            {
                center = apex + direction * (range * l_Cosine);
                radius = range * std::sin(outerAngle);

                return;
            }

            radius = range / (2.0f * l_Cosine);
            center = apex + direction * radius;
        }

        // Every light in hierarchy order: the first c_MaxDirectionalLights directional lights, and every point and spot light that reaches anywhere, with the sphere in view space holding what it lights. A scene with no lights at all is lit by the default sun. The clusters span the depths the spheres reach, within the projection's own for an orthographic view
        void GatherLights(Scene& scene, const RenderView& view, SceneDrawList& list)
        {
            SceneRegistry& l_Registry = scene.GetRegistry();
            // A scaled camera's view scales too, so a sphere grows by the most the view stretches it. The view is the inverse of a turn then a scale, whose longest row is that stretch, and its longest column covers a view built some other way
            const glm::mat3 l_ViewBasis(view.View);
            const glm::mat3 l_ViewRows = glm::transpose(l_ViewBasis);
            const float l_ViewScale = std::max({ glm::length(l_ViewBasis[0]), glm::length(l_ViewBasis[1]), glm::length(l_ViewBasis[2]), glm::length(l_ViewRows[0]), glm::length(l_ViewRows[1]), glm::length(l_ViewRows[2]) });
            float l_Nearest = std::numeric_limits<float>::max();
            float l_Farthest = std::numeric_limits<float>::lowest();
            bool l_AnyLight = false;
            for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
            {
                const LightComponent* l_Light = l_Registry.try_get<LightComponent>(it_Entity.GetHandle());
                if (l_Light == nullptr)
                {
                    continue;
                }

                l_AnyLight = true;
                const glm::mat4& l_World = l_Registry.get<WorldTransformComponent>(it_Entity.GetHandle()).Matrix;
                const glm::vec3 l_Axis(l_World[2]);
                const float l_Length = glm::length(l_Axis);
                const glm::vec3 l_Back = l_Length > 1e-12f ? l_Axis / l_Length : glm::vec3(0.0f, 0.0f, 1.0f);
                const glm::vec3 l_Radiance = l_Light->Color * l_Light->Intensity;
                if (l_Light->Type == LightType::Directional)
                {
                    if (list.DirectionalCount == SceneDrawList::c_MaxDirectionalLights)
                    {
                        ++list.DroppedLights;

                        continue;
                    }

                    list.Directional[list.DirectionalCount++] = { l_Back, l_Radiance };

                    continue;
                }

                const float l_Range = l_Light->GetRange();
                if (!(l_Range > 0.0f) || !std::isfinite(l_Range))
                {
                    continue;
                }

                if (list.Lights.size() == SceneDrawList::c_MaxPunctualLights)
                {
                    ++list.DroppedLights;

                    continue;
                }

                PunctualLight& l_Punctual = list.Lights.emplace_back();
                l_Punctual.Position = glm::vec3(l_World[3]);
                l_Punctual.Range = l_Range;
                l_Punctual.Direction = -l_Back;
                l_Punctual.Radiance = l_Radiance;

                glm::vec3 l_Center = l_Punctual.Position;
                float l_Radius = l_Range;
                if (l_Light->Type == LightType::Spot)
                {
                    const float l_Outer = glm::radians(std::clamp(l_Light->OuterConeAngle, 0.01f, 90.0f));
                    const float l_Inner = std::min(glm::radians(std::max(l_Light->InnerConeAngle, 0.0f)), l_Outer);
                    const float l_CosineOuter = std::cos(l_Outer);
                    l_Punctual.ConeScale = 1.0f / std::max(std::cos(l_Inner) - l_CosineOuter, 0.001f);
                    l_Punctual.ConeOffset = -l_CosineOuter * l_Punctual.ConeScale;
                    GetConeBounds(l_Punctual.Position, l_Punctual.Direction, l_Range, l_Outer, l_Center, l_Radius);
                }

                const glm::vec3 l_ViewCenter(view.View * glm::vec4(l_Center, 1.0f));
                const float l_ViewRadius = l_Radius * l_ViewScale;
                list.LightBounds.emplace_back(l_ViewCenter, l_ViewRadius);
                l_Nearest = std::min(l_Nearest, -l_ViewCenter.z - l_ViewRadius);
                l_Farthest = std::max(l_Farthest, -l_ViewCenter.z + l_ViewRadius);
            }

            if (!l_AnyLight)
            {
                list.Directional[0] = { glm::normalize(c_DefaultToSun), glm::vec3(Renderer3D::c_DefaultSunIntensity) };
                list.DirectionalCount = 1;
                list.DefaultSun = true;
            }

            ClusterGrid& l_Grid = list.Clusters;
            l_Grid.Perspective = !view.Orthographic;
            l_Grid.Scale = glm::vec2(view.Projection[0][0], view.Projection[1][1]);
            l_Grid.Offset = view.Orthographic ? glm::vec2(view.Projection[3][0], view.Projection[3][1]) : glm::vec2(0.0f);
            if (list.Lights.empty())
            {
                return;
            }

            if (l_Grid.Perspective)
            {
                // From the nearest light's reach, or the near plane of the infinite reversed projection if that is farther, to the farthest light's. A pixel nearer than the first slice is in it, and one beyond the last in that, and no light reaches either
                l_Grid.Near = std::max({ view.Projection[3][2], l_Nearest, 1e-4f });
                l_Grid.Far = std::max(l_Farthest, l_Grid.Near * 2.0f);

                return;
            }

            // Depth runs from 1 to 0 across the depths an orthographic projection keeps
            const float l_DepthScale = view.Projection[2][2];
            const float l_KeptA = l_DepthScale != 0.0f ? view.Projection[3][2] / l_DepthScale : l_Nearest;
            const float l_KeptB = l_DepthScale != 0.0f ? (view.Projection[3][2] - 1.0f) / l_DepthScale : l_Farthest;
            l_Grid.Near = std::max(l_Nearest, std::min(l_KeptA, l_KeptB));
            l_Grid.Far = std::min(l_Farthest, std::max(l_KeptA, l_KeptB));
            if (!(l_Grid.Far > l_Grid.Near))
            {
                l_Grid.Far = l_Grid.Near + 1.0f;
            }
        }

        std::uint32_t GetMaterialIndex(const Asset* asset)
        {
            return asset != nullptr && asset->GetAssetType() == MaterialAsset::c_AssetType ? static_cast<const MaterialAsset*>(asset)->GetTableIndex() : MaterialLoader::c_DefaultIndex;
        }
    }

    // The camera looks down its world -Z, with the view the inverse of its world transform
    RenderView RenderView::FromCamera(const CameraComponent& camera, const glm::mat4& world, float aspectRatio)
    {
        return FromMatrices(glm::inverse(world), camera.GetProjection(aspectRatio), camera.Projection == CameraProjection::Orthographic);
    }

    RenderView RenderView::FromMatrices(const glm::mat4& view, const glm::mat4& projection, bool orthographic)
    {
        const glm::mat4 l_World = glm::inverse(view);

        RenderView l_View;
        l_View.View = view;
        l_View.Projection = projection;
        l_View.ViewProjection = projection * view;
        l_View.Position = glm::vec3(l_World[3]);
        const glm::vec3 l_Back(l_World[2]);
        const float l_Length = glm::length(l_Back);
        l_View.Forward = l_Length > 1e-12f ? -l_Back / l_Length : glm::vec3(0.0f, 0.0f, -1.0f);
        l_View.Orthographic = orthographic;

        return l_View;
    }

    // The ends are exact, so the last slice ends at Far on the GPU as on the CPU
    float ClusterGrid::GetSliceDepth(std::uint32_t slice) const
    {
        if (slice == 0)
        {
            return Near;
        }

        if (slice >= c_Slices)
        {
            return Far;
        }

        const float l_Fraction = static_cast<float>(slice) / static_cast<float>(c_Slices);

        return Perspective ? Near * std::pow(Far / Near, l_Fraction) : Near + (Far - Near) * l_Fraction;
    }

    // The tile's corners in normalized device coordinates, carried back to view space at the slice's two depths, which for a perspective view are where the tile is narrowest and widest
    void ClusterGrid::GetBounds(std::uint32_t x, std::uint32_t y, std::uint32_t slice, glm::vec3& minimum, glm::vec3& maximum) const
    {
        const float l_Near = GetSliceDepth(slice);
        const float l_Far = GetSliceDepth(slice + 1);
        const glm::vec2 l_Low(static_cast<float>(x) * 2.0f / static_cast<float>(c_TilesX) - 1.0f, static_cast<float>(y) * 2.0f / static_cast<float>(c_TilesY) - 1.0f);
        const glm::vec2 l_High(static_cast<float>(x + 1) * 2.0f / static_cast<float>(c_TilesX) - 1.0f, static_cast<float>(y + 1) * 2.0f / static_cast<float>(c_TilesY) - 1.0f);
        glm::vec2 l_Minimum;
        glm::vec2 l_Maximum;
        if (Perspective)
        {
            const glm::vec2 l_LowSlope = l_Low / Scale;
            const glm::vec2 l_HighSlope = l_High / Scale;
            l_Minimum = glm::min(glm::min(l_LowSlope * l_Near, l_LowSlope * l_Far), glm::min(l_HighSlope * l_Near, l_HighSlope * l_Far));
            l_Maximum = glm::max(glm::max(l_LowSlope * l_Near, l_LowSlope * l_Far), glm::max(l_HighSlope * l_Near, l_HighSlope * l_Far));
        }
        else
        {
            const glm::vec2 l_LowSide = (l_Low - Offset) / Scale;
            const glm::vec2 l_HighSide = (l_High - Offset) / Scale;
            l_Minimum = glm::min(l_LowSide, l_HighSide);
            l_Maximum = glm::max(l_LowSide, l_HighSide);
        }

        minimum = glm::vec3(l_Minimum, -l_Far);
        maximum = glm::vec3(l_Maximum, -l_Near);
    }

    float GetSquaredDistance(const glm::vec3& point, const glm::vec3& minimum, const glm::vec3& maximum)
    {
        const glm::vec3 l_Outside = glm::max(glm::max(minimum - point, point - maximum), glm::vec3(0.0f));

        return glm::dot(l_Outside, l_Outside);
    }

    // From the rows of the view-projection, for depth from 0 to 1: left, right, bottom, top, then depth below 1, the near plane with reversed depth, and depth above 0, which an infinite far plane always passes
    Frustum Frustum::FromViewProjection(const glm::mat4& viewProjection)
    {
        const auto a_Row = [&viewProjection](glm::length_t row) { return glm::vec4(viewProjection[0][row], viewProjection[1][row], viewProjection[2][row], viewProjection[3][row]); };
        const glm::vec4 l_X = a_Row(0);
        const glm::vec4 l_Y = a_Row(1);
        const glm::vec4 l_Z = a_Row(2);
        const glm::vec4 l_W = a_Row(3);

        Frustum l_Frustum;
        l_Frustum.Planes = { l_W + l_X, l_W - l_X, l_W + l_Y, l_W - l_Y, l_W - l_Z, l_Z };

        return l_Frustum;
    }

    bool Frustum::Intersects(const glm::vec3& center, const glm::vec3& extents) const
    {
        for (const glm::vec4& it_Plane : Planes)
        {
            const glm::vec3 l_Normal(it_Plane);
            if (glm::dot(l_Normal, center) + it_Plane.w + glm::dot(glm::abs(l_Normal), extents) < 0.0f)
            {
                return false;
            }
        }

        return true;
    }

    void GetWorldBounds(const MeshBounds& local, const glm::mat4& world, glm::vec3& center, glm::vec3& extents)
    {
        const glm::vec3 l_Center = (local.Min + local.Max) * 0.5f;
        const glm::vec3 l_Extents = (local.Max - local.Min) * 0.5f;
        center = glm::vec3(world * glm::vec4(l_Center, 1.0f));
        extents = glm::abs(glm::vec3(world[0])) * l_Extents.x + glm::abs(glm::vec3(world[1])) * l_Extents.y + glm::abs(glm::vec3(world[2])) * l_Extents.z;
    }

    void SceneDrawList::Clear()
    {
        Draws.clear();
        Directional = {};
        DirectionalCount = 0;
        Lights.clear();
        LightBounds.clear();
        Clusters = {};
        DefaultSun = false;
        DroppedLights = 0;
        Submeshes = 0;
        Culled = 0;
        Pending = 0;
    }

    Renderer3D::Renderer3D(RHI::Device& device, const MaterialLoader& materials) : m_Device(device), m_Materials(materials)
    {
        m_LinearSampler = CreateMaterialSampler(m_Device, RHI::Filter::Linear, "Material linear sampler");
        m_NearestSampler = CreateMaterialSampler(m_Device, RHI::Filter::Nearest, "Material nearest sampler");

        const std::string_view l_Extension = m_Device.GetInfo().API == GraphicsAPI::D3D12 ? "dxil" : "spv";
        Expected<FileBuffer, FileError> l_Vertex = FileSystem::ReadFile(std::format("/engine/shaders/Mesh.VertexMain.{}", l_Extension));
        Expected<FileBuffer, FileError> l_Pixel = FileSystem::ReadFile(std::format("/engine/shaders/Mesh.PixelMain.{}", l_Extension));
        Expected<FileBuffer, FileError> l_DepthPixel = FileSystem::ReadFile(std::format("/engine/shaders/Mesh.DepthPixelMain.{}", l_Extension));
        if (l_Vertex && l_Pixel && l_DepthPixel)
        {
            m_VertexShader = std::move(*l_Vertex);
            m_PixelShader = std::move(*l_Pixel);
            m_DepthPixelShader = std::move(*l_DepthPixel);
        }

        // Without the cluster shader every pixel shades every light, which is right but slow
        const Expected<FileBuffer, FileError> l_Clusters = FileSystem::ReadFile(std::format("/engine/shaders/LightClusters.AssignLights.{}", l_Extension));
        if (l_Clusters)
        {
            RHI::ComputePipelineDescription l_Description;
            l_Description.ComputeShader = { *l_Clusters, "AssignLights" };
            l_Description.DebugName = "Light clusters";
            m_ClusterPipeline = m_Device.CreateComputePipeline(l_Description);
        }

        if (!m_ClusterPipeline)
        {
            TR_CORE_WARN("Renderer3D: the light cluster shader is missing from /engine/shaders, so every pixel shades every point and spot light");
        }

        RHI::BufferDescription l_Readback;
        l_Readback.Size = c_ClusterCountsSize;
        l_Readback.Usage = RHI::BufferUsage::CopyDestination;
        l_Readback.Memory = RHI::MemoryType::Readback;
        l_Readback.DebugName = "Light cluster statistics";
        for (RHI::BufferHandle& it_Readback : m_ClusterReadbacks)
        {
            it_Readback = m_Device.CreateBuffer(l_Readback);
        }
    }

    Renderer3D::~Renderer3D()
    {
        for (const PipelineEntry& it_Entry : m_Pipelines)
        {
            m_Device.DestroyPipeline(it_Entry.Pipeline);
        }

        for (const RHI::BufferHandle it_Readback : m_ClusterReadbacks)
        {
            m_Device.DestroyBuffer(it_Readback);
        }

        m_Device.DestroyPipeline(m_ClusterPipeline);
        m_Device.DestroySampler(m_LinearSampler);
        m_Device.DestroySampler(m_NearestSampler);
    }

    // Meshes and materials no view has drawn for a while are released, and an emptied cache gives its buckets back
    void Renderer3D::BeginFrame()
    {
        ++m_Frame;
        std::erase_if(m_Meshes, [this](const auto& entry) { return m_Frame - entry.second.LastUsedFrame > c_AssetKeepFrames; });
        std::erase_if(m_MaterialCache, [this](const auto& entry) { return m_Frame - entry.second.LastUsedFrame > c_AssetKeepFrames; });
        if (m_Meshes.empty())
        {
            MeshCache().swap(m_Meshes);
        }

        if (m_MaterialCache.empty())
        {
            MaterialCache().swap(m_MaterialCache);
        }

        m_Statistics.PrePassDraws = 0;
        m_Statistics.OpaqueDraws = 0;
        m_Statistics.PipelineChanges = 0;
        ReadClusterStatistics();
    }

    // The counts the first view copied c_FramesInFlight frames ago, whose frame the device has waited for, read before this frame's view copies over them. A frame whose first view had no clusters leaves nothing to read
    void Renderer3D::ReadClusterStatistics()
    {
        const std::size_t l_Slot = static_cast<std::size_t>(m_Frame % RHI::c_FramesInFlight);
        m_Statistics.MostLightsInCluster = 0;
        m_Statistics.OverfullClusters = 0;
        if (!std::exchange(m_ClusterReadbackWritten[l_Slot], false))
        {
            return;
        }

        const std::span<const std::byte> l_Data = m_Device.GetMappedData(m_ClusterReadbacks[l_Slot]);
        if (l_Data.size() < c_ClusterCountsSize)
        {
            return;
        }

        for (std::uint32_t it_Cluster = 0; it_Cluster < ClusterGrid::c_Count; ++it_Cluster)
        {
            std::uint32_t l_Count = 0;
            std::memcpy(&l_Count, l_Data.data() + std::size_t{ it_Cluster } * sizeof(std::uint32_t), sizeof(l_Count));
            m_Statistics.MostLightsInCluster = std::max(m_Statistics.MostLightsInCluster, l_Count);
            m_Statistics.OverfullClusters += l_Count > ClusterGrid::c_MaxLights ? 1 : 0;
        }
    }

    // Every mesh and material at once, as when the project whose assets they are closes
    void Renderer3D::ReleaseAssets()
    {
        MeshCache().swap(m_Meshes);
        MaterialCache().swap(m_MaterialCache);
    }

    const MeshAsset* Renderer3D::ResolveMesh(UUID id)
    {
        auto a_Found = m_Meshes.find(id);
        if (a_Found == m_Meshes.end())
        {
            a_Found = m_Meshes.emplace(id, Cached<MeshAsset>{ AssetRef<MeshAsset>(id), m_Frame }).first;
        }

        a_Found->second.LastUsedFrame = m_Frame;
        const Asset* l_Asset = a_Found->second.Asset.IsReady() ? AssetManager::GetAsset(id) : nullptr;
        const MeshAsset* l_Mesh = l_Asset != nullptr && l_Asset->GetAssetType() == MeshAsset::c_AssetType ? static_cast<const MeshAsset*>(l_Asset) : nullptr;

        return l_Mesh != nullptr && l_Mesh->GetBuffer() && l_Mesh->GetShaderResourceIndex() != RHI::c_NoBindlessIndex ? l_Mesh : nullptr;
    }

    // A material still loading reads as the default, as its loader's placeholder is
    const MaterialAsset* Renderer3D::ResolveMaterial(UUID id)
    {
        if (!id)
        {
            return nullptr;
        }

        auto a_Found = m_MaterialCache.find(id);
        if (a_Found == m_MaterialCache.end())
        {
            a_Found = m_MaterialCache.emplace(id, Cached<MaterialAsset>{ AssetRef<MaterialAsset>(id), m_Frame }).first;
        }

        a_Found->second.LastUsedFrame = m_Frame;
        const Asset* l_Asset = AssetManager::GetAsset(id);

        return l_Asset != nullptr && l_Asset->GetAssetType() == MaterialAsset::c_AssetType ? static_cast<const MaterialAsset*>(l_Asset) : nullptr;
    }

    // Each submesh of each ready mesh in hierarchy order, then culled in batches on the job system, then sorted stably, so draws with equal keys keep hierarchy order
    void Renderer3D::Collect(Scene& scene, const RenderView& view, SceneDrawList& list)
    {
        TR_PROFILE_FUNCTION();

        list.Clear();
        list.View = view;
        GatherLights(scene, view, list);

        m_Candidates.clear();
        SceneRegistry& l_Registry = scene.GetRegistry();
        for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            const MeshRendererComponent* l_Renderer = l_Registry.try_get<MeshRendererComponent>(it_Entity.GetHandle());
            if (l_Renderer == nullptr || !l_Renderer->Mesh)
            {
                continue;
            }

            const MeshAsset* l_Mesh = ResolveMesh(l_Renderer->Mesh);
            if (l_Mesh == nullptr)
            {
                ++list.Pending;

                continue;
            }

            const glm::mat4& l_World = l_Registry.get<WorldTransformComponent>(it_Entity.GetHandle()).Matrix;
            const std::span<const Submesh> l_Submeshes = l_Mesh->GetSubmeshes();
            for (const Submesh& it_Submesh : l_Submeshes)
            {
                const UUID l_MaterialID = it_Submesh.MaterialSlot < l_Renderer->Materials.size() ? l_Renderer->Materials[it_Submesh.MaterialSlot] : UUID();
                const MaterialAsset* l_Material = ResolveMaterial(l_MaterialID);

                MeshDraw& l_Draw = m_Candidates.emplace_back();
                l_Draw.Entity = it_Entity.GetUUID();
                l_Draw.Mesh = l_Mesh;
                l_Draw.Submesh = static_cast<std::uint32_t>(&it_Submesh - l_Submeshes.data());
                l_Draw.Material = l_Material != nullptr ? l_Material->GetTableIndex() : MaterialLoader::c_DefaultIndex;
                l_Draw.World = l_World;
                Finish(l_Draw, l_Material != nullptr && l_Material->GetData().DoubleSided);
            }
        }

        const Frustum l_Frustum = Frustum::FromViewProjection(view.ViewProjection);
        m_Visible.assign(m_Candidates.size(), 0);
        JobSystem::ParallelFor(m_Candidates.size(), [this, &l_Frustum](std::size_t begin, std::size_t end)
        {
            for (std::size_t it_Index = begin; it_Index < end; ++it_Index)
            {
                const MeshDraw& l_Draw = m_Candidates[it_Index];
                glm::vec3 l_Center;
                glm::vec3 l_Extents;
                GetWorldBounds(l_Draw.Mesh->GetSubmeshes()[l_Draw.Submesh].Bounds, l_Draw.World, l_Center, l_Extents);
                m_Visible[it_Index] = l_Frustum.Intersects(l_Center, l_Extents) ? 1 : 0;
            }
        }, c_CullBatch);

        for (std::size_t it_Index = 0; it_Index < m_Candidates.size(); ++it_Index)
        {
            if (m_Visible[it_Index] != 0)
            {
                list.Draws.push_back(m_Candidates[it_Index]);
            }
        }

        std::ranges::stable_sort(list.Draws, {}, &MeshDraw::Key);
        list.Submeshes = static_cast<std::uint32_t>(m_Candidates.size());
        list.Culled = list.Submeshes - static_cast<std::uint32_t>(list.Draws.size());
        m_Statistics.Submeshes = list.Submeshes;
        m_Statistics.Visible = static_cast<std::uint32_t>(list.Draws.size());
        m_Statistics.Pending = list.Pending;
    }

    // As Collect decides, but one submesh after another with no cache and no workers, looking each asset up in the asset manager
    void Renderer3D::CollectReference(Scene& scene, const RenderView& view, SceneDrawList& list)
    {
        list.Clear();
        list.View = view;
        GatherLights(scene, view, list);

        const Frustum l_Frustum = Frustum::FromViewProjection(view.ViewProjection);
        SceneRegistry& l_Registry = scene.GetRegistry();
        for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            const MeshRendererComponent* l_Renderer = l_Registry.try_get<MeshRendererComponent>(it_Entity.GetHandle());
            if (l_Renderer == nullptr || !l_Renderer->Mesh)
            {
                continue;
            }

            const Asset* l_Asset = AssetManager::GetState(l_Renderer->Mesh) == AssetState::Ready ? AssetManager::GetAsset(l_Renderer->Mesh) : nullptr;
            const MeshAsset* l_Mesh = l_Asset != nullptr && l_Asset->GetAssetType() == MeshAsset::c_AssetType ? static_cast<const MeshAsset*>(l_Asset) : nullptr;
            if (l_Mesh == nullptr || !l_Mesh->GetBuffer() || l_Mesh->GetShaderResourceIndex() == RHI::c_NoBindlessIndex)
            {
                ++list.Pending;

                continue;
            }

            const glm::mat4& l_World = l_Registry.get<WorldTransformComponent>(it_Entity.GetHandle()).Matrix;
            for (std::uint32_t it_Submesh = 0; it_Submesh < l_Mesh->GetSubmeshes().size(); ++it_Submesh)
            {
                ++list.Submeshes;
                const Submesh& l_Submesh = l_Mesh->GetSubmeshes()[it_Submesh];
                glm::vec3 l_Center;
                glm::vec3 l_Extents;
                GetWorldBounds(l_Submesh.Bounds, l_World, l_Center, l_Extents);
                if (!l_Frustum.Intersects(l_Center, l_Extents))
                {
                    ++list.Culled;

                    continue;
                }

                const UUID l_MaterialID = l_Submesh.MaterialSlot < l_Renderer->Materials.size() ? l_Renderer->Materials[l_Submesh.MaterialSlot] : UUID();
                const Asset* l_Material = l_MaterialID ? AssetManager::GetAsset(l_MaterialID) : nullptr;

                MeshDraw l_Draw;
                l_Draw.Entity = it_Entity.GetUUID();
                l_Draw.Mesh = l_Mesh;
                l_Draw.Submesh = it_Submesh;
                l_Draw.Material = GetMaterialIndex(l_Material);
                l_Draw.World = l_World;
                Finish(l_Draw, l_Material != nullptr && l_Material->GetAssetType() == MaterialAsset::c_AssetType && static_cast<const MaterialAsset*>(l_Material)->GetData().DoubleSided);

                // Insertion keeps equal keys in hierarchy order, as the stable sort does
                const auto a_Place = std::ranges::upper_bound(list.Draws, l_Draw.Key, {}, &MeshDraw::Key);
                list.Draws.insert(a_Place, l_Draw);
            }
        }
    }

    // Each cluster's box against each light's sphere, in light order, as the cluster pass does it a cluster to a thread
    void Renderer3D::BuildClustersReference(const SceneDrawList& list, std::vector<std::uint32_t>& counts, std::vector<std::uint32_t>& lights)
    {
        counts.assign(ClusterGrid::c_Count, 0);
        lights.assign(std::size_t{ ClusterGrid::c_Count } * ClusterGrid::c_MaxLights, 0);
        for (std::uint32_t it_Slice = 0; it_Slice < ClusterGrid::c_Slices; ++it_Slice)
        {
            for (std::uint32_t it_Y = 0; it_Y < ClusterGrid::c_TilesY; ++it_Y)
            {
                for (std::uint32_t it_X = 0; it_X < ClusterGrid::c_TilesX; ++it_X)
                {
                    const std::uint32_t l_Cluster = ClusterGrid::GetIndex(it_X, it_Y, it_Slice);
                    glm::vec3 l_Minimum;
                    glm::vec3 l_Maximum;
                    list.Clusters.GetBounds(it_X, it_Y, it_Slice, l_Minimum, l_Maximum);
                    for (std::size_t it_Light = 0; it_Light < list.LightBounds.size(); ++it_Light)
                    {
                        const glm::vec4& l_Sphere = list.LightBounds[it_Light];
                        if (GetSquaredDistance(glm::vec3(l_Sphere), l_Minimum, l_Maximum) > l_Sphere.w * l_Sphere.w)
                        {
                            continue;
                        }

                        if (counts[l_Cluster] < ClusterGrid::c_MaxLights)
                        {
                            lights[std::size_t{ l_Cluster } * ClusterGrid::c_MaxLights + counts[l_Cluster]] = static_cast<std::uint32_t>(it_Light);
                        }

                        ++counts[l_Cluster];
                    }
                }
            }
        }
    }

    // The pre-pass writes the nearest depth of every opaque submesh, the cluster pass sorts the point and spot lights into clusters, and the opaque pass then shades only the surface that depth belongs to, with the lights of the cluster each pixel lies in. The lights and their spheres go into the upload ring now, for both passes to read
    Renderer3D::Passes Renderer3D::AddPasses(FrameGraph& graph, const SceneDrawList& list, FrameGraphTexture target, const RHI::TextureDescription& targetDescription, const std::array<float, 4>& clearColor, const SceneOptions& options)
    {
        RHI::TextureDescription l_DepthDescription;
        l_DepthDescription.Width = targetDescription.Width;
        l_DepthDescription.Height = targetDescription.Height;
        l_DepthDescription.TextureFormat = c_DepthFormat;
        l_DepthDescription.ClearDepth = 0.0f;
        l_DepthDescription.DebugName = "Scene depth";

        Passes l_Passes;
        l_Passes.Depth = graph.CreateTexture("Scene depth", l_DepthDescription);
        const FrameGraphTexture l_Depth = l_Passes.Depth;
        const RHI::Format l_Format = targetDescription.TextureFormat;
        const std::uint32_t l_Width = targetDescription.Width;
        const std::uint32_t l_Height = targetDescription.Height;
        const SceneDrawList* l_List = &list;

        graph.AddPass("Depth pre-pass", FrameGraphPassType::Raster, [l_Depth](FrameGraphPassBuilder& builder)
        {
            builder.SetDepthAttachment({ l_Depth, RHI::LoadOp::Clear, 0.0f });
        }, [this, l_List, l_Format, l_Width, l_Height](const FrameGraphContext& context)
        {
            RecordDraws(context.GetCommands(), *l_List, l_Format, true, l_Width, l_Height, {});
        });

        LightInputs l_Inputs;
        l_Inputs.Flags = options.LightHeatmap ? c_LightHeatmapFlag : 0;
        RHI::UploadAllocation l_Upload;
        const std::uint64_t l_LightsSize = std::uint64_t{ list.Lights.size() } * sizeof(PunctualLight);
        if (!list.Lights.empty())
        {
            l_Upload = m_Device.AllocateUpload(l_LightsSize + std::uint64_t{ list.LightBounds.size() } * sizeof(glm::vec4), c_UploadAlignment);
            if (l_Upload.Data.empty() || l_Upload.ShaderResourceIndex == RHI::c_NoBindlessIndex)
            {
                TR_CORE_ERROR("Renderer3D: no upload memory for {} lights, so point and spot lights light nothing this frame", list.Lights.size());
            }
            else
            {
                std::memcpy(l_Upload.Data.data(), list.Lights.data(), l_LightsSize);
                std::memcpy(l_Upload.Data.data() + l_LightsSize, list.LightBounds.data(), list.LightBounds.size() * sizeof(glm::vec4));
                l_Inputs.Lights = l_Upload.ShaderResourceIndex;
                l_Inputs.LightsOffset = static_cast<std::uint32_t>(l_Upload.Offset);
            }
        }

        const bool l_HasLights = l_Inputs.Lights != RHI::c_NoBindlessIndex;
        const bool l_Clustered = l_HasLights && !options.ShadeAllLights && m_ClusterPipeline;
        l_Inputs.Flags |= l_HasLights && !l_Clustered ? c_ShadeAllLightsFlag : 0;

        const bool l_FirstView = std::exchange(m_ClusterReadbackFrame, m_Frame) != m_Frame;
        if (l_FirstView)
        {
            m_Statistics.Lights = static_cast<std::uint32_t>(list.Lights.size());
        }

        if (l_Clustered)
        {
            l_Passes.ClusterCounts = graph.CreateBuffer("Light cluster counts", c_ClusterCountsSize);
            l_Passes.ClusterLights = graph.CreateBuffer("Light cluster lights", c_ClusterLightsSize);
            const FrameGraphBuffer l_Counts = l_Passes.ClusterCounts;
            const FrameGraphBuffer l_Lights = l_Passes.ClusterLights;

            ClusterPushData l_Push;
            l_Push.Bounds = { l_Upload.ShaderResourceIndex, 0 };
            l_Push.BoundsOffset = static_cast<std::uint32_t>(l_Upload.Offset + l_LightsSize);
            l_Push.LightCount = static_cast<std::uint32_t>(list.Lights.size());
            l_Push.Near = list.Clusters.Near;
            l_Push.Far = list.Clusters.Far;
            l_Push.ScaleX = list.Clusters.Scale.x;
            l_Push.ScaleY = list.Clusters.Scale.y;
            l_Push.OffsetX = list.Clusters.Offset.x;
            l_Push.OffsetY = list.Clusters.Offset.y;
            l_Push.Perspective = list.Clusters.Perspective ? 1 : 0;

            const RHI::PipelineHandle l_Pipeline = m_ClusterPipeline;
            graph.AddPass("Light clusters", FrameGraphPassType::Compute, [l_Counts, l_Lights](FrameGraphPassBuilder& builder)
            {
                builder.Write(l_Counts, RHI::ResourceState::UnorderedAccess);
                builder.Write(l_Lights, RHI::ResourceState::UnorderedAccess);
            }, [l_Push, l_Pipeline, l_Counts, l_Lights](const FrameGraphContext& context)
            {
                ClusterPushData l_Data = l_Push;
                l_Data.Counts = { context.GetDevice().GetUnorderedAccessIndex(context.GetBuffer(l_Counts)), 0 };
                l_Data.Lights = { context.GetDevice().GetUnorderedAccessIndex(context.GetBuffer(l_Lights)), 0 };

                context.GetCommands().SetPipeline(l_Pipeline);
                context.GetCommands().PushConstants(std::as_bytes(std::span(&l_Data, 1)));
                context.GetCommands().Dispatch((ClusterGrid::c_Count + c_ClusterGroupSize - 1) / c_ClusterGroupSize, 1, 1);
            });

            // The first view in a frame copies its counts back, for the statistics c_FramesInFlight frames later
            const std::size_t l_Slot = static_cast<std::size_t>(m_Frame % RHI::c_FramesInFlight);
            if (l_FirstView && m_ClusterReadbacks[l_Slot])
            {
                m_ClusterReadbackWritten[l_Slot] = true;
                const FrameGraphBuffer l_Readback = graph.ImportBuffer("Light cluster statistics", m_ClusterReadbacks[l_Slot], c_ClusterCountsSize, RHI::ResourceState::CopyDestination, RHI::ResourceState::CopyDestination);
                graph.AddPass("Light cluster statistics", FrameGraphPassType::Copy, [l_Counts, l_Readback](FrameGraphPassBuilder& builder)
                {
                    builder.Read(l_Counts, RHI::ResourceState::CopySource);
                    builder.Write(l_Readback, RHI::ResourceState::CopyDestination);
                    builder.SetSideEffect();
                }, [l_Counts, l_Readback](const FrameGraphContext& context)
                {
                    context.GetCommands().CopyBuffer(context.GetBuffer(l_Counts), 0, context.GetBuffer(l_Readback), 0, c_ClusterCountsSize);
                });
            }
        }

        const FrameGraphBuffer l_Counts = l_Passes.ClusterCounts;
        const FrameGraphBuffer l_Lights = l_Passes.ClusterLights;
        graph.AddPass("Opaque", FrameGraphPassType::Raster, [target, l_Depth, clearColor, l_Counts, l_Lights](FrameGraphPassBuilder& builder)
        {
            builder.AddColorAttachment({ target, RHI::LoadOp::Clear, clearColor });
            builder.SetDepthAttachment({ l_Depth, RHI::LoadOp::Load, 0.0f });
            if (l_Counts)
            {
                builder.Read(l_Counts, RHI::ResourceState::ShaderResource);
                builder.Read(l_Lights, RHI::ResourceState::ShaderResource);
            }
        }, [this, l_List, l_Format, l_Width, l_Height, l_Inputs, l_Counts, l_Lights](const FrameGraphContext& context)
        {
            LightInputs l_Shading = l_Inputs;
            if (l_Counts)
            {
                l_Shading.ClusterCounts = context.GetDevice().GetShaderResourceIndex(context.GetBuffer(l_Counts));
                l_Shading.ClusterLights = context.GetDevice().GetShaderResourceIndex(context.GetBuffer(l_Lights));
            }

            RecordDraws(context.GetCommands(), *l_List, l_Format, false, l_Width, l_Height, l_Shading);
        });

        return l_Passes;
    }

    RHI::PipelineHandle Renderer3D::GetPipeline(RHI::Format colorFormat, PipelineKind kind, bool depthOnly)
    {
        const auto a_Found = std::ranges::find_if(m_Pipelines, [&](const PipelineEntry& entry) { return entry.Kind == kind && entry.DepthOnly == depthOnly && (depthOnly || entry.Format == colorFormat); });
        if (a_Found != m_Pipelines.end())
        {
            return a_Found->Pipeline;
        }

        if (m_VertexShader.empty() || m_PixelShader.empty() || m_DepthPixelShader.empty())
        {
            if (!m_ReportedNoShaders)
            {
                TR_CORE_ERROR("Renderer3D: the mesh shaders are missing from /engine/shaders, so meshes are not drawn");
                m_ReportedNoShaders = true;
            }

            return {};
        }

        const RHI::PipelineHandle l_Pipeline = CreatePipeline(m_Device, m_VertexShader, depthOnly ? std::span<const std::byte>(m_DepthPixelShader) : std::span<const std::byte>(m_PixelShader), colorFormat, kind, depthOnly);
        m_Pipelines.push_back({ colorFormat, kind, depthOnly, l_Pipeline });

        return l_Pipeline;
    }

    // The frame's constants and every draw's record go into the upload ring once for each pass, and each draw names its record by index
    void Renderer3D::RecordDraws(RHI::CommandList& commands, const SceneDrawList& list, RHI::Format colorFormat, bool depthOnly, std::uint32_t width, std::uint32_t height, const LightInputs& lights)
    {
        TR_PROFILE_FUNCTION();

        if (list.Draws.empty() || width == 0 || height == 0)
        {
            return;
        }

        const RHI::UploadAllocation l_Frame = m_Device.AllocateUpload(sizeof(FrameData), c_UploadAlignment);
        const RHI::UploadAllocation l_Draws = m_Device.AllocateUpload(std::uint64_t{ list.Draws.size() } * sizeof(DrawData), c_UploadAlignment);
        if (l_Frame.Data.empty() || l_Draws.Data.empty() || l_Frame.ShaderResourceIndex == RHI::c_NoBindlessIndex || l_Draws.ShaderResourceIndex == RHI::c_NoBindlessIndex)
        {
            TR_CORE_ERROR("Renderer3D: no upload memory for {} draws, so they are not drawn this frame", list.Draws.size());

            return;
        }

        FrameData l_FrameData;
        for (glm::length_t it_Column = 0; it_Column < 4; ++it_Column)
        {
            l_FrameData.ViewProjection[static_cast<std::size_t>(it_Column)] = list.View.ViewProjection[it_Column];
        }

        // A point's view depth is its dot product with the negated third row of the view, and its slice comes from that depth's logarithm for a perspective view or the depth itself for an orthographic one, scaled and offset so the slices meet where ClusterGrid puts them
        const ClusterGrid& l_Grid = list.Clusters;
        const float l_SliceScale = l_Grid.Perspective ? static_cast<float>(ClusterGrid::c_Slices) / std::log2(l_Grid.Far / l_Grid.Near) : static_cast<float>(ClusterGrid::c_Slices) / (l_Grid.Far - l_Grid.Near);
        const float l_SliceBias = l_Grid.Perspective ? -l_SliceScale * std::log2(l_Grid.Near) : -l_SliceScale * l_Grid.Near;
        glm::vec3 l_Ambient(0.0f);
        for (std::uint32_t it_Light = 0; it_Light < list.DirectionalCount; ++it_Light)
        {
            l_FrameData.Directional[std::size_t{ it_Light } * 2] = glm::vec4(list.Directional[it_Light].Direction, 0.0f);
            l_FrameData.Directional[std::size_t{ it_Light } * 2 + 1] = glm::vec4(list.Directional[it_Light].Radiance, 0.0f);
            l_Ambient += list.Directional[it_Light].Radiance * c_AmbientFraction;
        }

        l_FrameData.Eye = glm::vec4(list.View.Position, list.View.Orthographic ? 1.0f : 0.0f);
        l_FrameData.Forward = glm::vec4(list.View.Forward, 0.0f);
        l_FrameData.ViewDepth = -glm::vec4(list.View.View[0][2], list.View.View[1][2], list.View.View[2][2], list.View.View[3][2]);
        l_FrameData.Ambient = glm::vec4(l_Ambient, 0.0f);
        l_FrameData.Counts = { list.DirectionalCount, lights.Lights != RHI::c_NoBindlessIndex ? static_cast<std::uint32_t>(list.Lights.size()) : 0, lights.Flags, l_Grid.Perspective ? 1u : 0u };
        l_FrameData.Slicing = glm::vec4(l_SliceScale, l_SliceBias, 0.0f, 0.0f);
        l_FrameData.MaterialTable = { m_Materials.GetTableShaderResourceIndex(), 0 };
        l_FrameData.LinearSampler = { m_Device.GetSamplerIndex(m_LinearSampler), 0 };
        l_FrameData.NearestSampler = { m_Device.GetSamplerIndex(m_NearestSampler), 0 };
        l_FrameData.Lights = { lights.Lights, 0 };
        l_FrameData.ClusterCounts = { lights.ClusterCounts, 0 };
        l_FrameData.ClusterLights = { lights.ClusterLights, 0 };
        l_FrameData.LightsOffset = lights.LightsOffset;
        std::memcpy(l_Frame.Data.data(), &l_FrameData, sizeof(l_FrameData));

        for (std::size_t it_Draw = 0; it_Draw < list.Draws.size(); ++it_Draw)
        {
            const MeshDraw& l_Draw = list.Draws[it_Draw];
            const MeshLayout& l_Layout = l_Draw.Mesh->GetLayout();

            DrawData l_Data;
            for (std::size_t it_Row = 0; it_Row < 3; ++it_Row)
            {
                const glm::length_t l_Row = static_cast<glm::length_t>(it_Row);
                l_Data.Rows[it_Row] = glm::vec4(l_Draw.World[0][l_Row], l_Draw.World[1][l_Row], l_Draw.World[2][l_Row], l_Draw.World[3][l_Row]);
            }

            l_Data.MeshBuffer = l_Draw.Mesh->GetShaderResourceIndex();
            l_Data.Material = l_Draw.Material;
            l_Data.Positions = ToOffset(l_Layout.Positions);
            l_Data.NormalTangents = ToOffset(l_Layout.NormalTangents);
            l_Data.TexCoords0 = ToOffset(l_Layout.TexCoords0);
            l_Data.TexCoords1 = ToOffset(l_Layout.TexCoords1);
            l_Data.Colors = ToOffset(l_Layout.Colors);
            std::memcpy(l_Draws.Data.data() + it_Draw * sizeof(DrawData), &l_Data, sizeof(l_Data));
        }

        PushData l_Push;
        l_Push.Frame = { l_Frame.ShaderResourceIndex, 0 };
        l_Push.Draws = { l_Draws.ShaderResourceIndex, 0 };
        l_Push.FrameOffset = static_cast<std::uint32_t>(l_Frame.Offset);
        l_Push.DrawOffset = static_cast<std::uint32_t>(l_Draws.Offset);

        commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f });
        commands.SetScissor({ 0, 0, width, height });

        std::uint32_t l_Pipeline = static_cast<std::uint32_t>(PipelineKind::Count);
        const MeshAsset* l_Mesh = nullptr;
        for (std::size_t it_Draw = 0; it_Draw < list.Draws.size(); ++it_Draw)
        {
            const MeshDraw& l_Draw = list.Draws[it_Draw];
            if (l_Draw.Pipeline != l_Pipeline)
            {
                const RHI::PipelineHandle l_Handle = GetPipeline(colorFormat, static_cast<PipelineKind>(l_Draw.Pipeline), depthOnly);
                if (!l_Handle)
                {
                    return;
                }

                commands.SetPipeline(l_Handle);
                l_Pipeline = l_Draw.Pipeline;
                l_Mesh = nullptr;
                ++m_Statistics.PipelineChanges;
            }

            if (l_Draw.Mesh != l_Mesh)
            {
                commands.SetIndexBuffer(l_Draw.Mesh->GetBuffer(), l_Draw.Mesh->GetLayout().Indices, l_Draw.Mesh->GetLayout().IndexFormat);
                l_Mesh = l_Draw.Mesh;
            }

            l_Push.DrawIndex = static_cast<std::uint32_t>(it_Draw);
            commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));

            const Submesh& l_Submesh = l_Draw.Mesh->GetSubmeshes()[l_Draw.Submesh];
            commands.DrawIndexed(l_Submesh.IndexCount, 1, l_Submesh.FirstIndex, 0);
        }

        (depthOnly ? m_Statistics.PrePassDraws : m_Statistics.OpaqueDraws) += static_cast<std::uint32_t>(list.Draws.size());
    }
}