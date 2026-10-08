#include "Trinity/Renderer/Renderer3D.hpp"

#include "Trinity/Asset/MaterialLoader.hpp"
#include "Trinity/Core/JobSystem.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/RHI/Pipeline.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <span>

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

        struct FrameData
        {
            std::array<glm::vec4, 4> ViewProjection{};
            glm::vec4 Eye{ 0.0f };
            glm::vec4 Forward{ 0.0f };
            glm::vec4 ToSun{ 0.0f };
            glm::vec4 SunRadiance{ 0.0f };
            glm::vec4 Ambient{ 0.0f };
            std::array<std::uint32_t, 2> MaterialTable{};
            std::array<std::uint32_t, 2> LinearSampler{};
            std::array<std::uint32_t, 2> NearestSampler{};
            std::array<std::uint32_t, 2> Padding{};
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
        static_assert(sizeof(FrameData) == 176);
        static_assert(sizeof(DrawData) == 80);

        constexpr std::uint64_t c_UploadAlignment = 16;
        constexpr std::uint32_t c_Absent = 0xFFFFFFFFu;
        // Submeshes culled together on one worker
        constexpr std::size_t c_CullBatch = 256;
        // About two seconds at 60 Hz without a view drawing it, after which a mesh or material is released
        constexpr std::uint64_t c_AssetKeepFrames = 120;
        // From above, a little to the side and in front, when a scene has no directional light of its own
        constexpr glm::vec3 c_DefaultToSun{ 0.3f, 0.8f, 0.5f };

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

        // The scene's first directional light in hierarchy order, or the default sun
        void FindSun(Scene& scene, SceneDrawList& list)
        {
            SceneRegistry& l_Registry = scene.GetRegistry();
            for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
            {
                const LightComponent* l_Light = l_Registry.try_get<LightComponent>(it_Entity.GetHandle());
                if (l_Light == nullptr || l_Light->Type != LightType::Directional)
                {
                    continue;
                }

                const glm::vec3 l_Axis(l_Registry.get<WorldTransformComponent>(it_Entity.GetHandle()).Matrix[2]);
                const float l_Length = glm::length(l_Axis);
                list.SunDirection = l_Length > 1e-12f ? l_Axis / l_Length : glm::vec3(0.0f, 0.0f, 1.0f);
                list.SunRadiance = l_Light->Color * l_Light->Intensity;
                list.DefaultSun = false;

                return;
            }

            list.SunDirection = glm::normalize(c_DefaultToSun);
            list.SunRadiance = glm::vec3(Renderer3D::c_DefaultSunIntensity);
            list.DefaultSun = true;
        }

        std::uint32_t GetMaterialIndex(const Asset* asset)
        {
            return asset != nullptr && asset->GetAssetType() == MaterialAsset::c_AssetType ? static_cast<const MaterialAsset*>(asset)->GetTableIndex() : MaterialLoader::c_DefaultIndex;
        }
    }

    // The camera looks down its world -Z, with the view the inverse of its world transform, which keeps only the camera's place and turn
    RenderView RenderView::FromCamera(const CameraComponent& camera, const glm::mat4& world, float aspectRatio)
    {
        RenderView l_View;
        l_View.ViewProjection = camera.GetProjection(aspectRatio) * glm::inverse(world);
        l_View.Position = glm::vec3(world[3]);
        const glm::vec3 l_Back(world[2]);
        const float l_Length = glm::length(l_Back);
        l_View.Forward = l_Length > 1e-12f ? -l_Back / l_Length : glm::vec3(0.0f, 0.0f, -1.0f);
        l_View.Orthographic = camera.Projection == CameraProjection::Orthographic;

        return l_View;
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
        SunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        SunRadiance = glm::vec3(0.0f);
        DefaultSun = false;
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
    }

    Renderer3D::~Renderer3D()
    {
        for (const PipelineEntry& it_Entry : m_Pipelines)
        {
            m_Device.DestroyPipeline(it_Entry.Pipeline);
        }

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
        FindSun(scene, list);

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
        FindSun(scene, list);

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

    // The pre-pass writes the nearest depth of every opaque submesh, and the opaque pass then shades only the surface that depth belongs to
    FrameGraphTexture Renderer3D::AddPasses(FrameGraph& graph, const SceneDrawList& list, FrameGraphTexture target, const RHI::TextureDescription& targetDescription, const std::array<float, 4>& clearColor)
    {
        RHI::TextureDescription l_DepthDescription;
        l_DepthDescription.Width = targetDescription.Width;
        l_DepthDescription.Height = targetDescription.Height;
        l_DepthDescription.TextureFormat = c_DepthFormat;
        l_DepthDescription.ClearDepth = 0.0f;
        l_DepthDescription.DebugName = "Scene depth";

        const FrameGraphTexture l_Depth = graph.CreateTexture("Scene depth", l_DepthDescription);
        const RHI::Format l_Format = targetDescription.TextureFormat;
        const std::uint32_t l_Width = targetDescription.Width;
        const std::uint32_t l_Height = targetDescription.Height;
        const SceneDrawList* l_List = &list;

        graph.AddPass("Depth pre-pass", FrameGraphPassType::Raster, [l_Depth](FrameGraphPassBuilder& builder)
        {
            builder.SetDepthAttachment({ l_Depth, RHI::LoadOp::Clear, 0.0f });
        }, [this, l_List, l_Format, l_Width, l_Height](const FrameGraphContext& context)
        {
            RecordDraws(context.GetCommands(), *l_List, l_Format, true, l_Width, l_Height);
        });

        graph.AddPass("Opaque", FrameGraphPassType::Raster, [target, l_Depth, clearColor](FrameGraphPassBuilder& builder)
        {
            builder.AddColorAttachment({ target, RHI::LoadOp::Clear, clearColor });
            builder.SetDepthAttachment({ l_Depth, RHI::LoadOp::Load, 0.0f });
        }, [this, l_List, l_Format, l_Width, l_Height](const FrameGraphContext& context)
        {
            RecordDraws(context.GetCommands(), *l_List, l_Format, false, l_Width, l_Height);
        });

        return l_Depth;
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
    void Renderer3D::RecordDraws(RHI::CommandList& commands, const SceneDrawList& list, RHI::Format colorFormat, bool depthOnly, std::uint32_t width, std::uint32_t height)
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

        l_FrameData.Eye = glm::vec4(list.View.Position, list.View.Orthographic ? 1.0f : 0.0f);
        l_FrameData.Forward = glm::vec4(list.View.Forward, 0.0f);
        l_FrameData.ToSun = glm::vec4(list.SunDirection, 0.0f);
        l_FrameData.SunRadiance = glm::vec4(list.SunRadiance, 0.0f);
        l_FrameData.Ambient = glm::vec4(list.SunRadiance * c_AmbientFraction, 0.0f);
        l_FrameData.MaterialTable = { m_Materials.GetTableShaderResourceIndex(), 0 };
        l_FrameData.LinearSampler = { m_Device.GetSamplerIndex(m_LinearSampler), 0 };
        l_FrameData.NearestSampler = { m_Device.GetSamplerIndex(m_NearestSampler), 0 };
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