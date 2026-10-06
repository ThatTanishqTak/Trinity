#pragma once

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/TextureAsset.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/RHI/Device.hpp"
#include "Trinity/Scene/Entity.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    class Scene;
    struct CameraComponent;

    [[nodiscard]] TRINITY_API Entity PickSprite(Scene& scene, const glm::vec2& worldPoint);

    class TRINITY_API Renderer2D
    {
    public:
        struct Statistics
        {
            std::uint32_t Sprites = 0;
            std::uint32_t DrawCalls = 0;
            std::uint32_t Textures = 0;
        };

        Renderer2D(RHI::Device& device, std::uint32_t whiteTexture);
        ~Renderer2D();

        Renderer2D(const Renderer2D&) = delete;
        Renderer2D& operator=(const Renderer2D&) = delete;

        void BeginFrame();

        bool DrawScene(RHI::CommandList& commands, Scene& scene, RHI::Format targetFormat, std::uint32_t width, std::uint32_t height);
        void DrawScene(RHI::CommandList& commands, Scene& scene, const glm::mat4& viewProjection, RHI::Format targetFormat, std::uint32_t width, std::uint32_t height);

        [[nodiscard]] const Statistics& GetStatistics() const { return m_Statistics; }

    private:
        struct SpriteInstance
        {
            std::array<float, 12> Rows{};
            std::array<std::uint32_t, 2> UVRect{};
            std::uint32_t Color = 0;
            std::uint32_t Texture = 0;
        };

        struct SortKey
        {
            std::int32_t Layer = 0;
            std::int32_t Order = 0;
            std::uint32_t Index = 0;
        };

        struct CachedTexture
        {
            AssetRef<TextureAsset> Texture;
            std::uint64_t LastUsedFrame = 0;
        };

        using TextureCache = std::unordered_map<UUID, CachedTexture, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, CachedTexture>, MemoryTag::Renderer>>;

        const CameraComponent* CollectSprites(Scene& scene, glm::mat4& cameraWorld);
        void DrawSprites(RHI::CommandList& commands, const glm::mat4& viewProjection, RHI::Format targetFormat, std::uint32_t width, std::uint32_t height);
        [[nodiscard]] std::uint32_t ResolveTexture(UUID id);
        [[nodiscard]] RHI::PipelineHandle GetPipeline(RHI::Format targetFormat);

        RHI::Device& m_Device;
        std::uint32_t m_WhiteTexture = RHI::c_NoBindlessIndex;
        RHI::SamplerHandle m_LinearSampler;
        RHI::SamplerHandle m_NearestSampler;
        FileBuffer m_VertexShader;
        FileBuffer m_PixelShader;
        std::vector<std::pair<RHI::Format, RHI::PipelineHandle>, TaggedAllocator<std::pair<RHI::Format, RHI::PipelineHandle>, MemoryTag::Renderer>> m_Pipelines;
        std::vector<SpriteInstance, TaggedAllocator<SpriteInstance, MemoryTag::Renderer>> m_Instances;
        std::vector<SortKey, TaggedAllocator<SortKey, MemoryTag::Renderer>> m_Keys;
        TextureCache m_Textures;
        Statistics m_Statistics;
        std::uint64_t m_Frame = 0;
        bool m_ReportedNoCamera = false;
        bool m_ReportedNoShaders = false;
    };
}