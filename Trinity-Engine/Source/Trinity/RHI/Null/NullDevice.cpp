#include "Trinity/RHI/Null/NullDevice.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <cstring>
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
        }

        void NullCommandList::EndRendering()
        {
            TR_CORE_ASSERT(m_Rendering, "EndRendering without BeginRendering.");

            m_Rendering = false;
            m_HasPipeline = false;
        }

        void NullCommandList::SetPipeline([[maybe_unused]] PipelineHandle pipeline)
        {
            TR_CORE_ASSERT(m_Rendering, "SetPipeline is recorded inside rendering.");
            TR_CORE_ASSERT(m_Device.IsAlive(pipeline), "SetPipeline with a destroyed or invalid pipeline.");

            m_HasPipeline = true;
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
            TR_CORE_ASSERT(m_HasPipeline, "PushConstants needs a pipeline.");
            TR_CORE_ASSERT(data.size() <= c_MaxPushConstantSize && data.size() % 4 == 0, "Push constants are whole 32-bit values, at most c_MaxPushConstantSize bytes.");
        }

        void NullCommandList::Draw([[maybe_unused]] std::uint32_t vertexCount, [[maybe_unused]] std::uint32_t instanceCount, [[maybe_unused]] std::uint32_t firstVertex, [[maybe_unused]] std::uint32_t firstInstance)
        {
            TR_CORE_ASSERT(m_HasPipeline, "Draw needs a pipeline.");
        }

        void NullCommandList::CopyBuffer([[maybe_unused]] BufferHandle source, [[maybe_unused]] std::uint64_t sourceOffset, [[maybe_unused]] BufferHandle destination, [[maybe_unused]] std::uint64_t destinationOffset, [[maybe_unused]] std::uint64_t size)
        {
            TR_CORE_ASSERT(m_Recording && !m_Rendering, "Copies are recorded within a frame and outside rendering.");
            TR_CORE_ASSERT(m_Device.IsAlive(source) && m_Device.IsAlive(destination), "CopyBuffer with a destroyed or invalid buffer.");
        }

        void NullCommandList::CopyTextureToBuffer([[maybe_unused]] TextureHandle source, [[maybe_unused]] BufferHandle destination)
        {
            TR_CORE_ASSERT(m_Recording && !m_Rendering, "Copies are recorded within a frame and outside rendering.");
            TR_CORE_ASSERT(m_Device.IsAlive(source) && m_Device.IsAlive(destination), "CopyTextureToBuffer with a destroyed or invalid resource.");
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
        }

        NullDevice::~NullDevice()
        {
            if (m_Buffers.GetCount() != 0 || m_Textures.GetCount() != 0 || m_Pipelines.GetCount() != 0)
            {
                TR_CORE_WARN("The null device was destroyed with {} buffer(s), {} texture(s) and {} pipeline(s) still alive", m_Buffers.GetCount(), m_Textures.GetCount(), m_Pipelines.GetCount());
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

        TextureHandle NullDevice::CreateTexture(const TextureDescription& description)
        {
            TR_CORE_ASSERT(description.Width != 0 && description.Height != 0 && description.MipLevels != 0, "Texture '{}' has a zero size or no mips.", description.DebugName);
            TR_CORE_ASSERT(description.TextureFormat != Format::Unknown, "Texture '{}' has no format.", description.DebugName);

            return m_Textures.Add({ description.Width, description.Height, description.TextureFormat });
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

        PipelineHandle NullDevice::CreateGraphicsPipeline([[maybe_unused]] const GraphicsPipelineDescription& description)
        {
            TR_CORE_ASSERT(!description.VertexShader.Code.empty() && !description.PixelShader.Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);
            TR_CORE_ASSERT(description.ColorFormats.size() <= c_MaxColorAttachments, "Pipeline '{}' has too many color formats.", description.DebugName);

            return m_Pipelines.Add({});
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
            m_CommandList.Begin();

            return m_CommandList;
        }

        void NullDevice::EndFrame()
        {
            TR_CORE_ASSERT(m_InFrame, "EndFrame without BeginFrame.");

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