#include "Trinity/RHI/Null/NullDevice.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace Trinity
{
    namespace RHI
    {
        NullCommandList::NullCommandList(NullDevice& device) : m_Device(device)
        {

        }

        void NullCommandList::Begin()
        {
            m_Recording = true;
            m_Rendering = false;
            m_HasPipeline = false;
            m_HasComputePipeline = false;
            m_HasIndexBuffer = false;
        }

        void NullCommandList::End()
        {
            TR_CORE_ASSERT(!m_Rendering, "The frame ended inside BeginRendering.");

            m_Recording = false;
        }

        void NullCommandList::TextureBarrier([[maybe_unused]] TextureHandle texture, [[maybe_unused]] ResourceState before, [[maybe_unused]] ResourceState after)
        {
            TR_CORE_ASSERT(m_Recording && !m_Rendering, "Barriers are recorded within a frame and outside rendering.");
            TR_CORE_ASSERT(m_Device.IsAlive(texture), "TextureBarrier on a destroyed or invalid texture.");
        }

        void NullCommandList::BufferBarrier([[maybe_unused]] BufferHandle buffer, [[maybe_unused]] ResourceState before, [[maybe_unused]] ResourceState after)
        {
            TR_CORE_ASSERT(m_Recording && !m_Rendering, "Barriers are recorded within a frame and outside rendering.");
            TR_CORE_ASSERT(m_Device.IsAlive(buffer), "BufferBarrier on a destroyed or invalid buffer.");
        }

        void NullCommandList::BeginRendering(const RenderingDescription& description)
        {
            TR_CORE_ASSERT(m_Recording && !m_Rendering, "BeginRendering is called once within a frame, before EndRendering.");
            TR_CORE_ASSERT(description.ColorAttachments.size() <= c_MaxColorAttachments, "Too many color attachments.");

            for (const ColorAttachment& it_Attachment : description.ColorAttachments)
            {
                TR_CORE_VERIFY(m_Device.IsAlive(it_Attachment.Texture), "BeginRendering with a destroyed or invalid color attachment.");
            }

            m_Rendering = true;
            m_HasComputePipeline = false;
        }

        void NullCommandList::EndRendering()
        {
            TR_CORE_ASSERT(m_Rendering, "EndRendering without BeginRendering.");

            m_Rendering = false;
            m_HasPipeline = false;
            m_HasIndexBuffer = false;
        }

        void NullCommandList::SetPipeline(PipelineHandle pipeline)
        {
            TR_CORE_ASSERT(m_Recording, "SetPipeline is recorded within a frame.");
            TR_CORE_ASSERT(m_Device.IsAlive(pipeline), "SetPipeline with a destroyed or invalid pipeline.");

            if (m_Device.IsComputePipeline(pipeline))
            {
                TR_CORE_ASSERT(!m_Rendering, "A compute pipeline is set outside rendering.");

                m_HasComputePipeline = true;
            }
            else
            {
                TR_CORE_ASSERT(m_Rendering, "A graphics pipeline is set inside rendering.");

                m_HasPipeline = true;
            }
        }

        void NullCommandList::SetViewport([[maybe_unused]] const Viewport& viewport)
        {
            TR_CORE_ASSERT(m_Rendering, "SetViewport is recorded inside rendering.");
        }

        void NullCommandList::SetScissor([[maybe_unused]] const Rect& scissor)
        {
            TR_CORE_ASSERT(m_Rendering, "SetScissor is recorded inside rendering.");
        }

        void NullCommandList::PushConstants([[maybe_unused]] std::span<const std::byte> data)
        {
            TR_CORE_ASSERT(m_Rendering ? m_HasPipeline : m_HasComputePipeline, "PushConstants needs a graphics pipeline inside rendering, or a compute pipeline outside it.");
            TR_CORE_ASSERT(data.size() <= c_MaxPushConstantSize && data.size() % 4 == 0, "Push constants are whole 32-bit values, at most c_MaxPushConstantSize bytes.");
        }

        void NullCommandList::Draw([[maybe_unused]] std::uint32_t vertexCount, [[maybe_unused]] std::uint32_t instanceCount, [[maybe_unused]] std::uint32_t firstVertex, [[maybe_unused]] std::uint32_t firstInstance)
        {
            TR_CORE_ASSERT(m_HasPipeline, "Draw needs a pipeline.");
        }

        void NullCommandList::SetIndexBuffer([[maybe_unused]] BufferHandle buffer, [[maybe_unused]] std::uint64_t offset, [[maybe_unused]] IndexFormat format)
        {
            TR_CORE_ASSERT(m_Rendering, "SetIndexBuffer is recorded inside rendering.");
            TR_CORE_ASSERT(m_Device.IsIndexRangeValid(buffer, offset, format), "SetIndexBuffer with a destroyed buffer, one without Index usage, or an offset past its end or not a multiple of the index size.");

            m_HasIndexBuffer = true;
        }

        void NullCommandList::DrawIndexed([[maybe_unused]] std::uint32_t indexCount, [[maybe_unused]] std::uint32_t instanceCount, [[maybe_unused]] std::uint32_t firstIndex, [[maybe_unused]] std::uint32_t firstInstance)
        {
            TR_CORE_ASSERT(m_HasPipeline && m_HasIndexBuffer, "DrawIndexed needs a pipeline and an index buffer.");
        }

        void NullCommandList::Dispatch([[maybe_unused]] std::uint32_t groupCountX, [[maybe_unused]] std::uint32_t groupCountY, [[maybe_unused]] std::uint32_t groupCountZ)
        {
            TR_CORE_ASSERT(m_Recording && !m_Rendering && m_HasComputePipeline, "Dispatch is recorded outside rendering, with a compute pipeline set.");
        }

        void NullCommandList::CopyBuffer([[maybe_unused]] BufferHandle source, [[maybe_unused]] std::uint64_t sourceOffset, [[maybe_unused]] BufferHandle destination, [[maybe_unused]] std::uint64_t destinationOffset, [[maybe_unused]] std::uint64_t size)
        {
            TR_CORE_ASSERT(m_Recording && !m_Rendering, "Copies are recorded within a frame and outside rendering.");
            TR_CORE_ASSERT(m_Device.IsAlive(source) && m_Device.IsAlive(destination), "CopyBuffer with a destroyed or invalid buffer.");
        }

        void NullCommandList::CopyTextureToBuffer([[maybe_unused]] TextureHandle source, [[maybe_unused]] std::uint32_t mipLevel, [[maybe_unused]] BufferHandle destination, [[maybe_unused]] std::uint64_t destinationOffset)
        {
            TR_CORE_ASSERT(m_Recording && !m_Rendering, "Copies are recorded within a frame and outside rendering.");
            TR_CORE_ASSERT(m_Device.IsAlive(source) && m_Device.IsAlive(destination), "CopyTextureToBuffer with a destroyed or invalid resource.");
            TR_CORE_ASSERT(m_Device.IsReadbackValid(source, mipLevel, destination, destinationOffset), "CopyTextureToBuffer with a mip the texture lacks, a misaligned offset, or a buffer too small for the mip.");
        }

        void NullCommandList::CopyBufferToTexture([[maybe_unused]] BufferHandle source, [[maybe_unused]] std::uint64_t sourceOffset, [[maybe_unused]] TextureHandle destination, [[maybe_unused]] std::uint32_t mipLevel, [[maybe_unused]] const Rect& region)
        {
            TR_CORE_ASSERT(m_Recording && !m_Rendering, "Copies are recorded within a frame and outside rendering.");
            TR_CORE_ASSERT(m_Device.IsAlive(source) && m_Device.IsAlive(destination), "CopyBufferToTexture with a destroyed or invalid resource.");
            TR_CORE_ASSERT(m_Device.IsCopyRegionValid(source, sourceOffset, destination, mipLevel, region), "CopyBufferToTexture with a misaligned offset, a region outside the mip, or a buffer too small for it.");
        }

        NullSwapChain::NullSwapChain(NullDevice& device, const SwapChainSpecification& specification) : m_Device(device), m_Specification(specification)
        {
            CreateTexture();
        }

        NullSwapChain::~NullSwapChain()
        {
            m_Device.DestroyTexture(m_Texture);
        }

        TextureHandle NullSwapChain::AcquireNextTexture()
        {
            return m_Texture;
        }

        void NullSwapChain::Present()
        {

        }

        void NullSwapChain::Resize(std::uint32_t width, std::uint32_t height)
        {
            m_Device.DestroyTexture(m_Texture);
            m_Specification.Width = width;
            m_Specification.Height = height;
            CreateTexture();
        }

        void NullSwapChain::SetVSync(bool enabled)
        {
            m_Specification.VSync = enabled;
        }

        void NullSwapChain::CreateTexture()
        {
            TextureDescription l_Description;
            l_Description.Width = std::max(m_Specification.Width, 1u);
            l_Description.Height = std::max(m_Specification.Height, 1u);
            l_Description.TextureFormat = m_Specification.ImageFormat;
            l_Description.Usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
            l_Description.DebugName = "Null swap chain";

            m_Texture = m_Device.CreateTexture(l_Description);
        }

        NullDevice::NullDevice([[maybe_unused]] const DeviceSpecification& specification) : m_CommandList(*this)
        {
            m_Info.API = GraphicsAPI::None;
            m_Info.AdapterName = "Null device";

            std::string l_Error;
            if (!m_UploadRing.Initialize(*this, l_Error))
            {
                TR_CORE_ERROR("The null device has no upload ring: {}", l_Error);
            }
        }

        NullDevice::~NullDevice()
        {
            m_UploadRing.Shutdown();

            if (m_Buffers.GetCount() != 0 || m_Textures.GetCount() != 0 || m_Pipelines.GetCount() != 0 || m_Samplers.GetCount() != 0)
            {
                TR_CORE_WARN("The null device was destroyed with {} buffer(s), {} texture(s), {} pipeline(s) and {} sampler(s) still alive", m_Buffers.GetCount(), m_Textures.GetCount(), m_Pipelines.GetCount(), m_Samplers.GetCount());
            }

            m_ReleasedBuffers.ReleaseAll(&NullDevice::ReleaseBuffer);
            m_Buffers.ForEach(&NullDevice::ReleaseBuffer);
        }

        void NullDevice::ReleaseBuffer(NullBuffer& buffer)
        {
            Memory::Free(buffer.Mapped);
            buffer.Mapped = nullptr;
        }

        BufferHandle NullDevice::CreateBuffer(const BufferDescription& description)
        {
            TR_CORE_ASSERT(description.Size != 0, "Buffer '{}' has no size.", description.DebugName);

            NullBuffer l_Buffer;
            l_Buffer.Size = description.Size;
            l_Buffer.Usage = description.Usage;
            if (description.Memory != MemoryType::GPU)
            {
                l_Buffer.Mapped = static_cast<std::byte*>(Memory::Allocate(static_cast<std::size_t>(description.Size), MemoryTag::Renderer));
                std::memset(l_Buffer.Mapped, 0, static_cast<std::size_t>(description.Size));
            }

            return m_Buffers.Add(l_Buffer);
        }

        void NullDevice::DestroyBuffer(BufferHandle buffer)
        {
            if (!buffer)
            {
                return;
            }

            std::optional<NullBuffer> l_Buffer = m_Buffers.Remove(buffer);
            TR_CORE_ASSERT(l_Buffer.has_value(), "DestroyBuffer on a buffer that was already destroyed.");

            if (l_Buffer)
            {
                m_ReleasedBuffers.Push(std::move(*l_Buffer));
            }
        }

        std::span<std::byte> NullDevice::GetMappedData(BufferHandle buffer)
        {
            const NullBuffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetMappedData on a destroyed or invalid buffer.");

            if (l_Buffer == nullptr || l_Buffer->Mapped == nullptr)
            {
                return {};
            }

            return { l_Buffer->Mapped, static_cast<std::size_t>(l_Buffer->Size) };
        }

        // What every GPU allows: a compressed format is only sampled and copied, a depth format is never a colour target or written by shaders, and a colour format is never a depth target
        bool NullDevice::IsFormatSupported(Format format, TextureUsage usage) const
        {
            if (format == Format::Unknown)
            {
                return false;
            }

            if (IsCompressedFormat(format))
            {
                return !HasFlag(usage, TextureUsage::RenderTarget) && !HasFlag(usage, TextureUsage::DepthStencil) && !HasFlag(usage, TextureUsage::UnorderedAccess);
            }

            if (IsDepthFormat(format))
            {
                return !HasFlag(usage, TextureUsage::RenderTarget) && !HasFlag(usage, TextureUsage::UnorderedAccess);
            }

            return !HasFlag(usage, TextureUsage::DepthStencil);
        }

        TextureHandle NullDevice::CreateTexture(const TextureDescription& description)
        {
            TR_CORE_ASSERT(description.Width != 0 && description.Height != 0 && description.MipLevels != 0, "Texture '{}' has a zero size or no mips.", description.DebugName);
            TR_CORE_ASSERT(description.TextureFormat != Format::Unknown, "Texture '{}' has no format.", description.DebugName);

            if (!CanCreateTexture(description))
            {
                return {};
            }

            return m_Textures.Add({ description.Width, description.Height, description.MipLevels, description.TextureFormat, description.Usage });
        }

        void NullDevice::DestroyTexture(TextureHandle texture)
        {
            if (!texture)
            {
                return;
            }

            [[maybe_unused]] const std::optional<NullTexture> l_Texture = m_Textures.Remove(texture);
            TR_CORE_ASSERT(l_Texture.has_value(), "DestroyTexture on a texture that was already destroyed.");
        }

        // Nothing reads them, so a resource's slot in the handle pool serves as its index
        std::uint32_t NullDevice::GetShaderResourceIndex(BufferHandle buffer)
        {
            const NullBuffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetShaderResourceIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr && HasFlag(l_Buffer->Usage, BufferUsage::ShaderResource) ? buffer.Index : c_NoBindlessIndex;
        }

        std::uint32_t NullDevice::GetUnorderedAccessIndex(BufferHandle buffer)
        {
            const NullBuffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr && HasFlag(l_Buffer->Usage, BufferUsage::UnorderedAccess) ? buffer.Index : c_NoBindlessIndex;
        }

        std::uint32_t NullDevice::GetShaderResourceIndex(TextureHandle texture)
        {
            const NullTexture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetShaderResourceIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr && HasFlag(l_Texture->Usage, TextureUsage::ShaderResource) ? texture.Index : c_NoBindlessIndex;
        }

        std::uint32_t NullDevice::GetUnorderedAccessIndex(TextureHandle texture)
        {
            const NullTexture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr && HasFlag(l_Texture->Usage, TextureUsage::UnorderedAccess) ? texture.Index : c_NoBindlessIndex;
        }

        bool NullDevice::IsIndexRangeValid(BufferHandle buffer, std::uint64_t offset, IndexFormat format)
        {
            const NullBuffer* l_Buffer = m_Buffers.Get(buffer);

            return l_Buffer != nullptr && HasFlag(l_Buffer->Usage, BufferUsage::Index) && offset < l_Buffer->Size && offset % GetIndexSize(format) == 0;
        }

        // The same rules the GPU backends assert, so a headless run catches a bad copy too
        bool NullDevice::IsCopyRegionValid(BufferHandle source, std::uint64_t sourceOffset, TextureHandle destination, std::uint32_t mipLevel, const Rect& region)
        {
            const NullBuffer* l_Buffer = m_Buffers.Get(source);
            const NullTexture* l_Texture = m_Textures.Get(destination);
            if (l_Buffer == nullptr || l_Texture == nullptr || mipLevel >= l_Texture->MipLevels || !HasFlag(l_Texture->Usage, TextureUsage::CopyDestination))
            {
                return false;
            }

            const std::uint64_t l_Size = GetTextureCopySize(l_Texture->TextureFormat, region.Width, region.Height);

            return IsRegionInsideMip(region, l_Texture->Width, l_Texture->Height, mipLevel) && IsRegionBlockAligned(region, l_Texture->TextureFormat, l_Texture->Width, l_Texture->Height, mipLevel) && sourceOffset % c_TextureCopyOffsetAlignment == 0 && sourceOffset + l_Size <= l_Buffer->Size;
        }

        bool NullDevice::IsReadbackValid(TextureHandle source, std::uint32_t mipLevel, BufferHandle destination, std::uint64_t destinationOffset)
        {
            const NullTexture* l_Texture = m_Textures.Get(source);
            const NullBuffer* l_Buffer = m_Buffers.Get(destination);
            if (l_Texture == nullptr || l_Buffer == nullptr || mipLevel >= l_Texture->MipLevels || !HasFlag(l_Texture->Usage, TextureUsage::CopySource))
            {
                return false;
            }

            const std::uint64_t l_Size = GetTextureCopySize(l_Texture->TextureFormat, GetMipSize(l_Texture->Width, mipLevel), GetMipSize(l_Texture->Height, mipLevel));

            return destinationOffset % c_TextureCopyOffsetAlignment == 0 && destinationOffset + l_Size <= l_Buffer->Size;
        }

        SamplerHandle NullDevice::CreateSampler([[maybe_unused]] const SamplerDescription& description)
        {
            return m_Samplers.Add({});
        }

        void NullDevice::DestroySampler(SamplerHandle sampler)
        {
            if (!sampler)
            {
                return;
            }

            [[maybe_unused]] const std::optional<NullSampler> l_Sampler = m_Samplers.Remove(sampler);
            TR_CORE_ASSERT(l_Sampler.has_value(), "DestroySampler on a sampler that was already destroyed.");
        }

        std::uint32_t NullDevice::GetSamplerIndex(SamplerHandle sampler)
        {
            const NullSampler* l_Sampler = m_Samplers.Get(sampler);
            TR_CORE_ASSERT(l_Sampler != nullptr, "GetSamplerIndex on a destroyed or invalid sampler.");

            return l_Sampler != nullptr ? sampler.Index : c_NoBindlessIndex;
        }

        PipelineHandle NullDevice::CreateGraphicsPipeline([[maybe_unused]] const GraphicsPipelineDescription& description)
        {
            TR_CORE_ASSERT(!description.VertexShader.Code.empty() && !description.PixelShader.Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);
            TR_CORE_ASSERT(description.ColorFormats.size() <= c_MaxColorAttachments, "Pipeline '{}' has too many color formats.", description.DebugName);

            return m_Pipelines.Add({});
        }

        PipelineHandle NullDevice::CreateComputePipeline([[maybe_unused]] const ComputePipelineDescription& description)
        {
            TR_CORE_ASSERT(!description.ComputeShader.Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);

            return m_Pipelines.Add({ true });
        }

        bool NullDevice::IsComputePipeline(PipelineHandle pipeline)
        {
            const NullPipeline* l_Pipeline = m_Pipelines.Get(pipeline);

            return l_Pipeline != nullptr && l_Pipeline->Compute;
        }

        void NullDevice::DestroyPipeline(PipelineHandle pipeline)
        {
            if (!pipeline)
            {
                return;
            }

            [[maybe_unused]] const std::optional<NullPipeline> l_Pipeline = m_Pipelines.Remove(pipeline);
            TR_CORE_ASSERT(l_Pipeline.has_value(), "DestroyPipeline on a pipeline that was already destroyed.");
        }

        Scope<SwapChain> NullDevice::CreateSwapChain(const SwapChainSpecification& specification)
        {
            return CreateScope<NullSwapChain>(*this, specification);
        }

        CommandList& NullDevice::BeginFrame()
        {
            TR_CORE_ASSERT(!m_InFrame, "BeginFrame was called twice without EndFrame.");

            m_InFrame = true;
            m_ReleasedBuffers.BeginFrame(&NullDevice::ReleaseBuffer);
            m_UploadRing.BeginFrame();
            m_CommandList.Begin();

            return m_CommandList;
        }

        void NullDevice::EndFrame()
        {
            TR_CORE_ASSERT(m_InFrame, "EndFrame without BeginFrame.");

            m_UploadRing.EndFrame();
            m_CommandList.End();
            m_ReleasedBuffers.EndFrame();
            m_InFrame = false;
        }

        void NullDevice::WaitIdle()
        {
            m_ReleasedBuffers.ReleaseIdle(&NullDevice::ReleaseBuffer);
        }
    }
}