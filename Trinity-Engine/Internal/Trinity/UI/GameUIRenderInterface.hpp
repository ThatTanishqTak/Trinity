#pragma once

#include "Trinity/Core/Memory.hpp"

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

    // What one context drew in a frame, in RmlUi's order, which the view's UI pass plays back. Transforms are kept apart, each command naming the one it set
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

        void Clear()
        {
            Commands.clear();
            Transforms.clear();
        }
    };

    // RmlUi's drawing, recorded into the draw list of the context rendering, for the view's UI pass to play back. Geometry and generated textures are kept as RmlUi hands them over, its vertices with premultiplied colours and its textures as premultiplied RGBA8. Those released are freed as the next frame begins, so nothing a recorded list names goes before it has been played back
    class GameUIRenderInterface final : public Rml::RenderInterface
    {
    public:
        struct Geometry
        {
            GameUIVector<Rml::Vertex> Vertices;
            GameUIVector<int> Indices;
        };

        struct Texture
        {
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

        GameUIRenderInterface() = default;
        ~GameUIRenderInterface() override;

        // Frees what was released while the frame before was recorded and played back
        void BeginFrame();
        // Every call until End goes into the list, which is cleared first
        void Begin(GameUIDrawList& list);
        void End();

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
        void Record(const GameUIDrawList::Command& command);
        void Free(Geometry* geometry);
        void Free(Texture* texture);

        GameUIDrawList* m_List = nullptr;
        GameUIVector<Geometry*> m_ReleasedGeometry;
        GameUIVector<Texture*> m_ReleasedTextures;
        Statistics m_Statistics;
    };
}