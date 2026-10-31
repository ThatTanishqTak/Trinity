#pragma once

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/TextureAsset.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Device.hpp"

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Vertex.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Trinity
{
    template<typename T>
    using GameUIVector = std::vector<T, TaggedAllocator<T, MemoryTag::GameUI>>;

    // What one context drew in a frame, in RmlUi's order, which the view's overlay pass plays back. Transforms are kept apart, each command naming the one it set
    struct GameUIDrawList
    {
        enum class CommandType : std::uint8_t
        {
            Geometry,
            EnableScissor,
            SetScissor,
            EnableClipMask,
            RenderToClipMask,
            SetTransform
        };

        // Geometry is drawn at its translation with its texture, or untextured for 0. A scissor is in the context's pixels, left, top, width and height. A transform of c_Identity is none
        struct Command
        {
            CommandType Type = CommandType::Geometry;
            bool Enable = false;
            Rml::ClipMaskOperation Operation = Rml::ClipMaskOperation::Set;
            std::uint32_t Transform = 0;
            Rml::CompiledGeometryHandle Geometry = 0;
            Rml::TextureHandle Texture = 0;
            std::array<float, 2> Translation{};
            std::array<std::int32_t, 4> Scissor{};
        };

        static constexpr std::uint32_t c_Identity = ~0u;

        GameUIVector<Command> Commands;
        // Column-major, as RmlUi keeps them
        GameUIVector<std::array<float, 16>> Transforms;
        // Whether it draws a clip mask, which its pass needs a stencil target for
        bool ClipMask = false;

        void Clear()
        {
            Commands.clear();
            Transforms.clear();
            ClipMask = false;
        }
    };

    // RmlUi's drawing through the RHI. Each context's calls are recorded into its draw list as it renders, for its view's overlay pass to play back in display space, blending colours multiplied by their alpha as RmlUi gives them. Geometry is kept as RmlUi hands it over and goes through the upload ring each frame it is drawn. A texture RmlUi generates, such as a font atlas, is uploaded as the next frame begins, and an image is a project's texture, named by its path in the assets. What RmlUi releases is freed as the next frame begins, so nothing a recorded list names goes before it has been played back. Without the shaders, everything is still recorded and kept, but nothing is drawn
    class GameUIRenderInterface final : public Rml::RenderInterface
    {
    public:
        struct Geometry
        {
            GameUIVector<Rml::Vertex> Vertices;
            GameUIVector<int> Indices;
        };

        // A generated texture's texture and pixels are its own, until it is uploaded, and an image's are its asset's
        struct Texture
        {
            RHI::TextureHandle Handle;
            AssetRef<TextureAsset> Image;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            GameUIVector<std::byte> Pixels;
        };

        struct Statistics
        {
            std::uint32_t Geometries = 0;
            std::uint32_t Textures = 0;
            std::uint64_t GeometryBytes = 0;
            std::uint64_t TextureBytes = 0;
        };

        GameUIRenderInterface(RHI::Device& device, RHI::Format displayFormat, RHI::Format stencilFormat);
        ~GameUIRenderInterface() override;

        GameUIRenderInterface(const GameUIRenderInterface&) = delete;
        GameUIRenderInterface& operator=(const GameUIRenderInterface&) = delete;

        // Frees what was released while the frame before was recorded and played back
        void BeginFrame();
        // Every call until End goes into the list, which is cleared first
        void Begin(GameUIDrawList& list);
        void End();

        // Inside the frame, before the frame graph runs
        void RecordUploads(RHI::CommandList& commands);
        [[nodiscard]] bool CanDraw() const { return static_cast<bool>(m_DrawPipeline); }
        // Whether a list that draws a clip mask can, which needs a stencil format the device takes
        [[nodiscard]] bool CanClip() const { return static_cast<bool>(m_MaskedPipeline); }
        // In the view's overlay pass, with a stencil target when the list draws a clip mask and CanClip
        void Draw(RHI::CommandList& commands, const GameUIDrawList& list, std::uint32_t width, std::uint32_t height, bool stencil) const;

        [[nodiscard]] static const Geometry& GetGeometry(Rml::CompiledGeometryHandle geometry) { return *reinterpret_cast<const Geometry*>(geometry); }
        [[nodiscard]] static const Texture& GetTexture(Rml::TextureHandle texture) { return *reinterpret_cast<const Texture*>(texture); }
        [[nodiscard]] Statistics GetStatistics() const { return m_Statistics; }

        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
        void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

        Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
        void ReleaseTexture(Rml::TextureHandle texture) override;

        void EnableScissorRegion(bool enable) override;
        void SetScissorRegion(Rml::Rectanglei region) override;

        void EnableClipMask(bool enable) override;
        void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;

        void SetTransform(const Rml::Matrix4f* transform) override;

    private:
        void CreatePipelines(RHI::Format displayFormat, RHI::Format stencilFormat);
        void Upload(RHI::CommandList& commands, Texture& texture);
        void Record(const GameUIDrawList::Command& command);
        void Free(Geometry* geometry);
        void Free(Texture* texture);

        RHI::Device& m_Device;
        RHI::SamplerHandle m_Sampler;
        // Drawn in a pass without a stencil target, then in one with it: not tested, tested against the clip mask, and the mask's own two, which write no colour
        RHI::PipelineHandle m_DrawPipeline;
        RHI::PipelineHandle m_StencilDrawPipeline;
        RHI::PipelineHandle m_MaskedPipeline;
        RHI::PipelineHandle m_MaskReplacePipeline;
        RHI::PipelineHandle m_MaskIncrementPipeline;

        GameUIDrawList* m_List = nullptr;
        GameUIVector<Texture*> m_Uploads;
        GameUIVector<Geometry*> m_ReleasedGeometry;
        GameUIVector<Texture*> m_ReleasedTextures;
        Statistics m_Statistics;
    };
}