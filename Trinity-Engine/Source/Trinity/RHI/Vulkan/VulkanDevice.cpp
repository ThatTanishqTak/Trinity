#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanSwapChain.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
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
            constexpr const char* c_ValidationLayer = "VK_LAYER_KHRONOS_validation";

            template<typename T>
            struct RequiredFeature
            {
                std::string_view Name;
                VkBool32 T::* Member;
            };

            // What the backend is built on. Descriptor indexing is for the bindless tables, and shader draw parameters is what Slang's SV_VertexID needs
            constexpr std::array<RequiredFeature<VkPhysicalDeviceVulkan11Features>, 1> c_Required11
            { {
                { "shaderDrawParameters", &VkPhysicalDeviceVulkan11Features::shaderDrawParameters }
            } };

            constexpr std::array<RequiredFeature<VkPhysicalDeviceVulkan12Features>, 10> c_Required12
            { {
                { "timelineSemaphore", &VkPhysicalDeviceVulkan12Features::timelineSemaphore },
                { "runtimeDescriptorArray", &VkPhysicalDeviceVulkan12Features::runtimeDescriptorArray },
                { "descriptorBindingPartiallyBound", &VkPhysicalDeviceVulkan12Features::descriptorBindingPartiallyBound },
                { "descriptorBindingUpdateUnusedWhilePending", &VkPhysicalDeviceVulkan12Features::descriptorBindingUpdateUnusedWhilePending },
                { "descriptorBindingSampledImageUpdateAfterBind", &VkPhysicalDeviceVulkan12Features::descriptorBindingSampledImageUpdateAfterBind },
                { "descriptorBindingStorageImageUpdateAfterBind", &VkPhysicalDeviceVulkan12Features::descriptorBindingStorageImageUpdateAfterBind },
                { "descriptorBindingStorageBufferUpdateAfterBind", &VkPhysicalDeviceVulkan12Features::descriptorBindingStorageBufferUpdateAfterBind },
                { "shaderSampledImageArrayNonUniformIndexing", &VkPhysicalDeviceVulkan12Features::shaderSampledImageArrayNonUniformIndexing },
                { "shaderStorageImageArrayNonUniformIndexing", &VkPhysicalDeviceVulkan12Features::shaderStorageImageArrayNonUniformIndexing },
                { "shaderStorageBufferArrayNonUniformIndexing", &VkPhysicalDeviceVulkan12Features::shaderStorageBufferArrayNonUniformIndexing }
            } };

            constexpr std::array<RequiredFeature<VkPhysicalDeviceVulkan13Features>, 2> c_Required13
            { {
                { "dynamicRendering", &VkPhysicalDeviceVulkan13Features::dynamicRendering },
                { "synchronization2", &VkPhysicalDeviceVulkan13Features::synchronization2 }
            } };

            // The Vulkan 1.1 to 1.3 feature structures, chained to each other. It points into itself, so it is never copied
            struct DeviceFeatures
            {
                VkPhysicalDeviceFeatures2 Features = MakeInfo<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
                VkPhysicalDeviceVulkan11Features Vulkan11 = MakeInfo<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
                VkPhysicalDeviceVulkan12Features Vulkan12 = MakeInfo<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
                VkPhysicalDeviceVulkan13Features Vulkan13 = MakeInfo<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);

                DeviceFeatures()
                {
                    Features.pNext = &Vulkan11;
                    Vulkan11.pNext = &Vulkan12;
                    Vulkan12.pNext = &Vulkan13;
                }

                DeviceFeatures(const DeviceFeatures&) = delete;
                DeviceFeatures& operator=(const DeviceFeatures&) = delete;
            };

            template<typename T, std::size_t N>
            void AddMissing(const T& features, const std::array<RequiredFeature<T>, N>& required, std::string& missing)
            {
                for (const RequiredFeature<T>& it_Feature : required)
                {
                    if (features.*(it_Feature.Member) != VK_TRUE)
                    {
                        missing += missing.empty() ? "" : ", ";
                        missing += it_Feature.Name;
                    }
                }
            }

            template<typename T, std::size_t N>
            void Enable(T& features, const std::array<RequiredFeature<T>, N>& required)
            {
                for (const RequiredFeature<T>& it_Feature : required)
                {
                    features.*(it_Feature.Member) = VK_TRUE;
                }
            }

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

            std::string FormatVersion(std::uint32_t version)
            {
                return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version));
            }

            std::string_view ToString(VkPhysicalDeviceType type)
            {
                switch (type)
                {
                    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                    {
                        return "discrete GPU";
                    }
                    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                    {
                        return "integrated GPU";
                    }
                    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                    {
                        return "virtual GPU";
                    }
                    case VK_PHYSICAL_DEVICE_TYPE_CPU:
                    {
                        return "CPU";
                    }
                    default:
                    {
                        return "other device";
                    }
                }
            }

            // Higher is preferred: a CPU device such as lavapipe is only picked when nothing else qualifies
            int GetTypeRank(VkPhysicalDeviceType type)
            {
                switch (type)
                {
                    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                    {
                        return 4;
                    }
                    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                    {
                        return 3;
                    }
                    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                    {
                        return 2;
                    }
                    case VK_PHYSICAL_DEVICE_TYPE_CPU:
                    {
                        return 1;
                    }
                    default:
                    {
                        return 0;
                    }
                }
            }

            bool HasExtension(const std::vector<VkExtensionProperties>& extensions, std::string_view name)
            {
                return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) { return name == extension.extensionName; });
            }

            std::vector<VkExtensionProperties> GetInstanceExtensions(const char* layer)
            {
                std::uint32_t l_Count = 0;
                vkEnumerateInstanceExtensionProperties(layer, &l_Count, nullptr);
                std::vector<VkExtensionProperties> l_Extensions(l_Count);
                vkEnumerateInstanceExtensionProperties(layer, &l_Count, l_Extensions.data());

                return l_Extensions;
            }

            std::vector<VkExtensionProperties> GetDeviceExtensions(VkPhysicalDevice device)
            {
                std::uint32_t l_Count = 0;
                vkEnumerateDeviceExtensionProperties(device, nullptr, &l_Count, nullptr);
                std::vector<VkExtensionProperties> l_Extensions(l_Count);
                vkEnumerateDeviceExtensionProperties(device, nullptr, &l_Count, l_Extensions.data());

                return l_Extensions;
            }

            bool HasValidationLayer()
            {
                std::uint32_t l_Count = 0;
                vkEnumerateInstanceLayerProperties(&l_Count, nullptr);
                std::vector<VkLayerProperties> l_Layers(l_Count);
                vkEnumerateInstanceLayerProperties(&l_Count, l_Layers.data());

                return std::ranges::any_of(l_Layers, [](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, c_ValidationLayer) == 0; });
            }

            std::uint64_t GetVideoMemory(VkPhysicalDevice device)
            {
                VkPhysicalDeviceMemoryProperties l_Memory{};
                vkGetPhysicalDeviceMemoryProperties(device, &l_Memory);

                std::uint64_t l_Largest = 0;
                for (std::uint32_t it_Heap = 0; it_Heap < l_Memory.memoryHeapCount; ++it_Heap)
                {
                    if ((l_Memory.memoryHeaps[it_Heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
                    {
                        l_Largest = std::max<std::uint64_t>(l_Largest, l_Memory.memoryHeaps[it_Heap].size);
                    }
                }

                return l_Largest;
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

            struct VulkanState
            {
                VkPipelineStageFlags2 Stage = VK_PIPELINE_STAGE_2_NONE;
                VkAccessFlags2 Access = VK_ACCESS_2_NONE;
                VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
            };

            constexpr VkPipelineStageFlags2 c_ShaderStages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            constexpr VkPipelineStageFlags2 c_DepthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

            // Undefined waits on all earlier writes, since the layout change that discards the contents is itself a write, and covering all commands also chains it onto a swap chain's acquire semaphore
            VulkanState ToVulkanState(ResourceState state)
            {
                switch (state)
                {
                    case ResourceState::Undefined:
                    {
                        return { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
                    }
                    case ResourceState::Present:
                    {
                        return { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
                    }
                    case ResourceState::RenderTarget:
                    {
                        return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
                    }
                    case ResourceState::DepthWrite:
                    {
                        return { c_DepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
                    }
                    case ResourceState::DepthRead:
                    {
                        return { c_DepthStages | c_ShaderStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
                    }
                    case ResourceState::ShaderResource:
                    {
                        return { c_ShaderStages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                    }
                    case ResourceState::UnorderedAccess:
                    {
                        return { c_ShaderStages, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL };
                    }
                    case ResourceState::CopySource:
                    {
                        return { VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
                    }
                    case ResourceState::CopyDestination:
                    {
                        return { VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };
                    }
                    case ResourceState::IndexBuffer:
                    {
                        return { VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
                    }
                    case ResourceState::IndirectArgument:
                    {
                        return { VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
                    }
                }

                return {};
            }

            VkImageAspectFlags GetAspect(Format format)
            {
                return IsDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            }

            VkAttachmentLoadOp ToVkLoadOp(LoadOp load)
            {
                switch (load)
                {
                    case LoadOp::Load:
                    {
                        return VK_ATTACHMENT_LOAD_OP_LOAD;
                    }
                    case LoadOp::Clear:
                    {
                        return VK_ATTACHMENT_LOAD_OP_CLEAR;
                    }
                    default:
                    {
                        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
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

            VkAttachmentStoreOp ToVkStoreOp(StoreOp store)
            {
                return store == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            }

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

        VulkanCommandList::VulkanCommandList(VulkanDevice& device) : m_Device(device)
        {

        }

        void VulkanCommandList::Begin(VkCommandBuffer commandBuffer)
        {
            VkCommandBufferBeginInfo l_Begin = MakeInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
            l_Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(commandBuffer, &l_Begin);

            m_CommandBuffer = commandBuffer;
            m_Rendering = false;
            m_HasPipeline = false;
            m_HasIndexBuffer = false;

            const VkDescriptorSet l_Set = m_Device.GetBindlessSet();
            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Device.GetPipelineLayout(), 0, 1, &l_Set, 0, nullptr);
        }

        void VulkanCommandList::End()
        {
            TR_CORE_ASSERT(!m_Rendering, "The frame ended inside BeginRendering.");

            vkEndCommandBuffer(m_CommandBuffer);
            m_CommandBuffer = VK_NULL_HANDLE;
        }

        void VulkanCommandList::TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "Barriers are recorded within a frame and outside rendering.");
            TR_CORE_ASSERT(after != ResourceState::Undefined, "A texture cannot move into the undefined state.");

            const VulkanTexture* l_Texture = m_Device.GetTexture(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "TextureBarrier on a destroyed or invalid texture.");
            if (l_Texture == nullptr)
            {
                return;
            }

            const VulkanState l_Before = ToVulkanState(before);
            const VulkanState l_After = ToVulkanState(after);

            VkImageMemoryBarrier2 l_Barrier = MakeInfo<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
            l_Barrier.srcStageMask = l_Before.Stage;
            l_Barrier.srcAccessMask = l_Before.Access;
            l_Barrier.dstStageMask = l_After.Stage;
            l_Barrier.dstAccessMask = l_After.Access;
            l_Barrier.oldLayout = l_Before.Layout;
            l_Barrier.newLayout = l_After.Layout;
            l_Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_Barrier.image = l_Texture->Image;
            l_Barrier.subresourceRange = { GetAspect(l_Texture->TextureFormat), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };

            VkDependencyInfo l_Dependency = MakeInfo<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
            l_Dependency.imageMemoryBarrierCount = 1;
            l_Dependency.pImageMemoryBarriers = &l_Barrier;
            vkCmdPipelineBarrier2(m_CommandBuffer, &l_Dependency);
        }

        void VulkanCommandList::BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "Barriers are recorded within a frame and outside rendering.");

            const VulkanBuffer* l_Buffer = m_Device.GetBuffer(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "BufferBarrier on a destroyed or invalid buffer.");
            if (l_Buffer == nullptr)
            {
                return;
            }

            const VulkanState l_Before = ToVulkanState(before);
            const VulkanState l_After = ToVulkanState(after);

            VkBufferMemoryBarrier2 l_Barrier = MakeInfo<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
            l_Barrier.srcStageMask = l_Before.Stage;
            l_Barrier.srcAccessMask = l_Before.Access;
            l_Barrier.dstStageMask = l_After.Stage;
            l_Barrier.dstAccessMask = l_After.Access;
            l_Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_Barrier.buffer = l_Buffer->Buffer;
            l_Barrier.offset = 0;
            l_Barrier.size = VK_WHOLE_SIZE;

            VkDependencyInfo l_Dependency = MakeInfo<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
            l_Dependency.bufferMemoryBarrierCount = 1;
            l_Dependency.pBufferMemoryBarriers = &l_Barrier;
            vkCmdPipelineBarrier2(m_CommandBuffer, &l_Dependency);
        }

        void VulkanCommandList::BeginRendering(const RenderingDescription& description)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "BeginRendering is called once within a frame, before EndRendering.");
            TR_CORE_ASSERT(description.ColorAttachments.size() <= c_MaxColorAttachments, "Too many color attachments.");

            std::array<VkRenderingAttachmentInfo, c_MaxColorAttachments> l_Colors{};
            std::uint32_t l_ColorCount = 0;
            Rect l_Area = description.RenderArea;
            for (const ColorAttachment& it_Attachment : description.ColorAttachments)
            {
                const VulkanTexture* l_Texture = m_Device.GetTexture(it_Attachment.Texture);
                TR_CORE_ASSERT(l_Texture != nullptr && l_Texture->AttachmentView != VK_NULL_HANDLE, "BeginRendering with a destroyed texture, or one without RenderTarget usage.");
                if (l_Texture == nullptr || l_Texture->AttachmentView == VK_NULL_HANDLE || l_ColorCount == c_MaxColorAttachments)
                {
                    continue;
                }

                if (l_Area.Width == 0 || l_Area.Height == 0)
                {
                    l_Area = { 0, 0, l_Texture->Width, l_Texture->Height };
                }

                VkRenderingAttachmentInfo& l_Info = l_Colors[l_ColorCount++];
                l_Info = MakeInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
                l_Info.imageView = l_Texture->AttachmentView;
                l_Info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                l_Info.loadOp = ToVkLoadOp(it_Attachment.Load);
                l_Info.storeOp = ToVkStoreOp(it_Attachment.Store);
                for (std::size_t it_Channel = 0; it_Channel < it_Attachment.ClearColor.size(); ++it_Channel)
                {
                    l_Info.clearValue.color.float32[it_Channel] = it_Attachment.ClearColor[it_Channel];
                }
            }

            VkRenderingAttachmentInfo l_Depth = MakeInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
            const VulkanTexture* l_DepthTexture = description.Depth.Texture ? m_Device.GetTexture(description.Depth.Texture) : nullptr;
            TR_CORE_ASSERT(!description.Depth.Texture || (l_DepthTexture != nullptr && l_DepthTexture->AttachmentView != VK_NULL_HANDLE), "BeginRendering with a destroyed depth texture, or one without DepthStencil usage.");
            if (l_DepthTexture != nullptr)
            {
                if (l_Area.Width == 0 || l_Area.Height == 0)
                {
                    l_Area = { 0, 0, l_DepthTexture->Width, l_DepthTexture->Height };
                }

                l_Depth.imageView = l_DepthTexture->AttachmentView;
                l_Depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                l_Depth.loadOp = ToVkLoadOp(description.Depth.Load);
                l_Depth.storeOp = ToVkStoreOp(description.Depth.Store);
                l_Depth.clearValue.depthStencil = { description.Depth.ClearDepth, 0 };
            }

            VkRenderingInfo l_Rendering = MakeInfo<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
            l_Rendering.renderArea = { { l_Area.X, l_Area.Y }, { l_Area.Width, l_Area.Height } };
            l_Rendering.layerCount = 1;
            l_Rendering.colorAttachmentCount = l_ColorCount;
            l_Rendering.pColorAttachments = l_Colors.data();
            l_Rendering.pDepthAttachment = l_DepthTexture != nullptr && l_DepthTexture->AttachmentView != VK_NULL_HANDLE ? &l_Depth : nullptr;
            vkCmdBeginRendering(m_CommandBuffer, &l_Rendering);

            m_Rendering = true;
        }

        void VulkanCommandList::EndRendering()
        {
            TR_CORE_ASSERT(m_Rendering, "EndRendering without BeginRendering.");

            vkCmdEndRendering(m_CommandBuffer);
            m_Rendering = false;
            m_HasPipeline = false;
            m_HasIndexBuffer = false;
        }

        void VulkanCommandList::SetPipeline(PipelineHandle pipeline)
        {
            TR_CORE_ASSERT(m_Rendering, "SetPipeline is recorded inside rendering.");

            const VulkanPipeline* l_Pipeline = m_Device.GetPipeline(pipeline);
            TR_CORE_ASSERT(l_Pipeline != nullptr, "SetPipeline with a destroyed or invalid pipeline.");
            if (l_Pipeline == nullptr)
            {
                return;
            }

            vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, l_Pipeline->Pipeline);
            m_HasPipeline = true;
        }

        // A negative height turns Vulkan's downward Y around, so clip space has Y up and the same winding as on D3D12
        void VulkanCommandList::SetViewport(const Viewport& viewport)
        {
            TR_CORE_ASSERT(m_Rendering, "SetViewport is recorded inside rendering.");

            const VkViewport l_Viewport{ viewport.X, viewport.Y + viewport.Height, viewport.Width, -viewport.Height, viewport.MinDepth, viewport.MaxDepth };
            vkCmdSetViewport(m_CommandBuffer, 0, 1, &l_Viewport);
        }

        void VulkanCommandList::SetScissor(const Rect& scissor)
        {
            TR_CORE_ASSERT(m_Rendering, "SetScissor is recorded inside rendering.");

            const VkRect2D l_Scissor{ { scissor.X, scissor.Y }, { scissor.Width, scissor.Height } };
            vkCmdSetScissor(m_CommandBuffer, 0, 1, &l_Scissor);
        }

        void VulkanCommandList::PushConstants(std::span<const std::byte> data)
        {
            TR_CORE_ASSERT(m_HasPipeline, "PushConstants needs a pipeline.");
            TR_CORE_ASSERT(data.size() <= c_MaxPushConstantSize && data.size() % 4 == 0, "Push constants are whole 32-bit values, at most c_MaxPushConstantSize bytes.");

            vkCmdPushConstants(m_CommandBuffer, m_Device.GetPipelineLayout(), VK_SHADER_STAGE_ALL, 0, static_cast<std::uint32_t>(data.size()), data.data());
        }

        void VulkanCommandList::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance)
        {
            TR_CORE_ASSERT(m_HasPipeline, "Draw needs a pipeline.");

            vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
        }

        void VulkanCommandList::SetIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format)
        {
            TR_CORE_ASSERT(m_Rendering, "SetIndexBuffer is recorded inside rendering.");

            const VulkanBuffer* l_Buffer = m_Device.GetBuffer(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "SetIndexBuffer with a destroyed or invalid buffer.");
            TR_CORE_ASSERT(l_Buffer == nullptr || (offset < l_Buffer->Size && offset % GetIndexSize(format) == 0), "SetIndexBuffer at offset {}, which is past the end of the buffer or not a multiple of the index size.", offset);
            if (l_Buffer == nullptr || offset >= l_Buffer->Size)
            {
                return;
            }

            vkCmdBindIndexBuffer(m_CommandBuffer, l_Buffer->Buffer, offset, format == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            m_HasIndexBuffer = true;
        }

        void VulkanCommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::uint32_t firstInstance)
        {
            TR_CORE_ASSERT(m_HasPipeline && m_HasIndexBuffer, "DrawIndexed needs a pipeline and an index buffer.");

            vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, firstIndex, 0, firstInstance);
        }

        void VulkanCommandList::CopyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination, std::uint64_t destinationOffset, std::uint64_t size)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "Copies are recorded within a frame and outside rendering.");

            const VulkanBuffer* l_Source = m_Device.GetBuffer(source);
            const VulkanBuffer* l_Destination = m_Device.GetBuffer(destination);
            TR_CORE_ASSERT(l_Source != nullptr && l_Destination != nullptr, "CopyBuffer with a destroyed or invalid buffer.");
            if (l_Source == nullptr || l_Destination == nullptr)
            {
                return;
            }

            TR_CORE_ASSERT(sourceOffset + size <= l_Source->Size && destinationOffset + size <= l_Destination->Size, "CopyBuffer reaches past the end of a buffer.");

            const VkBufferCopy l_Region{ sourceOffset, destinationOffset, size };
            vkCmdCopyBuffer(m_CommandBuffer, l_Source->Buffer, l_Destination->Buffer, 1, &l_Region);
        }

        // The whole mip, laid out in the buffer as CopyBufferToTexture reads it: rows of texels or blocks GetTextureCopyRowPitch apart
        void VulkanCommandList::CopyTextureToBuffer(TextureHandle source, std::uint32_t mipLevel, BufferHandle destination, std::uint64_t destinationOffset)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "Copies are recorded within a frame and outside rendering.");

            const VulkanTexture* l_Texture = m_Device.GetTexture(source);
            const VulkanBuffer* l_Buffer = m_Device.GetBuffer(destination);
            TR_CORE_ASSERT(l_Texture != nullptr && l_Buffer != nullptr, "CopyTextureToBuffer with a destroyed or invalid resource.");
            if (l_Texture == nullptr || l_Buffer == nullptr)
            {
                return;
            }

            const std::uint32_t l_Width = GetMipSize(l_Texture->Width, mipLevel);
            const std::uint32_t l_Height = GetMipSize(l_Texture->Height, mipLevel);
            const std::uint32_t l_BlockBytes = GetFormatSize(l_Texture->TextureFormat);
            const std::uint64_t l_RowPitch = GetTextureCopyRowPitch(l_Texture->TextureFormat, l_Width);
            [[maybe_unused]] const std::uint64_t l_Size = GetTextureCopySize(l_Texture->TextureFormat, l_Width, l_Height);
            TR_CORE_ASSERT(l_BlockBytes != 0 && l_RowPitch % l_BlockBytes == 0, "{} rows cannot be copied {} bytes apart.", ToString(l_Texture->TextureFormat), l_RowPitch);
            TR_CORE_ASSERT(mipLevel < l_Texture->MipLevels, "CopyTextureToBuffer from mip {} of a texture with {} mip(s).", mipLevel, l_Texture->MipLevels);
            TR_CORE_ASSERT(destinationOffset % c_TextureCopyOffsetAlignment == 0, "CopyTextureToBuffer writes to offset {}, which is not a multiple of {}.", destinationOffset, c_TextureCopyOffsetAlignment);
            TR_CORE_ASSERT(destinationOffset + l_Size <= l_Buffer->Size, "CopyTextureToBuffer needs {} bytes from offset {}, and the buffer has {}.", l_Size, destinationOffset, l_Buffer->Size);
            if (l_BlockBytes == 0)
            {
                return;
            }

            // Vulkan counts the buffer's row length in texels, whole blocks of them for a compressed format
            VkBufferImageCopy l_Region{};
            l_Region.bufferOffset = destinationOffset;
            l_Region.bufferRowLength = static_cast<std::uint32_t>(l_RowPitch / l_BlockBytes) * GetFormatBlockDimension(l_Texture->TextureFormat);
            l_Region.imageSubresource = { GetAspect(l_Texture->TextureFormat), mipLevel, 0, 1 };
            l_Region.imageExtent = { l_Width, l_Height, 1 };
            vkCmdCopyImageToBuffer(m_CommandBuffer, l_Texture->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, l_Buffer->Buffer, 1, &l_Region);
        }

        void VulkanCommandList::CopyBufferToTexture(BufferHandle source, std::uint64_t sourceOffset, TextureHandle destination, std::uint32_t mipLevel, const Rect& region)
        {
            TR_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE && !m_Rendering, "Copies are recorded within a frame and outside rendering.");

            const VulkanBuffer* l_Buffer = m_Device.GetBuffer(source);
            const VulkanTexture* l_Texture = m_Device.GetTexture(destination);
            TR_CORE_ASSERT(l_Buffer != nullptr && l_Texture != nullptr, "CopyBufferToTexture with a destroyed or invalid resource.");
            if (l_Buffer == nullptr || l_Texture == nullptr)
            {
                return;
            }

            const std::uint32_t l_BlockBytes = GetFormatSize(l_Texture->TextureFormat);
            const std::uint64_t l_RowPitch = GetTextureCopyRowPitch(l_Texture->TextureFormat, region.Width);
            [[maybe_unused]] const std::uint64_t l_Size = GetTextureCopySize(l_Texture->TextureFormat, region.Width, region.Height);
            TR_CORE_ASSERT(l_BlockBytes != 0 && l_RowPitch % l_BlockBytes == 0, "{} rows cannot be copied {} bytes apart.", ToString(l_Texture->TextureFormat), l_RowPitch);
            TR_CORE_ASSERT(mipLevel < l_Texture->MipLevels && IsRegionInsideMip(region, l_Texture->Width, l_Texture->Height, mipLevel), "CopyBufferToTexture with a region outside mip {} of a {}x{} texture with {} mip(s).", mipLevel, l_Texture->Width, l_Texture->Height, l_Texture->MipLevels);
            TR_CORE_ASSERT(IsRegionBlockAligned(region, l_Texture->TextureFormat, l_Texture->Width, l_Texture->Height, mipLevel), "CopyBufferToTexture with a region of {} that does not start on a block or cover whole blocks.", ToString(l_Texture->TextureFormat));
            TR_CORE_ASSERT(sourceOffset % c_TextureCopyOffsetAlignment == 0, "CopyBufferToTexture reads from offset {}, which is not a multiple of {}.", sourceOffset, c_TextureCopyOffsetAlignment);
            TR_CORE_ASSERT(sourceOffset + l_Size <= l_Buffer->Size, "CopyBufferToTexture needs {} bytes from offset {}, and the buffer has {}.", l_Size, sourceOffset, l_Buffer->Size);
            if (l_BlockBytes == 0)
            {
                return;
            }

            VkBufferImageCopy l_Region{};
            l_Region.bufferOffset = sourceOffset;
            l_Region.bufferRowLength = static_cast<std::uint32_t>(l_RowPitch / l_BlockBytes) * GetFormatBlockDimension(l_Texture->TextureFormat);
            l_Region.imageSubresource = { GetAspect(l_Texture->TextureFormat), mipLevel, 0, 1 };
            l_Region.imageOffset = { region.X, region.Y, 0 };
            l_Region.imageExtent = { region.Width, region.Height, 1 };
            vkCmdCopyBufferToImage(m_CommandBuffer, l_Buffer->Buffer, l_Texture->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_Region);
        }

        Scope<VulkanDevice> VulkanDevice::Create(const DeviceSpecification& specification, std::string& error)
        {
            Scope<VulkanDevice> l_Device = CreateScope<VulkanDevice>();
            if (!l_Device->Initialize(specification, error))
            {
                return nullptr;
            }

            return l_Device;
        }

        VulkanDevice::VulkanDevice() : m_CommandList(*this)
        {

        }

        VulkanDevice::~VulkanDevice()
        {
            // The validation layer reports objects still alive in vkDestroyDevice and vkDestroyInstance, so the messenger outlives the device
            if (m_Device != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_Device);
                m_UploadRing.Shutdown();

                if (m_Buffers.GetCount() != 0 || m_Textures.GetCount() != 0 || m_Pipelines.GetCount() != 0 || m_Samplers.GetCount() != 0)
                {
                    TR_CORE_WARN("Vulkan: the device was destroyed with {} buffer(s), {} texture(s), {} pipeline(s) and {} sampler(s) still alive", m_Buffers.GetCount(), m_Textures.GetCount(), m_Pipelines.GetCount(), m_Samplers.GetCount());
                }

                m_Releases.ReleaseAll([this](const VulkanRelease& release) { Release(release); });
                m_Buffers.ForEach([this](const VulkanBuffer& buffer) { Release(ToRelease(buffer)); });
                m_Textures.ForEach([this](const VulkanTexture& texture) { Release(ToRelease(texture)); });
                m_Pipelines.ForEach([this](const VulkanPipeline& pipeline) { vkDestroyPipeline(m_Device, pipeline.Pipeline, nullptr); });
                m_Samplers.ForEach([this](const VulkanSampler& sampler) { Release(ToRelease(sampler)); });

                vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
                vkDestroyDescriptorPool(m_Device, m_BindlessPool, nullptr);
                vkDestroyDescriptorSetLayout(m_Device, m_BindlessLayout, nullptr);

                for (const FrameContext& it_Frame : m_Frames)
                {
                    vkDestroyCommandPool(m_Device, it_Frame.CommandPool, nullptr);
                }

                vkDestroySemaphore(m_Device, m_FrameTimeline, nullptr);

                if (m_Allocator != nullptr)
                {
                    vmaDestroyAllocator(m_Allocator);
                }

                vkDestroyDevice(m_Device, nullptr);
            }

            if (m_Messenger != VK_NULL_HANDLE)
            {
                vkDestroyDebugUtilsMessengerEXT(m_Instance, m_Messenger, nullptr);
            }

            if (m_Instance != VK_NULL_HANDLE)
            {
                vkDestroyInstance(m_Instance, nullptr);
            }

            if (m_Validation && m_Device != VK_NULL_HANDLE)
            {
                TR_CORE_INFO("Vulkan validation: {} warning(s) or error(s) while the device lived", m_MessageCount.load());
            }
        }

        bool VulkanDevice::Initialize(const DeviceSpecification& specification, std::string& error)
        {
            if (volkInitialize() != VK_SUCCESS)
            {
                error = "no Vulkan loader was found";

                return false;
            }

            const std::uint32_t l_LoaderVersion = volkGetInstanceVersion();
            if (l_LoaderVersion < VK_API_VERSION_1_3)
            {
                error = std::format("the Vulkan loader is {}, and the engine needs 1.3", FormatVersion(l_LoaderVersion));

                return false;
            }

            return CreateInstance(specification, l_LoaderVersion, error) && CreateLogicalDevice(error) && CreateAllocator(error) && CreateFrames(error) && CreateBindless(error) && m_UploadRing.Initialize(*this, error);
        }

        bool VulkanDevice::CreateInstance(const DeviceSpecification& specification, std::uint32_t loaderVersion, std::string& error)
        {
            const std::vector<VkExtensionProperties> l_Available = GetInstanceExtensions(nullptr);

            std::vector<const char*> l_Extensions;
            std::vector<const char*> l_Layers;
            std::vector<const char*> l_SurfaceExtensions{ VK_KHR_SURFACE_EXTENSION_NAME };
#if defined(VK_USE_PLATFORM_WIN32_KHR)
            l_SurfaceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
            for (const char* it_Extension : l_SurfaceExtensions)
            {
                if (HasExtension(l_Available, it_Extension))
                {
                    l_Extensions.push_back(it_Extension);
                }
            }

            m_Validation = specification.EnableValidation;
            bool l_GPUValidation = false;
            if (m_Validation)
            {
                const std::vector<VkExtensionProperties> l_LayerExtensions = HasValidationLayer() ? GetInstanceExtensions(c_ValidationLayer) : std::vector<VkExtensionProperties>{};
                if (!HasValidationLayer() || !(HasExtension(l_Available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) || HasExtension(l_LayerExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)))
                {
                    TR_CORE_WARN("Vulkan: the Khronos validation layer is missing, so validation is off. It comes with the Vulkan SDK");
                    m_Validation = false;
                }
                else
                {
                    l_Layers.push_back(c_ValidationLayer);
                    l_Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

                    if (specification.EnableGPUValidation)
                    {
                        if (HasExtension(l_LayerExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME))
                        {
                            l_Extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
                            l_GPUValidation = true;
                        }
                        else
                        {
                            TR_CORE_WARN("Vulkan: the validation layer has no VK_EXT_validation_features, so GPU-assisted validation is off");
                        }
                    }

                    TR_CORE_INFO("Vulkan: validation layer on{}", l_GPUValidation ? ", with GPU-assisted validation" : "");
                }
            }

            VkApplicationInfo l_Application = MakeInfo<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
            l_Application.pApplicationName = "Trinity";
            l_Application.pEngineName = "Trinity";
            l_Application.apiVersion = loaderVersion >= VK_API_VERSION_1_4 ? VK_API_VERSION_1_4 : VK_API_VERSION_1_3;

            // Chained into instance creation as well, so messages from vkCreateInstance and vkDestroyInstance reach the log too
            VkDebugUtilsMessengerCreateInfoEXT l_Messenger = MakeInfo<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
            l_Messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            l_Messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            l_Messenger.pfnUserCallback = &VulkanDevice::OnDebugMessage;
            l_Messenger.pUserData = this;

            const VkValidationFeatureEnableEXT l_GPUAssisted = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT;
            VkValidationFeaturesEXT l_ValidationFeatures = MakeInfo<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
            l_ValidationFeatures.enabledValidationFeatureCount = 1;
            l_ValidationFeatures.pEnabledValidationFeatures = &l_GPUAssisted;
            if (l_GPUValidation)
            {
                l_Messenger.pNext = &l_ValidationFeatures;
            }

            VkInstanceCreateInfo l_Create = MakeInfo<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
            l_Create.pNext = m_Validation ? &l_Messenger : nullptr;
            l_Create.pApplicationInfo = &l_Application;
            l_Create.enabledLayerCount = static_cast<std::uint32_t>(l_Layers.size());
            l_Create.ppEnabledLayerNames = l_Layers.data();
            l_Create.enabledExtensionCount = static_cast<std::uint32_t>(l_Extensions.size());
            l_Create.ppEnabledExtensionNames = l_Extensions.data();

            const VkResult l_Result = vkCreateInstance(&l_Create, nullptr, &m_Instance);
            if (l_Result != VK_SUCCESS)
            {
                error = std::format("vkCreateInstance failed with {}", FormatResult(l_Result));

                return false;
            }

            volkLoadInstanceOnly(m_Instance);

            if (m_Validation)
            {
                l_Messenger.pNext = nullptr;
                if (vkCreateDebugUtilsMessengerEXT(m_Instance, &l_Messenger, nullptr, &m_Messenger) != VK_SUCCESS)
                {
                    TR_CORE_WARN("Vulkan: the debug messenger could not be created, so validation messages are lost");
                }
            }

            return true;
        }

        bool VulkanDevice::CreateLogicalDevice(std::string& error)
        {
            std::uint32_t l_Count = 0;
            vkEnumeratePhysicalDevices(m_Instance, &l_Count, nullptr);
            std::vector<VkPhysicalDevice> l_PhysicalDevices(l_Count);
            vkEnumeratePhysicalDevices(m_Instance, &l_Count, l_PhysicalDevices.data());

            std::string l_Skipped;
            int l_BestRank = -1;
            for (VkPhysicalDevice it_PhysicalDevice : l_PhysicalDevices)
            {
                VkPhysicalDeviceProperties l_Properties{};
                vkGetPhysicalDeviceProperties(it_PhysicalDevice, &l_Properties);
                const std::string_view l_Name = l_Properties.deviceName;

                const auto a_Skip = [&l_Skipped, l_Name](std::string_view reason)
                {
                    l_Skipped += std::format("{}{}: {}", l_Skipped.empty() ? "" : "; ", l_Name, reason);
                };

                if (l_Properties.apiVersion < VK_API_VERSION_1_3)
                {
                    a_Skip(std::format("Vulkan {}, and the engine needs 1.3", FormatVersion(l_Properties.apiVersion)));

                    continue;
                }

                std::uint32_t l_FamilyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(it_PhysicalDevice, &l_FamilyCount, nullptr);
                std::vector<VkQueueFamilyProperties> l_Families(l_FamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(it_PhysicalDevice, &l_FamilyCount, l_Families.data());

                const auto a_Graphics = std::ranges::find_if(l_Families, [](const VkQueueFamilyProperties& family) { return (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && (family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0; });
                if (a_Graphics == l_Families.end())
                {
                    a_Skip("no graphics and compute queue");

                    continue;
                }

                DeviceFeatures l_Features;
                vkGetPhysicalDeviceFeatures2(it_PhysicalDevice, &l_Features.Features);

                std::string l_Missing;
                AddMissing(l_Features.Vulkan11, c_Required11, l_Missing);
                AddMissing(l_Features.Vulkan12, c_Required12, l_Missing);
                AddMissing(l_Features.Vulkan13, c_Required13, l_Missing);
                if (!l_Missing.empty())
                {
                    a_Skip(std::format("no {}", l_Missing));

                    continue;
                }

                const int l_Rank = GetTypeRank(l_Properties.deviceType);
                if (l_Rank > l_BestRank)
                {
                    l_BestRank = l_Rank;
                    m_PhysicalDevice = it_PhysicalDevice;
                    m_QueueFamily = static_cast<std::uint32_t>(std::distance(l_Families.begin(), a_Graphics));
                }
            }

            if (m_PhysicalDevice == VK_NULL_HANDLE)
            {
                error = l_Skipped.empty() ? std::string("no physical device was found") : std::format("no physical device has what the engine needs ({})", l_Skipped);

                return false;
            }

            VkPhysicalDeviceVulkan12Properties l_DriverProperties = MakeInfo<VkPhysicalDeviceVulkan12Properties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);
            VkPhysicalDeviceProperties2 l_Properties = MakeInfo<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
            l_Properties.pNext = &l_DriverProperties;
            vkGetPhysicalDeviceProperties2(m_PhysicalDevice, &l_Properties);

            DeviceFeatures l_Enabled;
            Enable(l_Enabled.Vulkan11, c_Required11);
            Enable(l_Enabled.Vulkan12, c_Required12);
            Enable(l_Enabled.Vulkan13, c_Required13);

            // Optional: without it, IsFormatSupported reports every BC format as unsupported and textures fall back to RGBA8
            DeviceFeatures l_Available;
            vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &l_Available.Features);
            m_TextureCompressionBC = l_Available.Features.features.textureCompressionBC == VK_TRUE;
            l_Enabled.Features.features.textureCompressionBC = l_Available.Features.features.textureCompressionBC;

            std::vector<const char*> l_Extensions;
            if (HasExtension(GetDeviceExtensions(m_PhysicalDevice), VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            {
                l_Extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            }

            const float l_Priority = 1.0f;
            VkDeviceQueueCreateInfo l_Queue = MakeInfo<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
            l_Queue.queueFamilyIndex = m_QueueFamily;
            l_Queue.queueCount = 1;
            l_Queue.pQueuePriorities = &l_Priority;

            VkDeviceCreateInfo l_Create = MakeInfo<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
            l_Create.pNext = &l_Enabled.Features;
            l_Create.queueCreateInfoCount = 1;
            l_Create.pQueueCreateInfos = &l_Queue;
            l_Create.enabledExtensionCount = static_cast<std::uint32_t>(l_Extensions.size());
            l_Create.ppEnabledExtensionNames = l_Extensions.data();

            const VkResult l_Result = vkCreateDevice(m_PhysicalDevice, &l_Create, nullptr, &m_Device);
            if (l_Result != VK_SUCCESS)
            {
                error = std::format("vkCreateDevice on {} failed with {}", l_Properties.properties.deviceName, FormatResult(l_Result));

                return false;
            }

            volkLoadDevice(m_Device);
            vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);

            m_Info.API = GraphicsAPI::Vulkan;
            m_Info.AdapterName = l_Properties.properties.deviceName;
            m_Info.VideoMemoryBytes = GetVideoMemory(m_PhysicalDevice);

            TR_CORE_INFO("Vulkan: {} ({}) with {} of video memory: Vulkan {}, {} {}, dynamic rendering, synchronization2, descriptor indexing, timeline semaphores{}", m_Info.AdapterName, ToString(l_Properties.properties.deviceType), Memory::FormatBytes(m_Info.VideoMemoryBytes), FormatVersion(l_Properties.properties.apiVersion), l_DriverProperties.driverName, l_DriverProperties.driverInfo, m_TextureCompressionBC ? ", BC textures" : "");

            return true;
        }

        bool VulkanDevice::CreateAllocator(std::string& error)
        {
            VmaAllocatorCreateInfo l_Create{};
            l_Create.physicalDevice = m_PhysicalDevice;
            l_Create.device = m_Device;
            l_Create.instance = m_Instance;
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
                error = std::format("VulkanMemoryAllocator could not be created on {} ({})", m_Info.AdapterName, FormatResult(l_Result));

                return false;
            }

            return true;
        }

        bool VulkanDevice::CreateFrames(std::string& error)
        {
            VkSemaphoreTypeCreateInfo l_Timeline = MakeInfo<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
            l_Timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

            VkSemaphoreCreateInfo l_Semaphore = MakeInfo<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
            l_Semaphore.pNext = &l_Timeline;

            VkResult l_Result = vkCreateSemaphore(m_Device, &l_Semaphore, nullptr, &m_FrameTimeline);
            for (FrameContext& it_Frame : m_Frames)
            {
                if (l_Result != VK_SUCCESS)
                {
                    break;
                }

                VkCommandPoolCreateInfo l_Pool = MakeInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
                l_Pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                l_Pool.queueFamilyIndex = m_QueueFamily;
                l_Result = vkCreateCommandPool(m_Device, &l_Pool, nullptr, &it_Frame.CommandPool);
                if (l_Result != VK_SUCCESS)
                {
                    break;
                }

                VkCommandBufferAllocateInfo l_Allocate = MakeInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
                l_Allocate.commandPool = it_Frame.CommandPool;
                l_Allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                l_Allocate.commandBufferCount = 1;
                l_Result = vkAllocateCommandBuffers(m_Device, &l_Allocate, &it_Frame.CommandBuffer);
            }

            if (l_Result != VK_SUCCESS)
            {
                error = std::format("the frame command pools and timeline semaphore could not be created on {} ({})", m_Info.AdapterName, FormatResult(l_Result));

                return false;
            }

            return true;
        }

        // One set holds every descriptor, and every pipeline shares its layout and the push constant range, so the set is bound once per frame
        bool VulkanDevice::CreateBindless(std::string& error)
        {
            VkPhysicalDeviceVulkan12Properties l_Limits = MakeInfo<VkPhysicalDeviceVulkan12Properties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);
            VkPhysicalDeviceProperties2 l_Properties = MakeInfo<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
            l_Properties.pNext = &l_Limits;
            vkGetPhysicalDeviceProperties2(m_PhysicalDevice, &l_Properties);

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
                    error = std::format("{} allows {} bindless {}, and the engine needs {}", m_Info.AdapterName, it_Check.Allowed, it_Check.Name, it_Check.Needed);

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

            VkResult l_Result = vkCreateDescriptorSetLayout(m_Device, &l_Layout, nullptr, &m_BindlessLayout);
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
                l_Result = vkCreateDescriptorPool(m_Device, &l_Pool, nullptr, &m_BindlessPool);
            }

            if (l_Result == VK_SUCCESS)
            {
                VkDescriptorSetAllocateInfo l_Allocate = MakeInfo<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
                l_Allocate.descriptorPool = m_BindlessPool;
                l_Allocate.descriptorSetCount = 1;
                l_Allocate.pSetLayouts = &m_BindlessLayout;
                l_Result = vkAllocateDescriptorSets(m_Device, &l_Allocate, &m_BindlessSet);
            }

            if (l_Result == VK_SUCCESS)
            {
                const VkPushConstantRange l_PushConstants{ VK_SHADER_STAGE_ALL, 0, c_MaxPushConstantSize };

                VkPipelineLayoutCreateInfo l_PipelineLayout = MakeInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
                l_PipelineLayout.setLayoutCount = 1;
                l_PipelineLayout.pSetLayouts = &m_BindlessLayout;
                l_PipelineLayout.pushConstantRangeCount = 1;
                l_PipelineLayout.pPushConstantRanges = &l_PushConstants;
                l_Result = vkCreatePipelineLayout(m_Device, &l_PipelineLayout, nullptr, &m_PipelineLayout);
            }

            if (l_Result != VK_SUCCESS)
            {
                error = std::format("the bindless descriptor set could not be created on {} ({})", m_Info.AdapterName, FormatResult(l_Result));

                return false;
            }

            SetDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<std::uint64_t>(m_BindlessSet), "Bindless");
            m_ResourceIndices.Reset(c_BindlessResourceCapacity);
            m_SamplerIndices.Reset(c_BindlessSamplerCapacity);

            return true;
        }

        VkImageView VulkanDevice::CreateAttachmentView(VkImage image, Format format)
        {
            VkImageViewCreateInfo l_Create = MakeInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
            l_Create.image = image;
            l_Create.viewType = VK_IMAGE_VIEW_TYPE_2D;
            l_Create.format = ToVkFormat(format);
            l_Create.subresourceRange = { GetAspect(format), 0, 1, 0, 1 };

            VkImageView l_View = VK_NULL_HANDLE;
            const VkResult l_Result = vkCreateImageView(m_Device, &l_Create, nullptr, &l_View);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: an attachment view of a {} image could not be created ({})", ToString(format), FormatResult(l_Result));

                return VK_NULL_HANDLE;
            }

            return l_View;
        }

        VkImageView VulkanDevice::CreateSampledView(VkImage image, Format format, std::uint32_t mipLevels)
        {
            VkImageViewCreateInfo l_Create = MakeInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
            l_Create.image = image;
            l_Create.viewType = VK_IMAGE_VIEW_TYPE_2D;
            l_Create.format = ToVkFormat(format);
            l_Create.subresourceRange = { GetAspect(format), 0, mipLevels, 0, 1 };

            VkImageView l_View = VK_NULL_HANDLE;
            const VkResult l_Result = vkCreateImageView(m_Device, &l_Create, nullptr, &l_View);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: a sampled view of a {} image could not be created ({})", ToString(format), FormatResult(l_Result));

                return VK_NULL_HANDLE;
            }

            return l_View;
        }

        std::uint32_t VulkanDevice::AddBufferDescriptor(VkBuffer buffer)
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
            vkUpdateDescriptorSets(m_Device, 1, &l_Write, 0, nullptr);

            return l_Index;
        }

        std::uint32_t VulkanDevice::AddImageDescriptor(std::uint32_t binding, VkDescriptorType type, VkImageView view, VkImageLayout layout)
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
            vkUpdateDescriptorSets(m_Device, 1, &l_Write, 0, nullptr);

            return l_Index;
        }

        VulkanDevice::VulkanRelease VulkanDevice::ToRelease(const VulkanBuffer& buffer)
        {
            VulkanRelease l_Release;
            l_Release.Buffer = buffer.Buffer;
            l_Release.Allocation = buffer.Allocation;
            l_Release.ShaderResourceIndex = buffer.ShaderResourceIndex;
            l_Release.UnorderedAccessIndex = buffer.UnorderedAccessIndex;

            return l_Release;
        }

        VulkanDevice::VulkanRelease VulkanDevice::ToRelease(const VulkanTexture& texture)
        {
            VulkanRelease l_Release;
            l_Release.Image = texture.Image;
            l_Release.View = texture.AttachmentView;
            l_Release.Allocation = texture.Allocation;
            l_Release.SampledView = texture.SampledView;
            l_Release.ShaderResourceIndex = texture.ShaderResourceIndex;
            l_Release.UnorderedAccessIndex = texture.UnorderedAccessIndex;

            return l_Release;
        }

        VulkanDevice::VulkanRelease VulkanDevice::ToRelease(const VulkanSampler& sampler)
        {
            VulkanRelease l_Release;
            l_Release.Sampler = sampler.Sampler;
            l_Release.SamplerIndex = sampler.Index;

            return l_Release;
        }

        // An index is only handed out again once the frames that could read it have finished
        void VulkanDevice::Release(const VulkanRelease& release)
        {
            m_ResourceIndices.Free(release.ShaderResourceIndex);
            m_ResourceIndices.Free(release.UnorderedAccessIndex);
            m_SamplerIndices.Free(release.SamplerIndex);

            if (release.Sampler != VK_NULL_HANDLE)
            {
                vkDestroySampler(m_Device, release.Sampler, nullptr);
            }

            if (release.Pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_Device, release.Pipeline, nullptr);
            }

            if (release.SampledView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_Device, release.SampledView, nullptr);
            }

            if (release.View != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_Device, release.View, nullptr);
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

        // Debug utils is only on with validation, and then the layer's messages name the object
        void VulkanDevice::SetDebugName(VkObjectType type, std::uint64_t handle, std::string_view name) const
        {
            if (!m_Validation || name.empty())
            {
                return;
            }

            const std::string l_Name(name);

            VkDebugUtilsObjectNameInfoEXT l_Info = MakeInfo<VkDebugUtilsObjectNameInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT);
            l_Info.objectType = type;
            l_Info.objectHandle = handle;
            l_Info.pObjectName = l_Name.c_str();
            vkSetDebugUtilsObjectNameEXT(m_Device, &l_Info);
        }

        BufferHandle VulkanDevice::CreateBuffer(const BufferDescription& description)
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

        void VulkanDevice::DestroyBuffer(BufferHandle buffer)
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

        std::span<std::byte> VulkanDevice::GetMappedData(BufferHandle buffer)
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
        bool VulkanDevice::IsFormatSupported(Format format, TextureUsage usage) const
        {
            const VkFormat l_Format = ToVkFormat(format);
            if (l_Format == VK_FORMAT_UNDEFINED || (IsCompressedFormat(format) && !m_TextureCompressionBC))
            {
                return false;
            }

            VkFormatProperties l_Properties{};
            vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, l_Format, &l_Properties);

            constexpr std::array<std::pair<TextureUsage, VkFormatFeatureFlags>, 6> c_Needs{ {
                { TextureUsage::ShaderResource, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
                { TextureUsage::UnorderedAccess, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT },
                { TextureUsage::RenderTarget, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT },
                { TextureUsage::DepthStencil, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT },
                { TextureUsage::CopySource, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT },
                { TextureUsage::CopyDestination, VK_FORMAT_FEATURE_TRANSFER_DST_BIT }
            } };

            return std::ranges::all_of(c_Needs, [usage, &l_Properties](const auto& need) { return !HasFlag(usage, need.first) || (l_Properties.optimalTilingFeatures & need.second) != 0; });
        }

        TextureHandle VulkanDevice::CreateTexture(const TextureDescription& description)
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
            l_Create.extent = { description.Width, description.Height, 1 };
            l_Create.mipLevels = description.MipLevels;
            l_Create.arrayLayers = 1;
            l_Create.samples = VK_SAMPLE_COUNT_1_BIT;
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
            l_Texture.Width = description.Width;
            l_Texture.Height = description.Height;
            l_Texture.MipLevels = description.MipLevels;
            SetDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(l_Texture.Image), description.DebugName);

            // Storage images are written one mip at a time, so the unordered access descriptor reuses the attachment view of mip 0
            if (HasFlag(description.Usage, TextureUsage::RenderTarget) || HasFlag(description.Usage, TextureUsage::DepthStencil) || HasFlag(description.Usage, TextureUsage::UnorderedAccess))
            {
                l_Texture.AttachmentView = CreateAttachmentView(l_Texture.Image, description.TextureFormat);
            }

            if (HasFlag(description.Usage, TextureUsage::ShaderResource))
            {
                l_Texture.SampledView = CreateSampledView(l_Texture.Image, description.TextureFormat, description.MipLevels);
                l_Texture.ShaderResourceIndex = AddImageDescriptor(c_SampledImageBinding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, l_Texture.SampledView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }

            if (HasFlag(description.Usage, TextureUsage::UnorderedAccess))
            {
                l_Texture.UnorderedAccessIndex = AddImageDescriptor(c_StorageImageBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, l_Texture.AttachmentView, VK_IMAGE_LAYOUT_GENERAL);
            }

            return m_Textures.Add(l_Texture);
        }

        void VulkanDevice::DestroyTexture(TextureHandle texture)
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

        TextureHandle VulkanDevice::AddSwapChainImage(VkImage image, Format format, std::uint32_t width, std::uint32_t height)
        {
            VulkanTexture l_Texture;
            l_Texture.Image = image;
            l_Texture.AttachmentView = CreateAttachmentView(image, format);
            l_Texture.TextureFormat = format;
            l_Texture.Width = width;
            l_Texture.Height = height;
            l_Texture.MipLevels = 1;

            return m_Textures.Add(l_Texture);
        }

        // The swap chain waits for the device to be idle first, so the view goes at once
        void VulkanDevice::RemoveSwapChainImage(TextureHandle texture)
        {
            const std::optional<VulkanTexture> l_Texture = m_Textures.Remove(texture);
            if (l_Texture)
            {
                vkDestroyImageView(m_Device, l_Texture->AttachmentView, nullptr);
            }
        }

        void VulkanDevice::WaitForSemaphore(VkSemaphore semaphore)
        {
            VkSemaphoreSubmitInfo l_Wait = MakeInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            l_Wait.semaphore = semaphore;
            l_Wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            m_FrameWaits.push_back(l_Wait);
        }

        void VulkanDevice::SignalSemaphore(VkSemaphore semaphore)
        {
            VkSemaphoreSubmitInfo l_Signal = MakeInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            l_Signal.semaphore = semaphore;
            l_Signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            m_FrameSignals.push_back(l_Signal);
        }

        std::uint32_t VulkanDevice::GetShaderResourceIndex(BufferHandle buffer)
        {
            const VulkanBuffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetShaderResourceIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr ? l_Buffer->ShaderResourceIndex : c_NoBindlessIndex;
        }

        std::uint32_t VulkanDevice::GetUnorderedAccessIndex(BufferHandle buffer)
        {
            const VulkanBuffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid buffer.");

            return l_Buffer != nullptr ? l_Buffer->UnorderedAccessIndex : c_NoBindlessIndex;
        }

        std::uint32_t VulkanDevice::GetShaderResourceIndex(TextureHandle texture)
        {
            const VulkanTexture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetShaderResourceIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr ? l_Texture->ShaderResourceIndex : c_NoBindlessIndex;
        }

        std::uint32_t VulkanDevice::GetUnorderedAccessIndex(TextureHandle texture)
        {
            const VulkanTexture* l_Texture = m_Textures.Get(texture);
            TR_CORE_ASSERT(l_Texture != nullptr, "GetUnorderedAccessIndex on a destroyed or invalid texture.");

            return l_Texture != nullptr ? l_Texture->UnorderedAccessIndex : c_NoBindlessIndex;
        }

        // Shaders read samplers from their own array at c_SamplerBinding, indexed separately from resources
        SamplerHandle VulkanDevice::CreateSampler(const SamplerDescription& description)
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
            const VkResult l_Result = vkCreateSampler(m_Device, &l_Create, nullptr, &l_Sampler.Sampler);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: sampler '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            l_Sampler.Index = m_SamplerIndices.Allocate();
            if (l_Sampler.Index == c_NoBindlessIndex)
            {
                TR_CORE_ERROR("Vulkan: sampler '{}' could not be created, since all {} bindless sampler indices are in use", description.DebugName, c_BindlessSamplerCapacity);
                vkDestroySampler(m_Device, l_Sampler.Sampler, nullptr);

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
            vkUpdateDescriptorSets(m_Device, 1, &l_Write, 0, nullptr);

            return m_Samplers.Add(l_Sampler);
        }

        void VulkanDevice::DestroySampler(SamplerHandle sampler)
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

        std::uint32_t VulkanDevice::GetSamplerIndex(SamplerHandle sampler)
        {
            const VulkanSampler* l_Sampler = m_Samplers.Get(sampler);
            TR_CORE_ASSERT(l_Sampler != nullptr, "GetSamplerIndex on a destroyed or invalid sampler.");

            return l_Sampler != nullptr ? l_Sampler->Index : c_NoBindlessIndex;
        }

        PipelineHandle VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDescription& description)
        {
            TR_CORE_ASSERT(!description.VertexShader.Code.empty() && !description.PixelShader.Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);
            TR_CORE_ASSERT(description.ColorFormats.size() <= c_MaxColorAttachments, "Pipeline '{}' has too many color formats.", description.DebugName);

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
                l_Result = vkCreateShaderModule(m_Device, &l_Module, nullptr, &l_Modules[it_Stage]);

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
            l_Rasterization.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo l_Multisample = MakeInfo<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
            l_Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo l_DepthStencil = MakeInfo<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
            l_DepthStencil.depthTestEnable = description.DepthTest ? VK_TRUE : VK_FALSE;
            l_DepthStencil.depthWriteEnable = description.DepthWrite ? VK_TRUE : VK_FALSE;
            l_DepthStencil.depthCompareOp = ToVkCompareOp(description.DepthCompare);

            std::array<VkPipelineColorBlendAttachmentState, c_MaxColorAttachments> l_Blends{};
            std::array<VkFormat, c_MaxColorAttachments> l_ColorFormats{};
            const std::uint32_t l_ColorCount = static_cast<std::uint32_t>(std::min<std::size_t>(description.ColorFormats.size(), c_MaxColorAttachments));
            for (std::uint32_t it_Color = 0; it_Color < l_ColorCount; ++it_Color)
            {
                VkPipelineColorBlendAttachmentState& l_Blend = l_Blends[it_Color];
                l_Blend.blendEnable = description.AlphaBlend ? VK_TRUE : VK_FALSE;
                l_Blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                l_Blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                l_Blend.colorBlendOp = VK_BLEND_OP_ADD;
                l_Blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                l_Blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                l_Blend.alphaBlendOp = VK_BLEND_OP_ADD;
                l_Blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

                l_ColorFormats[it_Color] = ToVkFormat(description.ColorFormats[it_Color]);
            }

            VkPipelineColorBlendStateCreateInfo l_ColorBlend = MakeInfo<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
            l_ColorBlend.attachmentCount = l_ColorCount;
            l_ColorBlend.pAttachments = l_Blends.data();

            const std::array<VkDynamicState, 2> l_DynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo l_Dynamic = MakeInfo<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
            l_Dynamic.dynamicStateCount = static_cast<std::uint32_t>(l_DynamicStates.size());
            l_Dynamic.pDynamicStates = l_DynamicStates.data();

            VkPipelineRenderingCreateInfo l_Rendering = MakeInfo<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
            l_Rendering.colorAttachmentCount = l_ColorCount;
            l_Rendering.pColorAttachmentFormats = l_ColorFormats.data();
            l_Rendering.depthAttachmentFormat = description.DepthFormat != Format::Unknown ? ToVkFormat(description.DepthFormat) : VK_FORMAT_UNDEFINED;

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
                l_Result = vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &l_Create, nullptr, &l_Pipeline.Pipeline);
            }

            for (VkShaderModule it_Module : l_Modules)
            {
                vkDestroyShaderModule(m_Device, it_Module, nullptr);
            }

            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: pipeline '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(l_Pipeline.Pipeline), description.DebugName);

            return m_Pipelines.Add(l_Pipeline);
        }

        void VulkanDevice::DestroyPipeline(PipelineHandle pipeline)
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

        Scope<SwapChain> VulkanDevice::CreateSwapChain(const SwapChainSpecification& specification)
        {
            return VulkanSwapChain::Create(*this, specification);
        }

        // Waits until the frame c_FramesInFlight before this one, which used the same command pool, has finished on the GPU
        CommandList& VulkanDevice::BeginFrame()
        {
            TR_CORE_ASSERT(!m_InFrame, "BeginFrame was called twice without EndFrame.");

            FrameContext& l_Frame = m_Frames[GetFrameSlot()];
            if (l_Frame.CompletionValue != 0)
            {
                VkSemaphoreWaitInfo l_Wait = MakeInfo<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
                l_Wait.semaphoreCount = 1;
                l_Wait.pSemaphores = &m_FrameTimeline;
                l_Wait.pValues = &l_Frame.CompletionValue;
                vkWaitSemaphores(m_Device, &l_Wait, UINT64_MAX);
            }

            m_InFrame = true;
            m_Releases.BeginFrame([this](const VulkanRelease& release) { Release(release); });
            m_UploadRing.BeginFrame();

            vkResetCommandPool(m_Device, l_Frame.CommandPool, 0);
            m_FrameWaits.clear();
            m_FrameSignals.clear();
            m_CommandList.Begin(l_Frame.CommandBuffer);

            return m_CommandList;
        }

        void VulkanDevice::EndFrame()
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

            const VkResult l_Result = vkQueueSubmit2(m_Queue, 1, &l_Submit, VK_NULL_HANDLE);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: frame {} could not be submitted ({})", m_FrameNumber, FormatResult(l_Result));
            }

            ++m_FrameNumber;
            m_Releases.EndFrame();
            m_InFrame = false;
        }

        void VulkanDevice::WaitIdle()
        {
            if (m_Device != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_Device);
                m_Releases.ReleaseIdle([this](const VulkanRelease& release) { Release(release); });
            }
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::OnDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void* context)
        {
            VulkanDevice* l_Device = static_cast<VulkanDevice*>(context);
            const std::string_view l_Message = data != nullptr && data->pMessage != nullptr ? data->pMessage : "";

            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            {
                l_Device->m_MessageCount.fetch_add(1, std::memory_order_relaxed);
                TR_CORE_ERROR("Vulkan validation: {}", l_Message);
            }
            else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
            {
                l_Device->m_MessageCount.fetch_add(1, std::memory_order_relaxed);
                TR_CORE_WARN("Vulkan validation: {}", l_Message);
            }
            else
            {
                TR_CORE_TRACE("Vulkan validation: {}", l_Message);
            }

            return VK_FALSE;
        }
    }
}