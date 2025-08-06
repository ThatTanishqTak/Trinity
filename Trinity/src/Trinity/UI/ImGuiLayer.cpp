#include "Trinity/trpch.h"
#include "Trinity/UI/ImGuiLayer.h"

#include <imgui.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

namespace Trinity
{
    ImGuiLayer::ImGuiLayer(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily, VkQueue queue,
        VkRenderPass renderPass, uint32_t imageCount) : m_Window(window), m_Instance(instance), m_PhysicalDevice(physicalDevice), m_Device(device), m_QueueFamily(queueFamily),
        m_Queue(queue), m_RenderPass(renderPass), m_ImageCount(imageCount)
    {
    }

    bool ImGuiLayer::Initialize()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& l_IO = ImGui::GetIO();
        l_IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(m_Window, true);

        VkDescriptorPoolSize l_PoolSizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        VkDescriptorPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        l_PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        l_PoolInfo.maxSets = 1000 * (uint32_t)(sizeof(l_PoolSizes) / sizeof(VkDescriptorPoolSize));
        l_PoolInfo.pPoolSizes = l_PoolSizes;
        l_PoolInfo.poolSizeCount = (uint32_t)(sizeof(l_PoolSizes) / sizeof(VkDescriptorPoolSize));
        if (vkCreateDescriptorPool(m_Device, &l_PoolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
        {
            return false;
        }

        ImGui_ImplVulkan_InitInfo l_InitInfo{};
        l_InitInfo.Instance = m_Instance;
        l_InitInfo.PhysicalDevice = m_PhysicalDevice;
        l_InitInfo.Device = m_Device;
        l_InitInfo.QueueFamily = m_QueueFamily;
        l_InitInfo.Queue = m_Queue;
        l_InitInfo.DescriptorPool = m_DescriptorPool;
        l_InitInfo.MinImageCount = m_ImageCount;
        l_InitInfo.ImageCount = m_ImageCount;
        l_InitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        l_InitInfo.CheckVkResultFn = nullptr;

        if (!ImGui_ImplVulkan_Init(&l_InitInfo))
        {
            return false;
        }

        return true;
    }

    void ImGuiLayer::Shutdown()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        ImGui::DestroyContext();
    }

    void ImGuiLayer::BeginFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiLayer::EndFrame(VkCommandBuffer commandBuffer)
    {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }

    void ImGuiLayer::OnEvent(Event& e)
    {
        ImGuiIO& l_IO = ImGui::GetIO();
        if (e.IsInCategory(EventCategoryMouse) && l_IO.WantCaptureMouse)
        {
            e.Handled = true;
        }

        if (e.IsInCategory(EventCategoryKeyboard) && l_IO.WantCaptureKeyboard)
        {
            e.Handled = true;
        }
    }
}