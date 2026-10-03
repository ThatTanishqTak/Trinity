#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"

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

            // A Vulkan structure with every member zero except its type
            template<typename T>
            T MakeInfo(VkStructureType type)
            {
                T l_Info{};
                l_Info.sType = type;

                return l_Info;
            }

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

            std::string FormatResult(VkResult result)
            {
                return std::format("{} ({})", ToString(result), static_cast<int>(result));
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
                    default:
                    {
                        return VK_FORMAT_UNDEFINED;
                    }
                }
            }

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
        }

        void VulkanCommandList::TextureBarrier([[maybe_unused]] TextureHandle texture, [[maybe_unused]] ResourceState before, [[maybe_unused]] ResourceState after)
        {

        }

        void VulkanCommandList::BufferBarrier([[maybe_unused]] BufferHandle buffer, [[maybe_unused]] ResourceState before, [[maybe_unused]] ResourceState after)
        {

        }

        void VulkanCommandList::BeginRendering([[maybe_unused]] const RenderingDescription& description)
        {

        }

        void VulkanCommandList::EndRendering()
        {

        }

        void VulkanCommandList::SetPipeline([[maybe_unused]] PipelineHandle pipeline)
        {

        }

        void VulkanCommandList::SetViewport([[maybe_unused]] const Viewport& viewport)
        {

        }

        void VulkanCommandList::SetScissor([[maybe_unused]] const Rect& scissor)
        {

        }

        void VulkanCommandList::PushConstants([[maybe_unused]] std::span<const std::byte> data)
        {

        }

        void VulkanCommandList::Draw([[maybe_unused]] std::uint32_t vertexCount, [[maybe_unused]] std::uint32_t instanceCount, [[maybe_unused]] std::uint32_t firstVertex, [[maybe_unused]] std::uint32_t firstInstance)
        {

        }

        void VulkanCommandList::CopyBuffer([[maybe_unused]] BufferHandle source, [[maybe_unused]] std::uint64_t sourceOffset, [[maybe_unused]] BufferHandle destination, [[maybe_unused]] std::uint64_t destinationOffset, [[maybe_unused]] std::uint64_t size)
        {

        }

        void VulkanCommandList::CopyTextureToBuffer([[maybe_unused]] TextureHandle source, [[maybe_unused]] BufferHandle destination)
        {

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

        VulkanDevice::~VulkanDevice()
        {
            // The validation layer reports objects still alive in vkDestroyDevice and vkDestroyInstance, so the messenger outlives the device
            if (m_Device != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_Device);

                if (m_Buffers.GetCount() != 0 || m_Textures.GetCount() != 0)
                {
                    TR_CORE_WARN("Vulkan: the device was destroyed with {} buffer(s) and {} texture(s) still alive", m_Buffers.GetCount(), m_Textures.GetCount());
                }

                m_Releases.ReleaseAll([this](const VulkanRelease& release) { Release(release); });
                m_Buffers.ForEach([this](const VulkanBuffer& buffer) { Release({ buffer.Buffer, VK_NULL_HANDLE, buffer.Allocation }); });
                m_Textures.ForEach([this](const VulkanTexture& texture) { Release({ VK_NULL_HANDLE, texture.Image, texture.Allocation }); });

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

            return CreateInstance(specification, l_LoaderVersion, error) && CreateLogicalDevice(error) && CreateAllocator(error);
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

            TR_CORE_INFO("Vulkan: {} ({}) with {} of video memory: Vulkan {}, {} {}, dynamic rendering, synchronization2, descriptor indexing, timeline semaphores", m_Info.AdapterName, ToString(l_Properties.properties.deviceType), Memory::FormatBytes(m_Info.VideoMemoryBytes), FormatVersion(l_Properties.properties.apiVersion), l_DriverProperties.driverName, l_DriverProperties.driverInfo);

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

        void VulkanDevice::Release(const VulkanRelease& release)
        {
            if (release.Buffer != VK_NULL_HANDLE)
            {
                vmaDestroyBuffer(m_Allocator, release.Buffer, release.Allocation);
            }

            if (release.Image != VK_NULL_HANDLE)
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
                m_Releases.Push({ l_Buffer->Buffer, VK_NULL_HANDLE, l_Buffer->Allocation });
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

        TextureHandle VulkanDevice::CreateTexture(const TextureDescription& description)
        {
            TR_CORE_ASSERT(description.Width != 0 && description.Height != 0 && description.MipLevels != 0, "Texture '{}' has a zero size or no mips.", description.DebugName);
            TR_CORE_ASSERT(description.TextureFormat != Format::Unknown, "Texture '{}' has no format.", description.DebugName);

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

            l_Texture.ImageFormat = l_Create.format;
            l_Texture.Width = description.Width;
            l_Texture.Height = description.Height;
            l_Texture.MipLevels = description.MipLevels;
            SetDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(l_Texture.Image), description.DebugName);

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
                m_Releases.Push({ VK_NULL_HANDLE, l_Texture->Image, l_Texture->Allocation });
            }
        }

        PipelineHandle VulkanDevice::CreateGraphicsPipeline([[maybe_unused]] const GraphicsPipelineDescription& description)
        {
            return {};
        }

        void VulkanDevice::DestroyPipeline([[maybe_unused]] PipelineHandle pipeline)
        {

        }

        Scope<SwapChain> VulkanDevice::CreateSwapChain([[maybe_unused]] const SwapChainSpecification& specification)
        {
            return nullptr;
        }

        CommandList& VulkanDevice::BeginFrame()
        {
            TR_CORE_ASSERT(!m_InFrame, "BeginFrame was called twice without EndFrame.");

            m_InFrame = true;
            m_Releases.BeginFrame([this](const VulkanRelease& release) { Release(release); });

            return m_CommandList;
        }

        void VulkanDevice::EndFrame()
        {
            TR_CORE_ASSERT(m_InFrame, "EndFrame without BeginFrame.");

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