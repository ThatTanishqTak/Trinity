#pragma once

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/TextureAsset.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Renderer/WorldUI.hpp"
#include "Trinity/RHI/Device.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/UI/UIWorldSettings.hpp"

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

    // Draws a scene's SpriteRenderers, and the world UI documents sorted with them, which are drawn as sprites showing their textures
    class TRINITY_API Renderer2D
    {
    public:
        struct Statistics
        {
            std::uint32_t Sprites = 0;
            std::uint32_t DrawCalls = 0;
            std::uint32_t Textures = 0;
            // Of the sprites, the world UI documents'
            std::uint32_t WorldDocuments = 0;
        };

        Renderer2D(RHI::Device& device, std::uint32_t whiteTexture);
        ~Renderer2D();

        Renderer2D(const Renderer2D&) = delete;
        Renderer2D& operator=(const Renderer2D&) = delete;

        void BeginFrame();
        void ReleaseTextures();
        // What a scene's world UI documents sorted with the sprites are drawn from, or none, when they are not drawn
        void SetWorldUISource(const WorldUISource* source) { m_WorldUI = source; }

        bool DrawScene(RHI::CommandList& commands, Scene& scene, RHI::Format targetFormat, std::uint32_t width, std::uint32_t height);
        void DrawScene(RHI::CommandList& commands, Scene& scene, const glm::mat4& viewProjection, RHI::Format targetFormat, std::uint32_t width, std::uint32_t height);
        // Each sprite's entity, as ToPickID gives it, into an R32Uint target, in the order DrawScene draws them, so a pixel holds the sprite on top there
        void DrawSceneIDs(RHI::CommandList& commands, Scene& scene, const glm::mat4& viewProjection, std::uint32_t width, std::uint32_t height);

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

        // A world UI document's sprite, which is put in place once the view it is drawn through is known, as it may face the camera
        struct WorldQuad
        {
            std::uint32_t Instance = 0;
            glm::mat4 World{ 1.0f };
            UIWorldSettings Settings;
            glm::uvec2 Size{ 0 };
        };

        struct CachedTexture
        {
            AssetRef<TextureAsset> Texture;
            std::uint64_t LastUsedFrame = 0;
        };

        using TextureCache = std::unordered_map<UUID, CachedTexture, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, CachedTexture>, MemoryTag::Renderer>>;

        const CameraComponent* CollectSprites(Scene& scene, glm::mat4& cameraWorld);
        void CollectWorldQuad(Scene& scene, Entity entity);
        // Each world UI document's sprite through this view-projection
        void PlaceWorldQuads(const glm::mat4& viewProjection);
        void DrawSprites(RHI::CommandList& commands, const glm::mat4& viewProjection, RHI::Format targetFormat, std::uint32_t width, std::uint32_t height);
        [[nodiscard]] std::uint32_t ResolveTexture(UUID id);
        [[nodiscard]] RHI::PipelineHandle GetPipeline(RHI::Format targetFormat);
        [[nodiscard]] RHI::PipelineHandle GetIDPipeline();

        RHI::Device& m_Device;
        std::uint32_t m_WhiteTexture = RHI::c_NoBindlessIndex;
        RHI::SamplerHandle m_LinearSampler;
        RHI::SamplerHandle m_NearestSampler;
        FileBuffer m_VertexShader;
        FileBuffer m_PixelShader;
        FileBuffer m_PickVertexShader;
        FileBuffer m_PickPixelShader;
        RHI::PipelineHandle m_IDPipeline;
        bool m_IDPipelineTried = false;
        std::vector<std::pair<RHI::Format, RHI::PipelineHandle>, TaggedAllocator<std::pair<RHI::Format, RHI::PipelineHandle>, MemoryTag::Renderer>> m_Pipelines;
        std::vector<SpriteInstance, TaggedAllocator<SpriteInstance, MemoryTag::Renderer>> m_Instances;
        std::vector<SortKey, TaggedAllocator<SortKey, MemoryTag::Renderer>> m_Keys;
        // Each instance's entity, as ToPickID gives it, in the same order
        std::vector<std::uint32_t, TaggedAllocator<std::uint32_t, MemoryTag::Renderer>> m_EntityIDs;
        std::vector<WorldQuad, TaggedAllocator<WorldQuad, MemoryTag::Renderer>> m_WorldQuads;
        const WorldUISource* m_WorldUI = nullptr;
        TextureCache m_Textures;
        Statistics m_Statistics;
        std::uint64_t m_Frame = 0;
        bool m_ReportedNoCamera = false;
        bool m_ReportedNoShaders = false;
    };
}