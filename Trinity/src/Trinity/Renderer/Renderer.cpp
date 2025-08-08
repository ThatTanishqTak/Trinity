#include "Trinity/trpch.h"

#include "Trinity/Camera/Culling.h"
#include "Trinity/Core/ResourceManager.h"
#include "Trinity/ECS/Components.h"
#include "Trinity/ECS/Scene.h"
#include "Trinity/Renderer/Renderer.h"
#include "Trinity/Renderer/Shader.h"
#include "Trinity/Renderer/Material.h"
#include "Trinity/Utilities/Utilities.h"
#include "Trinity/Vulkan/VulkanContext.h"

#include <backends/imgui_impl_vulkan.h>

namespace Trinity
{
    Renderer::Renderer(VulkanContext* context) : m_Context(context), m_Shader(context)
    {

    }

    bool Renderer::Initialize()
    {
        TR_CORE_INFO("-------INITIALIZING RENDERER-------");

        CreateRenderPass();
        CreateDescriptorSetLayout();
        CreateGraphicsPipeline(m_PrimitiveTopology);
        CreateDepthResources();
        CreateFramebuffers();
        CreateCommandPool();
        CreateTextureImage();
        CreateVertexBuffer();
        CreateIndexBuffer();
        CreateUniformBuffers();
        CreateShadowResources();
        CreateDescriptorPool();
        CreateDescriptorSets();
        CreateCommandBuffer();
        CreateSyncObjects();

        m_BloomPass.Initialize();
        m_ToneMappingPass.Initialize();

        TR_CORE_INFO("-------RENDERER INITIALIZED-------");

        return true;
    }

    void Renderer::Shutdown()
    {
        TR_CORE_INFO("-------SHUTTING DOWN RENDERER-------");

        vkDeviceWaitIdle(m_Context->GetDevice());
        TR_CORE_TRACE("Renderer is ready to be shutdown");

        CleanupImGuiImageDescriptors();

        m_Shader.Destroy();
        m_GraphicsPipeline = VK_NULL_HANDLE;

        for (auto& it_Fence : m_InFlightFence)
        {
            if (it_Fence)
            {
                vkDestroyFence(m_Context->GetDevice(), it_Fence, nullptr);
            }
        }
        TR_CORE_TRACE("In-Flight fences destroyed");

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
        }
        TR_CORE_TRACE("Semaphores destroyed");

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

        if (!m_DepthImageViews.empty())
        {
            for (size_t i = 0; i < m_DepthImageViews.size(); ++i)
            {
                if (m_DepthImageViews[i])
                {
                    vkDestroyImageView(m_Context->GetDevice(), m_DepthImageViews[i], nullptr);
                }

                if (m_DepthImages[i])
                {
                    vkDestroyImage(m_Context->GetDevice(), m_DepthImages[i], nullptr);
                }

                if (m_DepthImageMemory[i])
                {
                    vkFreeMemory(m_Context->GetDevice(), m_DepthImageMemory[i], nullptr);
                }
            }

            m_DepthImageViews.clear();
            m_DepthImages.clear();
            m_DepthImageMemory.clear();
            TR_CORE_TRACE("Depth resources destroyed");
        }

        if (m_ShadowSampler)
        {
            vkDestroySampler(m_Context->GetDevice(), m_ShadowSampler, nullptr);
            m_ShadowSampler = VK_NULL_HANDLE;
        }

        if (m_ShadowImageView)
        {
            vkDestroyImageView(m_Context->GetDevice(), m_ShadowImageView, nullptr);
            m_ShadowImageView = VK_NULL_HANDLE;
        }

        if (m_ShadowImage)
        {
            vkDestroyImage(m_Context->GetDevice(), m_ShadowImage, nullptr);
            m_ShadowImage = VK_NULL_HANDLE;
        }

        if (m_ShadowImageMemory)
        {
            vkFreeMemory(m_Context->GetDevice(), m_ShadowImageMemory, nullptr);
            m_ShadowImageMemory = VK_NULL_HANDLE;
        }

        if (m_ShadowFramebuffer)
        {
            vkDestroyFramebuffer(m_Context->GetDevice(), m_ShadowFramebuffer, nullptr);
            m_ShadowFramebuffer = VK_NULL_HANDLE;
        }

        if (m_ShadowRenderPass)
        {
            vkDestroyRenderPass(m_Context->GetDevice(), m_ShadowRenderPass, nullptr);
            m_ShadowRenderPass = VK_NULL_HANDLE;
        }

        if (m_PipelineLayout)
        {
            vkDestroyPipelineLayout(m_Context->GetDevice(), m_PipelineLayout, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
            TR_CORE_TRACE("Pipeline layout destroyed");
        }

        if (m_DescriptorPool)
        {
            vkDestroyDescriptorPool(m_Context->GetDevice(), m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
            TR_CORE_TRACE("Descriptor pool destroyed");
        }

        if (m_DescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(m_Context->GetDevice(), m_DescriptorSetLayout, nullptr);
            m_DescriptorSetLayout = VK_NULL_HANDLE;
            TR_CORE_TRACE("Descriptor set layout destroyed");
        }

        if (m_RenderPass)
        {
            vkDestroyRenderPass(m_Context->GetDevice(), m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
            TR_CORE_TRACE("Render pass destroyed");
        }

        m_VertexBuffer.Destroy();
        m_IndexBuffer.Destroy();

        for (auto& it_Frame : m_Frames)
        {
            it_Frame.GlobalUniform.Destroy();
            it_Frame.LightUniform.Destroy();
            it_Frame.MaterialUniform.Destroy();
        }
        m_Frames.clear();

        m_Texture.Destroy();

        TR_CORE_INFO("-------RENDERER SHUTDOWN COMPLETE-------");
    }

    void Renderer::DrawFrame(const std::function<void(VkCommandBuffer)>& recordCallback)
    {
        if (m_Shader.Update())
        {
            vkDeviceWaitIdle(m_Context->GetDevice());
            m_GraphicsPipeline = VK_NULL_HANDLE;
            CreateGraphicsPipeline(m_PrimitiveTopology);
        }

        vkWaitForFences(m_Context->GetDevice(), 1, &m_InFlightFence[m_CurrentFrame], VK_TRUE, UINT64_MAX);

        uint32_t l_ImageIndex;
        VkResult l_Result = vkAcquireNextImageKHR(m_Context->GetDevice(), m_Context->GetSwapChain(), UINT64_MAX, m_ImageAvailableSemaphore[m_CurrentFrame], VK_NULL_HANDLE, &l_ImageIndex);

        if (l_Result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapChain();

            return;
        }

        else if (l_Result != VK_SUCCESS && l_Result != VK_SUBOPTIMAL_KHR)
        {
            TR_CORE_ERROR("Failed to acquire swap chain image!");
        }

        if (m_ImagesInFlight[l_ImageIndex] != VK_NULL_HANDLE)
        {
            vkWaitForFences(m_Context->GetDevice(), 1, &m_ImagesInFlight[l_ImageIndex], VK_TRUE, UINT64_MAX);
        }

        m_ImagesInFlight[l_ImageIndex] = m_InFlightFence[m_CurrentFrame];

        float l_AspectRatio = static_cast<float>(m_Context->GetSwapChainExtent().width) / static_cast<float>(m_Context->GetSwapChainExtent().height);
        if (m_Camera.GetProjectionType() == Camera::ProjectionType::Perspective)
        {
            m_Camera.SetPerspective(m_Camera.GetFov(), l_AspectRatio, m_Camera.GetNear(), m_Camera.GetFar());
        }

        else
        {
            m_Camera.SetOrthographic(m_Camera.GetOrthoSize(), l_AspectRatio, m_Camera.GetNear(), m_Camera.GetFar());
        }

        vkResetFences(m_Context->GetDevice(), 1, &m_InFlightFence[m_CurrentFrame]);

        vkResetCommandBuffer(m_CommandBuffer[l_ImageIndex], 0);
        VkCommandBufferBeginInfo l_BeginInfo{};
        l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        l_BeginInfo.flags = 0;
        l_BeginInfo.pInheritanceInfo = nullptr;
        if (vkBeginCommandBuffer(m_CommandBuffer[l_ImageIndex], &l_BeginInfo) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to begin recording command buffer");

            return;
        }

        m_RenderGraph.Clear();
        m_RenderGraph.AddPass("Shadow", [this, l_ImageIndex]() { RenderShadowPass(l_ImageIndex); }, true);
        m_RenderGraph.AddPass("Geometry", [this, l_ImageIndex, &recordCallback]() { RenderMainPass(l_ImageIndex, recordCallback); });
        m_RenderGraph.AddPass("Bloom", [this]() { m_BloomPass.Execute(); }, true);
        m_RenderGraph.AddPass("ToneMapping", [this]() { m_ToneMappingPass.Execute(); }, true);
        m_RenderGraph.Execute();

        if (vkEndCommandBuffer(m_CommandBuffer[l_ImageIndex]) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to record command buffer");

            return;
        }

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore l_WaitSemaphores[] = { m_ImageAvailableSemaphore[m_CurrentFrame] };
        VkPipelineStageFlags l_WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        l_SubmitInfo.waitSemaphoreCount = 1;
        l_SubmitInfo.pWaitSemaphores = l_WaitSemaphores;
        l_SubmitInfo.pWaitDstStageMask = l_WaitStages;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &m_CommandBuffer[l_ImageIndex];

        VkSemaphore l_SignalSemaphores[] = { m_RenderFinshedSemaphore[l_ImageIndex] };
        l_SubmitInfo.signalSemaphoreCount = 1;
        l_SubmitInfo.pSignalSemaphores = l_SignalSemaphores;

        if (vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &l_SubmitInfo, m_InFlightFence[m_CurrentFrame]) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to submit draw command buffer!");
        }

        VkSwapchainKHR l_Spawchain = m_Context->GetSwapChain();

        VkPresentInfoKHR l_PresentInfo{};
        l_PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        l_PresentInfo.waitSemaphoreCount = 1;
        l_PresentInfo.pWaitSemaphores = l_SignalSemaphores;
        l_PresentInfo.swapchainCount = 1;
        l_PresentInfo.pSwapchains = &l_Spawchain;
        l_PresentInfo.pImageIndices = &l_ImageIndex;

        VkResult l_PresentResult = vkQueuePresentKHR(m_Context->GetPresentQueue(), &l_PresentInfo);
        if (l_PresentResult == VK_ERROR_OUT_OF_DATE_KHR || l_PresentResult == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapChain();

            return;
        }

        else if (l_PresentResult != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to present swap chain image!");
        }

        m_LastImageIndex = l_ImageIndex;
        m_CurrentFrame = (m_CurrentFrame + 1) % m_InFlightFence.size();
    }

    void Renderer::OnWindowResize()
    {
        RecreateSwapChain();
    }

    ImTextureID Renderer::GetViewportImage()
    {
        if (m_ImGuiImageDescriptors.empty())
        {
            CreateImGuiImageDescriptors();
        }
        
        if (m_ImGuiImageDescriptors.empty())
        {
            return 0;
        }
        
        return (ImTextureID)m_ImGuiImageDescriptors[m_LastImageIndex];
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

        VkAttachmentDescription l_DepthAttachment{};
        l_DepthAttachment.format = FindDepthFormat();
        l_DepthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        l_DepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        l_DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        l_DepthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        l_DepthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        l_DepthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_DepthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference l_DepthAttachmentRef{};
        l_DepthAttachmentRef.attachment = 1;
        l_DepthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription l_Subpass{};
        l_Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        l_Subpass.colorAttachmentCount = 1;
        l_Subpass.pColorAttachments = &l_ColorAttachmentRef;
        l_Subpass.pDepthStencilAttachment = &l_DepthAttachmentRef;

        std::array<VkAttachmentDescription, 2> l_Attachments{ l_ColorAttachment, l_DepthAttachment };

        VkRenderPassCreateInfo l_RenderPassInfo{};
        l_RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        l_RenderPassInfo.attachmentCount = static_cast<uint32_t>(l_Attachments.size());
        l_RenderPassInfo.pAttachments = l_Attachments.data();
        l_RenderPassInfo.subpassCount = 1;
        l_RenderPassInfo.pSubpasses = &l_Subpass;

        VkSubpassDependency l_Dependency{};
        l_Dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        l_Dependency.dstSubpass = 0;
        l_Dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        l_Dependency.srcAccessMask = 0;
        l_Dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        l_Dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        l_RenderPassInfo.dependencyCount = 1;
        l_RenderPassInfo.pDependencies = &l_Dependency;

        if (vkCreateRenderPass(m_Context->GetDevice(), &l_RenderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create render pass");
        }

        TR_CORE_TRACE("Render pass created");
    }

    void Renderer::CreateDescriptorSetLayout()
    {
        TR_CORE_TRACE("Creating descriptor set layout");

        VkDescriptorSetLayoutBinding l_LayoutBinding{};
        l_LayoutBinding.binding = 0;
        l_LayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_LayoutBinding.descriptorCount = 1;
        l_LayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        l_LayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding l_SamplerBinding{};
        l_SamplerBinding.binding = static_cast<uint32_t>(TextureSlot::Albedo);
        l_SamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_SamplerBinding.descriptorCount = 1;
        l_SamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_SamplerBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding l_NormalBinding{};
        l_NormalBinding.binding = static_cast<uint32_t>(TextureSlot::Normal);
        l_NormalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_NormalBinding.descriptorCount = 1;
        l_NormalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_NormalBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding l_RoughnessBinding{};
        l_RoughnessBinding.binding = static_cast<uint32_t>(TextureSlot::Roughness);
        l_RoughnessBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_RoughnessBinding.descriptorCount = 1;
        l_RoughnessBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_RoughnessBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding l_MetallicBinding{};
        l_MetallicBinding.binding = static_cast<uint32_t>(TextureSlot::Metallic);
        l_MetallicBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_MetallicBinding.descriptorCount = 1;
        l_MetallicBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_MetallicBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding l_LightBinding{};
        l_LightBinding.binding = 2;
        l_LightBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_LightBinding.descriptorCount = 1;
        l_LightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_LightBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding l_MaterialBinding{};
        l_MaterialBinding.binding = 3;
        l_MaterialBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_MaterialBinding.descriptorCount = 1;
        l_MaterialBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_MaterialBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding l_ShadowBinding{};
        l_ShadowBinding.binding = 4;
        l_ShadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_ShadowBinding.descriptorCount = 1;
        l_ShadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_ShadowBinding.pImmutableSamplers = nullptr;

        std::array<VkDescriptorSetLayoutBinding, 8> l_Bindings{ l_LayoutBinding, l_SamplerBinding, l_LightBinding, l_MaterialBinding, l_ShadowBinding, 
                                                                l_NormalBinding, l_RoughnessBinding, l_MetallicBinding };

        VkDescriptorSetLayoutCreateInfo l_CreateInfo{};
        l_CreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        l_CreateInfo.bindingCount = static_cast<uint32_t>(l_Bindings.size());
        l_CreateInfo.pBindings = l_Bindings.data();

        if (vkCreateDescriptorSetLayout(m_Context->GetDevice(), &l_CreateInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create descriptor set layout");

            return;
        }

        TR_CORE_TRACE("Descriptor set layout created");
    }

    void Renderer::CreateDescriptorPool()
    {
        TR_CORE_TRACE("Creating descriptor pool");

        auto a_SwapChainImages = m_Context->GetSwapChainImages();
        std::array<VkDescriptorPoolSize, 2> l_PoolSizes{};
        l_PoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_PoolSizes[0].descriptorCount = static_cast<uint32_t>(a_SwapChainImages.size()) * 3;
        l_PoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_PoolSizes[1].descriptorCount = static_cast<uint32_t>(a_SwapChainImages.size()) * 5;

        VkDescriptorPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        l_PoolInfo.poolSizeCount = static_cast<uint32_t>(l_PoolSizes.size());
        l_PoolInfo.pPoolSizes = l_PoolSizes.data();
        l_PoolInfo.maxSets = static_cast<uint32_t>(a_SwapChainImages.size());

        if (vkCreateDescriptorPool(m_Context->GetDevice(), &l_PoolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create descriptor pool");

            return;
        }

        TR_CORE_TRACE("Descriptor pool created");
    }

    void Renderer::CreateDescriptorSets()
    {
        TR_CORE_TRACE("Creating descriptor sets");

        auto a_SwapChainImages = m_Context->GetSwapChainImages();
        std::vector<VkDescriptorSetLayout> l_Layouts(a_SwapChainImages.size(), m_DescriptorSetLayout);

        VkDescriptorSetAllocateInfo l_AllocInfo{};
        l_AllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        l_AllocInfo.descriptorPool = m_DescriptorPool;
        l_AllocInfo.descriptorSetCount = static_cast<uint32_t>(a_SwapChainImages.size());
        l_AllocInfo.pSetLayouts = l_Layouts.data();

        std::vector<VkDescriptorSet> l_Sets(a_SwapChainImages.size());
        if (vkAllocateDescriptorSets(m_Context->GetDevice(), &l_AllocInfo, l_Sets.data()) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to allocate descriptor sets");

            return;
        }

        for (size_t i = 0; i < a_SwapChainImages.size(); ++i)
        {
            m_Frames[i].DescriptorSet = l_Sets[i];

            VkDescriptorBufferInfo l_BufferInfo{};
            l_BufferInfo.buffer = m_Frames[i].GlobalUniform.GetBuffer();
            l_BufferInfo.offset = 0;
            l_BufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorBufferInfo l_LightBufferInfo{};
            l_LightBufferInfo.buffer = m_Frames[i].LightUniform.GetBuffer();
            l_LightBufferInfo.offset = 0;
            l_LightBufferInfo.range = sizeof(LightBufferObject);

            VkDescriptorBufferInfo l_MaterialBufferInfo{};
            l_MaterialBufferInfo.buffer = m_Frames[i].MaterialUniform.GetBuffer();
            l_MaterialBufferInfo.offset = 0;
            l_MaterialBufferInfo.range = sizeof(MaterialBufferObject);

            VkDescriptorImageInfo l_ImageInfo{};
            l_ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_ImageInfo.imageView = m_Texture.GetImageView();
            l_ImageInfo.sampler = m_Texture.GetSampler();

            VkDescriptorImageInfo l_ShadowInfo{};
            l_ShadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            l_ShadowInfo.imageView = m_ShadowImageView;
            l_ShadowInfo.sampler = m_ShadowSampler;

            VkDescriptorImageInfo l_NormalInfo = l_ImageInfo;
            VkDescriptorImageInfo l_RoughnessInfo = l_ImageInfo;
            VkDescriptorImageInfo l_MetallicInfo = l_ImageInfo;

            std::array<VkWriteDescriptorSet, 8> l_DescriptorWrites{};
            l_DescriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_DescriptorWrites[0].dstSet = m_Frames[i].DescriptorSet;
            l_DescriptorWrites[0].dstBinding = 0;
            l_DescriptorWrites[0].dstArrayElement = 0;
            l_DescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_DescriptorWrites[0].descriptorCount = 1;
            l_DescriptorWrites[0].pBufferInfo = &l_BufferInfo;

            l_DescriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_DescriptorWrites[1].dstSet = m_Frames[i].DescriptorSet;
            l_DescriptorWrites[1].dstBinding = static_cast<uint32_t>(TextureSlot::Albedo);
            l_DescriptorWrites[1].dstArrayElement = 0;
            l_DescriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_DescriptorWrites[1].descriptorCount = 1;
            l_DescriptorWrites[1].pImageInfo = &l_ImageInfo;

            l_DescriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_DescriptorWrites[2].dstSet = m_Frames[i].DescriptorSet;
            l_DescriptorWrites[2].dstBinding = 2;
            l_DescriptorWrites[2].dstArrayElement = 0;
            l_DescriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_DescriptorWrites[2].descriptorCount = 1;
            l_DescriptorWrites[2].pBufferInfo = &l_LightBufferInfo;

            l_DescriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_DescriptorWrites[3].dstSet = m_Frames[i].DescriptorSet;
            l_DescriptorWrites[3].dstBinding = 3;
            l_DescriptorWrites[3].dstArrayElement = 0;
            l_DescriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_DescriptorWrites[3].descriptorCount = 1;
            l_DescriptorWrites[3].pBufferInfo = &l_MaterialBufferInfo;

            l_DescriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_DescriptorWrites[4].dstSet = m_Frames[i].DescriptorSet;
            l_DescriptorWrites[4].dstBinding = 4;
            l_DescriptorWrites[4].dstArrayElement = 0;
            l_DescriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_DescriptorWrites[4].descriptorCount = 1;
            l_DescriptorWrites[4].pImageInfo = &l_ShadowInfo;

            l_DescriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_DescriptorWrites[5].dstSet = m_Frames[i].DescriptorSet;
            l_DescriptorWrites[5].dstBinding = static_cast<uint32_t>(TextureSlot::Normal);
            l_DescriptorWrites[5].dstArrayElement = 0;
            l_DescriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_DescriptorWrites[5].descriptorCount = 1;
            l_DescriptorWrites[5].pImageInfo = &l_NormalInfo;

            l_DescriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_DescriptorWrites[6].dstSet = m_Frames[i].DescriptorSet;
            l_DescriptorWrites[6].dstBinding = static_cast<uint32_t>(TextureSlot::Roughness);
            l_DescriptorWrites[6].dstArrayElement = 0;
            l_DescriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_DescriptorWrites[6].descriptorCount = 1;
            l_DescriptorWrites[6].pImageInfo = &l_RoughnessInfo;

            l_DescriptorWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_DescriptorWrites[7].dstSet = m_Frames[i].DescriptorSet;
            l_DescriptorWrites[7].dstBinding = static_cast<uint32_t>(TextureSlot::Metallic);
            l_DescriptorWrites[7].dstArrayElement = 0;
            l_DescriptorWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_DescriptorWrites[7].descriptorCount = 1;
            l_DescriptorWrites[7].pImageInfo = &l_MetallicInfo;

            vkUpdateDescriptorSets(m_Context->GetDevice(), static_cast<uint32_t>(l_DescriptorWrites.size()), l_DescriptorWrites.data(), 0, nullptr);
        }

        TR_CORE_TRACE("Descriptor sets created");
    }

    void Renderer::CreateGraphicsPipeline(VkPrimitiveTopology topology)
    {
        TR_CORE_TRACE("Creating graphics pipeline");

        if (!m_Context)
        {
            TR_CORE_ERROR("Vulkan context is not set");

            return;
        }

        if (m_Shader.GetModule(VK_SHADER_STAGE_VERTEX_BIT) == VK_NULL_HANDLE || m_Shader.GetModule(VK_SHADER_STAGE_FRAGMENT_BIT) == VK_NULL_HANDLE)
        {
            if (!m_Shader.Load("Assets/Shaders/Simple.vert", VK_SHADER_STAGE_VERTEX_BIT) || !m_Shader.Load("Assets/Shaders/Simple.frag", VK_SHADER_STAGE_FRAGMENT_BIT))
            {
                return;
            }
        }

        auto a_BindingDescription = Vertex::GetBindingDescription();
        auto a_AttributeDescriptions = Vertex::GetAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo it_VertexInputInfo{};
        it_VertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        it_VertexInputInfo.vertexBindingDescriptionCount = 1;
        it_VertexInputInfo.pVertexBindingDescriptions = &a_BindingDescription;
        it_VertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(a_AttributeDescriptions.size());
        it_VertexInputInfo.pVertexAttributeDescriptions = a_AttributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo l_InputAssembly{};
        l_InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        l_InputAssembly.topology = topology;
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

        VkPipelineDepthStencilStateCreateInfo l_DepthStencil{};
        l_DepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        l_DepthStencil.depthTestEnable = VK_TRUE;
        l_DepthStencil.depthWriteEnable = VK_TRUE;
        l_DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        l_DepthStencil.depthBoundsTestEnable = VK_FALSE;
        l_DepthStencil.stencilTestEnable = VK_FALSE;

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
        l_PipelineLayoutInfo.setLayoutCount = 1;
        l_PipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
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
        l_PipelineInfo.pVertexInputState = &it_VertexInputInfo;
        l_PipelineInfo.pInputAssemblyState = &l_InputAssembly;
        l_PipelineInfo.pViewportState = &l_ViewportState;
        l_PipelineInfo.pRasterizationState = &l_Rasterizer;
        l_PipelineInfo.pMultisampleState = &l_Multisampling;
        l_PipelineInfo.pDepthStencilState = &l_DepthStencil;
        l_PipelineInfo.pColorBlendState = &l_ColorBlending;
        l_PipelineInfo.pDynamicState = &l_DynamicStateInfo;
        l_PipelineInfo.layout = m_PipelineLayout;
        l_PipelineInfo.renderPass = m_RenderPass;
        l_PipelineInfo.subpass = 0;
        l_PipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        l_PipelineInfo.basePipelineIndex = -1;

        uint32_t l_MaxLights = MaxLights;
        m_GraphicsPipeline = m_Shader.GetPipeline(l_PipelineInfo, &l_MaxLights, sizeof(l_MaxLights));

        if (m_GraphicsPipeline == VK_NULL_HANDLE)
        {
            TR_CORE_ERROR("Failed to create graphics pipeline");

            return;
        }

        TR_CORE_TRACE("Graphics pipeline created");
    }

    void Renderer::CreateDepthResources()
    {
        TR_CORE_TRACE("Creating depth resources");

        VkFormat l_DepthFormat = FindDepthFormat();

        m_DepthImages.resize(m_Context->GetSwapChainImages().size());
        m_DepthImageMemory.resize(m_Context->GetSwapChainImages().size());
        m_DepthImageViews.resize(m_Context->GetSwapChainImages().size());

        for (size_t i = 0; i < m_DepthImages.size(); ++i)
        {
            VkImageCreateInfo l_ImageInfo{};
            l_ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
            l_ImageInfo.extent.width = m_Context->GetSwapChainExtent().width;
            l_ImageInfo.extent.height = m_Context->GetSwapChainExtent().height;
            l_ImageInfo.extent.depth = 1;
            l_ImageInfo.mipLevels = 1;
            l_ImageInfo.arrayLayers = 1;
            l_ImageInfo.format = l_DepthFormat;
            l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            l_ImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(m_Context->GetDevice(), &l_ImageInfo, nullptr, &m_DepthImages[i]) != VK_SUCCESS)
            {
                TR_CORE_ERROR("Failed to create depth image");

                return;
            }

            VkMemoryRequirements l_MemoryRequirments{};
            vkGetImageMemoryRequirements(m_Context->GetDevice(), m_DepthImages[i], &l_MemoryRequirments);

            VkMemoryAllocateInfo l_AllocateInfo{};
            l_AllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            l_AllocateInfo.allocationSize = l_MemoryRequirments.size;
            l_AllocateInfo.memoryTypeIndex = m_Context->FindMemoryType(l_MemoryRequirments.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(m_Context->GetDevice(), &l_AllocateInfo, nullptr, &m_DepthImageMemory[i]) != VK_SUCCESS)
            {
                TR_CORE_ERROR("Failed to allocate depth image memory");

                return;
            }

            vkBindImageMemory(m_Context->GetDevice(), m_DepthImages[i], m_DepthImageMemory[i], 0);

            VkImageViewCreateInfo l_ViewInfo{};
            l_ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            l_ViewInfo.image = m_DepthImages[i];
            l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            l_ViewInfo.format = l_DepthFormat;
            l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (HasStencilComponent(l_DepthFormat))
            {
                l_ViewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            l_ViewInfo.subresourceRange.baseMipLevel = 0;
            l_ViewInfo.subresourceRange.levelCount = 1;
            l_ViewInfo.subresourceRange.baseArrayLayer = 0;
            l_ViewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(m_Context->GetDevice(), &l_ViewInfo, nullptr, &m_DepthImageViews[i]) != VK_SUCCESS)
            {
                TR_CORE_ERROR("Failed to create depth image view");

                return;
            }
        }

        QueueFamilyIndices l_Indices = m_Context->FindQueueFamilies(m_Context->GetPhysicalDevice());

        VkCommandPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        l_PoolInfo.queueFamilyIndex = l_Indices.GraphicsFamily.value();
        l_PoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        VkCommandPool l_CommandPool = VK_NULL_HANDLE;
        vkCreateCommandPool(m_Context->GetDevice(), &l_PoolInfo, nullptr, &l_CommandPool);

        VkCommandBufferAllocateInfo l_AllocInfoCB{};
        l_AllocInfoCB.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocInfoCB.commandPool = l_CommandPool;
        l_AllocInfoCB.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocInfoCB.commandBufferCount = 1;

        VkCommandBuffer l_CommandBuffer = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_Context->GetDevice(), &l_AllocInfoCB, &l_CommandBuffer);

        VkCommandBufferBeginInfo l_BeginInfo{};
        l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        l_BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        for (size_t i = 0; i < m_DepthImages.size(); ++i)
        {
            VkImageMemoryBarrier l_Barrier{};
            l_Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            l_Barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            l_Barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            l_Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_Barrier.image = m_DepthImages[i];
            l_Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (HasStencilComponent(l_DepthFormat))
            {
                l_Barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            l_Barrier.subresourceRange.baseMipLevel = 0;
            l_Barrier.subresourceRange.levelCount = 1;
            l_Barrier.subresourceRange.baseArrayLayer = 0;
            l_Barrier.subresourceRange.layerCount = 1;
            l_Barrier.srcAccessMask = 0;
            l_Barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_Barrier);
        }

        vkEndCommandBuffer(l_CommandBuffer);

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;

        vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &l_SubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GetGraphicsQueue());

        vkFreeCommandBuffers(m_Context->GetDevice(), l_CommandPool, 1, &l_CommandBuffer);
        vkDestroyCommandPool(m_Context->GetDevice(), l_CommandPool, nullptr);

        TR_CORE_TRACE("Depth resources created");
    }

    void Renderer::CreateFramebuffers()
    {
        TR_CORE_TRACE("Creating framebuffers");

        m_Framebuffers.resize(m_Context->GetSwapChainImages().size());

        for (size_t i = 0; i < m_Context->GetSwapChainImages().size(); i++)
        {
            VkImageView l_Attachments[] = { m_Context->GetSwapChainImages()[i], m_DepthImageViews[i] };

            VkFramebufferCreateInfo l_FramebufferInfo{};
            l_FramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            l_FramebufferInfo.renderPass = m_RenderPass;
            l_FramebufferInfo.attachmentCount = 2;
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

        QueueFamilyIndices l_QueueFamilyIndices = m_Context->FindQueueFamilies(m_Context->GetPhysicalDevice());

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

    void Renderer::CreateTextureImage()
    {
        TR_CORE_TRACE("Loading texture");

        m_Texture = Texture(m_Context);
        if (auto a_Error = m_Texture.LoadFromFile("Assets/Textures/Checkers.png", 0, 0))
        {
            TR_CORE_ERROR("{}", *a_Error);
        }

        TR_CORE_TRACE("Texture loaded");
    }

    void Renderer::CreateVertexBuffer()
    {
        TR_CORE_TRACE("Creating vertex buffer");

        std::vector<Vertex> l_Vertices = 
        {
            { { 0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.5f, 1.0f} },
            { { 0.5f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} },
            { {-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f} } 
        };

        m_VertexBuffer = VertexBuffer(m_Context);
        if (auto a_Error = m_VertexBuffer.Create(l_Vertices))
        {
            TR_CORE_ERROR("{}", *a_Error);
        }

        TR_CORE_TRACE("Vertex buffer created");
    }

    void Renderer::CreateIndexBuffer()
    {
        TR_CORE_TRACE("Creating index buffer");

        std::vector<uint32_t> l_Indices = { 0, 1, 2 };

        m_IndexBuffer = IndexBuffer(m_Context);
        if (auto a_Error = m_IndexBuffer.Create(l_Indices))
        {
            TR_CORE_ERROR("{}", *a_Error);
        }

        TR_CORE_TRACE("Index buffer created");
    }

    void Renderer::CreateUniformBuffers()
    {
        TR_CORE_TRACE("Creating uniform buffers");

        VkDeviceSize l_BufferSize = sizeof(UniformBufferObject);
        VkDeviceSize l_LightBufferSize = sizeof(LightBufferObject);
        VkDeviceSize l_MaterialBufferSize = sizeof(MaterialBufferObject);

        auto a_SwapChainImages = m_Context->GetSwapChainImages();

        m_Frames.resize(a_SwapChainImages.size());

        for (size_t i = 0; i < a_SwapChainImages.size(); ++i)
        {
            m_Frames[i].GlobalUniform = UniformBuffer(m_Context);
            m_Frames[i].LightUniform = UniformBuffer(m_Context);
            m_Frames[i].MaterialUniform = UniformBuffer(m_Context);

            if (auto a_Error = m_Frames[i].GlobalUniform.Create(l_BufferSize))
            {
                TR_CORE_ERROR("{}", *a_Error);
            }

            if (auto a_Error = m_Frames[i].LightUniform.Create(l_LightBufferSize))
            {
                TR_CORE_ERROR("{}", *a_Error);
            }

            if (auto a_Error = m_Frames[i].MaterialUniform.Create(l_MaterialBufferSize))
            {
                TR_CORE_ERROR("{}", *a_Error);
            }
        }

        TR_CORE_TRACE("Uniform buffers created: {}", m_Frames.size());
    }

    void Renderer::CreateShadowResources()
    {
        TR_CORE_TRACE("Creating shadow resources");

        VkFormat l_DepthFormat = FindDepthFormat();

        VkImageCreateInfo l_ImageInfo{};
        l_ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = m_ShadowMapSize;
        l_ImageInfo.extent.height = m_ShadowMapSize;
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = 1;
        l_ImageInfo.arrayLayers = 1;
        l_ImageInfo.format = l_DepthFormat;
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(m_Context->GetDevice(), &l_ImageInfo, nullptr, &m_ShadowImage);

        VkMemoryRequirements l_MemoryRequirment{};
        vkGetImageMemoryRequirements(m_Context->GetDevice(), m_ShadowImage, &l_MemoryRequirment);

        VkMemoryAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        l_AllocateInfo.allocationSize = l_MemoryRequirment.size;
        l_AllocateInfo.memoryTypeIndex = m_Context->FindMemoryType(l_MemoryRequirment.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(m_Context->GetDevice(), &l_AllocateInfo, nullptr, &m_ShadowImageMemory);
        vkBindImageMemory(m_Context->GetDevice(), m_ShadowImage, m_ShadowImageMemory, 0);

        VkImageViewCreateInfo l_ViewInfo{};
        l_ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        l_ViewInfo.image = m_ShadowImage;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_ViewInfo.format = l_DepthFormat;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = 1;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(m_Context->GetDevice(), &l_ViewInfo, nullptr, &m_ShadowImageView);

        // Transition layout
        QueueFamilyIndices l_Indices = m_Context->FindQueueFamilies(m_Context->GetPhysicalDevice());

        VkCommandPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        l_PoolInfo.queueFamilyIndex = l_Indices.GraphicsFamily.value();
        l_PoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        VkCommandPool l_CommandPool = VK_NULL_HANDLE;
        vkCreateCommandPool(m_Context->GetDevice(), &l_PoolInfo, nullptr, &l_CommandPool);

        VkCommandBufferAllocateInfo l_CommandBufferAllocateInfo{};
        l_CommandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_CommandBufferAllocateInfo.commandPool = l_CommandPool;
        l_CommandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_CommandBufferAllocateInfo.commandBufferCount = 1;

        VkCommandBuffer l_CommandBuffer = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_Context->GetDevice(), &l_CommandBufferAllocateInfo, &l_CommandBuffer);

        VkCommandBufferBeginInfo l_BeginInfo{};
        l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        l_BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        VkImageMemoryBarrier l_MemoryBarrier{};
        l_MemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        l_MemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_MemoryBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        l_MemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_MemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_MemoryBarrier.image = m_ShadowImage;
        l_MemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        l_MemoryBarrier.subresourceRange.baseMipLevel = 0;
        l_MemoryBarrier.subresourceRange.levelCount = 1;
        l_MemoryBarrier.subresourceRange.baseArrayLayer = 0;
        l_MemoryBarrier.subresourceRange.layerCount = 1;
        l_MemoryBarrier.srcAccessMask = 0;
        l_MemoryBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_MemoryBarrier);

        vkEndCommandBuffer(l_CommandBuffer);

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;

        vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &l_SubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GetGraphicsQueue());

        vkFreeCommandBuffers(m_Context->GetDevice(), l_CommandPool, 1, &l_CommandBuffer);
        vkDestroyCommandPool(m_Context->GetDevice(), l_CommandPool, nullptr);

        VkSamplerCreateInfo l_SamplerInfo{};
        l_SamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        l_SamplerInfo.compareEnable = VK_TRUE;
        l_SamplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        vkCreateSampler(m_Context->GetDevice(), &l_SamplerInfo, nullptr, &m_ShadowSampler);

        VkAttachmentDescription l_DepthAttachment{};
        l_DepthAttachment.format = l_DepthFormat;
        l_DepthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        l_DepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        l_DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        l_DepthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        l_DepthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        l_DepthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_DepthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference l_DepthRefference{};
        l_DepthRefference.attachment = 0;
        l_DepthRefference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription l_Subpass{};
        l_Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        l_Subpass.colorAttachmentCount = 0;
        l_Subpass.pDepthStencilAttachment = &l_DepthRefference;

        VkRenderPassCreateInfo l_RenderPassInfo{};
        l_RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        l_RenderPassInfo.attachmentCount = 1;
        l_RenderPassInfo.pAttachments = &l_DepthAttachment;
        l_RenderPassInfo.subpassCount = 1;
        l_RenderPassInfo.pSubpasses = &l_Subpass;
        vkCreateRenderPass(m_Context->GetDevice(), &l_RenderPassInfo, nullptr, &m_ShadowRenderPass);

        VkFramebufferCreateInfo l_FrameBufferInfo{};
        l_FrameBufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        l_FrameBufferInfo.renderPass = m_ShadowRenderPass;
        l_FrameBufferInfo.attachmentCount = 1;
        l_FrameBufferInfo.pAttachments = &m_ShadowImageView;
        l_FrameBufferInfo.width = m_ShadowMapSize;
        l_FrameBufferInfo.height = m_ShadowMapSize;
        l_FrameBufferInfo.layers = 1;
        vkCreateFramebuffer(m_Context->GetDevice(), &l_FrameBufferInfo, nullptr, &m_ShadowFramebuffer);

        TR_CORE_TRACE("Shadow resources created");
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
        TR_CORE_TRACE("Creating synchronization objects");

        size_t l_SwapChainImageCount = m_Context->GetSwapChainImages().size();
        m_ImageAvailableSemaphore.resize(l_SwapChainImageCount);
        m_RenderFinshedSemaphore.resize(l_SwapChainImageCount);
        m_InFlightFence.resize(l_SwapChainImageCount);
        m_ImagesInFlight.resize(m_Context->GetSwapChainImages().size(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo l_SemaphoreInfo{};
        l_SemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo l_FenceInfo{};
        l_FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        l_FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < l_SwapChainImageCount; ++i)
        {
            if (vkCreateSemaphore(m_Context->GetDevice(), &l_SemaphoreInfo, nullptr, &m_ImageAvailableSemaphore[i]) != VK_SUCCESS ||
                vkCreateSemaphore(m_Context->GetDevice(), &l_SemaphoreInfo, nullptr, &m_RenderFinshedSemaphore[i]) != VK_SUCCESS)
            {
                TR_CORE_ERROR("Failed to create sync objects for frame {}", i);
            }
        }
        TR_CORE_TRACE("Semaphores created");

        for (size_t i = 0; i < l_SwapChainImageCount; ++i)
        {
            if (vkCreateFence(m_Context->GetDevice(), &l_FenceInfo, nullptr, &m_InFlightFence[i]) != VK_SUCCESS)
            {
                TR_CORE_ERROR("Failed to create fence for frame {}", i);
            }
        }
        TR_CORE_TRACE("Fences created");

        TR_CORE_TRACE("Synchronization objects created");
    }

    void Renderer::RenderShadowPass(uint32_t imageIndex)
    {
        VkRenderPassBeginInfo l_RenderPassInfo{};
        l_RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        l_RenderPassInfo.renderPass = m_ShadowRenderPass;
        l_RenderPassInfo.framebuffer = m_ShadowFramebuffer;
        l_RenderPassInfo.renderArea.offset = { 0, 0 };
        l_RenderPassInfo.renderArea.extent = { m_ShadowMapSize, m_ShadowMapSize };

        VkClearValue l_Clear{};
        l_Clear.depthStencil = { 1.0f, 0 };
        l_RenderPassInfo.clearValueCount = 1;
        l_RenderPassInfo.pClearValues = &l_Clear;

        vkCmdBeginRenderPass(m_CommandBuffer[imageIndex], &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(m_CommandBuffer[imageIndex]);
    }

    void Renderer::RenderMainPass(uint32_t imageIndex, const std::function<void(VkCommandBuffer)>& recordCallback)
    {
        VkRenderPassBeginInfo l_RenderPassInfo{};
        l_RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        l_RenderPassInfo.renderPass = m_RenderPass;
        l_RenderPassInfo.framebuffer = m_Framebuffers[imageIndex];
        l_RenderPassInfo.renderArea.offset = { 0, 0 };
        l_RenderPassInfo.renderArea.extent = m_Context->GetSwapChainExtent();

        std::array<VkClearValue, 2> l_ClearValues{};
        l_ClearValues[0].color = { {0.01f, 0.01f, 0.01f, 1.0f} };
        l_ClearValues[1].depthStencil = { 1.0f, 0 };
        l_RenderPassInfo.clearValueCount = static_cast<uint32_t>(l_ClearValues.size());
        l_RenderPassInfo.pClearValues = l_ClearValues.data();

        vkCmdBeginRenderPass(m_CommandBuffer[imageIndex], &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        if (m_GraphicsPipeline != VK_NULL_HANDLE)
        {
            vkCmdBindPipeline(m_CommandBuffer[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);
        }

        else
        {
            TR_CORE_ERROR("Graphics pipeline not created");
            vkCmdEndRenderPass(m_CommandBuffer[imageIndex]);

            return;
        }

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

        if (m_Scene)
        {
            Culling::Frustum l_Frustum = Culling::CreateFrustum(m_Camera.GetProjectionMatrix() * m_Camera.GetViewMatrix());

            LightBufferObject l_Light{};
            auto a_LightView = m_Scene->GetRegistry().view<LightComponent>();
            for (auto it_Entity : a_LightView)
            {
                if (l_Light.LightCount >= static_cast<int>(MaxLights))
                {
                    break;
                }
                auto& a_Light = a_LightView.get<LightComponent>(it_Entity);
                l_Light.Lights[l_Light.LightCount].Position = a_Light.Position;
                l_Light.Lights[l_Light.LightCount].Color = a_Light.Color;
                l_Light.Lights[l_Light.LightCount].Intensity = a_Light.Intensity;
                l_Light.Lights[l_Light.LightCount].Type = static_cast<int>(a_Light.LightType);
                ++l_Light.LightCount;
            }

            void* l_LightData = m_Frames[imageIndex].LightUniform.Map();
            std::memcpy(l_LightData, &l_Light, sizeof(l_Light));
            m_Frames[imageIndex].LightUniform.Unmap();

            auto a_View = m_Scene->GetRegistry().view<TransformComponent, MeshComponent, MaterialComponent>();
            for (auto [it_Entity, a_Transform, a_Mesh, a_Material] : a_View.each())
            {
                float l_Scale = std::max({ a_Transform.Scale.x, a_Transform.Scale.y, a_Transform.Scale.z });
                float l_Radius = a_Mesh.MeshHandle->GetBoundingRadius() * l_Scale;
                if (!Culling::IsVisible(l_Frustum, a_Transform.Translation, l_Radius))
                {
                    continue;
                }

                UniformBufferObject l_Ubo{};
                l_Ubo.Model = a_Transform.GetTransform();
                l_Ubo.Update(m_Camera);

                void* l_Data = m_Frames[imageIndex].GlobalUniform.Map();
                std::memcpy(l_Data, &l_Ubo, sizeof(l_Ubo));
                m_Frames[imageIndex].GlobalUniform.Unmap();

                MaterialBufferObject l_Material{};
                l_Material.Albedo = a_Material.Albedo;
                l_Material.Roughness = a_Material.Roughness;
                l_Material.Metallic = a_Material.Metallic;
                l_Material.Specular = a_Material.Specular;

                void* l_MaterialData = m_Frames[imageIndex].MaterialUniform.Map();
                std::memcpy(l_MaterialData, &l_Material, sizeof(l_Material));
                m_Frames[imageIndex].MaterialUniform.Unmap();

                VkBuffer vertexBuffers[] = { a_Mesh.MeshHandle->GetVertexBuffer().GetBuffer() };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(m_CommandBuffer[imageIndex], 0, 1, vertexBuffers, offsets);

                uint32_t l_IndexCount = a_Mesh.MeshHandle->GetIndexBuffer().GetIndexCount();
                if (l_IndexCount > 0)
                {
                    vkCmdBindIndexBuffer(m_CommandBuffer[imageIndex], a_Mesh.MeshHandle->GetIndexBuffer().GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdBindDescriptorSets(m_CommandBuffer[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &m_Frames[imageIndex].DescriptorSet, 0, nullptr);
                    vkCmdDrawIndexed(m_CommandBuffer[imageIndex], l_IndexCount, 1, 0, 0, 0);
                }

                else
                {
                    vkCmdBindDescriptorSets(m_CommandBuffer[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &m_Frames[imageIndex].DescriptorSet, 0, nullptr);
                    vkCmdDraw(m_CommandBuffer[imageIndex], a_Mesh.MeshHandle->GetVertexBuffer().GetVertexCount(), 1, 0, 0);
                }
            }
        }

        if (recordCallback)
        {
            recordCallback(m_CommandBuffer[imageIndex]);
        }


        vkCmdEndRenderPass(m_CommandBuffer[imageIndex]);
    }

    void Renderer::CleanupSwapChain()
    {
        TR_CORE_TRACE("Cleaning up swapchain");

        CleanupImGuiImageDescriptors();

        if (m_DescriptorPool)
        {
            vkDestroyDescriptorPool(m_Context->GetDevice(), m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }

        for (auto& it_Frame : m_Frames)
        {
            it_Frame.GlobalUniform.Destroy();
            it_Frame.LightUniform.Destroy();
            it_Frame.MaterialUniform.Destroy();
        }
        m_Frames.clear();

        for (auto it_Framebuffer : m_Framebuffers)
        {
            if (it_Framebuffer)
            {
                vkDestroyFramebuffer(m_Context->GetDevice(), it_Framebuffer, nullptr);
            }
        }
        m_Framebuffers.clear();

        for (size_t i = 0; i < m_DepthImageViews.size(); ++i)
        {
            if (m_DepthImageViews[i])
            {
                vkDestroyImageView(m_Context->GetDevice(), m_DepthImageViews[i], nullptr);
            }

            if (m_DepthImages[i])
            {
                vkDestroyImage(m_Context->GetDevice(), m_DepthImages[i], nullptr);
            }

            if (m_DepthImageMemory[i])
            {
                vkFreeMemory(m_Context->GetDevice(), m_DepthImageMemory[i], nullptr);
            }
        }
        m_DepthImageViews.clear();
        m_DepthImages.clear();
        m_DepthImageMemory.clear();

        if (!m_CommandBuffer.empty())
        {
            vkFreeCommandBuffers(m_Context->GetDevice(), m_CommandPool, static_cast<uint32_t>(m_CommandBuffer.size()), m_CommandBuffer.data());
            m_CommandBuffer.clear();
        }

        for (auto it_Fence : m_InFlightFence)
        {
            if (it_Fence)
            {
                vkDestroyFence(m_Context->GetDevice(), it_Fence, nullptr);
            }
        }
        m_InFlightFence.clear();

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
        }
        m_ImageAvailableSemaphore.clear();
        m_RenderFinshedSemaphore.clear();
        m_ImagesInFlight.clear();

        m_Shader.Destroy();
        m_GraphicsPipeline = VK_NULL_HANDLE;

        if (m_PipelineLayout)
        {
            vkDestroyPipelineLayout(m_Context->GetDevice(), m_PipelineLayout, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
        }

        if (m_RenderPass)
        {
            vkDestroyRenderPass(m_Context->GetDevice(), m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }
    }

    void Renderer::RecreateSwapChain()
    {
        TR_CORE_TRACE("Recreating swapchain");

        vkDeviceWaitIdle(m_Context->GetDevice());

        CleanupSwapChain();
        m_Context->RecreateSwapChain();

        CreateRenderPass();
        CreateGraphicsPipeline(m_PrimitiveTopology);
        CreateDepthResources();
        CreateFramebuffers();
        CreateUniformBuffers();
        CreateDescriptorPool();
        CreateDescriptorSets();
        CreateCommandBuffer();
        CreateSyncObjects();

        TR_CORE_TRACE("Swapchain recreated");
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------------//

    VkFormat Renderer::FindDepthFormat()
    {
        std::vector<VkFormat> l_Candidates = 
        { 
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT 
        };

        for (VkFormat it_Format : l_Candidates)
        {
            VkFormatProperties l_Props;
            vkGetPhysicalDeviceFormatProperties(m_Context->GetPhysicalDevice(), it_Format, &l_Props);
            if (l_Props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            {
                return it_Format;
            }
        }

        return VK_FORMAT_D32_SFLOAT;
    }

    bool Renderer::HasStencilComponent(VkFormat format)
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    void Renderer::CreateImGuiImageDescriptors()
    {
        CleanupImGuiImageDescriptors();
        if (!m_Context)
        {
            return;
        }
        
        if (m_ImGuiImageSampler == VK_NULL_HANDLE)
        {
            VkSamplerCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            info.magFilter = VK_FILTER_LINEAR;
            info.minFilter = VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.minLod = 0.0f;
            info.maxLod = 0.0f;
            info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
            vkCreateSampler(m_Context->GetDevice(), &info, nullptr, &m_ImGuiImageSampler);
        }
        auto views = m_Context->GetSwapChainImages();
        m_ImGuiImageDescriptors.resize(views.size());
        for (size_t i = 0; i < views.size(); ++i)
        {
            m_ImGuiImageDescriptors[i] = ImGui_ImplVulkan_AddTexture(m_ImGuiImageSampler, views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void Renderer::CleanupImGuiImageDescriptors()
    {
        for (auto desc : m_ImGuiImageDescriptors)
        {
            ImGui_ImplVulkan_RemoveTexture(desc);
        }
        m_ImGuiImageDescriptors.clear();

        if (m_ImGuiImageSampler)
        {
            vkDestroySampler(m_Context->GetDevice(), m_ImGuiImageSampler, nullptr);
            m_ImGuiImageSampler = VK_NULL_HANDLE;
        }
    }
}