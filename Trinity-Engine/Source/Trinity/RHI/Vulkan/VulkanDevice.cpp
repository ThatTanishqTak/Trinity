#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanInstance.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            template<typename T>
            struct RequiredFeature
            {
                std::string_view Name;
                VkBool32 T::* Member;
            };

            // What the backend is built on. Descriptor indexing is for the bindless tables, shader draw parameters is what Slang's SV_VertexID needs, depth clamp and bias clamp are for shadow maps, and independent blend is for an integer target beside a blended one
            constexpr std::array<RequiredFeature<VkPhysicalDeviceFeatures>, 3> c_Required10
            { {
                { "depthClamp", &VkPhysicalDeviceFeatures::depthClamp },
                { "depthBiasClamp", &VkPhysicalDeviceFeatures::depthBiasClamp },
                { "independentBlend", &VkPhysicalDeviceFeatures::independentBlend }
            } };

            constexpr std::array<RequiredFeature<VkPhysicalDeviceVulkan11Features>, 1> c_Required11
            { {
                { "shaderDrawParameters", &VkPhysicalDeviceVulkan11Features::shaderDrawParameters }
            } };

            constexpr std::array<RequiredFeature<VkPhysicalDeviceVulkan12Features>, 11> c_Required12
            { {
                { "timelineSemaphore", &VkPhysicalDeviceVulkan12Features::timelineSemaphore },
                { "hostQueryReset", &VkPhysicalDeviceVulkan12Features::hostQueryReset },
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

            // A shader's discard is a demotion to a helper invocation in SPIR-V 1.6, which every Vulkan 1.3 device supports
            constexpr std::array<RequiredFeature<VkPhysicalDeviceVulkan13Features>, 3> c_Required13
            { {
                { "dynamicRendering", &VkPhysicalDeviceVulkan13Features::dynamicRendering },
                { "synchronization2", &VkPhysicalDeviceVulkan13Features::synchronization2 },
                { "shaderDemoteToHelperInvocation", &VkPhysicalDeviceVulkan13Features::shaderDemoteToHelperInvocation }
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

            std::vector<VkExtensionProperties> GetDeviceExtensions(VkPhysicalDevice device)
            {
                std::uint32_t l_Count = 0;
                vkEnumerateDeviceExtensionProperties(device, nullptr, &l_Count, nullptr);
                std::vector<VkExtensionProperties> l_Extensions(l_Count);
                vkEnumerateDeviceExtensionProperties(device, nullptr, &l_Count, l_Extensions.data());

                return l_Extensions;
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

        VulkanDevice::VulkanDevice(const VulkanInstance& instance) : m_Instance(instance)
        {

        }

        VulkanDevice::~VulkanDevice()
        {
            Destroy();
        }

        bool VulkanDevice::Initialize(const DeviceSpecification& specification, std::string& error)
        {
            std::uint32_t l_Count = 0;
            vkEnumeratePhysicalDevices(m_Instance.GetHandle(), &l_Count, nullptr);
            std::vector<VkPhysicalDevice> l_PhysicalDevices(l_Count);
            vkEnumeratePhysicalDevices(m_Instance.GetHandle(), &l_Count, l_PhysicalDevices.data());

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
                AddMissing(l_Features.Features.features, c_Required10, l_Missing);
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

            // Optional: without valid timestamp bits on the queue, CreateQueryPool refuses with an error
            std::uint32_t l_FamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &l_FamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> l_Families(l_FamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &l_FamilyCount, l_Families.data());
            m_TimestampsSupported = m_QueueFamily < l_Families.size() && l_Families[m_QueueFamily].timestampValidBits != 0 && l_Properties.properties.limits.timestampPeriod > 0.0f;
            m_TimestampPeriod = l_Properties.properties.limits.timestampPeriod;

            DeviceFeatures l_Enabled;
            Enable(l_Enabled.Features.features, c_Required10);
            Enable(l_Enabled.Vulkan11, c_Required11);
            Enable(l_Enabled.Vulkan12, c_Required12);
            Enable(l_Enabled.Vulkan13, c_Required13);

            // Optional: without it, IsFormatSupported reports every BC format as unsupported and textures fall back to RGBA8
            DeviceFeatures l_Available;
            vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &l_Available.Features);
            m_TextureCompressionBC = l_Available.Features.features.textureCompressionBC == VK_TRUE;
            l_Enabled.Features.features.textureCompressionBC = l_Available.Features.features.textureCompressionBC;

            const std::vector<VkExtensionProperties> l_DeviceExtensions = GetDeviceExtensions(m_PhysicalDevice);
            std::vector<const char*> l_Extensions;
            if (HasExtension(l_DeviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            {
                l_Extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            }

            // Optional: without it the memory budget shows each heap's size alone
            m_MemoryBudget = HasExtension(l_DeviceExtensions, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
            if (m_MemoryBudget)
            {
                l_Extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
            }

            // A family of its own for transfers and one for compute, the transfer one taking the uploads when renderer.copy_queue is on and the compute one not used yet
            const auto a_Find = [&l_Families](VkQueueFlags wanted, VkQueueFlags unwanted)
            {
                return std::ranges::find_if(l_Families, [wanted, unwanted](const VkQueueFamilyProperties& family) { return (family.queueFlags & wanted) != 0 && (family.queueFlags & unwanted) == 0; });
            };

            const auto a_Transfer = a_Find(VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
            m_TransferFamily = specification.EnableCopyQueue && a_Transfer != l_Families.end() ? static_cast<std::uint32_t>(std::distance(l_Families.begin(), a_Transfer)) : m_QueueFamily;

            const float l_Priority = 1.0f;
            std::array<VkDeviceQueueCreateInfo, 2> l_Queues{};
            for (std::size_t it_Index = 0; it_Index < l_Queues.size(); ++it_Index)
            {
                l_Queues[it_Index] = MakeInfo<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
                l_Queues[it_Index].queueFamilyIndex = it_Index == 0 ? m_QueueFamily : m_TransferFamily;
                l_Queues[it_Index].queueCount = 1;
                l_Queues[it_Index].pQueuePriorities = &l_Priority;
            }

            VkDeviceCreateInfo l_Create = MakeInfo<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
            l_Create.pNext = &l_Enabled.Features;
            l_Create.queueCreateInfoCount = m_TransferFamily != m_QueueFamily ? 2 : 1;
            l_Create.pQueueCreateInfos = l_Queues.data();
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
            vkGetDeviceQueue(m_Device, m_TransferFamily, 0, &m_TransferQueue);

            m_Info.API = GraphicsAPI::Vulkan;
            m_Info.AdapterName = l_Properties.properties.deviceName;
            m_Info.VideoMemoryBytes = GetVideoMemory(m_PhysicalDevice);

            TR_CORE_INFO("Vulkan: {} ({}) with {} of video memory: Vulkan {}, {} {}, dynamic rendering, synchronization2, descriptor indexing, timeline semaphores{}", m_Info.AdapterName, ToString(l_Properties.properties.deviceType), Memory::FormatBytes(m_Info.VideoMemoryBytes), FormatVersion(l_Properties.properties.apiVersion), l_DriverProperties.driverName, l_DriverProperties.driverInfo, m_TextureCompressionBC ? ", BC textures" : "");

            // Each family named with its queue count
            const auto a_Describe = [&l_Families](std::vector<VkQueueFamilyProperties>::const_iterator family) -> std::string
            {
                if (family == l_Families.cend())
                {
                    return "none";
                }

                return std::format("{} ({} {})", std::distance(l_Families.cbegin(), family), family->queueCount, family->queueCount == 1 ? "queue" : "queues");
            };

            TR_CORE_INFO("Vulkan: queue families: graphics and compute {}, dedicated transfer {}, dedicated compute {}", a_Describe(l_Families.cbegin() + m_QueueFamily), a_Describe(a_Find(VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)), a_Describe(a_Find(VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT)));

            if (!specification.EnableCopyQueue)
            {
                TR_CORE_INFO("Vulkan: uploads are recorded into the frame's commands, as renderer.copy_queue is off");
            }
            else if (m_TransferFamily != m_QueueFamily)
            {
                TR_CORE_INFO("Vulkan: uploads go to the transfer queue of family {}, and every buffer is shared by it and the graphics family", m_TransferFamily);
            }
            else
            {
                TR_CORE_INFO("Vulkan: uploads go to the graphics queue in a submission of their own, as no family is for transfers alone");
            }

            return true;
        }

        void VulkanDevice::Destroy()
        {
            if (m_Device != VK_NULL_HANDLE)
            {
                vkDestroyDevice(m_Device, nullptr);
                m_Device = VK_NULL_HANDLE;
            }
        }

        void VulkanDevice::WaitIdle() const
        {
            vkDeviceWaitIdle(m_Device);
        }

        // Debug utils is on with validation, and outside Distribution whenever the loader has it, so RenderDoc and the layer's messages name the object
        void VulkanDevice::SetDebugName(VkObjectType type, std::uint64_t handle, std::string_view name) const
        {
            if (!m_Instance.HasDebugUtils() || name.empty())
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
    }
}