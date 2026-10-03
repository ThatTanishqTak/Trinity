#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
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

            return CreateInstance(specification, l_LoaderVersion, error) && CreateLogicalDevice(error);
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

        BufferHandle VulkanDevice::CreateBuffer([[maybe_unused]] const BufferDescription& description)
        {
            return {};
        }

        void VulkanDevice::DestroyBuffer([[maybe_unused]] BufferHandle buffer)
        {

        }

        std::span<std::byte> VulkanDevice::GetMappedData([[maybe_unused]] BufferHandle buffer)
        {
            return {};
        }

        TextureHandle VulkanDevice::CreateTexture([[maybe_unused]] const TextureDescription& description)
        {
            return {};
        }

        void VulkanDevice::DestroyTexture([[maybe_unused]] TextureHandle texture)
        {

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
            return m_CommandList;
        }

        void VulkanDevice::EndFrame()
        {

        }

        void VulkanDevice::WaitIdle()
        {
            if (m_Device != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_Device);
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