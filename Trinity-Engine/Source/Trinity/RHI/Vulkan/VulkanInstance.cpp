#include "Trinity/RHI/Vulkan/VulkanInstance.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <string_view>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            constexpr const char* c_ValidationLayer = "VK_LAYER_KHRONOS_validation";

            std::vector<VkExtensionProperties> GetInstanceExtensions(const char* layer)
            {
                std::uint32_t l_Count = 0;
                vkEnumerateInstanceExtensionProperties(layer, &l_Count, nullptr);
                std::vector<VkExtensionProperties> l_Extensions(l_Count);
                vkEnumerateInstanceExtensionProperties(layer, &l_Count, l_Extensions.data());

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
        }

        VulkanInstance::~VulkanInstance()
        {
            Destroy();
        }

        bool VulkanInstance::Initialize(const DeviceSpecification& specification, std::string& error)
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

            return Create(specification, l_LoaderVersion, error);
        }

        bool VulkanInstance::Create(const DeviceSpecification& specification, std::uint32_t loaderVersion, std::string& error)
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
            l_Messenger.pfnUserCallback = &VulkanInstance::OnDebugMessage;
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

        void VulkanInstance::Destroy()
        {
            if (m_Messenger != VK_NULL_HANDLE)
            {
                vkDestroyDebugUtilsMessengerEXT(m_Instance, m_Messenger, nullptr);
                m_Messenger = VK_NULL_HANDLE;
            }

            if (m_Instance != VK_NULL_HANDLE)
            {
                vkDestroyInstance(m_Instance, nullptr);
                m_Instance = VK_NULL_HANDLE;
            }
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL VulkanInstance::OnDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void* context)
        {
            VulkanInstance* l_Instance = static_cast<VulkanInstance*>(context);
            const std::string_view l_Message = data != nullptr && data->pMessage != nullptr ? data->pMessage : "";

            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            {
                l_Instance->m_MessageCount.fetch_add(1, std::memory_order_relaxed);
                TR_CORE_ERROR("Vulkan validation: {}", l_Message);
            }
            else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
            {
                l_Instance->m_MessageCount.fetch_add(1, std::memory_order_relaxed);
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