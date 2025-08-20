#include "Trinity/trpch.h"

#include "Trinity/Renderer/Renderer.h"
#include "Trinity/Vulkan/VulkanContext.h"
#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Camera/Camera.h"
#include "Trinity/Renderer/Shader.h"
#include "Trinity/Renderer/Mesh.h"

namespace Trinity
{
    Renderer::Renderer(VulkanContext* context) : m_Context(context)
    {

    }

    bool Renderer::Initialize()
    {
        if (!m_Context)
        {
            return false;
        }

        VkDevice l_Device = m_Context->GetDevice();

        VkCommandPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        l_PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        l_PoolInfo.queueFamilyIndex = m_Context->FindQueueFamilies(m_Context->GetPhysicalDevice()).GraphicsFamily.value();

        if (vkCreateCommandPool(l_Device, &l_PoolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create command pool");

            return false;
        }

        uint32_t l_ImageCount = static_cast<uint32_t>(m_Context->GetSwapChainImages().size());
        m_CommandBuffers.resize(l_ImageCount);
        VkCommandBufferAllocateInfo l_AllocInfo{};
        l_AllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocInfo.commandPool = m_CommandPool;
        l_AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocInfo.commandBufferCount = l_ImageCount;

        if (vkAllocateCommandBuffers(l_Device, &l_AllocInfo, m_CommandBuffers.data()) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to allocate command buffers");

            return false;
        }

        VkAttachmentDescription l_ColorAttachment{};
        l_ColorAttachment.format = m_Context->GetSwapChainImageFormat();
        l_ColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        l_ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        l_ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        l_ColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        l_ColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        l_ColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ColorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference l_ColorRef{};
        l_ColorRef.attachment = 0;
        l_ColorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription l_Subpass{};
        l_Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        l_Subpass.colorAttachmentCount = 1;
        l_Subpass.pColorAttachments = &l_ColorRef;

        VkRenderPassCreateInfo l_RenderPassInfo{};
        l_RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        l_RenderPassInfo.attachmentCount = 1;
        l_RenderPassInfo.pAttachments = &l_ColorAttachment;
        l_RenderPassInfo.subpassCount = 1;
        l_RenderPassInfo.pSubpasses = &l_Subpass;

        if (vkCreateRenderPass(l_Device, &l_RenderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create render pass");

            return false;
        }

        // Framebuffers
        auto l_ImageViews = m_Context->GetSwapChainImages();
        m_Framebuffers.resize(l_ImageCount);
        for (size_t i = 0; i < l_ImageCount; ++i)
        {
            VkImageView l_Attachments[] = { l_ImageViews[i] };
            VkFramebufferCreateInfo l_FramebufferInfo{};
            l_FramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            l_FramebufferInfo.renderPass = m_RenderPass;
            l_FramebufferInfo.attachmentCount = 1;
            l_FramebufferInfo.pAttachments = l_Attachments;
            l_FramebufferInfo.width = m_Context->GetSwapChainExtent().width;
            l_FramebufferInfo.height = m_Context->GetSwapChainExtent().height;
            l_FramebufferInfo.layers = 1;

            if (vkCreateFramebuffer(l_Device, &l_FramebufferInfo, nullptr, &m_Framebuffers[i]) != VK_SUCCESS)
            {
                TR_CORE_ERROR("Failed to create framebuffer");

                return false;
            }
        }

        // Synchronization objects
        m_SyncObjects.resize(l_ImageCount);
        VkSemaphoreCreateInfo l_SemInfo{};
        l_SemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo l_FenceInfo{};
        l_FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        l_FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (auto& it_Sync : m_SyncObjects)
        {
            if (vkCreateSemaphore(l_Device, &l_SemInfo, nullptr, &it_Sync.ImageAvailable) != VK_SUCCESS ||
                vkCreateSemaphore(l_Device, &l_SemInfo, nullptr, &it_Sync.RenderFinished) != VK_SUCCESS ||
                vkCreateFence(l_Device, &l_FenceInfo, nullptr, &it_Sync.InFlight) != VK_SUCCESS)
            {
                TR_CORE_ERROR("Failed to create synchronization objects");

                return false;
            }
        }

        if (!CreatePipeline())
        {
            return false;
        }

        return true;
    }

    void Renderer::Shutdown()
    {
        if (!m_Context)
        {
            return;
        }

        VkDevice l_Device = m_Context->GetDevice();

        DestroyPipeline();

        for (auto l_Framebuffer : m_Framebuffers)
        {
            vkDestroyFramebuffer(l_Device, l_Framebuffer, nullptr);
        }
        m_Framebuffers.clear();

        for (auto& it_Sync : m_SyncObjects)
        {
            if (it_Sync.ImageAvailable)
            {
                vkDestroySemaphore(l_Device, it_Sync.ImageAvailable, nullptr);
            }
            
            if (it_Sync.RenderFinished)
            {
                vkDestroySemaphore(l_Device, it_Sync.RenderFinished, nullptr);
            }
         
            if (it_Sync.InFlight)
            {
                vkDestroyFence(l_Device, it_Sync.InFlight, nullptr);
            }
        }
        m_SyncObjects.clear();

        if (m_RenderPass)
        {
            vkDestroyRenderPass(l_Device, m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }

        if (!m_CommandBuffers.empty())
        {
            vkFreeCommandBuffers(l_Device, m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
            m_CommandBuffers.clear();
        }

        if (m_CommandPool)
        {
            vkDestroyCommandPool(l_Device, m_CommandPool, nullptr);
            m_CommandPool = VK_NULL_HANDLE;
        }
    }

    void Renderer::DrawFrame(const std::function<void(VkCommandBuffer)>& recordFunc)
    {
        if (m_CommandBuffers.empty())
        {
            return;
        }

        VkDevice l_Device = m_Context->GetDevice();
        VkSwapchainKHR l_SwapChain = m_Context->GetSwapChain();
        VkQueue l_GraphicsQueue = m_Context->GetGraphicsQueue();
        VkQueue l_PresentQueue = m_Context->GetPresentQueue();

        auto& it_Sync = m_SyncObjects[m_CurrentFrame];
        vkWaitForFences(l_Device, 1, &it_Sync.InFlight, VK_TRUE, UINT64_MAX);

        uint32_t l_ImageIndex = 0;
        vkAcquireNextImageKHR(l_Device, l_SwapChain, UINT64_MAX, it_Sync.ImageAvailable, VK_NULL_HANDLE, &l_ImageIndex);

        VkCommandBuffer l_CommandBuffer = m_CommandBuffers[l_ImageIndex];
        vkResetCommandBuffer(l_CommandBuffer, 0);

        VkCommandBufferBeginInfo l_BeginInfo{};
        l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        VkClearValue l_ClearColor{ {{0.f, 0.f, 0.f, 1.f}} };
        VkRenderPassBeginInfo l_RenderPassInfo{};
        l_RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        l_RenderPassInfo.renderPass = m_RenderPass;
        l_RenderPassInfo.framebuffer = m_Framebuffers[l_ImageIndex];
        l_RenderPassInfo.renderArea.offset = { 0, 0 };
        l_RenderPassInfo.renderArea.extent = m_Context->GetSwapChainExtent();
        l_RenderPassInfo.clearValueCount = 1;
        l_RenderPassInfo.pClearValues = &l_ClearColor;

        vkCmdBeginRenderPass(l_CommandBuffer, &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);

        if (recordFunc)
        {
            recordFunc(l_CommandBuffer);
        }

        vkCmdEndRenderPass(l_CommandBuffer);
        vkEndCommandBuffer(l_CommandBuffer);

        VkSemaphore l_WaitSemaphores[] = { it_Sync.ImageAvailable };
        VkPipelineStageFlags l_WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore l_SignalSemaphores[] = { it_Sync.RenderFinished };

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        l_SubmitInfo.waitSemaphoreCount = 1;
        l_SubmitInfo.pWaitSemaphores = l_WaitSemaphores;
        l_SubmitInfo.pWaitDstStageMask = l_WaitStages;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;
        l_SubmitInfo.signalSemaphoreCount = 1;
        l_SubmitInfo.pSignalSemaphores = l_SignalSemaphores;

        vkResetFences(l_Device, 1, &it_Sync.InFlight);
        vkQueueSubmit(l_GraphicsQueue, 1, &l_SubmitInfo, it_Sync.InFlight);

        VkPresentInfoKHR l_PresentInfo{};
        l_PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        l_PresentInfo.waitSemaphoreCount = 1;
        l_PresentInfo.pWaitSemaphores = l_SignalSemaphores;
        l_PresentInfo.swapchainCount = 1;
        l_PresentInfo.pSwapchains = &l_SwapChain;
        l_PresentInfo.pImageIndices = &l_ImageIndex;

        vkQueuePresentKHR(l_PresentQueue, &l_PresentInfo);

        m_CurrentFrame = (m_CurrentFrame + 1) % static_cast<uint32_t>(m_SyncObjects.size());
    }

    void Renderer::OnWindowResize()
    {
        if (!m_Context)
        {
            return;
        }

        VkDevice l_Device = m_Context->GetDevice();
        vkDeviceWaitIdle(l_Device);

        // Cleanup existing resources
        DestroyPipeline();
        for (auto it_Framebuffer : m_Framebuffers)
        {
            vkDestroyFramebuffer(l_Device, it_Framebuffer, nullptr);
        }
        m_Framebuffers.clear();

        if (!m_CommandBuffers.empty())
        {
            vkFreeCommandBuffers(l_Device, m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
            m_CommandBuffers.clear();
        }

        for (auto& it_Sync : m_SyncObjects)
        {
            if (it_Sync.ImageAvailable)
            {
                vkDestroySemaphore(l_Device, it_Sync.ImageAvailable, nullptr);
            }
            
            if (it_Sync.RenderFinished)
            {
                vkDestroySemaphore(l_Device, it_Sync.RenderFinished, nullptr);
            }
         
            if (it_Sync.InFlight)
            {
                vkDestroyFence(l_Device, it_Sync.InFlight, nullptr);
            }
        }
        m_SyncObjects.clear();

        // Recreate swap chain and dependent resources
        m_Context->RecreateSwapChain();

        uint32_t l_ImageCount = static_cast<uint32_t>(m_Context->GetSwapChainImages().size());

        m_CommandBuffers.resize(l_ImageCount);
        VkCommandBufferAllocateInfo l_AllocInfo{};
        l_AllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocInfo.commandPool = m_CommandPool;
        l_AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocInfo.commandBufferCount = l_ImageCount;
        vkAllocateCommandBuffers(l_Device, &l_AllocInfo, m_CommandBuffers.data());

        auto l_ImageViews = m_Context->GetSwapChainImages();
        m_Framebuffers.resize(l_ImageCount);
        for (size_t i = 0; i < l_ImageCount; ++i)
        {
            VkImageView l_Attachments[] = { l_ImageViews[i] };
            VkFramebufferCreateInfo it_FramebufferInfo{};
            it_FramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            it_FramebufferInfo.renderPass = m_RenderPass;
            it_FramebufferInfo.attachmentCount = 1;
            it_FramebufferInfo.pAttachments = l_Attachments;
            it_FramebufferInfo.width = m_Context->GetSwapChainExtent().width;
            it_FramebufferInfo.height = m_Context->GetSwapChainExtent().height;
            it_FramebufferInfo.layers = 1;
            vkCreateFramebuffer(l_Device, &it_FramebufferInfo, nullptr, &m_Framebuffers[i]);
        }

        m_SyncObjects.resize(l_ImageCount);
        
        VkSemaphoreCreateInfo l_SemInfo{};
        l_SemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        
        VkFenceCreateInfo l_FenceInfo{};
        l_FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        l_FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (auto& it_Sync : m_SyncObjects)
        {
            vkCreateSemaphore(l_Device, &l_SemInfo, nullptr, &it_Sync.ImageAvailable);
            vkCreateSemaphore(l_Device, &l_SemInfo, nullptr, &it_Sync.RenderFinished);
            vkCreateFence(l_Device, &l_FenceInfo, nullptr, &it_Sync.InFlight);
        }

        CreatePipeline();
    }

    bool Renderer::CreatePipeline()
    {
        if (!m_Context)
        {
            return false;
        }

        VkDevice l_Device = m_Context->GetDevice();

        Shader l_Shader(m_Context);
        if (auto a_Error = l_Shader.LoadFromFile("Resources/TrinityForge/DefaultAssets/Shaders/Simple"))
        {
            TR_CORE_ERROR("Failed to load shader: {}", *a_Error);

            return false;
        }

        VkPipelineShaderStageCreateInfo l_ShaderStages[2]{};
        l_ShaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l_ShaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        l_ShaderStages[0].module = l_Shader.GetVertexModule();
        l_ShaderStages[0].pName = "main";

        l_ShaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l_ShaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_ShaderStages[1].module = l_Shader.GetFragmentModule();
        l_ShaderStages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo l_VertexInput{};
        l_VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        auto l_BindingDescription = Mesh::GetBindingDescription();
        auto l_AttributeDescriptions = Mesh::GetAttributeDescriptions();
        l_VertexInput.vertexBindingDescriptionCount = 1;
        l_VertexInput.pVertexBindingDescriptions = &l_BindingDescription;
        l_VertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(l_AttributeDescriptions.size());
        l_VertexInput.pVertexAttributeDescriptions = l_AttributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo l_InputAssembly{};
        l_InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        l_InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport l_Viewport{};
        l_Viewport.x = 0.0f;
        l_Viewport.y = 0.0f;
        l_Viewport.width = static_cast<float>(m_Context->GetSwapChainExtent().width);
        l_Viewport.height = static_cast<float>(m_Context->GetSwapChainExtent().height);
        l_Viewport.minDepth = 0.0f;
        l_Viewport.maxDepth = 1.0f;

        VkRect2D l_Scissor{};
        l_Scissor.offset = { 0, 0 };
        l_Scissor.extent = m_Context->GetSwapChainExtent();

        VkPipelineViewportStateCreateInfo l_ViewportState{};
        l_ViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        l_ViewportState.viewportCount = 1;
        l_ViewportState.pViewports = &l_Viewport;
        l_ViewportState.scissorCount = 1;
        l_ViewportState.pScissors = &l_Scissor;

        VkPipelineRasterizationStateCreateInfo l_Rasterizer{};
        l_Rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        l_Rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        l_Rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        l_Rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        l_Rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo l_Multisampling{};
        l_Multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        l_Multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState l_ColorBlendAttachment{};
        l_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo l_ColorBlending{};
        l_ColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        l_ColorBlending.attachmentCount = 1;
        l_ColorBlending.pAttachments = &l_ColorBlendAttachment;

        VkPipelineLayoutCreateInfo l_LayoutInfo{};
        l_LayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (vkCreatePipelineLayout(l_Device, &l_LayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create pipeline layout");

            return false;
        }

        VkGraphicsPipelineCreateInfo l_PipelineInfo{};
        l_PipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        l_PipelineInfo.stageCount = 2;
        l_PipelineInfo.pStages = l_ShaderStages;
        l_PipelineInfo.pVertexInputState = &l_VertexInput;
        l_PipelineInfo.pInputAssemblyState = &l_InputAssembly;
        l_PipelineInfo.pViewportState = &l_ViewportState;
        l_PipelineInfo.pRasterizationState = &l_Rasterizer;
        l_PipelineInfo.pMultisampleState = &l_Multisampling;
        l_PipelineInfo.pColorBlendState = &l_ColorBlending;
        l_PipelineInfo.layout = m_PipelineLayout;
        l_PipelineInfo.renderPass = m_RenderPass;
        l_PipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(l_Device, VK_NULL_HANDLE, 1, &l_PipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create graphics pipeline");

            return false;
        }

        return true;
    }

    void Renderer::DestroyPipeline()
    {
        if (!m_Context)
        {
            return;
        }

        VkDevice l_Device = m_Context->GetDevice();

        if (m_Pipeline)
        {
            vkDestroyPipeline(l_Device, m_Pipeline, nullptr);
            m_Pipeline = VK_NULL_HANDLE;
        }

        if (m_PipelineLayout)
        {
            vkDestroyPipelineLayout(l_Device, m_PipelineLayout, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
        }
    }
}