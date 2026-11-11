#include "Trinity/RHI/Vulkan/VulkanContext.hpp"
#include "Trinity/RHI/Vulkan/VulkanSwapChain.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            std::string_view ToString(VkResult result)
            {
                switch (result)
                {
                    case VK_SUCCESS:
                    {
                        return "VK_SUCCESS";
                    }
                    case VK_ERROR_OUT_OF_HOST_MEMORY:
                    {
                        return "VK_ERROR_OUT_OF_HOST_MEMORY";
                    }
                    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                    {
                        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
                    }
                    case VK_ERROR_INITIALIZATION_FAILED:
                    {
                        return "VK_ERROR_INITIALIZATION_FAILED";
                    }
                    case VK_ERROR_LAYER_NOT_PRESENT:
                    {
                        return "VK_ERROR_LAYER_NOT_PRESENT";
                    }
                    case VK_ERROR_EXTENSION_NOT_PRESENT:
                    {
                        return "VK_ERROR_EXTENSION_NOT_PRESENT";
                    }
                    case VK_ERROR_FEATURE_NOT_PRESENT:
                    {
                        return "VK_ERROR_FEATURE_NOT_PRESENT";
                    }
                    case VK_ERROR_INCOMPATIBLE_DRIVER:
                    {
                        return "VK_ERROR_INCOMPATIBLE_DRIVER";
                    }
                    case VK_ERROR_DEVICE_LOST:
                    {
                        return "VK_ERROR_DEVICE_LOST";
                    }
                    default:
                    {
                        return "another VkResult";
                    }
                }
            }

            VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology)
            {
                switch (topology)
                {
                    case PrimitiveTopology::TriangleStrip:
                    {
                        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                    }
                    case PrimitiveTopology::LineList:
                    {
                        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                    }
                    case PrimitiveTopology::PointList:
                    {
                        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                    }
                    default:
                    {
                        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                    }
                }
            }

            VkCullModeFlags ToVkCullMode(CullMode cull)
            {
                switch (cull)
                {
                    case CullMode::Front:
                    {
                        return VK_CULL_MODE_FRONT_BIT;
                    }
                    case CullMode::Back:
                    {
                        return VK_CULL_MODE_BACK_BIT;
                    }
                    default:
                    {
                        return VK_CULL_MODE_NONE;
                    }
                }
            }

            VkCompareOp ToVkCompareOp(CompareOp compare)
            {
                switch (compare)
                {
                    case CompareOp::Never:
                    {
                        return VK_COMPARE_OP_NEVER;
                    }
                    case CompareOp::Less:
                    {
                        return VK_COMPARE_OP_LESS;
                    }
                    case CompareOp::Equal:
                    {
                        return VK_COMPARE_OP_EQUAL;
                    }
                    case CompareOp::LessOrEqual:
                    {
                        return VK_COMPARE_OP_LESS_OR_EQUAL;
                    }
                    case CompareOp::Greater:
                    {
                        return VK_COMPARE_OP_GREATER;
                    }
                    case CompareOp::NotEqual:
                    {
                        return VK_COMPARE_OP_NOT_EQUAL;
                    }
                    case CompareOp::GreaterOrEqual:
                    {
                        return VK_COMPARE_OP_GREATER_OR_EQUAL;
                    }
                    default:
                    {
                        return VK_COMPARE_OP_ALWAYS;
                    }
                }
            }

            VkStencilOp ToVkStencilOp(StencilOp operation)
            {
                switch (operation)
                {
                    case StencilOp::Zero:
                    {
                        return VK_STENCIL_OP_ZERO;
                    }
                    case StencilOp::Replace:
                    {
                        return VK_STENCIL_OP_REPLACE;
                    }
                    case StencilOp::IncrementClamp:
                    {
                        return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
                    }
                    case StencilOp::DecrementClamp:
                    {
                        return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
                    }
                    default:
                    {
                        return VK_STENCIL_OP_KEEP;
                    }
                }
            }
        }

        VkFormat ToVkFormat(Format format)
        {
            switch (format)
            {
                case Format::RGBA8Unorm:
                {
                    return VK_FORMAT_R8G8B8A8_UNORM;
                }
                case Format::RGBA8Srgb:
                {
                    return VK_FORMAT_R8G8B8A8_SRGB;
                }
                case Format::BGRA8Unorm:
                {
                    return VK_FORMAT_B8G8R8A8_UNORM;
                }
                case Format::BGRA8Srgb:
                {
                    return VK_FORMAT_B8G8R8A8_SRGB;
                }
                case Format::RGBA16Float:
                {
                    return VK_FORMAT_R16G16B16A16_SFLOAT;
                }
                case Format::R11G11B10Float:
                {
                    return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
                }
                case Format::R32Float:
                {
                    return VK_FORMAT_R32_SFLOAT;
                }
                case Format::R32Uint:
                {
                    return VK_FORMAT_R32_UINT;
                }
                case Format::RG32Float:
                {
                    return VK_FORMAT_R32G32_SFLOAT;
                }
                case Format::RGB32Float:
                {
                    return VK_FORMAT_R32G32B32_SFLOAT;
                }
                case Format::RGBA32Float:
                {
                    return VK_FORMAT_R32G32B32A32_SFLOAT;
                }
                case Format::D32Float:
                {
                    return VK_FORMAT_D32_SFLOAT;
                }
                case Format::D32FloatS8:
                {
                    return VK_FORMAT_D32_SFLOAT_S8_UINT;
                }
                case Format::BC1Unorm:
                {
                    return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
                }
                case Format::BC1Srgb:
                {
                    return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
                }
                case Format::BC3Unorm:
                {
                    return VK_FORMAT_BC3_UNORM_BLOCK;
                }
                case Format::BC3Srgb:
                {
                    return VK_FORMAT_BC3_SRGB_BLOCK;
                }
                case Format::BC4Unorm:
                {
                    return VK_FORMAT_BC4_UNORM_BLOCK;
                }
                case Format::BC5Unorm:
                {
                    return VK_FORMAT_BC5_UNORM_BLOCK;
                }
                case Format::BC7Unorm:
                {
                    return VK_FORMAT_BC7_UNORM_BLOCK;
                }
                case Format::BC7Srgb:
                {
                    return VK_FORMAT_BC7_SRGB_BLOCK;
                }
                default:
                {
                    return VK_FORMAT_UNDEFINED;
                }
            }
        }

        std::string FormatResult(VkResult result)
        {
            return std::format("{} ({})", ToString(result), static_cast<int>(result));
        }

        VkImageAspectFlags GetAspect(Format format)
        {
            if (HasStencil(format))
            {
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            return IsDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        }

        std::string FormatVersion(std::uint32_t version)
        {
            return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version));
        }

        bool HasExtension(const std::vector<VkExtensionProperties>& extensions, std::string_view name)
        {
            return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) { return name == extension.extensionName; });
        }

        Scope<VulkanContext> VulkanContext::Create(const DeviceSpecification& specification, std::string& error)
        {
            Scope<VulkanContext> l_Device = CreateScope<VulkanContext>();
            if (!l_Device->Initialize(specification, error))
            {
                return nullptr;
            }

            return l_Device;
        }

        VulkanContext::VulkanContext() : m_Device(m_Instance), m_Allocator(m_Instance, m_Device), m_Bindless(m_Device), m_Releases(m_Device, m_Allocator, m_Bindless), m_CommandList(*this)
        {

        }

        VulkanContext::~VulkanContext()
        {
            const bool l_Report = m_Instance.IsValidationEnabled() && m_Device.GetHandle() != VK_NULL_HANDLE;

            // The validation layer reports objects still alive in vkDestroyDevice and vkDestroyInstance, so the messenger outlives the device
            if (m_Device.GetHandle() != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_Device.GetHandle());
                m_UploadRing.Shutdown();

                if (m_Buffers.GetCount() != 0 || m_Textures.GetCount() != 0 || m_Pipelines.GetCount() != 0 || m_Samplers.GetCount() != 0 || m_QueryPools.GetCount() != 0)
                {
                    TR_CORE_WARN("Vulkan: the device was destroyed with {} buffer(s), {} texture(s), {} pipeline(s), {} sampler(s) and {} query pool(s) still alive", m_Buffers.GetCount(), m_Textures.GetCount(), m_Pipelines.GetCount(), m_Samplers.GetCount(), m_QueryPools.GetCount());
                }

                m_Releases.ReleaseAll();
                m_Buffers.ForEach([this](const VulkanBuffer& buffer) { m_Releases.Release(buffer.ToRelease()); });
                m_Textures.ForEach([this](const VulkanTexture& texture) { m_Releases.Release(texture.ToRelease()); });
                m_Pipelines.ForEach([this](const VulkanPipeline& pipeline) { vkDestroyPipeline(m_Device.GetHandle(), pipeline.Pipeline, nullptr); });
                m_Samplers.ForEach([this](const VulkanSampler& sampler) { m_Releases.Release(sampler.ToRelease()); });
                m_QueryPools.ForEach([this](const VulkanQueryPool& pool) { vkDestroyQueryPool(m_Device.GetHandle(), pool.Pool, nullptr); });

                m_Bindless.Destroy();

                for (const FrameContext& it_Frame : m_Frames)
                {
                    vkDestroyCommandPool(m_Device.GetHandle(), it_Frame.CommandPool, nullptr);
                }

                vkDestroySemaphore(m_Device.GetHandle(), m_FrameTimeline, nullptr);

                m_Allocator.Destroy();
                m_Device.Destroy();
            }

            m_Instance.Destroy();

            if (l_Report)
            {
                TR_CORE_INFO("Vulkan validation: {} warning(s) or error(s) while the device lived", m_Instance.GetMessageCount());
            }
        }

        bool VulkanContext::Initialize(const DeviceSpecification& specification, std::string& error)
        {
            return m_Instance.Initialize(specification, error) && m_Device.Initialize(error) && m_Allocator.Initialize(error) && CreateFrames(error) && m_Bindless.Initialize(error) && m_UploadRing.Initialize(*this, error);
        }

        bool VulkanContext::CreateFrames(std::string& error)
        {
            VkSemaphoreTypeCreateInfo l_Timeline = MakeInfo<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
            l_Timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

            VkSemaphoreCreateInfo l_Semaphore = MakeInfo<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
            l_Semaphore.pNext = &l_Timeline;

            VkResult l_Result = vkCreateSemaphore(m_Device.GetHandle(), &l_Semaphore, nullptr, &m_FrameTimeline);
            for (FrameContext& it_Frame : m_Frames)
            {
                if (l_Result != VK_SUCCESS)
                {
                    break;
                }

                VkCommandPoolCreateInfo l_Pool = MakeInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
                l_Pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                l_Pool.queueFamilyIndex = m_Device.GetQueueFamily();
                l_Result = vkCreateCommandPool(m_Device.GetHandle(), &l_Pool, nullptr, &it_Frame.CommandPool);
                if (l_Result != VK_SUCCESS)
                {
                    break;
                }

                VkCommandBufferAllocateInfo l_Allocate = MakeInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
                l_Allocate.commandPool = it_Frame.CommandPool;
                l_Allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                l_Allocate.commandBufferCount = 1;
                l_Result = vkAllocateCommandBuffers(m_Device.GetHandle(), &l_Allocate, &it_Frame.CommandBuffer);
            }

            if (l_Result != VK_SUCCESS)
            {
                error = std::format("the frame command pools and timeline semaphore could not be created on {} ({})", m_Device.GetInfo().AdapterName, FormatResult(l_Result));

                return false;
            }

            return true;
        }

        BufferHandle VulkanContext::CreateBuffer(const BufferDescription& description)
        {
            const std::optional<VulkanBuffer> l_Buffer = VulkanBuffer::Create(m_Device, m_Allocator, m_Bindless, description);
            if (!l_Buffer)
            {
                return {};
            }

            return m_Buffers.Add(*l_Buffer);
        }

        void VulkanContext::DestroyBuffer(BufferHandle buffer)
        {
            if (!buffer)
            {
                return;
            }

            const std::optional<VulkanBuffer> l_Buffer = m_Buffers.Remove(buffer);
            TR_CORE_ASSERT(l_Buffer.has_value(), "DestroyBuffer on a buffer that was already destroyed.");

            if (l_Buffer)
            {
                m_Releases.Push(l_Buffer->ToRelease());
            }
        }

        std::span<std::byte> VulkanContext::GetMappedData(BufferHandle buffer)
        {
            const VulkanBuffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetMappedData on a destroyed or invalid buffer.");

            if (l_Buffer == nullptr || l_Buffer->Mapped == nullptr)
            {
                return {};
            }

            return { l_Buffer->Mapped, static_cast<std::size_t>(l_Buffer->Size) };
        }

        bool VulkanContext::IsFormatSupported(Format format, TextureUsage usage, std::uint32_t sampleCount) const
        {
            return VulkanTexture::IsFormatSupported(m_Device, format, usage, sampleCount);
        }

        TextureHandle VulkanContext::CreateTexture(const TextureDescription& description)
        {
            TR_CORE_ASSERT(description.Width != 0 && description.Height != 0 && description.MipLevels != 0, "Texture '{}' has a zero size or no mips.", description.DebugName);
            TR_CORE_ASSERT(description.TextureFormat != Format::Unknown, "Texture '{}' has no format.", description.DebugName);

            if (!CanCreateTexture(description))
            {
                return {};
            }

            std::optional<VulkanTexture> l_Texture = VulkanTexture::Create(m_Device, m_Allocator, m_Bindless, description);
            if (!l_Texture)
            {
                return {};
            }

            return m_Textures.Add(std::move(*l_Texture));
        }

        void VulkanContext::DestroyTexture(TextureHandle texture)
        {
            if (!texture)
            {
                return;
            }

            const std::optional<VulkanTexture> l_Texture = m_Textures.Remove(texture);
            TR_CORE_ASSERT(l_Texture.has_value(), "DestroyTexture on a texture that was already destroyed.");

            if (l_Texture)
            {
                m_Releases.Push(l_Texture->ToRelease());
            }
        }

        TextureHandle VulkanContext::AddSwapChainImage(VkImage image, Format format, std::uint32_t width, std::uint32_t height)
        {
            return m_Textures.Add(VulkanTexture::FromSwapChainImage(m_Device, image, format, width, height));
        }

        // The swap chain waits for the device to be idle first, so the view goes at once
        void VulkanContext::RemoveSwapChainImage(TextureHandle texture)
        {
            const std::optional<VulkanTexture> l_Texture = m_Textures.Remove(texture);
            if (l_Texture)
            {
                l_Texture->DestroySwapChainView(m_Device);
            }
        }

        void VulkanContext::WaitForSemaphore(VkSemaphore semaphore)
        {
            VkSemaphoreSubmitInfo l_Wait = MakeInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            l_Wait.semaphore = semaphore;
            l_Wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            m_FrameWaits.push_back(l_Wait);
        }

        void VulkanContext::SignalSemaphore(VkSemaphore semaphore)
        {
            VkSemaphoreSubmitInfo l_Signal = MakeInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            l_Signal.semaphore = semaphore;
            l_Signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            m_FrameSignals.push_back(l_Signal);
        }

        std::uint32_t VulkanContext::GetShaderResourceIndex(BufferHandle buffer)
        {
            const VulkanBuffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetShaderResourceIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr ? l_Buffer->ShaderResourceIndex : c_NoBindlessIndex;
        }

        std::uint32_t VulkanContext::GetUnorderedAccessIndex(BufferHandle buffer)
        {
            const VulkanBuffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr ? l_Buffer->UnorderedAccessIndex : c_NoBindlessIndex;
        }

        std::uint32_t VulkanContext::GetShaderResourceIndex(TextureHandle texture)
        {
            const VulkanTexture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetShaderResourceIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr ? l_Texture->ShaderResourceIndex : c_NoBindlessIndex;
        }

        std::uint32_t VulkanContext::GetUnorderedAccessIndex(TextureHandle texture)
        {
            const VulkanTexture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr ? l_Texture->UnorderedAccessIndex : c_NoBindlessIndex;
        }

        SamplerHandle VulkanContext::CreateSampler(const SamplerDescription& description)
        {
            const std::optional<VulkanSampler> l_Sampler = VulkanSampler::Create(m_Device, m_Bindless, description);
            if (!l_Sampler)
            {
                return {};
            }

            return m_Samplers.Add(*l_Sampler);
        }

        void VulkanContext::DestroySampler(SamplerHandle sampler)
        {
            if (!sampler)
            {
                return;
            }

            const std::optional<VulkanSampler> l_Sampler = m_Samplers.Remove(sampler);
            TR_CORE_ASSERT(l_Sampler.has_value(), "DestroySampler on a sampler that was already destroyed.");

            if (l_Sampler)
            {
                m_Releases.Push(l_Sampler->ToRelease());
            }
        }

        std::uint32_t VulkanContext::GetSamplerIndex(SamplerHandle sampler)
        {
            const VulkanSampler* l_Sampler = m_Samplers.Get(sampler);
            TR_CORE_ASSERT(l_Sampler != nullptr, "GetSamplerIndex on a destroyed or invalid sampler.");

            return l_Sampler != nullptr ? l_Sampler->Index : c_NoBindlessIndex;
        }

        PipelineHandle VulkanContext::CreateGraphicsPipeline(const GraphicsPipelineDescription& description)
        {
            TR_CORE_ASSERT(!description.VertexShader.Code.empty() && !description.PixelShader.Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);
            TR_CORE_ASSERT(description.ColorFormats.size() <= c_MaxColorAttachments, "Pipeline '{}' has too many color formats.", description.DebugName);
            TR_CORE_ASSERT(!description.StencilTest || HasStencil(description.DepthFormat), "Pipeline '{}' tests stencil without a depth format that has it.", description.DebugName);

            const std::array<const ShaderDescription*, 2> l_Shaders{ &description.VertexShader, &description.PixelShader };
            const std::array<VkShaderStageFlagBits, 2> l_StageBits{ VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT };
            std::array<std::string, 2> l_EntryPoints{};
            std::array<VkShaderModule, 2> l_Modules{};
            std::array<VkPipelineShaderStageCreateInfo, 2> l_Stages{};
            VkResult l_Result = VK_SUCCESS;
            for (std::size_t it_Stage = 0; it_Stage < l_Shaders.size() && l_Result == VK_SUCCESS; ++it_Stage)
            {
                const std::span<const std::byte> l_Code = l_Shaders[it_Stage]->Code;
                TR_CORE_ASSERT(l_Code.size() % 4 == 0 && reinterpret_cast<std::uintptr_t>(l_Code.data()) % 4 == 0, "Pipeline '{}' has SPIR-V that is not whole, aligned 32-bit words.", description.DebugName);

                VkShaderModuleCreateInfo l_Module = MakeInfo<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
                l_Module.codeSize = l_Code.size();
                l_Module.pCode = reinterpret_cast<const std::uint32_t*>(l_Code.data());
                l_Result = vkCreateShaderModule(m_Device.GetHandle(), &l_Module, nullptr, &l_Modules[it_Stage]);

                l_EntryPoints[it_Stage] = l_Shaders[it_Stage]->EntryPoint;
                l_Stages[it_Stage] = MakeInfo<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
                l_Stages[it_Stage].stage = l_StageBits[it_Stage];
                l_Stages[it_Stage].module = l_Modules[it_Stage];
                l_Stages[it_Stage].pName = l_EntryPoints[it_Stage].c_str();
            }

            const VkPipelineVertexInputStateCreateInfo l_VertexInput = MakeInfo<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);

            VkPipelineInputAssemblyStateCreateInfo l_InputAssembly = MakeInfo<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
            l_InputAssembly.topology = ToVkTopology(description.Topology);

            VkPipelineViewportStateCreateInfo l_Viewport = MakeInfo<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
            l_Viewport.viewportCount = 1;
            l_Viewport.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo l_Rasterization = MakeInfo<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
            l_Rasterization.polygonMode = VK_POLYGON_MODE_FILL;
            l_Rasterization.cullMode = ToVkCullMode(description.Cull);
            l_Rasterization.frontFace = description.FrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
            l_Rasterization.depthClampEnable = description.DepthClamp ? VK_TRUE : VK_FALSE;
            l_Rasterization.depthBiasEnable = description.DepthBiasConstant != 0 || description.DepthBiasSlope != 0.0f ? VK_TRUE : VK_FALSE;
            l_Rasterization.depthBiasConstantFactor = static_cast<float>(description.DepthBiasConstant);
            l_Rasterization.depthBiasSlopeFactor = description.DepthBiasSlope;
            l_Rasterization.depthBiasClamp = description.DepthBiasClamp;
            l_Rasterization.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo l_Multisample = MakeInfo<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
            l_Multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(description.SampleCount);

            VkPipelineDepthStencilStateCreateInfo l_DepthStencil = MakeInfo<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
            l_DepthStencil.depthTestEnable = description.DepthTest ? VK_TRUE : VK_FALSE;
            l_DepthStencil.depthWriteEnable = description.DepthWrite ? VK_TRUE : VK_FALSE;
            l_DepthStencil.depthCompareOp = ToVkCompareOp(description.DepthCompare);
            l_DepthStencil.stencilTestEnable = description.StencilTest ? VK_TRUE : VK_FALSE;
            l_DepthStencil.front = { VK_STENCIL_OP_KEEP, ToVkStencilOp(description.StencilPass), VK_STENCIL_OP_KEEP, ToVkCompareOp(description.StencilCompare), description.StencilReadMask, description.StencilWriteMask, 0 };
            l_DepthStencil.back = l_DepthStencil.front;

            std::array<VkPipelineColorBlendAttachmentState, c_MaxColorAttachments> l_Blends{};
            std::array<VkFormat, c_MaxColorAttachments> l_ColorFormats{};
            const std::uint32_t l_ColorCount = static_cast<std::uint32_t>(std::min<std::size_t>(description.ColorFormats.size(), c_MaxColorAttachments));
            for (std::uint32_t it_Color = 0; it_Color < l_ColorCount; ++it_Color)
            {
                // An integer target is never blended
                VkPipelineColorBlendAttachmentState& l_Blend = l_Blends[it_Color];
                l_Blend.blendEnable = description.AlphaBlend && !IsIntegerFormat(description.ColorFormats[it_Color]) ? VK_TRUE : VK_FALSE;
                l_Blend.srcColorBlendFactor = description.PremultipliedAlpha ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
                l_Blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                l_Blend.colorBlendOp = VK_BLEND_OP_ADD;
                l_Blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                l_Blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                l_Blend.alphaBlendOp = VK_BLEND_OP_ADD;
                l_Blend.colorWriteMask = description.ColorWrite ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT : 0;

                l_ColorFormats[it_Color] = ToVkFormat(description.ColorFormats[it_Color]);
            }

            VkPipelineColorBlendStateCreateInfo l_ColorBlend = MakeInfo<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
            l_ColorBlend.attachmentCount = l_ColorCount;
            l_ColorBlend.pAttachments = l_Blends.data();

            // The stencil reference is set on the command list, as D3D12 sets it
            const std::array<VkDynamicState, 3> l_DynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE };
            VkPipelineDynamicStateCreateInfo l_Dynamic = MakeInfo<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
            l_Dynamic.dynamicStateCount = static_cast<std::uint32_t>(description.StencilTest ? l_DynamicStates.size() : l_DynamicStates.size() - 1);
            l_Dynamic.pDynamicStates = l_DynamicStates.data();

            VkPipelineRenderingCreateInfo l_Rendering = MakeInfo<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
            l_Rendering.colorAttachmentCount = l_ColorCount;
            l_Rendering.pColorAttachmentFormats = l_ColorFormats.data();
            l_Rendering.depthAttachmentFormat = description.DepthFormat != Format::Unknown ? ToVkFormat(description.DepthFormat) : VK_FORMAT_UNDEFINED;
            l_Rendering.stencilAttachmentFormat = HasStencil(description.DepthFormat) ? ToVkFormat(description.DepthFormat) : VK_FORMAT_UNDEFINED;

            VkGraphicsPipelineCreateInfo l_Create = MakeInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
            l_Create.pNext = &l_Rendering;
            l_Create.stageCount = static_cast<std::uint32_t>(l_Stages.size());
            l_Create.pStages = l_Stages.data();
            l_Create.pVertexInputState = &l_VertexInput;
            l_Create.pInputAssemblyState = &l_InputAssembly;
            l_Create.pViewportState = &l_Viewport;
            l_Create.pRasterizationState = &l_Rasterization;
            l_Create.pMultisampleState = &l_Multisample;
            l_Create.pDepthStencilState = &l_DepthStencil;
            l_Create.pColorBlendState = &l_ColorBlend;
            l_Create.pDynamicState = &l_Dynamic;
            l_Create.layout = m_Bindless.GetPipelineLayout();

            VulkanPipeline l_Pipeline;
            if (l_Result == VK_SUCCESS)
            {
                l_Result = vkCreateGraphicsPipelines(m_Device.GetHandle(), VK_NULL_HANDLE, 1, &l_Create, nullptr, &l_Pipeline.Pipeline);
            }

            for (VkShaderModule it_Module : l_Modules)
            {
                vkDestroyShaderModule(m_Device.GetHandle(), it_Module, nullptr);
            }

            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: pipeline '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(l_Pipeline.Pipeline), description.DebugName);

            return m_Pipelines.Add(l_Pipeline);
        }

        // Reset on the host at once, so its first timestamps need no reset recorded before them
        QueryPoolHandle VulkanContext::CreateQueryPool(const QueryPoolDescription& description)
        {
            TR_CORE_ASSERT(description.Count != 0, "Query pool '{}' has no queries.", description.DebugName);
            if (!m_Device.HasTimestamps())
            {
                TR_CORE_ERROR("Vulkan: query pool '{}' could not be created, since {} has no timestamps on its graphics queue", description.DebugName, m_Device.GetInfo().AdapterName);

                return {};
            }

            VkQueryPoolCreateInfo l_Create = MakeInfo<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
            l_Create.queryType = VK_QUERY_TYPE_TIMESTAMP;
            l_Create.queryCount = description.Count;

            VulkanQueryPool l_Pool;
            l_Pool.Count = description.Count;
            const VkResult l_Result = vkCreateQueryPool(m_Device.GetHandle(), &l_Create, nullptr, &l_Pool.Pool);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: query pool '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            vkResetQueryPool(m_Device.GetHandle(), l_Pool.Pool, 0, description.Count);
            SetDebugName(VK_OBJECT_TYPE_QUERY_POOL, reinterpret_cast<std::uint64_t>(l_Pool.Pool), description.DebugName);

            return m_QueryPools.Add(l_Pool);
        }

        void VulkanContext::DestroyQueryPool(QueryPoolHandle pool)
        {
            if (!pool)
            {
                return;
            }

            const std::optional<VulkanQueryPool> l_Pool = m_QueryPools.Remove(pool);
            TR_CORE_ASSERT(l_Pool.has_value(), "DestroyQueryPool on a query pool that was already destroyed.");

            if (l_Pool)
            {
                VulkanRelease l_Release;
                l_Release.QueryPool = l_Pool->Pool;
                m_Releases.Push(std::move(l_Release));
            }
        }

        // Vulkan gives nanoseconds per tick
        std::uint64_t VulkanContext::GetTimestampFrequency() const
        {
            return static_cast<std::uint64_t>(std::llround(1000000000.0 / static_cast<double>(m_Device.GetTimestampPeriod())));
        }

        PipelineHandle VulkanContext::CreateComputePipeline(const ComputePipelineDescription& description)
        {
            const std::span<const std::byte> l_Code = description.ComputeShader.Code;
            TR_CORE_ASSERT(!l_Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);
            TR_CORE_ASSERT(l_Code.size() % 4 == 0 && reinterpret_cast<std::uintptr_t>(l_Code.data()) % 4 == 0, "Pipeline '{}' has SPIR-V that is not whole, aligned 32-bit words.", description.DebugName);

            VkShaderModuleCreateInfo l_Module = MakeInfo<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
            l_Module.codeSize = l_Code.size();
            l_Module.pCode = reinterpret_cast<const std::uint32_t*>(l_Code.data());

            VkShaderModule l_ShaderModule = VK_NULL_HANDLE;
            VkResult l_Result = vkCreateShaderModule(m_Device.GetHandle(), &l_Module, nullptr, &l_ShaderModule);

            const std::string l_EntryPoint(description.ComputeShader.EntryPoint);

            VkComputePipelineCreateInfo l_Create = MakeInfo<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
            l_Create.stage = MakeInfo<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            l_Create.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            l_Create.stage.module = l_ShaderModule;
            l_Create.stage.pName = l_EntryPoint.c_str();
            l_Create.layout = m_Bindless.GetPipelineLayout();

            VulkanPipeline l_Pipeline;
            l_Pipeline.BindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
            if (l_Result == VK_SUCCESS)
            {
                l_Result = vkCreateComputePipelines(m_Device.GetHandle(), VK_NULL_HANDLE, 1, &l_Create, nullptr, &l_Pipeline.Pipeline);
            }

            vkDestroyShaderModule(m_Device.GetHandle(), l_ShaderModule, nullptr);

            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: pipeline '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(l_Pipeline.Pipeline), description.DebugName);

            return m_Pipelines.Add(l_Pipeline);
        }

        void VulkanContext::DestroyPipeline(PipelineHandle pipeline)
        {
            if (!pipeline)
            {
                return;
            }

            const std::optional<VulkanPipeline> l_Pipeline = m_Pipelines.Remove(pipeline);
            TR_CORE_ASSERT(l_Pipeline.has_value(), "DestroyPipeline on a pipeline that was already destroyed.");

            if (l_Pipeline)
            {
                VulkanRelease l_Release;
                l_Release.Pipeline = l_Pipeline->Pipeline;
                m_Releases.Push(l_Release);
            }
        }

        Scope<SwapChain> VulkanContext::CreateSwapChain(const SwapChainSpecification& specification)
        {
            return VulkanSwapChain::Create(*this, specification);
        }

        // Waits until the frame c_FramesInFlight before this one, which used the same command pool, has finished on the GPU
        CommandList& VulkanContext::BeginFrame()
        {
            TR_CORE_ASSERT(!m_InFrame, "BeginFrame was called twice without EndFrame.");

            FrameContext& l_Frame = m_Frames[GetFrameSlot()];
            if (l_Frame.CompletionValue != 0)
            {
                VkSemaphoreWaitInfo l_Wait = MakeInfo<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
                l_Wait.semaphoreCount = 1;
                l_Wait.pSemaphores = &m_FrameTimeline;
                l_Wait.pValues = &l_Frame.CompletionValue;
                vkWaitSemaphores(m_Device.GetHandle(), &l_Wait, UINT64_MAX);
            }

            m_InFrame = true;
            m_Releases.BeginFrame();
            m_UploadRing.BeginFrame();

            vkResetCommandPool(m_Device.GetHandle(), l_Frame.CommandPool, 0);
            m_FrameWaits.clear();
            m_FrameSignals.clear();
            m_CommandList.Begin(l_Frame.CommandBuffer);

            return m_CommandList;
        }

        void VulkanContext::EndFrame()
        {
            TR_CORE_ASSERT(m_InFrame, "EndFrame without BeginFrame.");

            FrameContext& l_Frame = m_Frames[GetFrameSlot()];
            m_UploadRing.EndFrame();
            m_CommandList.End();

            l_Frame.CompletionValue = m_FrameNumber + 1;
            SignalSemaphore(m_FrameTimeline);
            m_FrameSignals.back().value = l_Frame.CompletionValue;

            VkCommandBufferSubmitInfo l_Commands = MakeInfo<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
            l_Commands.commandBuffer = l_Frame.CommandBuffer;

            VkSubmitInfo2 l_Submit = MakeInfo<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
            l_Submit.waitSemaphoreInfoCount = static_cast<std::uint32_t>(m_FrameWaits.size());
            l_Submit.pWaitSemaphoreInfos = m_FrameWaits.data();
            l_Submit.commandBufferInfoCount = 1;
            l_Submit.pCommandBufferInfos = &l_Commands;
            l_Submit.signalSemaphoreInfoCount = static_cast<std::uint32_t>(m_FrameSignals.size());
            l_Submit.pSignalSemaphoreInfos = m_FrameSignals.data();

            const VkResult l_Result = vkQueueSubmit2(m_Device.GetQueue(), 1, &l_Submit, VK_NULL_HANDLE);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: frame {} could not be submitted ({})", m_FrameNumber, FormatResult(l_Result));
            }

            ++m_FrameNumber;
            m_Releases.EndFrame();
            m_InFrame = false;
        }

        void VulkanContext::WaitIdle()
        {
            if (m_Device.GetHandle() != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_Device.GetHandle());
                m_Releases.ReleaseIdle();
            }
        }
    }
}