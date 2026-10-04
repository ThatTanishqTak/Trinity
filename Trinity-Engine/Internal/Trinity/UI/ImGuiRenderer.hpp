#pragma once

#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Device.hpp"

#include <cstdint>
#include <vector>

struct ImDrawData;
struct ImTextureData;
struct ImTextureRect;

namespace Trinity
{
    // Draws Dear ImGui through the RHI. Vertices and indices go into the upload ring every frame, textures follow ImGui's create, update and destroy requests, and ImTextureID is a texture's bindless index
    class ImGuiRenderer
    {
    public:
        ImGuiRenderer(RHI::Device& device, RHI::Format outputFormat);
        ~ImGuiRenderer();

        ImGuiRenderer(const ImGuiRenderer&) = delete;
        ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

        void UpdateTextures(RHI::CommandList& commands);
        void Render(RHI::CommandList& commands, const ImDrawData& drawData);
        void DestroyTextures();

    private:
        struct TextureEntry
        {
            const ImTextureData* Data = nullptr;
            RHI::TextureHandle Texture;
        };

        void CreateTexture(RHI::CommandList& commands, ImTextureData& texture);
        void UploadTexture(RHI::CommandList& commands, RHI::TextureHandle destination, ImTextureData& texture, const ImTextureRect& rect, RHI::ResourceState before);
        void DestroyTexture(ImTextureData& texture);
        [[nodiscard]] RHI::TextureHandle FindTexture(const ImTextureData& texture) const;

        RHI::Device& m_Device;
        RHI::PipelineHandle m_Pipeline;
        RHI::SamplerHandle m_LinearSampler;
        RHI::SamplerHandle m_NearestSampler;
        std::vector<TextureEntry, TaggedAllocator<TextureEntry, MemoryTag::UI>> m_Textures;
        std::uint32_t m_CreatedTextures = 0;
        std::uint32_t m_UpdatedTextures = 0;
        std::uint32_t m_DestroyedTextures = 0;
    };
}