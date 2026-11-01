#include "Trinity/UI/GameUIRenderInterface.hpp"

#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <span>
#include <string_view>
#include <utility>

namespace Trinity
{
    namespace
    {
        // Laid out as GameUI.slang reads it
        struct PushData
        {
            std::array<float, 4> Column0{};
            std::array<float, 4> Column1{};
            std::array<float, 4> Column3{};
            std::array<std::uint32_t, 2> Vertices{};
            std::array<std::uint32_t, 2> Texture{};
            std::array<std::uint32_t, 2> Sampler{};
            std::array<float, 2> Translation{};
            std::uint32_t VertexOffset = 0;
            std::uint32_t Flags = 0;
        };

        static_assert(sizeof(PushData) == 88);
        static_assert(sizeof(Rml::Vertex) == 20, "GameUI.slang reads 20-byte vertices");

        constexpr std::uint32_t c_Textured = 1;
        constexpr std::uint32_t c_EncodeSrgb = 2;
        constexpr std::uint32_t c_Premultiply = 4;

        constexpr std::array<float, 16> c_IdentityTransform{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

        // The view's pixels, with Y down, to clip space with Y up, after the element's transform, whose W is kept so a perspective divides as it should. Depth is always 0, so nothing is clipped by it
        void SetClipTransform(PushData& push, const std::array<float, 16>& transform, std::uint32_t width, std::uint32_t height)
        {
            const float l_X = 2.0f / static_cast<float>(width);
            const float l_Y = -2.0f / static_cast<float>(height);
            const auto a_Column = [&transform, l_X, l_Y](std::size_t column) -> std::array<float, 4>
            {
                const float* l_Source = transform.data() + column * 4;

                return { l_X * l_Source[0] - l_Source[3], l_Y * l_Source[1] + l_Source[3], 0.0f, l_Source[3] };
            };

            push.Column0 = a_Column(0);
            push.Column1 = a_Column(1);
            push.Column3 = a_Column(3);
        }

        RHI::Rect ClampScissor(const std::array<std::int32_t, 4>& region, std::uint32_t width, std::uint32_t height)
        {
            const std::int32_t l_Left = std::clamp(region[0], 0, static_cast<std::int32_t>(width));
            const std::int32_t l_Top = std::clamp(region[1], 0, static_cast<std::int32_t>(height));
            const std::int32_t l_Right = std::clamp(region[0] + region[2], l_Left, static_cast<std::int32_t>(width));
            const std::int32_t l_Bottom = std::clamp(region[1] + region[3], l_Top, static_cast<std::int32_t>(height));

            return { l_Left, l_Top, static_cast<std::uint32_t>(l_Right - l_Left), static_cast<std::uint32_t>(l_Bottom - l_Top) };
        }
    }

    GameUIRenderInterface::GameUIRenderInterface(RHI::Device& device, RHI::Format displayFormat, RHI::Format stencilFormat) : m_Device(device)
    {
        RHI::SamplerDescription l_Sampler;
        l_Sampler.AddressU = RHI::AddressMode::ClampToEdge;
        l_Sampler.AddressV = RHI::AddressMode::ClampToEdge;
        l_Sampler.AddressW = RHI::AddressMode::ClampToEdge;
        l_Sampler.DebugName = "GameUI sampler";
        m_Sampler = m_Device.CreateSampler(l_Sampler);

        CreatePipelines(displayFormat, stencilFormat);
    }

    // RmlUi has released everything it made by the time it shuts down, so what is left over is reported rather than freed, as the GameUI tag would show it anyway
    GameUIRenderInterface::~GameUIRenderInterface()
    {
        BeginFrame();
        if (m_Statistics.Geometries != 0 || m_Statistics.Textures != 0)
        {
            TR_CORE_WARN("GameUI: {} geometry and {} texture(s) were never released by RmlUi", m_Statistics.Geometries, m_Statistics.Textures);
        }

        for (const RHI::PipelineHandle it_Pipeline : { m_DrawPipeline, m_StencilDrawPipeline, m_MaskedPipeline, m_MaskReplacePipeline, m_MaskIncrementPipeline })
        {
            m_Device.DestroyPipeline(it_Pipeline);
        }

        m_Device.DestroySampler(m_Sampler);
    }

    // A device without the stencil format draws what a clip mask would clip unclipped, which only a transformed element's overflow needs
    void GameUIRenderInterface::CreatePipelines(RHI::Format displayFormat, RHI::Format stencilFormat)
    {
        const std::string_view l_Extension = m_Device.GetInfo().API == GraphicsAPI::D3D12 ? "dxil" : "spv";
        const Expected<FileBuffer, FileError> l_VertexShader = FileSystem::ReadFile(std::format("/engine/shaders/GameUI.VertexMain.{}", l_Extension));
        const Expected<FileBuffer, FileError> l_PixelShader = FileSystem::ReadFile(std::format("/engine/shaders/GameUI.PixelMain.{}", l_Extension));
        if (!l_VertexShader || !l_PixelShader)
        {
            TR_CORE_INFO("GameUI: no {} shaders under /engine/shaders, so documents are laid out and recorded, but nothing is drawn", l_Extension);

            return;
        }

        const std::array<RHI::Format, 1> l_ColorFormats{ displayFormat };

        RHI::GraphicsPipelineDescription l_Description;
        l_Description.VertexShader = { *l_VertexShader, "VertexMain" };
        l_Description.PixelShader = { *l_PixelShader, "PixelMain" };
        l_Description.ColorFormats = l_ColorFormats;
        l_Description.Cull = RHI::CullMode::None;
        l_Description.AlphaBlend = true;
        l_Description.PremultipliedAlpha = true;
        l_Description.DebugName = "GameUI";
        m_DrawPipeline = m_Device.CreateGraphicsPipeline(l_Description);
        if (!m_DrawPipeline)
        {
            TR_CORE_ERROR("GameUI: the pipeline could not be created, so nothing is drawn");

            return;
        }

        if (!m_Device.IsFormatSupported(stencilFormat, RHI::TextureUsage::DepthStencil))
        {
            TR_CORE_WARN("GameUI: this device has no {} target, so what a transformed element clips is drawn unclipped", RHI::ToString(stencilFormat));

            return;
        }

        l_Description.DepthFormat = stencilFormat;
        l_Description.DebugName = "GameUI with stencil";
        m_StencilDrawPipeline = m_Device.CreateGraphicsPipeline(l_Description);

        l_Description.StencilTest = true;
        l_Description.StencilCompare = RHI::CompareOp::Equal;
        l_Description.DebugName = "GameUI clipped";
        m_MaskedPipeline = m_Device.CreateGraphicsPipeline(l_Description);

        l_Description.StencilCompare = RHI::CompareOp::Always;
        l_Description.StencilPass = RHI::StencilOp::Replace;
        l_Description.AlphaBlend = false;
        l_Description.ColorWrite = false;
        l_Description.DebugName = "GameUI clip mask";
        m_MaskReplacePipeline = m_Device.CreateGraphicsPipeline(l_Description);

        l_Description.StencilPass = RHI::StencilOp::IncrementClamp;
        l_Description.DebugName = "GameUI clip mask intersect";
        m_MaskIncrementPipeline = m_Device.CreateGraphicsPipeline(l_Description);

        if (!m_StencilDrawPipeline || !m_MaskedPipeline || !m_MaskReplacePipeline || !m_MaskIncrementPipeline)
        {
            TR_CORE_ERROR("GameUI: the clip mask pipelines could not be created, so what a transformed element clips is drawn unclipped");
            for (RHI::PipelineHandle* it_Pipeline : { &m_StencilDrawPipeline, &m_MaskedPipeline, &m_MaskReplacePipeline, &m_MaskIncrementPipeline })
            {
                m_Device.DestroyPipeline(std::exchange(*it_Pipeline, RHI::PipelineHandle{}));
            }
        }
    }

    void GameUIRenderInterface::BeginFrame()
    {
        for (Geometry* it_Geometry : m_ReleasedGeometry)
        {
            Free(it_Geometry);
        }

        for (Texture* it_Texture : m_ReleasedTextures)
        {
            Free(it_Texture);
        }

        m_ReleasedGeometry.clear();
        m_ReleasedTextures.clear();
    }

    void GameUIRenderInterface::Begin(GameUIDrawList& list)
    {
        TR_CORE_ASSERT(m_List == nullptr, "A GameUI draw list is already being recorded.");

        list.Clear();
        m_List = &list;
    }

    void GameUIRenderInterface::End()
    {
        m_List = nullptr;
    }

    void GameUIRenderInterface::RecordUploads(RHI::CommandList& commands)
    {
        for (Texture* it_Texture : m_Uploads)
        {
            Upload(commands, *it_Texture);
        }

        m_Uploads.clear();
    }

    // Its pixels go once they are copied, as RmlUi never reads a texture back
    void GameUIRenderInterface::Upload(RHI::CommandList& commands, Texture& texture)
    {
        RHI::TextureDescription l_Description;
        l_Description.Width = texture.Width;
        l_Description.Height = texture.Height;
        l_Description.TextureFormat = RHI::Format::RGBA8Unorm;
        l_Description.Usage = RHI::TextureUsage::ShaderResource | RHI::TextureUsage::CopyDestination;
        l_Description.DebugName = "GameUI texture";
        texture.Handle = m_Device.CreateTexture(l_Description);

        const std::uint64_t l_RowPitch = RHI::GetTextureCopyRowPitch(RHI::Format::RGBA8Unorm, texture.Width);
        const RHI::UploadAllocation l_Staging = texture.Handle ? m_Device.AllocateUpload(l_RowPitch * texture.Height, RHI::c_TextureCopyOffsetAlignment) : RHI::UploadAllocation{};
        if (l_Staging.Data.empty())
        {
            TR_CORE_ERROR("GameUI: a {}x{} texture could not be made, so it is not drawn", texture.Width, texture.Height);
            m_Device.DestroyTexture(std::exchange(texture.Handle, RHI::TextureHandle{}));
            GameUIVector<std::byte>().swap(texture.Pixels);

            return;
        }

        const std::size_t l_RowSize = std::size_t{ texture.Width } * 4;
        for (std::uint32_t it_Row = 0; it_Row < texture.Height; ++it_Row)
        {
            std::memcpy(l_Staging.Data.data() + it_Row * l_RowPitch, texture.Pixels.data() + it_Row * l_RowSize, l_RowSize);
        }

        commands.TextureBarrier(texture.Handle, RHI::ResourceState::Undefined, RHI::ResourceState::CopyDestination);
        commands.CopyBufferToTexture(l_Staging.Buffer, l_Staging.Offset, texture.Handle, 0, 0, { 0, 0, texture.Width, texture.Height });
        commands.TextureBarrier(texture.Handle, RHI::ResourceState::CopyDestination, RHI::ResourceState::ShaderResource);
        GameUIVector<std::byte>().swap(texture.Pixels);
    }

    // The list's geometry goes into the upload ring together, after a triangle covering the view, which clears the clip mask. RmlUi's clip masks are its OpenGL renderer's: Set clears the stencil to 0 and writes 1 where the geometry is, SetInverse clears to 1 and writes 0, Intersect adds 1 where the geometry is, and what is clipped draws where the stencil holds the count so far
    void GameUIRenderInterface::Draw(RHI::CommandList& commands, const GameUIDrawList& list, std::uint32_t width, std::uint32_t height, bool stencil) const
    {
        TR_PROFILE_FUNCTION();

        std::uint64_t l_VertexBytes = 3 * sizeof(Rml::Vertex);
        std::uint64_t l_IndexBytes = 0;
        for (const GameUIDrawList::Command& it_Command : list.Commands)
        {
            if (it_Command.Type == GameUIDrawList::CommandType::Geometry || it_Command.Type == GameUIDrawList::CommandType::RenderToClipMask)
            {
                const Geometry& l_Geometry = GetGeometry(it_Command.Geometry);
                l_VertexBytes += l_Geometry.Vertices.size() * sizeof(Rml::Vertex);
                l_IndexBytes += l_Geometry.Indices.size() * sizeof(int);
            }
        }

        if (!m_DrawPipeline || l_IndexBytes == 0 || width == 0 || height == 0)
        {
            return;
        }

        const RHI::UploadAllocation l_Vertices = m_Device.AllocateUpload(l_VertexBytes, 4);
        const RHI::UploadAllocation l_Indices = m_Device.AllocateUpload(l_IndexBytes, 4);
        if (l_Vertices.Data.empty() || l_Indices.Data.empty())
        {
            return;
        }

        std::array<Rml::Vertex, 3> l_Cover{};
        l_Cover[1].position = { 2.0f * static_cast<float>(width), 0.0f };
        l_Cover[2].position = { 0.0f, 2.0f * static_cast<float>(height) };
        std::memcpy(l_Vertices.Data.data(), l_Cover.data(), sizeof(l_Cover));

        const RHI::Rect l_View{ 0, 0, width, height };
        std::size_t l_VertexOffset = sizeof(l_Cover);
        std::uint32_t l_FirstIndex = 0;
        RHI::PipelineHandle l_Bound;
        std::uint32_t l_Reference = ~0u;
        std::uint32_t l_TestValue = 0;
        bool l_Clipped = false;
        bool l_Scissored = false;
        std::array<std::int32_t, 4> l_Scissor{};
        const std::array<float, 16>* l_Transform = &c_IdentityTransform;

        PushData l_Push;
        l_Push.Vertices = { l_Vertices.ShaderResourceIndex, 0 };
        l_Push.Sampler = { m_Device.GetSamplerIndex(m_Sampler), 0 };
        SetClipTransform(l_Push, *l_Transform, width, height);
        commands.SetIndexBuffer(l_Indices.Buffer, l_Indices.Offset, RHI::IndexFormat::UInt32);
        // A list drawn after another in the same pass starts unscissored, whatever the last left
        commands.SetScissor(l_View);

        const auto a_Bind = [&commands, &l_Bound](RHI::PipelineHandle pipeline)
        {
            if (pipeline != l_Bound)
            {
                commands.SetPipeline(pipeline);
                l_Bound = pipeline;
            }
        };

        const auto a_SetReference = [&commands, &l_Reference](std::uint32_t reference)
        {
            if (reference != l_Reference)
            {
                commands.SetStencilReference(reference);
                l_Reference = reference;
            }
        };

        // An image whose asset failed, or a generated texture that could not be made, draws nothing
        const auto a_DrawGeometry = [&](const GameUIDrawList::Command& command)
        {
            const Geometry& l_Geometry = GetGeometry(command.Geometry);
            const std::size_t l_Vertex = l_VertexOffset;
            const std::uint32_t l_Index = l_FirstIndex;
            std::memcpy(l_Vertices.Data.data() + l_Vertex, l_Geometry.Vertices.data(), l_Geometry.Vertices.size() * sizeof(Rml::Vertex));
            std::memcpy(l_Indices.Data.data() + std::size_t{ l_Index } * sizeof(int), l_Geometry.Indices.data(), l_Geometry.Indices.size() * sizeof(int));
            l_VertexOffset += l_Geometry.Vertices.size() * sizeof(Rml::Vertex);
            l_FirstIndex += static_cast<std::uint32_t>(l_Geometry.Indices.size());

            l_Push.Flags = 0;
            l_Push.Texture = { 0, 0 };
            if (command.Texture != 0)
            {
                const Texture& l_Texture = GetTexture(command.Texture);
                const TextureAsset* l_Image = l_Texture.Image.Get();
                const std::uint32_t l_ResourceIndex = l_Image != nullptr ? l_Image->GetShaderResourceIndex() : l_Texture.Handle ? m_Device.GetShaderResourceIndex(l_Texture.Handle) : RHI::c_NoBindlessIndex;
                if (l_ResourceIndex == RHI::c_NoBindlessIndex)
                {
                    return;
                }

                l_Push.Texture = { l_ResourceIndex, 0 };
                l_Push.Flags = c_Textured | (l_Image != nullptr ? c_Premultiply | (l_Image->IsSrgb() ? c_EncodeSrgb : 0) : 0);
            }

            l_Push.Translation = command.Translation;
            l_Push.VertexOffset = static_cast<std::uint32_t>(l_Vertices.Offset + l_Vertex);
            commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
            commands.DrawIndexed(static_cast<std::uint32_t>(l_Geometry.Indices.size()), 1, l_Index, 0);
        };

        for (const GameUIDrawList::Command& it_Command : list.Commands)
        {
            switch (it_Command.Type)
            {
                case GameUIDrawList::CommandType::Geometry:
                {
                    const bool l_Masked = stencil && l_Clipped;
                    a_Bind(!stencil ? m_DrawPipeline : l_Masked ? m_MaskedPipeline : m_StencilDrawPipeline);
                    if (l_Masked)
                    {
                        a_SetReference(l_TestValue);
                    }

                    a_DrawGeometry(it_Command);

                    break;
                }
                case GameUIDrawList::CommandType::EnableScissor:
                case GameUIDrawList::CommandType::SetScissor:
                {
                    l_Scissored = it_Command.Type == GameUIDrawList::CommandType::EnableScissor ? it_Command.Enable : l_Scissored;
                    l_Scissor = it_Command.Type == GameUIDrawList::CommandType::SetScissor ? it_Command.Scissor : l_Scissor;
                    commands.SetScissor(l_Scissored ? ClampScissor(l_Scissor, width, height) : l_View);

                    break;
                }
                case GameUIDrawList::CommandType::EnableClipMask:
                {
                    l_Clipped = it_Command.Enable;

                    break;
                }
                case GameUIDrawList::CommandType::RenderToClipMask:
                {
                    if (!stencil)
                    {
                        break;
                    }

                    const Rml::ClipMaskOperation l_Operation = it_Command.Operation;
                    if (l_Operation != Rml::ClipMaskOperation::Intersect)
                    {
                        a_Bind(m_MaskReplacePipeline);
                        a_SetReference(l_Operation == Rml::ClipMaskOperation::SetInverse ? 1 : 0);
                        SetClipTransform(l_Push, c_IdentityTransform, width, height);
                        l_Push.Translation = { 0.0f, 0.0f };
                        l_Push.VertexOffset = static_cast<std::uint32_t>(l_Vertices.Offset);
                        l_Push.Flags = 0;
                        commands.SetScissor(l_View);
                        commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
                        commands.Draw(3, 1, 0, 0);
                        commands.SetScissor(l_Scissored ? ClampScissor(l_Scissor, width, height) : l_View);
                        SetClipTransform(l_Push, *l_Transform, width, height);
                    }

                    a_Bind(l_Operation == Rml::ClipMaskOperation::Intersect ? m_MaskIncrementPipeline : m_MaskReplacePipeline);
                    if (l_Operation != Rml::ClipMaskOperation::Intersect)
                    {
                        a_SetReference(l_Operation == Rml::ClipMaskOperation::Set ? 1 : 0);
                    }

                    GameUIDrawList::Command l_Mask = it_Command;
                    l_Mask.Texture = 0;
                    a_DrawGeometry(l_Mask);
                    l_TestValue = l_Operation == Rml::ClipMaskOperation::Intersect ? l_TestValue + 1 : 1;

                    break;
                }
                case GameUIDrawList::CommandType::SetTransform:
                {
                    l_Transform = it_Command.Transform == GameUIDrawList::c_Identity ? &c_IdentityTransform : &list.Transforms[it_Command.Transform];
                    SetClipTransform(l_Push, *l_Transform, width, height);

                    break;
                }
            }
        }

        commands.SetScissor(l_View);
    }

    Rml::CompiledGeometryHandle GameUIRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
    {
        Geometry* l_Geometry = Memory::New<Geometry>(MemoryTag::GameUI);
        l_Geometry->Vertices.assign(vertices.begin(), vertices.end());
        l_Geometry->Indices.assign(indices.begin(), indices.end());

        ++m_Statistics.Geometries;
        m_Statistics.GeometryBytes += vertices.size() * sizeof(Rml::Vertex) + indices.size() * sizeof(int);

        return reinterpret_cast<Rml::CompiledGeometryHandle>(l_Geometry);
    }

    void GameUIRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::Geometry;
        l_Command.Geometry = geometry;
        l_Command.Texture = texture;
        l_Command.Translation = { translation.x, translation.y };
        Record(l_Command);
    }

    void GameUIRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
    {
        if (geometry != 0)
        {
            m_ReleasedGeometry.push_back(reinterpret_cast<Geometry*>(geometry));
        }
    }

    // A project's texture by its path, which a document names as a file beside it, such as ../Textures/Logo.png from /assets/UI/Menu.rml, as the registry knows each file by its path in the virtual file system. One not loaded yet is loaded at once, so the document lays out at the image's size from its first frame. 0 for anything else, which RmlUi reports once and draws nothing for
    Rml::TextureHandle GameUIRenderInterface::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source)
    {
        const AssetRegistry* l_Registry = AssetManager::GetRegistry();
        const AssetRecord* l_Record = l_Registry != nullptr ? l_Registry->FindByPath(source) : nullptr;
        if (l_Record == nullptr || l_Record->Importer != TextureAsset::c_AssetType)
        {
            TR_CORE_ERROR("GameUI: the project has no texture at {}", source);

            return 0;
        }

        Texture* l_Texture = Memory::New<Texture>(MemoryTag::GameUI);
        l_Texture->Image = AssetRef<TextureAsset>(l_Record->ID);
        AssetManager::LoadNow(l_Record->ID);

        const TextureAsset* l_Image = l_Texture->Image.Get();
        if (l_Image == nullptr)
        {
            Memory::Delete(l_Texture);

            return 0;
        }

        l_Texture->Width = l_Image->GetWidth();
        l_Texture->Height = l_Image->GetHeight();
        dimensions = { static_cast<int>(l_Texture->Width), static_cast<int>(l_Texture->Height) };
        ++m_Statistics.Textures;

        return reinterpret_cast<Rml::TextureHandle>(l_Texture);
    }

    // Font atlases and the like, which RmlUi makes in premultiplied RGBA8
    Rml::TextureHandle GameUIRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions)
    {
        if (dimensions.x <= 0 || dimensions.y <= 0 || source.size() != static_cast<std::size_t>(dimensions.x) * static_cast<std::size_t>(dimensions.y) * 4)
        {
            TR_CORE_ERROR("GameUI: a {}x{} texture of {} bytes is no RGBA8 image, so it is not made", dimensions.x, dimensions.y, source.size());

            return 0;
        }

        Texture* l_Texture = Memory::New<Texture>(MemoryTag::GameUI);
        l_Texture->Width = static_cast<std::uint32_t>(dimensions.x);
        l_Texture->Height = static_cast<std::uint32_t>(dimensions.y);
        l_Texture->Pixels.resize(source.size());
        std::memcpy(l_Texture->Pixels.data(), source.data(), source.size());
        m_Uploads.push_back(l_Texture);

        ++m_Statistics.Textures;
        m_Statistics.TextureBytes += source.size();

        return reinterpret_cast<Rml::TextureHandle>(l_Texture);
    }

    void GameUIRenderInterface::ReleaseTexture(Rml::TextureHandle texture)
    {
        if (texture != 0)
        {
            m_ReleasedTextures.push_back(reinterpret_cast<Texture*>(texture));
        }
    }

    void GameUIRenderInterface::EnableScissorRegion(bool enable)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::EnableScissor;
        l_Command.Enable = enable;
        Record(l_Command);
    }

    void GameUIRenderInterface::SetScissorRegion(Rml::Rectanglei region)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::SetScissor;
        l_Command.Scissor = { region.Left(), region.Top(), region.Width(), region.Height() };
        Record(l_Command);
    }

    void GameUIRenderInterface::EnableClipMask(bool enable)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::EnableClipMask;
        l_Command.Enable = enable;
        Record(l_Command);
    }

    void GameUIRenderInterface::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::RenderToClipMask;
        l_Command.Operation = operation;
        l_Command.Geometry = geometry;
        l_Command.Translation = { translation.x, translation.y };
        Record(l_Command);

        if (m_List != nullptr)
        {
            m_List->ClipMask = true;
        }
    }

    void GameUIRenderInterface::SetTransform(const Rml::Matrix4f* transform)
    {
        GameUIDrawList::Command l_Command;
        l_Command.Type = GameUIDrawList::CommandType::SetTransform;
        l_Command.Transform = GameUIDrawList::c_Identity;
        if (transform != nullptr && m_List != nullptr)
        {
            l_Command.Transform = static_cast<std::uint32_t>(m_List->Transforms.size());
            std::array<float, 16>& l_Transform = m_List->Transforms.emplace_back();
            std::copy_n(transform->data(), l_Transform.size(), l_Transform.begin());
        }

        Record(l_Command);
    }

    // RmlUi only draws as a context renders, which GameUI always records
    void GameUIRenderInterface::Record(const GameUIDrawList::Command& command)
    {
        TR_CORE_ASSERT(m_List != nullptr, "RmlUi drew outside a context's render.");

        if (m_List != nullptr)
        {
            m_List->Commands.push_back(command);
        }
    }

    void GameUIRenderInterface::Free(Geometry* geometry)
    {
        --m_Statistics.Geometries;
        m_Statistics.GeometryBytes -= geometry->Vertices.size() * sizeof(Rml::Vertex) + geometry->Indices.size() * sizeof(int);
        Memory::Delete(geometry);
    }

    // The device keeps a texture until the frames that may still draw it have finished
    void GameUIRenderInterface::Free(Texture* texture)
    {
        std::erase(m_Uploads, texture);
        --m_Statistics.Textures;
        m_Statistics.TextureBytes -= texture->Image.GetID() ? 0 : std::uint64_t{ texture->Width } * texture->Height * 4;
        m_Device.DestroyTexture(texture->Handle);
        Memory::Delete(texture);
    }
}