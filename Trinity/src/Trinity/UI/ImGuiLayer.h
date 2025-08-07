#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>

#include "Trinity/Events/Event.h"
#include "Trinity/UI/Panel.h"

namespace Trinity
{
    class ImGuiLayer
    {
    public:
        ImGuiLayer(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily, 
            VkQueue queue, VkRenderPass renderPass, uint32_t imageCount);
        ~ImGuiLayer() = default;

        bool Initialize();
        void Shutdown();

        void BeginFrame();
        void EndFrame(VkCommandBuffer commandBuffer);

        void RegisterPanel(std::unique_ptr<Panel> panel);

        void OnEvent(Event& e);

    private:
        GLFWwindow* m_Window;
        VkInstance m_Instance;
        VkPhysicalDevice m_PhysicalDevice;
        VkDevice m_Device;
        uint32_t m_QueueFamily;
        VkQueue m_Queue;
        VkRenderPass m_RenderPass;
        uint32_t m_ImageCount;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

        std::vector<std::unique_ptr<Panel>> m_Panels;
    };
}