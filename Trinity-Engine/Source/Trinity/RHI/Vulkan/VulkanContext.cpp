#include "Trinity/RHI/Vulkan/VulkanContext.hpp"
#include "Trinity/RHI/Vulkan/VulkanSwapChain.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"

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

            // VulkanMemoryAllocator's bookkeeping, and what the driver allocates for the objects it creates through it, counted under the Renderer tag
            void* VKAPI_PTR AllocateHostMemory([[maybe_unused]] void* userData, std::size_t size, std::size_t alignment, [[maybe_unused]] VkSystemAllocationScope scope)
            {
                return Memory::TryAllocate(size, MemoryTag::Renderer, alignment);
            }

            void* VKAPI_PTR ReallocateHostMemory([[maybe_unused]] void* userData, void* original, std::size_t size, std::size_t alignment, [[maybe_unused]] VkSystemAllocationScope scope)
            {
                return Memory::Reallocate(original, size, MemoryTag::Renderer, alignment);
            }

            void VKAPI_PTR FreeHostMemory([[maybe_unused]] void* userData, void* memory)
            {
                Memory::Free(memory);
            }

            constexpr VkAllocationCallbacks c_HostAllocator{ nullptr, &AllocateHostMemory, &ReallocateHostMemory, &FreeHostMemory, nullptr, nullptr };

            VkBufferUsageFlags ToVkBufferUsage(BufferUsage usage, MemoryType memory)
            {
                VkBufferUsageFlags l_Flags = 0;
                if (HasFlag(usage, BufferUsage::ShaderResource) || HasFlag(usage, BufferUsage::UnorderedAccess))
                {
                    l_Flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                }

                if (HasFlag(usage, BufferUsage::Index))
                {
                    l_Flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                }

                if (HasFlag(usage, BufferUsage::Indirect))
                {
                    l_Flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
                }

                if (HasFlag(usage, BufferUsage::CopySource) || memory == MemoryType::Upload)
                {
                    l_Flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                }

                if (HasFlag(usage, BufferUsage::CopyDestination) || memory == MemoryType::Readback)
                {
                    l_Flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                }

                return l_Flags;
            }

            VkImageUsageFlags ToVkImageUsage(TextureUsage usage)
            {
                VkImageUsageFlags l_Flags = 0;
                if (HasFlag(usage, TextureUsage::ShaderResource))
                {
                    l_Flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
                }

                if (HasFlag(usage, TextureUsage::UnorderedAccess))
                {
                    l_Flags |= VK_IMAGE_USAGE_STORAGE_BIT;
                }

                if (HasFlag(usage, TextureUsage::RenderTarget))
                {
                    l_Flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                }

                if (HasFlag(usage, TextureUsage::DepthStencil))
                {
                    l_Flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                }

                if (HasFlag(usage, TextureUsage::CopySource))
                {
                    l_Flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                }

                if (HasFlag(usage, TextureUsage::CopyDestination))
                {
                    l_Flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                }

                return l_Flags;
            }

            VkImageViewType ToVkSampledViewType(TextureDimension dimension)
            {
                switch (dimension)
                {
                    case TextureDimension::Texture2DArray:
                    {
                        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                    }
                    case TextureDimension::TextureCube:
                    {
                        return VK_IMAGE_VIEW_TYPE_CUBE;
                    }
                    default:
                    {
                        return VK_IMAGE_VIEW_TYPE_2D;
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

            // The bindings Slang gives descriptor handles when Bindless.slang turns off mutable descriptors
            constexpr std::uint32_t c_SamplerBinding = 0;
            constexpr std::uint32_t c_SampledImageBinding = 2;
            constexpr std::uint32_t c_StorageImageBinding = 3;
            constexpr std::uint32_t c_StorageBufferBinding = 7;

            struct BindlessLimit
            {
                std::string_view Name;
                std::uint32_t Allowed = 0;
                std::uint32_t Needed = 0;
            };

            VkFilter ToVkFilter(Filter filter)
            {
                return filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            }

            VkSamplerAddressMode ToVkAddressMode(AddressMode address)
            {
                switch (address)
                {
                    case AddressMode::MirroredRepeat:
                    {
                        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                    }
                    case AddressMode::ClampToEdge:
                    {
                        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    }
                    default:
                    {
                        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
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

        VulkanContext::VulkanContext() : m_Device(m_Instance), m_CommandList(*this)
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

                m_Releases.ReleaseAll([this](const VulkanRelease& release) { Release(release); });
                m_Buffers.ForEach([this](const VulkanBuffer& buffer) { Release(ToRelease(buffer)); });
                m_Textures.ForEach([this](const VulkanTexture& texture) { Release(ToRelease(texture)); });
                m_Pipelines.ForEach([this](const VulkanPipeline& pipeline) { vkDestroyPipeline(m_Device.GetHandle(), pipeline.Pipeline, nullptr); });
                m_Samplers.ForEach([this](const VulkanSampler& sampler) { Release(ToRelease(sampler)); });
                m_QueryPools.ForEach([this](const VulkanQueryPool& pool) { vkDestroyQueryPool(m_Device.GetHandle(), pool.Pool, nullptr); });

                vkDestroyPipelineLayout(m_Device.GetHandle(), m_PipelineLayout, nullptr);
                vkDestroyDescriptorPool(m_Device.GetHandle(), m_BindlessPool, nullptr);
                vkDestroyDescriptorSetLayout(m_Device.GetHandle(), m_BindlessLayout, nullptr);

                for (const FrameContext& it_Frame : m_Frames)
                {
                    vkDestroyCommandPool(m_Device.GetHandle(), it_Frame.CommandPool, nullptr);
                }

                vkDestroySemaphore(m_Device.GetHandle(), m_FrameTimeline, nullptr);

                if (m_Allocator != nullptr)
                {
                    vmaDestroyAllocator(m_Allocator);
                }

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
            return m_Instance.Initialize(specification, error) && m_Device.Initialize(error) && CreateAllocator(error) && CreateFrames(error) && CreateBindless(error) && m_UploadRing.Initialize(*this, error);
        }

        bool VulkanContext::CreateAllocator(std::string& error)
        {
            VmaAllocatorCreateInfo l_Create{};
            l_Create.physicalDevice = m_Device.GetPhysicalDevice();
            l_Create.device = m_Device.GetHandle();
            l_Create.instance = m_Instance.GetHandle();
            l_Create.vulkanApiVersion = VK_API_VERSION_1_3;
            l_Create.pAllocationCallbacks = &c_HostAllocator;

            VmaVulkanFunctions l_Functions{};
            VkResult l_Result = vmaImportVulkanFunctionsFromVolk(&l_Create, &l_Functions);
            if (l_Result == VK_SUCCESS)
            {
                l_Create.pVulkanFunctions = &l_Functions;
                l_Result = vmaCreateAllocator(&l_Create, &m_Allocator);
            }

            if (l_Result != VK_SUCCESS)
            {
                error = std::format("VulkanMemoryAllocator could not be created on {} ({})", m_Device.GetInfo().AdapterName, FormatResult(l_Result));

                return false;
            }

            return true;
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

        // One set holds every descriptor, and every pipeline shares its layout and the push constant range, so the set is bound once per frame
        bool VulkanContext::CreateBindless(std::string& error)
        {
            VkPhysicalDeviceVulkan12Properties l_Limits = MakeInfo<VkPhysicalDeviceVulkan12Properties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);
            VkPhysicalDeviceProperties2 l_Properties = MakeInfo<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
            l_Properties.pNext = &l_Limits;
            vkGetPhysicalDeviceProperties2(m_Device.GetPhysicalDevice(), &l_Properties);

            const std::array<BindlessLimit, 5> l_Checks
            { {
                { "samplers", std::min(l_Limits.maxPerStageDescriptorUpdateAfterBindSamplers, l_Limits.maxDescriptorSetUpdateAfterBindSamplers), c_BindlessSamplerCapacity },
                { "sampled images", std::min(l_Limits.maxPerStageDescriptorUpdateAfterBindSampledImages, l_Limits.maxDescriptorSetUpdateAfterBindSampledImages), c_BindlessResourceCapacity },
                { "storage images", std::min(l_Limits.maxPerStageDescriptorUpdateAfterBindStorageImages, l_Limits.maxDescriptorSetUpdateAfterBindStorageImages), c_BindlessResourceCapacity },
                { "storage buffers", std::min(l_Limits.maxPerStageDescriptorUpdateAfterBindStorageBuffers, l_Limits.maxDescriptorSetUpdateAfterBindStorageBuffers), c_BindlessResourceCapacity },
                { "descriptors per stage", l_Limits.maxPerStageUpdateAfterBindResources, c_BindlessSamplerCapacity + 3 * c_BindlessResourceCapacity }
            } };

            for (const BindlessLimit& it_Check : l_Checks)
            {
                if (it_Check.Allowed < it_Check.Needed)
                {
                    error = std::format("{} allows {} bindless {}, and the engine needs {}", m_Device.GetInfo().AdapterName, it_Check.Allowed, it_Check.Name, it_Check.Needed);

                    return false;
                }
            }

            const std::array<VkDescriptorSetLayoutBinding, 4> l_Bindings
            { {
                { c_SamplerBinding, VK_DESCRIPTOR_TYPE_SAMPLER, c_BindlessSamplerCapacity, VK_SHADER_STAGE_ALL, nullptr },
                { c_SampledImageBinding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, c_BindlessResourceCapacity, VK_SHADER_STAGE_ALL, nullptr },
                { c_StorageImageBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, c_BindlessResourceCapacity, VK_SHADER_STAGE_ALL, nullptr },
                { c_StorageBufferBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, c_BindlessResourceCapacity, VK_SHADER_STAGE_ALL, nullptr }
            } };

            constexpr VkDescriptorBindingFlags c_BindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
            std::array<VkDescriptorBindingFlags, 4> l_Flags{};
            l_Flags.fill(c_BindingFlags);

            VkDescriptorSetLayoutBindingFlagsCreateInfo l_BindingFlags = MakeInfo<VkDescriptorSetLayoutBindingFlagsCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
            l_BindingFlags.bindingCount = static_cast<std::uint32_t>(l_Flags.size());
            l_BindingFlags.pBindingFlags = l_Flags.data();

            VkDescriptorSetLayoutCreateInfo l_Layout = MakeInfo<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
            l_Layout.pNext = &l_BindingFlags;
            l_Layout.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            l_Layout.bindingCount = static_cast<std::uint32_t>(l_Bindings.size());
            l_Layout.pBindings = l_Bindings.data();

            VkResult l_Result = vkCreateDescriptorSetLayout(m_Device.GetHandle(), &l_Layout, nullptr, &m_BindlessLayout);
            if (l_Result == VK_SUCCESS)
            {
                const std::array<VkDescriptorPoolSize, 4> l_Sizes
                { {
                    { VK_DESCRIPTOR_TYPE_SAMPLER, c_BindlessSamplerCapacity },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, c_BindlessResourceCapacity },
                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, c_BindlessResourceCapacity },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, c_BindlessResourceCapacity }
                } };

                VkDescriptorPoolCreateInfo l_Pool = MakeInfo<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
                l_Pool.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
                l_Pool.maxSets = 1;
                l_Pool.poolSizeCount = static_cast<std::uint32_t>(l_Sizes.size());
                l_Pool.pPoolSizes = l_Sizes.data();
                l_Result = vkCreateDescriptorPool(m_Device.GetHandle(), &l_Pool, nullptr, &m_BindlessPool);
            }

            if (l_Result == VK_SUCCESS)
            {
                VkDescriptorSetAllocateInfo l_Allocate = MakeInfo<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
                l_Allocate.descriptorPool = m_BindlessPool;
                l_Allocate.descriptorSetCount = 1;
                l_Allocate.pSetLayouts = &m_BindlessLayout;
                l_Result = vkAllocateDescriptorSets(m_Device.GetHandle(), &l_Allocate, &m_BindlessSet);
            }

            if (l_Result == VK_SUCCESS)
            {
                const VkPushConstantRange l_PushConstants{ VK_SHADER_STAGE_ALL, 0, c_MaxPushConstantSize };

                VkPipelineLayoutCreateInfo l_PipelineLayout = MakeInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
                l_PipelineLayout.setLayoutCount = 1;
                l_PipelineLayout.pSetLayouts = &m_BindlessLayout;
                l_PipelineLayout.pushConstantRangeCount = 1;
                l_PipelineLayout.pPushConstantRanges = &l_PushConstants;
                l_Result = vkCreatePipelineLayout(m_Device.GetHandle(), &l_PipelineLayout, nullptr, &m_PipelineLayout);
            }

            if (l_Result != VK_SUCCESS)
            {
                error = std::format("the bindless descriptor set could not be created on {} ({})", m_Device.GetInfo().AdapterName, FormatResult(l_Result));

                return false;
            }

            SetDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<std::uint64_t>(m_BindlessSet), "Bindless");
            m_ResourceIndices.Reset(c_BindlessResourceCapacity);
            m_SamplerIndices.Reset(c_BindlessSamplerCapacity);

            return true;
        }

        VkImageView VulkanContext::CreateView(VkImage image, Format format, VkImageViewType type, std::uint32_t baseMipLevel, std::uint32_t mipLevels, std::uint32_t baseArrayLayer, std::uint32_t arrayLayers)
        {
            VkImageViewCreateInfo l_Create = MakeInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
            l_Create.image = image;
            l_Create.viewType = type;
            l_Create.format = ToVkFormat(format);
            l_Create.subresourceRange = { GetAspect(format), baseMipLevel, mipLevels, baseArrayLayer, arrayLayers };

            VkImageView l_View = VK_NULL_HANDLE;
            const VkResult l_Result = vkCreateImageView(m_Device.GetHandle(), &l_Create, nullptr, &l_View);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: a view of {} mip(s) and {} layer(s) of a {} image could not be created ({})", mipLevels, arrayLayers, ToString(format), FormatResult(l_Result));

                return VK_NULL_HANDLE;
            }

            return l_View;
        }

        std::uint32_t VulkanContext::AddBufferDescriptor(VkBuffer buffer)
        {
            const std::uint32_t l_Index = m_ResourceIndices.Allocate();
            if (l_Index == c_NoBindlessIndex)
            {
                TR_CORE_ERROR("Vulkan: all {} bindless resource indices are in use", c_BindlessResourceCapacity);

                return c_NoBindlessIndex;
            }

            const VkDescriptorBufferInfo l_Info{ buffer, 0, VK_WHOLE_SIZE };

            VkWriteDescriptorSet l_Write = MakeInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            l_Write.dstSet = m_BindlessSet;
            l_Write.dstBinding = c_StorageBufferBinding;
            l_Write.dstArrayElement = l_Index;
            l_Write.descriptorCount = 1;
            l_Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            l_Write.pBufferInfo = &l_Info;
            vkUpdateDescriptorSets(m_Device.GetHandle(), 1, &l_Write, 0, nullptr);

            return l_Index;
        }

        std::uint32_t VulkanContext::AddImageDescriptor(std::uint32_t binding, VkDescriptorType type, VkImageView view, VkImageLayout layout)
        {
            if (view == VK_NULL_HANDLE)
            {
                return c_NoBindlessIndex;
            }

            const std::uint32_t l_Index = m_ResourceIndices.Allocate();
            if (l_Index == c_NoBindlessIndex)
            {
                TR_CORE_ERROR("Vulkan: all {} bindless resource indices are in use", c_BindlessResourceCapacity);

                return c_NoBindlessIndex;
            }

            const VkDescriptorImageInfo l_Info{ VK_NULL_HANDLE, view, layout };

            VkWriteDescriptorSet l_Write = MakeInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            l_Write.dstSet = m_BindlessSet;
            l_Write.dstBinding = binding;
            l_Write.dstArrayElement = l_Index;
            l_Write.descriptorCount = 1;
            l_Write.descriptorType = type;
            l_Write.pImageInfo = &l_Info;
            vkUpdateDescriptorSets(m_Device.GetHandle(), 1, &l_Write, 0, nullptr);

            return l_Index;
        }

        VulkanContext::VulkanRelease VulkanContext::ToRelease(const VulkanBuffer& buffer)
        {
            VulkanRelease l_Release;
            l_Release.Buffer = buffer.Buffer;
            l_Release.Allocation = buffer.Allocation;
            l_Release.ShaderResourceIndex = buffer.ShaderResourceIndex;
            l_Release.UnorderedAccessIndex = buffer.UnorderedAccessIndex;

            return l_Release;
        }

        VulkanContext::VulkanRelease VulkanContext::ToRelease(const VulkanTexture& texture)
        {
            VulkanRelease l_Release;
            l_Release.Image = texture.Image;
            l_Release.Views = texture.AttachmentViews;
            if (texture.StorageView != VK_NULL_HANDLE)
            {
                l_Release.Views.push_back(texture.StorageView);
            }

            l_Release.Allocation = texture.Allocation;
            l_Release.SampledView = texture.SampledView;
            l_Release.ShaderResourceIndex = texture.ShaderResourceIndex;
            l_Release.UnorderedAccessIndex = texture.UnorderedAccessIndex;

            return l_Release;
        }

        VulkanContext::VulkanRelease VulkanContext::ToRelease(const VulkanSampler& sampler)
        {
            VulkanRelease l_Release;
            l_Release.Sampler = sampler.Sampler;
            l_Release.SamplerIndex = sampler.Index;

            return l_Release;
        }

        // An index is only handed out again once the frames that could read it have finished
        void VulkanContext::Release(const VulkanRelease& release)
        {
            m_ResourceIndices.Free(release.ShaderResourceIndex);
            m_ResourceIndices.Free(release.UnorderedAccessIndex);
            m_SamplerIndices.Free(release.SamplerIndex);

            if (release.Sampler != VK_NULL_HANDLE)
            {
                vkDestroySampler(m_Device.GetHandle(), release.Sampler, nullptr);
            }

            if (release.Pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_Device.GetHandle(), release.Pipeline, nullptr);
            }

            if (release.QueryPool != VK_NULL_HANDLE)
            {
                vkDestroyQueryPool(m_Device.GetHandle(), release.QueryPool, nullptr);
            }

            if (release.SampledView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_Device.GetHandle(), release.SampledView, nullptr);
            }

            for (VkImageView it_View : release.Views)
            {
                if (it_View != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(m_Device.GetHandle(), it_View, nullptr);
                }
            }

            if (release.Buffer != VK_NULL_HANDLE)
            {
                vmaDestroyBuffer(m_Allocator, release.Buffer, release.Allocation);
            }

            if (release.Image != VK_NULL_HANDLE && release.Allocation != nullptr)
            {
                vmaDestroyImage(m_Allocator, release.Image, release.Allocation);
            }
        }

        BufferHandle VulkanContext::CreateBuffer(const BufferDescription& description)
        {
            TR_CORE_ASSERT(description.Size != 0, "Buffer '{}' has no size.", description.DebugName);

            VkBufferCreateInfo l_Create = MakeInfo<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
            l_Create.size = description.Size;
            l_Create.usage = ToVkBufferUsage(description.Usage, description.Memory);
            l_Create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            TR_CORE_ASSERT(l_Create.usage != 0, "Buffer '{}' has no usage.", description.DebugName);

            // Upload and Readback memory is coherent, so writes and reads through the mapping need no flush or invalidate
            VmaAllocationCreateInfo l_Allocation{};
            switch (description.Memory)
            {
                case MemoryType::GPU:
                {
                    l_Allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

                    break;
                }
                case MemoryType::Upload:
                {
                    l_Allocation.usage = VMA_MEMORY_USAGE_AUTO;
                    l_Allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                    l_Allocation.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

                    break;
                }
                case MemoryType::Readback:
                {
                    l_Allocation.usage = VMA_MEMORY_USAGE_AUTO;
                    l_Allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                    l_Allocation.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                    l_Allocation.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

                    break;
                }
            }

            VulkanBuffer l_Buffer;
            VmaAllocationInfo l_Info{};
            const VkResult l_Result = vmaCreateBuffer(m_Allocator, &l_Create, &l_Allocation, &l_Buffer.Buffer, &l_Buffer.Allocation, &l_Info);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: buffer '{}' of {} could not be created ({})", description.DebugName, Memory::FormatBytes(description.Size), FormatResult(l_Result));

                return {};
            }

            l_Buffer.Mapped = static_cast<std::byte*>(l_Info.pMappedData);
            l_Buffer.Size = description.Size;
            SetDebugName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(l_Buffer.Buffer), description.DebugName);

            // Both views of a buffer are the same storage buffer descriptor, and Slang marks the read-only one NonWritable
            if (HasFlag(description.Usage, BufferUsage::ShaderResource))
            {
                l_Buffer.ShaderResourceIndex = AddBufferDescriptor(l_Buffer.Buffer);
            }

            if (HasFlag(description.Usage, BufferUsage::UnorderedAccess))
            {
                l_Buffer.UnorderedAccessIndex = AddBufferDescriptor(l_Buffer.Buffer);
            }

            return m_Buffers.Add(l_Buffer);
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
                m_Releases.Push(ToRelease(*l_Buffer));
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

        // The driver's optimal-tiling features for the format must cover every usage asked for
        bool VulkanContext::IsFormatSupported(Format format, TextureUsage usage, std::uint32_t sampleCount) const
        {
            const VkFormat l_Format = ToVkFormat(format);
            if (l_Format == VK_FORMAT_UNDEFINED || (IsCompressedFormat(format) && !m_Device.HasTextureCompressionBC()) || (HasStencil(format) && usage != TextureUsage::DepthStencil))
            {
                return false;
            }

            VkFormatProperties l_Properties{};
            vkGetPhysicalDeviceFormatProperties(m_Device.GetPhysicalDevice(), l_Format, &l_Properties);

            constexpr std::array<std::pair<TextureUsage, VkFormatFeatureFlags>, 6> c_Needs{ {
                { TextureUsage::ShaderResource, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
                { TextureUsage::UnorderedAccess, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT },
                { TextureUsage::RenderTarget, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT },
                { TextureUsage::DepthStencil, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT },
                { TextureUsage::CopySource, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT },
                { TextureUsage::CopyDestination, VK_FORMAT_FEATURE_TRANSFER_DST_BIT }
            } };

            if (!std::ranges::all_of(c_Needs, [usage, &l_Properties](const auto& need) { return !HasFlag(usage, need.first) || (l_Properties.optimalTilingFeatures & need.second) != 0; }))
            {
                return false;
            }

            if (sampleCount == 1)
            {
                return true;
            }

            const VkImageUsageFlags l_Usage = ToVkImageUsage(usage);
            VkImageFormatProperties l_ImageProperties{};
            if (l_Usage == 0 || vkGetPhysicalDeviceImageFormatProperties(m_Device.GetPhysicalDevice(), l_Format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, l_Usage, 0, &l_ImageProperties) != VK_SUCCESS)
            {
                return false;
            }

            return (l_ImageProperties.sampleCounts & sampleCount) != 0;
        }

        TextureHandle VulkanContext::CreateTexture(const TextureDescription& description)
        {
            TR_CORE_ASSERT(description.Width != 0 && description.Height != 0 && description.MipLevels != 0, "Texture '{}' has a zero size or no mips.", description.DebugName);
            TR_CORE_ASSERT(description.TextureFormat != Format::Unknown, "Texture '{}' has no format.", description.DebugName);

            if (!CanCreateTexture(description))
            {
                return {};
            }

            VkImageCreateInfo l_Create = MakeInfo<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
            l_Create.imageType = VK_IMAGE_TYPE_2D;
            l_Create.format = ToVkFormat(description.TextureFormat);
            l_Create.flags = description.Dimension == TextureDimension::TextureCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
            l_Create.extent = { description.Width, description.Height, 1 };
            l_Create.mipLevels = description.MipLevels;
            l_Create.arrayLayers = description.ArrayLayers;
            l_Create.samples = static_cast<VkSampleCountFlagBits>(description.SampleCount);
            l_Create.tiling = VK_IMAGE_TILING_OPTIMAL;
            l_Create.usage = ToVkImageUsage(description.Usage);
            l_Create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            l_Create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            TR_CORE_ASSERT(l_Create.usage != 0, "Texture '{}' has no usage.", description.DebugName);

            VmaAllocationCreateInfo l_Allocation{};
            l_Allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

            VulkanTexture l_Texture;
            const VkResult l_Result = vmaCreateImage(m_Allocator, &l_Create, &l_Allocation, &l_Texture.Image, &l_Texture.Allocation, nullptr);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: texture '{}' ({}x{} {}) could not be created ({})", description.DebugName, description.Width, description.Height, ToString(description.TextureFormat), FormatResult(l_Result));

                return {};
            }

            l_Texture.TextureFormat = description.TextureFormat;
            l_Texture.Dimension = description.Dimension;
            l_Texture.Width = description.Width;
            l_Texture.Height = description.Height;
            l_Texture.MipLevels = description.MipLevels;
            l_Texture.ArrayLayers = description.ArrayLayers;
            l_Texture.SampleCount = description.SampleCount;
            SetDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(l_Texture.Image), description.DebugName);

            if (HasFlag(description.Usage, TextureUsage::RenderTarget) || HasFlag(description.Usage, TextureUsage::DepthStencil))
            {
                l_Texture.AttachmentViews.reserve(std::size_t{ description.MipLevels } * description.ArrayLayers);
                for (std::uint32_t it_Layer = 0; it_Layer < description.ArrayLayers; ++it_Layer)
                {
                    for (std::uint32_t it_Mip = 0; it_Mip < description.MipLevels; ++it_Mip)
                    {
                        l_Texture.AttachmentViews.push_back(CreateView(l_Texture.Image, description.TextureFormat, VK_IMAGE_VIEW_TYPE_2D, it_Mip, 1, it_Layer, 1));
                    }
                }
            }

            if (HasFlag(description.Usage, TextureUsage::ShaderResource))
            {
                l_Texture.SampledView = CreateView(l_Texture.Image, description.TextureFormat, ToVkSampledViewType(description.Dimension), 0, description.MipLevels, 0, description.ArrayLayers);
                l_Texture.ShaderResourceIndex = AddImageDescriptor(c_SampledImageBinding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, l_Texture.SampledView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }

            // Storage images are written one mip at a time, and a cube's faces as the layers of an array, since a storage view cannot be a cube
            if (HasFlag(description.Usage, TextureUsage::UnorderedAccess))
            {
                const VkImageViewType l_Type = description.Dimension == TextureDimension::Texture2D ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                l_Texture.StorageView = CreateView(l_Texture.Image, description.TextureFormat, l_Type, 0, 1, 0, description.ArrayLayers);
                l_Texture.UnorderedAccessIndex = AddImageDescriptor(c_StorageImageBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, l_Texture.StorageView, VK_IMAGE_LAYOUT_GENERAL);
            }

            return m_Textures.Add(std::move(l_Texture));
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
                m_Releases.Push(ToRelease(*l_Texture));
            }
        }

        TextureHandle VulkanContext::AddSwapChainImage(VkImage image, Format format, std::uint32_t width, std::uint32_t height)
        {
            VulkanTexture l_Texture;
            l_Texture.Image = image;
            l_Texture.AttachmentViews.push_back(CreateView(image, format, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1));
            l_Texture.TextureFormat = format;
            l_Texture.Width = width;
            l_Texture.Height = height;
            l_Texture.MipLevels = 1;
            l_Texture.ArrayLayers = 1;

            return m_Textures.Add(std::move(l_Texture));
        }

        // The swap chain waits for the device to be idle first, so the view goes at once
        void VulkanContext::RemoveSwapChainImage(TextureHandle texture)
        {
            const std::optional<VulkanTexture> l_Texture = m_Textures.Remove(texture);
            if (l_Texture)
            {
                vkDestroyImageView(m_Device.GetHandle(), l_Texture->AttachmentViews.front(), nullptr);
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

        // Shaders read samplers from their own array at c_SamplerBinding, indexed separately from resources
        SamplerHandle VulkanContext::CreateSampler(const SamplerDescription& description)
        {
            VkSamplerCreateInfo l_Create = MakeInfo<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
            l_Create.magFilter = ToVkFilter(description.MagFilter);
            l_Create.minFilter = ToVkFilter(description.MinFilter);
            l_Create.mipmapMode = description.MipFilter == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
            l_Create.addressModeU = ToVkAddressMode(description.AddressU);
            l_Create.addressModeV = ToVkAddressMode(description.AddressV);
            l_Create.addressModeW = ToVkAddressMode(description.AddressW);
            l_Create.mipLodBias = description.MipLodBias;
            l_Create.minLod = description.MinLod;
            l_Create.maxLod = description.MaxLod >= c_LodUnclamped ? VK_LOD_CLAMP_NONE : description.MaxLod;
            l_Create.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

            VulkanSampler l_Sampler;
            const VkResult l_Result = vkCreateSampler(m_Device.GetHandle(), &l_Create, nullptr, &l_Sampler.Sampler);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: sampler '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            l_Sampler.Index = m_SamplerIndices.Allocate();
            if (l_Sampler.Index == c_NoBindlessIndex)
            {
                TR_CORE_ERROR("Vulkan: sampler '{}' could not be created, since all {} bindless sampler indices are in use", description.DebugName, c_BindlessSamplerCapacity);
                vkDestroySampler(m_Device.GetHandle(), l_Sampler.Sampler, nullptr);

                return {};
            }

            SetDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(l_Sampler.Sampler), description.DebugName);

            const VkDescriptorImageInfo l_Info{ l_Sampler.Sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };

            VkWriteDescriptorSet l_Write = MakeInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            l_Write.dstSet = m_BindlessSet;
            l_Write.dstBinding = c_SamplerBinding;
            l_Write.dstArrayElement = l_Sampler.Index;
            l_Write.descriptorCount = 1;
            l_Write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            l_Write.pImageInfo = &l_Info;
            vkUpdateDescriptorSets(m_Device.GetHandle(), 1, &l_Write, 0, nullptr);

            return m_Samplers.Add(l_Sampler);
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
                m_Releases.Push(ToRelease(*l_Sampler));
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
            l_Create.layout = m_PipelineLayout;

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
            l_Create.layout = m_PipelineLayout;

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
            m_Releases.BeginFrame([this](const VulkanRelease& release) { Release(release); });
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
                m_Releases.ReleaseIdle([this](const VulkanRelease& release) { Release(release); });
            }
        }
    }
}