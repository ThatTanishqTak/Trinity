#include "Trinity/trpch.h"

#include "Trinity/Renderer/Renderer.h"
#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Vulkan/VulkanContext.h"

namespace Trinity
{
    Renderer::Renderer(VulkanContext* context) : m_Context(context)
    {

    }

    bool Renderer::Initialize()
    {
        TR_CORE_INFO("-------INITIALIZING RENDERER-------");

        CreateRenderPass();
        CreateGraphicsPipeline();
        CreateFramebuffers();
        CreateCommandPool();
        CreateCommandBuffer();
        CreateSyncObjects();

        TR_CORE_INFO("-------RENDERER INITIALIZED-------");

        return true;
    }

    void Renderer::Shutdown()
    {
        TR_CORE_INFO("-------SHUTTING DOWN RENDERER-------");

        vkDeviceWaitIdle(m_Context->GetDevice());
        TR_CORE_TRACE("Renderer is ready to be shutdown");

        if (!m_ImageAvailableSemaphore.empty())
        {
            for (size_t i = 0; i < m_ImageAvailableSemaphore.size(); ++i)
            {
                if (m_ImageAvailableSemaphore[i])
                {
                    vkDestroySemaphore(m_Context->GetDevice(), m_ImageAvailableSemaphore[i], nullptr);
                }

                if (m_RenderFinshedSemaphore[i])
                {
                    vkDestroySemaphore(m_Context->GetDevice(), m_RenderFinshedSemaphore[i], nullptr);
                }

                if (m_InFlightFence[i])
                {
                    vkDestroyFence(m_Context->GetDevice(), m_InFlightFence[i], nullptr);
                }
            }
            TR_CORE_TRACE("Semaphores destroyed");
        }

        if (m_CommandPool)
        {
            vkDestroyCommandPool(m_Context->GetDevice(), m_CommandPool, nullptr);
            TR_CORE_TRACE("Command pool destroyed");
        }

        if (!m_Framebuffers.empty())
        {
            for (auto it_Framebuffer : m_Framebuffers)
            {
                vkDestroyFramebuffer(m_Context->GetDevice(), it_Framebuffer, nullptr);
            }
            TR_CORE_TRACE("Framebuffers destroyed");
        }

        if (m_GraphicsPipeline)
        {
            vkDestroyPipeline(m_Context->GetDevice(), m_GraphicsPipeline, nullptr);
            m_GraphicsPipeline = VK_NULL_HANDLE;
            TR_CORE_TRACE("Graphics pipeline destroyed");
        }

        if (m_PipelineLayout)
        {
            vkDestroyPipelineLayout(m_Context->GetDevice(), m_PipelineLayout, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
            TR_CORE_TRACE("Pipeline layout destroyed");
        }

        if (m_RenderPass)
        {
            vkDestroyRenderPass(m_Context->GetDevice(), m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
            TR_CORE_TRACE("Render pass destroyed");
        }

        TR_CORE_INFO("-------RENDERER SHUTDOWN COMPLETE-------");
    }

    void Renderer::DrawFrame()
    {
        TR_CORE_TRACE("Current Frame: {}, Frame Rate: {}, DeltaTime: {}", m_CurrentFrame, Utilities::Time::GetFPS(), Utilities::Time::GetDeltaTime());

        //vkDeviceWaitIdle(m_Context->GetDevice());

        vkWaitForFences(m_Context->GetDevice(), 1, &m_InFlightFence[m_CurrentFrame], VK_TRUE, UINT64_MAX);
        vkResetFences(m_Context->GetDevice(), 1, &m_InFlightFence[m_CurrentFrame]);

        uint32_t l_ImageIndex = 0;
        vkAcquireNextImageKHR(m_Context->GetDevice(), m_Context->GetSwapChain(), UINT32_MAX, m_ImageAvailableSemaphore[l_ImageIndex], VK_NULL_HANDLE, &l_ImageIndex);

        if (m_ImagesInFlight[l_ImageIndex] != VK_NULL_HANDLE)
        {
            vkWaitForFences(m_Context->GetDevice(), 1, &m_ImagesInFlight[l_ImageIndex], VK_TRUE, UINT64_MAX);
        }

        m_ImagesInFlight[l_ImageIndex] = m_InFlightFence[l_ImageIndex];

        vkResetCommandBuffer(m_CommandBuffer[l_ImageIndex], 0);
        RecordCommandBuffer(l_ImageIndex);

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore l_WaitSemaphore[] = { m_ImageAvailableSemaphore[l_ImageIndex]};
        VkPipelineStageFlags l_WaitFlags[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        l_SubmitInfo.waitSemaphoreCount = 1;
        l_SubmitInfo.pWaitSemaphores = l_WaitSemaphore;
        l_SubmitInfo.pWaitDstStageMask = l_WaitFlags;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &m_CommandBuffer[l_ImageIndex];

        VkSemaphore l_SignalSemaphores[] = { m_RenderFinshedSemaphore[l_ImageIndex] };
        l_SubmitInfo.signalSemaphoreCount = 1;
        l_SubmitInfo.pSignalSemaphores = l_SignalSemaphores;

        if (vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &l_SubmitInfo, m_InFlightFence[l_ImageIndex]) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to submit draw command buffer");

            return;
        }

        VkPresentInfoKHR l_PresentInfo{};
        l_PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        l_PresentInfo.waitSemaphoreCount = 1;
        l_PresentInfo.pWaitSemaphores = l_SignalSemaphores;

        VkSwapchainKHR l_SwapChains[] = { m_Context->GetSwapChain() };
        l_PresentInfo.swapchainCount = 1;
        l_PresentInfo.pSwapchains = l_SwapChains;
        l_PresentInfo.pImageIndices = &l_ImageIndex;
        l_PresentInfo.pResults = nullptr; // Optional

        vkQueuePresentKHR(m_Context->GetPresentQueue(), &l_PresentInfo);

        l_ImageIndex = (l_ImageIndex + 1) % m_ImageAvailableSemaphore.size();
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------------//

    void Renderer::CreateRenderPass()
    {
        TR_CORE_TRACE("Creating render pass");

        VkAttachmentDescription l_ColorAttachment{};
        l_ColorAttachment.format = m_Context->GetSwapChainImageFormat();
        l_ColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        l_ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        l_ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        l_ColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        l_ColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        l_ColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ColorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference l_ColorAttachmentRef{};
        l_ColorAttachmentRef.attachment = 0;
        l_ColorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription l_Subpass{};
        l_Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        l_Subpass.colorAttachmentCount = 1;
        l_Subpass.pColorAttachments = &l_ColorAttachmentRef;

        VkRenderPassCreateInfo l_RenderPassInfo{};
        l_RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        l_RenderPassInfo.attachmentCount = 1;
        l_RenderPassInfo.pAttachments = &l_ColorAttachment;
        l_RenderPassInfo.subpassCount = 1;
        l_RenderPassInfo.pSubpasses = &l_Subpass;

        VkSubpassDependency l_Dependency{};
        l_Dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        l_Dependency.dstSubpass = 0;
        l_Dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        l_Dependency.srcAccessMask = 0;
        l_Dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        l_Dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        l_RenderPassInfo.dependencyCount = 1;
        l_RenderPassInfo.pDependencies = &l_Dependency;

        if (vkCreateRenderPass(m_Context->GetDevice(), &l_RenderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create render pass");
        }

        TR_CORE_TRACE("Render pass created");
    }

    void Renderer::CreateGraphicsPipeline()
    {
        TR_CORE_TRACE("Creating graphics pipeline");

        if (!m_Context)
        {
            TR_CORE_ERROR("Vulkan context is not set");

            return;
        }

        auto a_VertextSource = Utilities::FileManagement::ReadFile("Assets/Shaders/Simple.vert.spv");
        auto a_FragmentSource = Utilities::FileManagement::ReadFile("Assets/Shaders/Simple.frag.spv");

        VkShaderModule l_VertShader = CreateShaderModule(a_VertextSource);
        VkShaderModule l_FragShader = CreateShaderModule(a_FragmentSource);

        VkPipelineShaderStageCreateInfo l_VertShaderStageInfo{};
        l_VertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l_VertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        l_VertShaderStageInfo.module = l_VertShader;
        l_VertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo l_FragShaderStageInfo{};
        l_FragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l_FragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_FragShaderStageInfo.module = l_FragShader;
        l_FragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo l_ShaderStages[] = { l_VertShaderStageInfo, l_FragShaderStageInfo };

        VkPipelineVertexInputStateCreateInfo l_VertexInputInfo{};
        l_VertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        l_VertexInputInfo.vertexBindingDescriptionCount = 0;
        l_VertexInputInfo.pVertexBindingDescriptions = nullptr;
        l_VertexInputInfo.vertexAttributeDescriptionCount = 0;
        l_VertexInputInfo.pVertexAttributeDescriptions = nullptr;

        VkPipelineInputAssemblyStateCreateInfo l_InputAssembly{};
        l_InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        l_InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        l_InputAssembly.primitiveRestartEnable = VK_FALSE;

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
        l_Rasterizer.depthClampEnable = VK_FALSE;
        l_Rasterizer.rasterizerDiscardEnable = VK_FALSE;
        l_Rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        l_Rasterizer.lineWidth = 1.0f;
        l_Rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        l_Rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        l_Rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo l_Multisampling{};
        l_Multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        l_Multisampling.sampleShadingEnable = VK_FALSE;
        l_Multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState l_ColorBlendAttachment{};
        l_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        l_ColorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo l_ColorBlending{};
        l_ColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        l_ColorBlending.logicOpEnable = VK_FALSE;
        l_ColorBlending.logicOp = VK_LOGIC_OP_COPY;
        l_ColorBlending.attachmentCount = 1;
        l_ColorBlending.pAttachments = &l_ColorBlendAttachment;
        l_ColorBlending.blendConstants[0] = 0.0f;
        l_ColorBlending.blendConstants[1] = 0.0f;
        l_ColorBlending.blendConstants[2] = 0.0f;
        l_ColorBlending.blendConstants[3] = 0.0f;

        VkPipelineLayoutCreateInfo l_PipelineLayoutInfo{};
        l_PipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        l_PipelineLayoutInfo.setLayoutCount = 0;
        l_PipelineLayoutInfo.pSetLayouts = nullptr;
        l_PipelineLayoutInfo.pushConstantRangeCount = 0;
        l_PipelineLayoutInfo.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(m_Context->GetDevice(), &l_PipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create pipeline layout");

            return;
        }

        std::array<VkDynamicState, 2> l_DynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo l_DynamicStateInfo{};
        l_DynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        l_DynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(l_DynamicStates.size());
        l_DynamicStateInfo.pDynamicStates = l_DynamicStates.data();

        VkGraphicsPipelineCreateInfo l_PipelineInfo{};
        l_PipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        l_PipelineInfo.stageCount = 2;
        l_PipelineInfo.pStages = l_ShaderStages;
        l_PipelineInfo.pVertexInputState = &l_VertexInputInfo;
        l_PipelineInfo.pInputAssemblyState = &l_InputAssembly;
        l_PipelineInfo.pViewportState = &l_ViewportState;
        l_PipelineInfo.pRasterizationState = &l_Rasterizer;
        l_PipelineInfo.pMultisampleState = &l_Multisampling;
        l_PipelineInfo.pDepthStencilState = nullptr;
        l_PipelineInfo.pColorBlendState = &l_ColorBlending;
        l_PipelineInfo.pDynamicState = &l_DynamicStateInfo;
        l_PipelineInfo.layout = m_PipelineLayout;
        l_PipelineInfo.renderPass = m_RenderPass;
        l_PipelineInfo.subpass = 0;
        l_PipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        l_PipelineInfo.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(m_Context->GetDevice(), VK_NULL_HANDLE, 1, &l_PipelineInfo, nullptr, &m_GraphicsPipeline) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create graphics pipeline");

            return;
        }

        vkDestroyShaderModule(m_Context->GetDevice(), l_FragShader, nullptr);
        vkDestroyShaderModule(m_Context->GetDevice(), l_VertShader, nullptr);

        TR_CORE_TRACE("Graphics pipeline created");
    }

    void Renderer::CreateFramebuffers()
    {
        TR_CORE_TRACE("Creating framebuffers");

        m_Framebuffers.resize(m_Context->GetSwapChainImages().size());

        for (size_t i = 0; i < m_Context->GetSwapChainImages().size(); i++)
        {
            VkImageView l_Attachments[] = { m_Context->GetSwapChainImages()[i] };

            VkFramebufferCreateInfo l_FramebufferInfo{};
            l_FramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            l_FramebufferInfo.renderPass = m_RenderPass;
            l_FramebufferInfo.attachmentCount = 1;
            l_FramebufferInfo.pAttachments = l_Attachments;
            l_FramebufferInfo.width = m_Context->GetSwapChainExtent().width;
            l_FramebufferInfo.height = m_Context->GetSwapChainExtent().height;
            l_FramebufferInfo.layers = 1;

            if (vkCreateFramebuffer(m_Context->GetDevice(), &l_FramebufferInfo, nullptr, &m_Framebuffers[i]) != VK_SUCCESS)
            {
                TR_CORE_ERROR("Failed to create framebuffer");

                return;
            }
        }

        TR_CORE_TRACE("Framebuffers created");
    }

    void Renderer::CreateCommandPool()
    {
        TR_CORE_TRACE("Creating command pool");

        QueueFamilyIndices l_QueueFamilyIndices = m_Context->FindQueueFamilies(m_Context->GetPhysicalDivice());

        VkCommandPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        l_PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        l_PoolInfo.queueFamilyIndex = l_QueueFamilyIndices.GraphicsFamily.value();

        if (vkCreateCommandPool(m_Context->GetDevice(), &l_PoolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create command pool");

            return;
        }

        TR_CORE_TRACE("Command pool created");
    }

    void Renderer::CreateCommandBuffer()
    {
        TR_CORE_TRACE("Creating command buffer");

        m_CommandBuffer.resize(m_Context->GetSwapChainImages().size());

        VkCommandBufferAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocateInfo.commandPool = m_CommandPool;
        l_AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocateInfo.commandBufferCount = static_cast<uint32_t>(m_Context->GetSwapChainImages().size());

        if (vkAllocateCommandBuffers(m_Context->GetDevice(), &l_AllocateInfo, m_CommandBuffer.data()) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create command buffers");

            return;
        }

        TR_CORE_TRACE("Command buffer created");
    }

    void Renderer::CreateSyncObjects()
    {
        TR_CORE_TRACE("Creating semaphores created");

        VkSemaphoreCreateInfo l_SemaphoreInfo{};
        l_SemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo l_FenceInfo{};
        l_FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        l_FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        size_t l_FrameCount = m_Context->GetSwapChainImages().size();
        m_ImageAvailableSemaphore.resize(l_FrameCount);
        m_RenderFinshedSemaphore.resize(l_FrameCount);
        m_InFlightFence.resize(l_FrameCount);
        m_ImagesInFlight.resize(l_FrameCount, VK_NULL_HANDLE);

        for (size_t i = 0; i < l_FrameCount; ++i)
        {
            if (vkCreateSemaphore(m_Context->GetDevice(), &l_SemaphoreInfo, nullptr, &m_ImageAvailableSemaphore[i]) != VK_SUCCESS ||
                vkCreateSemaphore(m_Context->GetDevice(), &l_SemaphoreInfo, nullptr, &m_RenderFinshedSemaphore[i]) != VK_SUCCESS ||
                vkCreateFence(m_Context->GetDevice(), &l_FenceInfo, nullptr, &m_InFlightFence[i]) != VK_SUCCESS)
            {
                TR_CORE_ERROR("Failed to create synchronization objects");

                return;
            }
        }

        TR_CORE_TRACE("semaphores created");
    }

    void Renderer::RecordCommandBuffer(uint32_t imageIndex)
    {
        VkCommandBufferBeginInfo l_BeginInfo{};
        l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        l_BeginInfo.flags = 0;
        l_BeginInfo.pInheritanceInfo = nullptr;

        if (vkBeginCommandBuffer(m_CommandBuffer[imageIndex], &l_BeginInfo) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to begin recording command buffer");

            return;
        }

        VkRenderPassBeginInfo l_RenderPassInfo{};
        l_RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        l_RenderPassInfo.renderPass = m_RenderPass;
        l_RenderPassInfo.framebuffer = m_Framebuffers[imageIndex];
        l_RenderPassInfo.renderArea.offset = { 0, 0 };
        l_RenderPassInfo.renderArea.extent = m_Context->GetSwapChainExtent();

        VkClearValue l_ClearColor = { {0.05f, 0.05f, 0.05f} };
        l_RenderPassInfo.clearValueCount = 1;
        l_RenderPassInfo.pClearValues = &l_ClearColor;

        vkCmdBeginRenderPass(m_CommandBuffer[imageIndex], &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(m_CommandBuffer[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

        VkViewport l_Viewport{};
        l_Viewport.x = 0.0f;
        l_Viewport.y = 0.0f;
        l_Viewport.width = static_cast<float>(m_Context->GetSwapChainExtent().width);
        l_Viewport.height = static_cast<float>(m_Context->GetSwapChainExtent().height);
        l_Viewport.minDepth = 0.0f;
        l_Viewport.maxDepth = 1.0f;
        vkCmdSetViewport(m_CommandBuffer[imageIndex], 0, 1, &l_Viewport);

        VkRect2D l_Scissor{};
        l_Scissor.offset = { 0, 0 };
        l_Scissor.extent = m_Context->GetSwapChainExtent();
        vkCmdSetScissor(m_CommandBuffer[imageIndex], 0, 1, &l_Scissor);

        vkCmdDraw(m_CommandBuffer[imageIndex], 3, 1, 0, 0);

        vkCmdEndRenderPass(m_CommandBuffer[imageIndex]);

        if (vkEndCommandBuffer(m_CommandBuffer[imageIndex]) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to record command buffer");
        }
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------------//

    VkShaderModule Renderer::CreateShaderModule(const std::vector<std::byte>& code)
    {
        VkShaderModuleCreateInfo l_CreateInfo{};
        l_CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        l_CreateInfo.codeSize = code.size();
        l_CreateInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule l_ShaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_Context->GetDevice(), &l_CreateInfo, nullptr, &l_ShaderModule) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create shader module");

            return VkShaderModule();
        }

        return l_ShaderModule;
    }
}