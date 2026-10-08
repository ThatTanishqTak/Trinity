#include "Trinity/Renderer/Renderer2D.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Renderer/PickID.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <span>
#include <string_view>

namespace Trinity
{
    namespace
    {
        // Laid out as Sprite.slang reads it: the view-projection's columns, three descriptor handles, then the instances' offset in the upload buffer, and the entities' after them in the ID pass
        struct PushData
        {
            std::array<glm::vec4, 4> ViewProjection{};
            std::array<std::uint32_t, 2> Instances{};
            std::array<std::uint32_t, 2> LinearSampler{};
            std::array<std::uint32_t, 2> NearestSampler{};
            std::uint32_t InstanceOffset = 0;
            std::uint32_t EntityOffset = 0;
        };

        static_assert(sizeof(PushData) == 96);

        constexpr std::uint32_t c_NearestBit = 0x80000000u;
        constexpr std::uint64_t c_InstanceAlignment = 16;

        // About two seconds at 60 Hz without a sprite naming it, after which a texture is released
        constexpr std::uint64_t c_TextureKeepFrames = 120;

        std::uint32_t PackUnorm16(float low, float high)
        {
            const auto a_Pack = [](float value) { return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 65535.0f)); };

            return a_Pack(low) | (a_Pack(high) << 16);
        }

        std::uint32_t PackColor(const glm::vec4& color)
        {
            const auto a_Pack = [](float value) { return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };

            return a_Pack(color.r) | (a_Pack(color.g) << 8) | (a_Pack(color.b) << 16) | (a_Pack(color.a) << 24);
        }

        RHI::SamplerHandle CreateSpriteSampler(RHI::Device& device, RHI::Filter filter, std::string_view name)
        {
            RHI::SamplerDescription l_Description;
            l_Description.MinFilter = filter;
            l_Description.MagFilter = filter;
            l_Description.MipFilter = filter;
            l_Description.AddressU = RHI::AddressMode::ClampToEdge;
            l_Description.AddressV = RHI::AddressMode::ClampToEdge;
            l_Description.AddressW = RHI::AddressMode::ClampToEdge;
            l_Description.DebugName = name;

            return device.CreateSampler(l_Description);
        }

        // Drawn later means drawn on top, by the same order Renderer2D sorts in
        bool IsDrawnBefore(std::int32_t layer, std::int32_t order, std::uint32_t index, std::int32_t otherLayer, std::int32_t otherOrder, std::uint32_t otherIndex)
        {
            if (layer != otherLayer)
            {
                return layer < otherLayer;
            }

            return order != otherOrder ? order < otherOrder : index < otherIndex;
        }
    }

    // The sprite drawn last of those whose rotated rectangle holds the point, in world X and Y. A sprite scaled flat to a line or a point holds nothing
    Entity PickSprite(Scene& scene, const glm::vec2& worldPoint)
    {
        TR_PROFILE_FUNCTION();

        SceneRegistry& l_Registry = scene.GetRegistry();
        Entity l_Picked;
        std::int32_t l_PickedLayer = 0;
        std::int32_t l_PickedOrder = 0;
        std::uint32_t l_PickedIndex = 0;
        std::uint32_t l_Index = 0;
        for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            const SpriteRendererComponent* l_Sprite = l_Registry.try_get<SpriteRendererComponent>(it_Entity.GetHandle());
            if (l_Sprite == nullptr)
            {
                continue;
            }

            const std::uint32_t l_SpriteIndex = l_Index++;
            const glm::mat4& l_World = l_Registry.get<WorldTransformComponent>(it_Entity.GetHandle()).Matrix;
            const glm::mat2 l_Axes{ glm::vec2(l_World[0]), glm::vec2(l_World[1]) };
            const float l_Determinant = glm::determinant(l_Axes);
            if (std::abs(l_Determinant) < 1e-12f)
            {
                continue;
            }

            const glm::vec2 l_Local = glm::inverse(l_Axes) * (worldPoint - glm::vec2(l_World[3]));
            if (std::abs(l_Local.x) > 0.5f || std::abs(l_Local.y) > 0.5f)
            {
                continue;
            }

            if (!l_Picked || IsDrawnBefore(l_PickedLayer, l_PickedOrder, l_PickedIndex, l_Sprite->SortingLayer, l_Sprite->OrderInLayer, l_SpriteIndex))
            {
                l_Picked = it_Entity;
                l_PickedLayer = l_Sprite->SortingLayer;
                l_PickedOrder = l_Sprite->OrderInLayer;
                l_PickedIndex = l_SpriteIndex;
            }
        }

        return l_Picked;
    }

    Renderer2D::Renderer2D(RHI::Device& device, std::uint32_t whiteTexture) : m_Device(device), m_WhiteTexture(whiteTexture)
    {
        m_LinearSampler = CreateSpriteSampler(m_Device, RHI::Filter::Linear, "Sprite linear sampler");
        m_NearestSampler = CreateSpriteSampler(m_Device, RHI::Filter::Nearest, "Sprite nearest sampler");

        const std::string_view l_Extension = m_Device.GetInfo().API == GraphicsAPI::D3D12 ? "dxil" : "spv";
        Expected<FileBuffer, FileError> l_Vertex = FileSystem::ReadFile(std::format("/engine/shaders/Sprite.VertexMain.{}", l_Extension));
        Expected<FileBuffer, FileError> l_Pixel = FileSystem::ReadFile(std::format("/engine/shaders/Sprite.PixelMain.{}", l_Extension));
        if (l_Vertex && l_Pixel)
        {
            m_VertexShader = std::move(*l_Vertex);
            m_PixelShader = std::move(*l_Pixel);
        }

        Expected<FileBuffer, FileError> l_PickVertex = FileSystem::ReadFile(std::format("/engine/shaders/Sprite.PickVertexMain.{}", l_Extension));
        Expected<FileBuffer, FileError> l_PickPixel = FileSystem::ReadFile(std::format("/engine/shaders/Sprite.PickPixelMain.{}", l_Extension));
        if (l_PickVertex && l_PickPixel)
        {
            m_PickVertexShader = std::move(*l_PickVertex);
            m_PickPixelShader = std::move(*l_PickPixel);
        }
    }

    Renderer2D::~Renderer2D()
    {
        for (const auto& [it_Format, it_Pipeline] : m_Pipelines)
        {
            m_Device.DestroyPipeline(it_Pipeline);
        }

        m_Device.DestroyPipeline(m_IDPipeline);
        m_Device.DestroySampler(m_LinearSampler);
        m_Device.DestroySampler(m_NearestSampler);
    }

    // Textures no sprite has named for a while are released, and an emptied cache gives its buckets back
    void Renderer2D::BeginFrame()
    {
        ++m_Frame;
        std::erase_if(m_Textures, [this](const auto& entry) { return m_Frame - entry.second.LastUsedFrame > c_TextureKeepFrames; });
        if (m_Textures.empty())
        {
            TextureCache().swap(m_Textures);
        }
    }

    // Every texture at once, as when the project whose assets they are closes, since the asset manager cannot hold one project's assets while another's are set
    void Renderer2D::ReleaseTextures()
    {
        TextureCache().swap(m_Textures);
    }

    // Through the primary camera, with the target's aspect ratio. Returns false, and draws nothing, when the scene has no primary camera
    bool Renderer2D::DrawScene(RHI::CommandList& commands, Scene& scene, RHI::Format targetFormat, std::uint32_t width, std::uint32_t height)
    {
        TR_PROFILE_FUNCTION();

        glm::mat4 l_CameraWorld{ 1.0f };
        const CameraComponent* l_Camera = CollectSprites(scene, l_CameraWorld);
        if (l_Camera == nullptr || width == 0 || height == 0)
        {
            if (l_Camera == nullptr && !m_ReportedNoCamera)
            {
                TR_CORE_INFO("Renderer2D: the scene has no primary camera, so no sprites are drawn until it has one");
                m_ReportedNoCamera = true;
            }

            m_Statistics = { static_cast<std::uint32_t>(m_Instances.size()), 0, static_cast<std::uint32_t>(m_Textures.size()) };

            return false;
        }

        m_ReportedNoCamera = false;

        const float l_AspectRatio = static_cast<float>(width) / static_cast<float>(height);
        DrawSprites(commands, l_Camera->GetProjection(l_AspectRatio) * glm::inverse(l_CameraWorld), targetFormat, width, height);

        return true;
    }

    // Through a camera of the caller's, such as an editor camera
    void Renderer2D::DrawScene(RHI::CommandList& commands, Scene& scene, const glm::mat4& viewProjection, RHI::Format targetFormat, std::uint32_t width, std::uint32_t height)
    {
        TR_PROFILE_FUNCTION();

        glm::mat4 l_CameraWorld{ 1.0f };
        static_cast<void>(CollectSprites(scene, l_CameraWorld));
        DrawSprites(commands, viewProjection, targetFormat, width, height);
    }

    // In DrawScene's order, over the whole of each quad, whatever its texture's alpha, as PickSprite counts it. The statistics stay those of the sprites drawn
    void Renderer2D::DrawSceneIDs(RHI::CommandList& commands, Scene& scene, const glm::mat4& viewProjection, std::uint32_t width, std::uint32_t height)
    {
        TR_PROFILE_FUNCTION();

        glm::mat4 l_CameraWorld{ 1.0f };
        static_cast<void>(CollectSprites(scene, l_CameraWorld));
        if (m_Instances.empty() || width == 0 || height == 0)
        {
            return;
        }

        const RHI::PipelineHandle l_Pipeline = GetIDPipeline();
        if (!l_Pipeline)
        {
            return;
        }

        std::ranges::sort(m_Keys, [](const SortKey& left, const SortKey& right) { return IsDrawnBefore(left.Layer, left.Order, left.Index, right.Layer, right.Order, right.Index); });

        const std::uint64_t l_InstancesSize = std::uint64_t{ m_Instances.size() } * sizeof(SpriteInstance);
        const RHI::UploadAllocation l_Upload = m_Device.AllocateUpload(l_InstancesSize + std::uint64_t{ m_EntityIDs.size() } * sizeof(std::uint32_t), c_InstanceAlignment);
        if (l_Upload.Data.empty() || l_Upload.ShaderResourceIndex == RHI::c_NoBindlessIndex)
        {
            TR_CORE_ERROR("Renderer2D: no upload memory for {} sprites' entities, so they cannot be picked this frame", m_Instances.size());

            return;
        }

        for (std::size_t it_Sprite = 0; it_Sprite < m_Keys.size(); ++it_Sprite)
        {
            const std::uint32_t l_Index = m_Keys[it_Sprite].Index;
            std::memcpy(l_Upload.Data.data() + it_Sprite * sizeof(SpriteInstance), &m_Instances[l_Index], sizeof(SpriteInstance));
            std::memcpy(l_Upload.Data.data() + l_InstancesSize + it_Sprite * sizeof(std::uint32_t), &m_EntityIDs[l_Index], sizeof(std::uint32_t));
        }

        PushData l_Push;
        for (glm::length_t it_Column = 0; it_Column < 4; ++it_Column)
        {
            l_Push.ViewProjection[static_cast<std::size_t>(it_Column)] = viewProjection[it_Column];
        }

        l_Push.Instances = { l_Upload.ShaderResourceIndex, 0 };
        l_Push.InstanceOffset = static_cast<std::uint32_t>(l_Upload.Offset);
        l_Push.EntityOffset = static_cast<std::uint32_t>(l_Upload.Offset + l_InstancesSize);

        commands.SetPipeline(l_Pipeline);
        commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f });
        commands.SetScissor({ 0, 0, width, height });
        commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
        commands.Draw(4, static_cast<std::uint32_t>(m_Instances.size()), 0, 0);
    }

    // In hierarchy order, which is the last tie-break when sorting. World transforms are read as they are, so the caller runs the transform pass first. The first primary camera met is the one used
    const CameraComponent* Renderer2D::CollectSprites(Scene& scene, glm::mat4& cameraWorld)
    {
        TR_PROFILE_FUNCTION();

        m_Instances.clear();
        m_Keys.clear();
        m_EntityIDs.clear();

        SceneRegistry& l_Registry = scene.GetRegistry();
        const CameraComponent* l_Camera = nullptr;
        UUID l_LastTexture;
        std::uint32_t l_LastIndex = m_WhiteTexture;
        for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            const entt::entity l_Handle = it_Entity.GetHandle();
            if (l_Camera == nullptr)
            {
                if (const CameraComponent* l_Found = l_Registry.try_get<CameraComponent>(l_Handle); l_Found != nullptr && l_Found->Primary)
                {
                    l_Camera = l_Found;
                    cameraWorld = l_Registry.get<WorldTransformComponent>(l_Handle).Matrix;
                }
            }

            const SpriteRendererComponent* l_Sprite = l_Registry.try_get<SpriteRendererComponent>(l_Handle);
            if (l_Sprite == nullptr)
            {
                continue;
            }

            // Sprites often share a texture with the one before, so the last lookup is kept
            if (l_Sprite->Texture != l_LastTexture || m_Instances.empty())
            {
                l_LastTexture = l_Sprite->Texture;
                l_LastIndex = ResolveTexture(l_Sprite->Texture);
            }

            // A flip swaps the rectangle's edges, so the quad keeps its winding and its place in the hierarchy's transform
            const glm::mat4& l_World = l_Registry.get<WorldTransformComponent>(l_Handle).Matrix;
            const float l_MinU = l_Sprite->FlipX ? l_Sprite->UVRect.z : l_Sprite->UVRect.x;
            const float l_MaxU = l_Sprite->FlipX ? l_Sprite->UVRect.x : l_Sprite->UVRect.z;
            const float l_MinV = l_Sprite->FlipY ? l_Sprite->UVRect.w : l_Sprite->UVRect.y;
            const float l_MaxV = l_Sprite->FlipY ? l_Sprite->UVRect.y : l_Sprite->UVRect.w;

            SpriteInstance& l_Instance = m_Instances.emplace_back();
            for (std::size_t it_Row = 0; it_Row < 3; ++it_Row)
            {
                for (std::size_t it_Column = 0; it_Column < 4; ++it_Column)
                {
                    l_Instance.Rows[it_Row * 4 + it_Column] = l_World[static_cast<glm::length_t>(it_Column)][static_cast<glm::length_t>(it_Row)];
                }
            }

            l_Instance.UVRect = { PackUnorm16(l_MinU, l_MinV), PackUnorm16(l_MaxU, l_MaxV) };
            l_Instance.Color = PackColor(l_Sprite->Tint);
            l_Instance.Texture = l_LastIndex;

            m_Keys.push_back({ l_Sprite->SortingLayer, l_Sprite->OrderInLayer, static_cast<std::uint32_t>(m_Keys.size()) });
            m_EntityIDs.push_back(ToPickID(l_Handle));
        }

        return l_Camera;
    }

    // Sorted by layer, then order in layer, then hierarchy order, and drawn back to front in one instanced draw: each sprite names its own texture, so a change of texture never splits it
    void Renderer2D::DrawSprites(RHI::CommandList& commands, const glm::mat4& viewProjection, RHI::Format targetFormat, std::uint32_t width, std::uint32_t height)
    {
        TR_PROFILE_FUNCTION();

        m_Statistics = { static_cast<std::uint32_t>(m_Instances.size()), 0, static_cast<std::uint32_t>(m_Textures.size()) };
        if (m_Instances.empty())
        {
            return;
        }

        const RHI::PipelineHandle l_Pipeline = GetPipeline(targetFormat);
        if (!l_Pipeline)
        {
            return;
        }

        std::ranges::sort(m_Keys, [](const SortKey& left, const SortKey& right) { return IsDrawnBefore(left.Layer, left.Order, left.Index, right.Layer, right.Order, right.Index); });

        const RHI::UploadAllocation l_Upload = m_Device.AllocateUpload(std::uint64_t{ m_Instances.size() } * sizeof(SpriteInstance), c_InstanceAlignment);
        if (l_Upload.Data.empty() || l_Upload.ShaderResourceIndex == RHI::c_NoBindlessIndex)
        {
            TR_CORE_ERROR("Renderer2D: no upload memory for {} sprites, so they are not drawn this frame", m_Instances.size());

            return;
        }

        for (std::size_t it_Sprite = 0; it_Sprite < m_Keys.size(); ++it_Sprite)
        {
            std::memcpy(l_Upload.Data.data() + it_Sprite * sizeof(SpriteInstance), &m_Instances[m_Keys[it_Sprite].Index], sizeof(SpriteInstance));
        }

        PushData l_Push;
        for (glm::length_t it_Column = 0; it_Column < 4; ++it_Column)
        {
            l_Push.ViewProjection[static_cast<std::size_t>(it_Column)] = viewProjection[it_Column];
        }

        l_Push.Instances = { l_Upload.ShaderResourceIndex, 0 };
        l_Push.LinearSampler = { m_Device.GetSamplerIndex(m_LinearSampler), 0 };
        l_Push.NearestSampler = { m_Device.GetSamplerIndex(m_NearestSampler), 0 };
        l_Push.InstanceOffset = static_cast<std::uint32_t>(l_Upload.Offset);

        // The whole target, so the sprites need nothing from whoever began the rendering pass
        commands.SetPipeline(l_Pipeline);
        commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f });
        commands.SetScissor({ 0, 0, width, height });
        commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
        commands.Draw(4, static_cast<std::uint32_t>(m_Instances.size()), 0, 0);

        m_Statistics.DrawCalls = 1;
    }

    // An invalid UUID, a texture still loading and one that failed all draw white: the loader's placeholder, or the white texture when there is none
    std::uint32_t Renderer2D::ResolveTexture(UUID id)
    {
        if (!id)
        {
            return m_WhiteTexture;
        }

        auto a_Found = m_Textures.find(id);
        if (a_Found == m_Textures.end())
        {
            a_Found = m_Textures.emplace(id, CachedTexture{ AssetRef<TextureAsset>(id), m_Frame }).first;
        }

        a_Found->second.LastUsedFrame = m_Frame;

        const Asset* l_Asset = AssetManager::GetAsset(id);
        if (l_Asset == nullptr || l_Asset->GetAssetType() != TextureAsset::c_AssetType)
        {
            return m_WhiteTexture;
        }

        const TextureAsset* l_Texture = static_cast<const TextureAsset*>(l_Asset);
        if (!l_Texture->GetTexture() || l_Texture->GetShaderResourceIndex() == RHI::c_NoBindlessIndex)
        {
            return m_WhiteTexture;
        }

        return l_Texture->GetShaderResourceIndex() | (l_Texture->GetFilter() == RHI::Filter::Nearest ? c_NearestBit : 0u);
    }

    // One pipeline for each target format sprites are drawn into, made the first time it is needed
    RHI::PipelineHandle Renderer2D::GetPipeline(RHI::Format targetFormat)
    {
        const auto a_Found = std::ranges::find(m_Pipelines, targetFormat, &std::pair<RHI::Format, RHI::PipelineHandle>::first);
        if (a_Found != m_Pipelines.end())
        {
            return a_Found->second;
        }

        if (m_VertexShader.empty() || m_PixelShader.empty())
        {
            if (!m_ReportedNoShaders)
            {
                TR_CORE_INFO("Renderer2D: no Sprite shaders under /engine/shaders, so sprites are not drawn");
                m_ReportedNoShaders = true;
            }

            return {};
        }

        const std::array<RHI::Format, 1> l_ColorFormats{ targetFormat };

        RHI::GraphicsPipelineDescription l_Description;
        l_Description.VertexShader = { m_VertexShader, "VertexMain" };
        l_Description.PixelShader = { m_PixelShader, "PixelMain" };
        l_Description.ColorFormats = l_ColorFormats;
        l_Description.Topology = RHI::PrimitiveTopology::TriangleStrip;
        l_Description.Cull = RHI::CullMode::None;
        l_Description.AlphaBlend = true;
        l_Description.DebugName = "Sprites";

        const RHI::PipelineHandle l_Pipeline = m_Device.CreateGraphicsPipeline(l_Description);
        if (!l_Pipeline)
        {
            TR_CORE_ERROR("Renderer2D: the sprite pipeline for {} targets could not be created", RHI::ToString(targetFormat));
        }

        // A failed format is remembered too, so it is not tried again every frame
        m_Pipelines.emplace_back(targetFormat, l_Pipeline);

        return l_Pipeline;
    }

    // Made the first time it is needed, and not tried again if that fails
    RHI::PipelineHandle Renderer2D::GetIDPipeline()
    {
        if (m_IDPipelineTried)
        {
            return m_IDPipeline;
        }

        m_IDPipelineTried = true;
        if (m_PickVertexShader.empty() || m_PickPixelShader.empty())
        {
            TR_CORE_INFO("Renderer2D: no Sprite pick shaders under /engine/shaders, so sprites cannot be picked");

            return {};
        }

        const std::array<RHI::Format, 1> l_ColorFormats{ RHI::Format::R32Uint };

        RHI::GraphicsPipelineDescription l_Description;
        l_Description.VertexShader = { m_PickVertexShader, "PickVertexMain" };
        l_Description.PixelShader = { m_PickPixelShader, "PickPixelMain" };
        l_Description.ColorFormats = l_ColorFormats;
        l_Description.Topology = RHI::PrimitiveTopology::TriangleStrip;
        l_Description.Cull = RHI::CullMode::None;
        l_Description.DebugName = "Sprite entity IDs";

        m_IDPipeline = m_Device.CreateGraphicsPipeline(l_Description);
        if (!m_IDPipeline)
        {
            TR_CORE_ERROR("Renderer2D: the sprite entity ID pipeline could not be created, so sprites cannot be picked");
        }

        return m_IDPipeline;
    }
}