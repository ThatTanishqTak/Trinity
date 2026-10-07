#include "Trinity/UI/ImGuiRenderer.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <span>
#include <string_view>

namespace Trinity
{
    namespace
    {
        // Laid out as ImGui.slang reads it
        struct PushData
        {
            std::array<std::uint32_t, 2> Vertices;
            std::array<std::uint32_t, 2> Texture;
            std::array<std::uint32_t, 2> Sampler;
            std::uint32_t VertexOffset = 0;
            std::uint32_t Flags = 0;
            std::array<float, 2> Scale;
            std::array<float, 2> Translate;
        };

        static_assert(sizeof(PushData) == 48);
        static_assert(sizeof(ImDrawVert) == 20, "ImGui.slang reads 20-byte vertices");

        // Empty: the draw loop recognises them and does the work, as ImGui's own backends do
        void ResetRenderState([[maybe_unused]] const ImDrawList* list, [[maybe_unused]] const ImDrawCmd* command)
        {

        }

        void SetSamplerLinear([[maybe_unused]] const ImDrawList* list, [[maybe_unused]] const ImDrawCmd* command)
        {

        }

        void SetSamplerNearest([[maybe_unused]] const ImDrawList* list, [[maybe_unused]] const ImDrawCmd* command)
        {

        }

        RHI::SamplerHandle CreateClampSampler(RHI::Device& device, RHI::Filter filter, std::string_view name)
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
    }

    ImGuiRenderer::ImGuiRenderer(RHI::Device& device, RHI::Format outputFormat) : m_Device(device)
    {
        m_LinearSampler = CreateClampSampler(m_Device, RHI::Filter::Linear, "ImGui linear sampler");
        m_NearestSampler = CreateClampSampler(m_Device, RHI::Filter::Nearest, "ImGui nearest sampler");

        ImGuiPlatformIO& l_PlatformIO = ImGui::GetPlatformIO();
        l_PlatformIO.DrawCallback_ResetRenderState = &ResetRenderState;
        l_PlatformIO.DrawCallback_SetSamplerLinear = &SetSamplerLinear;
        l_PlatformIO.DrawCallback_SetSamplerNearest = &SetSamplerNearest;

        const std::string_view l_Extension = m_Device.GetInfo().API == GraphicsAPI::D3D12 ? "dxil" : "spv";
        const Expected<FileBuffer, FileError> l_VertexShader = FileSystem::ReadFile(std::format("/engine/shaders/ImGui.VertexMain.{}", l_Extension));
        const Expected<FileBuffer, FileError> l_PixelShader = FileSystem::ReadFile(std::format("/engine/shaders/ImGui.PixelMain.{}", l_Extension));
        if (!l_VertexShader || !l_PixelShader)
        {
            TR_CORE_INFO("ImGui: no {} shaders under /engine/shaders, so the UI is built and its textures kept, but nothing is drawn", l_Extension);

            return;
        }

        const std::array<RHI::Format, 1> l_ColorFormats{ outputFormat };

        RHI::GraphicsPipelineDescription l_Description;
        l_Description.VertexShader = { *l_VertexShader, "VertexMain" };
        l_Description.PixelShader = { *l_PixelShader, "PixelMain" };
        l_Description.ColorFormats = l_ColorFormats;
        l_Description.Cull = RHI::CullMode::None;
        l_Description.AlphaBlend = true;
        l_Description.DebugName = "ImGui";

        m_Pipeline = m_Device.CreateGraphicsPipeline(l_Description);
        if (!m_Pipeline)
        {
            TR_CORE_ERROR("ImGui: the pipeline could not be created, so nothing is drawn");
        }
    }

    ImGuiRenderer::~ImGuiRenderer()
    {
        ImGuiPlatformIO& l_PlatformIO = ImGui::GetPlatformIO();
        l_PlatformIO.DrawCallback_ResetRenderState = nullptr;
        l_PlatformIO.DrawCallback_SetSamplerLinear = nullptr;
        l_PlatformIO.DrawCallback_SetSamplerNearest = nullptr;

        m_Device.DestroyPipeline(m_Pipeline);
        m_Device.DestroySampler(m_LinearSampler);
        m_Device.DestroySampler(m_NearestSampler);

        TR_CORE_INFO("ImGui: the renderer created {}, updated {} and destroyed {} texture(s)", m_CreatedTextures, m_UpdatedTextures, m_DestroyedTextures);
    }

    // A texture that ImGui no longer uses in any frame is destroyed at once, since the device's release queue keeps it until the frames in flight have finished
    void ImGuiRenderer::UpdateTextures(RHI::CommandList& commands)
    {
        TR_PROFILE_FUNCTION();

        for (ImTextureData* it_Texture : ImGui::GetPlatformIO().Textures)
        {
            switch (it_Texture->Status)
            {
                case ImTextureStatus_WantCreate:
                {
                    CreateTexture(commands, *it_Texture);

                    break;
                }
                case ImTextureStatus_WantUpdates:
                {
                    UploadTexture(commands, FindTexture(*it_Texture), *it_Texture, it_Texture->UpdateRect, RHI::ResourceState::ShaderResource);
                    it_Texture->SetStatus(ImTextureStatus_OK);
                    ++m_UpdatedTextures;

                    break;
                }
                case ImTextureStatus_WantDestroy:
                {
                    DestroyTexture(*it_Texture);

                    break;
                }
                default:
                {
                    break;
                }
            }
        }
    }

    void ImGuiRenderer::DestroyTextures()
    {
        for (ImTextureData* it_Texture : ImGui::GetPlatformIO().Textures)
        {
            if (it_Texture->RefCount == 1)
            {
                DestroyTexture(*it_Texture);
            }
        }
    }

    void ImGuiRenderer::CreateTexture(RHI::CommandList& commands, ImTextureData& texture)
    {
        TR_CORE_ASSERT(texture.Format == ImTextureFormat_RGBA32, "The ImGui renderer only takes RGBA32 textures.");

        RHI::TextureDescription l_Description;
        l_Description.Width = static_cast<std::uint32_t>(texture.Width);
        l_Description.Height = static_cast<std::uint32_t>(texture.Height);
        l_Description.TextureFormat = RHI::Format::RGBA8Unorm;
        l_Description.Usage = RHI::TextureUsage::ShaderResource | RHI::TextureUsage::CopyDestination;
        l_Description.DebugName = "ImGui texture";

        const RHI::TextureHandle l_Texture = m_Device.CreateTexture(l_Description);
        const std::uint32_t l_Index = l_Texture ? m_Device.GetShaderResourceIndex(l_Texture) : RHI::c_NoBindlessIndex;
        if (l_Index == RHI::c_NoBindlessIndex)
        {
            TR_CORE_ERROR("ImGui: a {}x{} texture could not be created", texture.Width, texture.Height);
            m_Device.DestroyTexture(l_Texture);

            return;
        }

        const ImTextureRect l_Whole{ 0, 0, static_cast<unsigned short>(texture.Width), static_cast<unsigned short>(texture.Height) };
        UploadTexture(commands, l_Texture, texture, l_Whole, RHI::ResourceState::Undefined);

        m_Textures.push_back({ &texture, l_Texture });
        texture.SetTexID(static_cast<ImTextureID>(l_Index));
        texture.SetStatus(ImTextureStatus_OK);
        ++m_CreatedTextures;
    }

    // Small uploads come from the upload ring. One too large for it, such as a whole atlas, gets its own staging buffer, destroyed at once and kept by the release queue until the copy has run
    void ImGuiRenderer::UploadTexture(RHI::CommandList& commands, RHI::TextureHandle destination, ImTextureData& texture, const ImTextureRect& rect, RHI::ResourceState before)
    {
        if (!destination || rect.w == 0 || rect.h == 0)
        {
            return;
        }

        const std::uint64_t l_RowPitch = RHI::GetTextureCopyRowPitch(RHI::Format::RGBA8Unorm, rect.w);
        const std::uint64_t l_Size = l_RowPitch * rect.h;

        RHI::BufferHandle l_Staging;
        std::uint64_t l_Offset = 0;
        std::span<std::byte> l_Data;
        const bool l_FromRing = l_Size <= m_Device.GetUploadCapacity() / 4;
        if (l_FromRing)
        {
            const RHI::UploadAllocation l_Allocation = m_Device.AllocateUpload(l_Size, RHI::c_TextureCopyOffsetAlignment);
            l_Staging = l_Allocation.Buffer;
            l_Offset = l_Allocation.Offset;
            l_Data = l_Allocation.Data;
        }
        else
        {
            RHI::BufferDescription l_Description;
            l_Description.Size = l_Size;
            l_Description.Memory = RHI::MemoryType::Upload;
            l_Description.DebugName = "ImGui texture staging";

            l_Staging = m_Device.CreateBuffer(l_Description);
            l_Data = l_Staging ? m_Device.GetMappedData(l_Staging) : std::span<std::byte>();
        }

        if (l_Data.empty())
        {
            TR_CORE_ERROR("ImGui: no staging memory for a {}x{} texture upload", rect.w, rect.h);
            m_Device.DestroyBuffer(l_FromRing ? RHI::BufferHandle{} : l_Staging);

            return;
        }

        const std::size_t l_RowSize = static_cast<std::size_t>(rect.w) * static_cast<std::size_t>(texture.BytesPerPixel);
        for (std::uint32_t it_Row = 0; it_Row < rect.h; ++it_Row)
        {
            std::memcpy(l_Data.data() + it_Row * l_RowPitch, texture.GetPixelsAt(rect.x, rect.y + static_cast<int>(it_Row)), l_RowSize);
        }

        commands.TextureBarrier(destination, before, RHI::ResourceState::CopyDestination);
        commands.CopyBufferToTexture(l_Staging, l_Offset, destination, 0, 0, { rect.x, rect.y, rect.w, rect.h });
        commands.TextureBarrier(destination, RHI::ResourceState::CopyDestination, RHI::ResourceState::ShaderResource);

        if (!l_FromRing)
        {
            m_Device.DestroyBuffer(l_Staging);
        }
    }

    void ImGuiRenderer::DestroyTexture(ImTextureData& texture)
    {
        const auto a_Entry = std::ranges::find(m_Textures, &texture, &TextureEntry::Data);
        if (a_Entry != m_Textures.end())
        {
            m_Device.DestroyTexture(a_Entry->Texture);
            *a_Entry = m_Textures.back();
            m_Textures.pop_back();
            ++m_DestroyedTextures;
        }

        texture.SetTexID(ImTextureID_Invalid);
        texture.SetStatus(ImTextureStatus_Destroyed);
    }

    RHI::TextureHandle ImGuiRenderer::FindTexture(const ImTextureData& texture) const
    {
        const auto a_Entry = std::ranges::find(m_Textures, &texture, &TextureEntry::Data);

        return a_Entry != m_Textures.end() ? a_Entry->Texture : RHI::TextureHandle{};
    }

    // Draws inside the output pass with its full viewport. Every command gets its own scissor, vertex base and texture, and the scissor covers the output again afterwards
    void ImGuiRenderer::Render(RHI::CommandList& commands, const ImDrawData& drawData)
    {
        TR_PROFILE_FUNCTION();

        const float l_Width = drawData.DisplaySize.x * drawData.FramebufferScale.x;
        const float l_Height = drawData.DisplaySize.y * drawData.FramebufferScale.y;
        if (!m_Pipeline || drawData.TotalVtxCount == 0 || l_Width <= 0.0f || l_Height <= 0.0f)
        {
            return;
        }

        const RHI::UploadAllocation l_Vertices = m_Device.AllocateUpload(static_cast<std::uint64_t>(drawData.TotalVtxCount) * sizeof(ImDrawVert), 4);
        const RHI::UploadAllocation l_Indices = m_Device.AllocateUpload(static_cast<std::uint64_t>(drawData.TotalIdxCount) * sizeof(ImDrawIdx), sizeof(ImDrawIdx));
        if (l_Vertices.Data.empty() || l_Indices.Data.empty())
        {
            return;
        }

        std::size_t l_VertexBytes = 0;
        std::size_t l_IndexBytes = 0;
        for (const ImDrawList* it_List : drawData.CmdLists)
        {
            const std::size_t l_ListVertexBytes = static_cast<std::size_t>(it_List->VtxBuffer.Size) * sizeof(ImDrawVert);
            const std::size_t l_ListIndexBytes = static_cast<std::size_t>(it_List->IdxBuffer.Size) * sizeof(ImDrawIdx);
            std::memcpy(l_Vertices.Data.data() + l_VertexBytes, it_List->VtxBuffer.Data, l_ListVertexBytes);
            std::memcpy(l_Indices.Data.data() + l_IndexBytes, it_List->IdxBuffer.Data, l_ListIndexBytes);
            l_VertexBytes += l_ListVertexBytes;
            l_IndexBytes += l_ListIndexBytes;
        }

        const RHI::IndexFormat l_IndexFormat = sizeof(ImDrawIdx) == 2 ? RHI::IndexFormat::UInt16 : RHI::IndexFormat::UInt32;
        const std::uint32_t l_LinearSampler = m_Device.GetSamplerIndex(m_LinearSampler);
        const std::uint32_t l_NearestSampler = m_Device.GetSamplerIndex(m_NearestSampler);

        // Pixels from DisplayPos to DisplayPos + DisplaySize map to clip space with Y up
        PushData l_PushData;
        l_PushData.Vertices = { l_Vertices.ShaderResourceIndex, 0 };
        l_PushData.Sampler = { l_LinearSampler, 0 };
        l_PushData.Scale = { 2.0f / drawData.DisplaySize.x, -2.0f / drawData.DisplaySize.y };
        l_PushData.Translate = { -1.0f - drawData.DisplayPos.x * l_PushData.Scale[0], 1.0f - drawData.DisplayPos.y * l_PushData.Scale[1] };

        const auto a_SetupRenderState = [&]()
        {
            commands.SetPipeline(m_Pipeline);
            commands.SetIndexBuffer(l_Indices.Buffer, l_Indices.Offset, l_IndexFormat);
            l_PushData.Sampler = { l_LinearSampler, 0 };
        };

        a_SetupRenderState();

        const ImVec2 l_ClipOffset = drawData.DisplayPos;
        const ImVec2 l_ClipScale = drawData.FramebufferScale;
        std::uint32_t l_GlobalVertex = 0;
        std::uint32_t l_GlobalIndex = 0;
        for (const ImDrawList* it_List : drawData.CmdLists)
        {
            for (const ImDrawCmd& it_Command : it_List->CmdBuffer)
            {
                if (it_Command.UserCallback != nullptr)
                {
                    if (it_Command.UserCallback == &ResetRenderState)
                    {
                        a_SetupRenderState();
                    }
                    else if (it_Command.UserCallback == &SetSamplerLinear)
                    {
                        l_PushData.Sampler = { l_LinearSampler, 0 };
                    }
                    else if (it_Command.UserCallback == &SetSamplerNearest)
                    {
                        l_PushData.Sampler = { l_NearestSampler, 0 };
                    }
                    else
                    {
                        it_Command.UserCallback(it_List, &it_Command);
                    }

                    continue;
                }

                const float l_MinX = std::max((it_Command.ClipRect.x - l_ClipOffset.x) * l_ClipScale.x, 0.0f);
                const float l_MinY = std::max((it_Command.ClipRect.y - l_ClipOffset.y) * l_ClipScale.y, 0.0f);
                const float l_MaxX = std::min((it_Command.ClipRect.z - l_ClipOffset.x) * l_ClipScale.x, l_Width);
                const float l_MaxY = std::min((it_Command.ClipRect.w - l_ClipOffset.y) * l_ClipScale.y, l_Height);
                const ImTextureID l_Texture = it_Command.GetTexID();
                if (l_MaxX <= l_MinX || l_MaxY <= l_MinY || l_Texture == ImTextureID_Invalid)
                {
                    continue;
                }

                const std::int32_t l_Left = static_cast<std::int32_t>(l_MinX);
                const std::int32_t l_Top = static_cast<std::int32_t>(l_MinY);
                commands.SetScissor({ l_Left, l_Top, static_cast<std::uint32_t>(static_cast<std::int32_t>(l_MaxX) - l_Left), static_cast<std::uint32_t>(static_cast<std::int32_t>(l_MaxY) - l_Top) });

                l_PushData.Texture = { static_cast<std::uint32_t>(l_Texture & 0xFFFFFFFF), 0 };
                l_PushData.Flags = static_cast<std::uint32_t>(l_Texture >> 32);
                l_PushData.VertexOffset = static_cast<std::uint32_t>(l_Vertices.Offset + (l_GlobalVertex + it_Command.VtxOffset) * sizeof(ImDrawVert));
                commands.PushConstants(std::as_bytes(std::span(&l_PushData, 1)));
                commands.DrawIndexed(it_Command.ElemCount, 1, l_GlobalIndex + it_Command.IdxOffset, 0);
            }

            l_GlobalVertex += static_cast<std::uint32_t>(it_List->VtxBuffer.Size);
            l_GlobalIndex += static_cast<std::uint32_t>(it_List->IdxBuffer.Size);
        }

        commands.SetScissor({ 0, 0, static_cast<std::uint32_t>(l_Width), static_cast<std::uint32_t>(l_Height) });
    }
}