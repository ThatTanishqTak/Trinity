#include "Trinity/RHI/Vulkan/VulkanSwapChain.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            std::string_view ToString(VkPresentModeKHR mode)
            {
                switch (mode)
                {
                    case VK_PRESENT_MODE_FIFO_KHR:
                    {
                        return "FIFO (vsync)";
                    }
                    case VK_PRESENT_MODE_MAILBOX_KHR:
                    {
                        return "mailbox";
                    }
                    case VK_PRESENT_MODE_IMMEDIATE_KHR:
                    {
                        return "immediate";
                    }
                    default:
                    {
                        return "another present mode";
                    }
                }
            }

            // FIFO is always there, and is the only mode that waits for vertical blank
            VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync)
            {
                if (!vsync)
                {
                    for (VkPresentModeKHR it_Mode : { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR })
                    {
                        if (std::ranges::find(modes, it_Mode) != modes.end())
                        {
                            return it_Mode;
                        }
                    }
                }

                return VK_PRESENT_MODE_FIFO_KHR;
            }
        }

        Scope<VulkanSwapChain> VulkanSwapChain::Create(VulkanDevice& device, const SwapChainSpecification& specification)
        {
            Scope<VulkanSwapChain> l_SwapChain = CreateScope<VulkanSwapChain>(device, specification);
            if (!l_SwapChain->Initialize())
            {
                return nullptr;
            }

            return l_SwapChain;
        }

        VulkanSwapChain::VulkanSwapChain(VulkanDevice& device, const SwapChainSpecification& specification) : m_Device(device), m_Specification(specification)
        {

        }

        VulkanSwapChain::~VulkanSwapChain()
        {
            m_Device.WaitIdle();

            const VkDevice l_Device = m_Device.GetHandle();
            for (TextureHandle it_Texture : m_Textures)
            {
                m_Device.RemoveSwapChainImage(it_Texture);
            }

            for (VkSemaphore it_Semaphore : m_PresentSemaphores)
            {
                vkDestroySemaphore(l_Device, it_Semaphore, nullptr);
            }

            for (VkSemaphore it_Semaphore : m_AcquireSemaphores)
            {
                vkDestroySemaphore(l_Device, it_Semaphore, nullptr);
            }

            vkDestroySwapchainKHR(l_Device, m_SwapChain, nullptr);
            vkDestroySurfaceKHR(m_Device.GetInstance(), m_Surface, nullptr);
        }

        bool VulkanSwapChain::Initialize()
        {
            const VkPhysicalDevice l_PhysicalDevice = m_Device.GetPhysicalDevice();
            const VkDevice l_Device = m_Device.GetHandle();

#if defined(VK_USE_PLATFORM_WIN32_KHR)
            VkWin32SurfaceCreateInfoKHR l_SurfaceCreate = MakeInfo<VkWin32SurfaceCreateInfoKHR>(VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR);
            l_SurfaceCreate.hinstance = ::GetModuleHandleW(nullptr);
            l_SurfaceCreate.hwnd = static_cast<HWND>(m_Specification.NativeWindow);

            const VkResult l_SurfaceResult = vkCreateWin32SurfaceKHR(m_Device.GetInstance(), &l_SurfaceCreate, nullptr, &m_Surface);
            if (l_SurfaceResult != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: the window surface could not be created ({})", FormatResult(l_SurfaceResult));

                return false;
            }
#else
            TR_CORE_ERROR("Vulkan: this platform has no window surface yet");

            return false;
#endif

            VkBool32 l_Supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(l_PhysicalDevice, m_Device.GetQueueFamily(), m_Surface, &l_Supported);
            if (l_Supported != VK_TRUE)
            {
                TR_CORE_ERROR("Vulkan: {} cannot present to this window from its graphics queue", m_Device.GetInfo().AdapterName);

                return false;
            }

            VkSurfaceCapabilitiesKHR l_Capabilities{};
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(l_PhysicalDevice, m_Surface, &l_Capabilities);

            std::uint32_t l_Count = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(l_PhysicalDevice, m_Surface, &l_Count, nullptr);
            std::vector<VkSurfaceFormatKHR> l_SurfaceFormats(l_Count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(l_PhysicalDevice, m_Surface, &l_Count, l_SurfaceFormats.data());

            vkGetPhysicalDeviceSurfacePresentModesKHR(l_PhysicalDevice, m_Surface, &l_Count, nullptr);
            std::vector<VkPresentModeKHR> l_PresentModes(l_Count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(l_PhysicalDevice, m_Surface, &l_Count, l_PresentModes.data());

            // The requested format first, then the 8-bit formats every desktop driver offers
            VkSurfaceFormatKHR l_SurfaceFormat{};
            for (Format it_Format : { m_Specification.ImageFormat, Format::BGRA8Unorm, Format::RGBA8Unorm })
            {
                const auto a_Match = std::ranges::find_if(l_SurfaceFormats, [it_Format](const VkSurfaceFormatKHR& format) { return format.format == ToVkFormat(it_Format) && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; });
                if (a_Match != l_SurfaceFormats.end())
                {
                    l_SurfaceFormat = *a_Match;
                    m_Format = it_Format;

                    break;
                }
            }

            if (m_Format == Format::Unknown)
            {
                TR_CORE_ERROR("Vulkan: the window offers neither {}, BGRA8Unorm nor RGBA8Unorm", ToString(m_Specification.ImageFormat));

                return false;
            }

            m_Width = l_Capabilities.currentExtent.width != UINT32_MAX ? l_Capabilities.currentExtent.width : std::clamp(m_Specification.Width, l_Capabilities.minImageExtent.width, l_Capabilities.maxImageExtent.width);
            m_Height = l_Capabilities.currentExtent.height != UINT32_MAX ? l_Capabilities.currentExtent.height : std::clamp(m_Specification.Height, l_Capabilities.minImageExtent.height, l_Capabilities.maxImageExtent.height);
            if (m_Width == 0 || m_Height == 0)
            {
                TR_CORE_ERROR("Vulkan: the window has no area to present to");

                return false;
            }

            std::uint32_t l_ImageCount = std::max(l_Capabilities.minImageCount + 1, 3u);
            if (l_Capabilities.maxImageCount != 0)
            {
                l_ImageCount = std::min(l_ImageCount, l_Capabilities.maxImageCount);
            }

            const VkPresentModeKHR l_PresentMode = ChoosePresentMode(l_PresentModes, m_Specification.VSync);

            VkSwapchainCreateInfoKHR l_Create = MakeInfo<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
            l_Create.surface = m_Surface;
            l_Create.minImageCount = l_ImageCount;
            l_Create.imageFormat = l_SurfaceFormat.format;
            l_Create.imageColorSpace = l_SurfaceFormat.colorSpace;
            l_Create.imageExtent = { m_Width, m_Height };
            l_Create.imageArrayLayers = 1;
            l_Create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            l_Create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            l_Create.preTransform = l_Capabilities.currentTransform;
            l_Create.compositeAlpha = (l_Capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0 ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
            l_Create.presentMode = l_PresentMode;
            l_Create.clipped = VK_TRUE;

            const VkResult l_Result = vkCreateSwapchainKHR(l_Device, &l_Create, nullptr, &m_SwapChain);
            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: the swap chain could not be created ({})", FormatResult(l_Result));

                return false;
            }

            vkGetSwapchainImagesKHR(l_Device, m_SwapChain, &l_Count, nullptr);
            std::vector<VkImage> l_Images(l_Count);
            vkGetSwapchainImagesKHR(l_Device, m_SwapChain, &l_Count, l_Images.data());

            const VkSemaphoreCreateInfo l_Semaphore = MakeInfo<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
            for (VkImage it_Image : l_Images)
            {
                m_Textures.push_back(m_Device.AddSwapChainImage(it_Image, m_Format, m_Width, m_Height));
                m_Device.SetDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(it_Image), "Swap chain image");

                VkSemaphore& l_Present = m_PresentSemaphores.emplace_back(VK_NULL_HANDLE);
                vkCreateSemaphore(l_Device, &l_Semaphore, nullptr, &l_Present);
            }

            for (VkSemaphore& it_Semaphore : m_AcquireSemaphores)
            {
                vkCreateSemaphore(l_Device, &l_Semaphore, nullptr, &it_Semaphore);
            }

            TR_CORE_INFO("Vulkan: swap chain of {} {}x{} {} images, presenting with {}", l_Images.size(), m_Width, m_Height, ToString(m_Format), ToString(l_PresentMode));

            return true;
        }

        TextureHandle VulkanSwapChain::AcquireNextTexture()
        {
            TR_CORE_ASSERT(m_Device.IsInFrame(), "AcquireNextTexture is called between Device::BeginFrame and EndFrame.");

            m_Acquired = false;

            const VkSemaphore l_Acquire = m_AcquireSemaphores[m_Device.GetFrameSlot()];
            const VkResult l_Result = vkAcquireNextImageKHR(m_Device.GetHandle(), m_SwapChain, UINT64_MAX, l_Acquire, VK_NULL_HANDLE, &m_ImageIndex);
            if (l_Result != VK_SUCCESS && l_Result != VK_SUBOPTIMAL_KHR)
            {
                Report(l_Result, "acquire");

                return {};
            }

            m_Device.WaitForSemaphore(l_Acquire);
            m_Device.SignalSemaphore(m_PresentSemaphores[m_ImageIndex]);
            m_Acquired = true;

            return m_Textures[m_ImageIndex];
        }

        void VulkanSwapChain::Present()
        {
            TR_CORE_ASSERT(!m_Device.IsInFrame(), "Present is called after Device::EndFrame.");

            if (!m_Acquired)
            {
                return;
            }

            m_Acquired = false;

            VkPresentInfoKHR l_Present = MakeInfo<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
            l_Present.waitSemaphoreCount = 1;
            l_Present.pWaitSemaphores = &m_PresentSemaphores[m_ImageIndex];
            l_Present.swapchainCount = 1;
            l_Present.pSwapchains = &m_SwapChain;
            l_Present.pImageIndices = &m_ImageIndex;

            const VkResult l_Result = vkQueuePresentKHR(m_Device.GetQueue(), &l_Present);
            if (l_Result != VK_SUCCESS && l_Result != VK_SUBOPTIMAL_KHR)
            {
                Report(l_Result, "present");
            }
        }

        void VulkanSwapChain::Resize([[maybe_unused]] std::uint32_t width, [[maybe_unused]] std::uint32_t height)
        {

        }

        void VulkanSwapChain::SetVSync(bool enabled)
        {
            m_Specification.VSync = enabled;
        }

        // Out of date means the window changed size, which is reported once rather than every frame
        void VulkanSwapChain::Report(VkResult result, const char* operation)
        {
            if (result != VK_ERROR_OUT_OF_DATE_KHR)
            {
                TR_CORE_ERROR("Vulkan: swap chain {} failed ({})", operation, FormatResult(result));

                return;
            }

            if (!m_ReportedOutOfDate)
            {
                TR_CORE_INFO("Vulkan: the swap chain no longer matches the window, so frames are not shown until it is recreated");
                m_ReportedOutOfDate = true;
            }
        }
    }
}