#include "Trinity/RHI/Vulkan/VulkanPipeline.hpp"
#include "Trinity/RHI/Vulkan/VulkanBindless.hpp"
#include "Trinity/RHI/Vulkan/VulkanDevice.hpp"
#include "Trinity/RHI/Vulkan/VulkanUtilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology)
            {
                switch (topology)
                {
                    case PrimitiveTopology::TriangleStrip:
                    {
                        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                    }
                    case PrimitiveTopology::LineList:
                    {
                        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                    }
                    case PrimitiveTopology::PointList:
                    {
                        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                    }
                    default:
                    {
                        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                    }
                }
            }

            VkCullModeFlags ToVkCullMode(CullMode cull)
            {
                switch (cull)
                {
                    case CullMode::Front:
                    {
                        return VK_CULL_MODE_FRONT_BIT;
                    }
                    case CullMode::Back:
                    {
                        return VK_CULL_MODE_BACK_BIT;
                    }
                    default:
                    {
                        return VK_CULL_MODE_NONE;
                    }
                }
            }

            VkCompareOp ToVkCompareOp(CompareOp compare)
            {
                switch (compare)
                {
                    case CompareOp::Never:
                    {
                        return VK_COMPARE_OP_NEVER;
                    }
                    case CompareOp::Less:
                    {
                        return VK_COMPARE_OP_LESS;
                    }
                    case CompareOp::Equal:
                    {
                        return VK_COMPARE_OP_EQUAL;
                    }
                    case CompareOp::LessOrEqual:
                    {
                        return VK_COMPARE_OP_LESS_OR_EQUAL;
                    }
                    case CompareOp::Greater:
                    {
                        return VK_COMPARE_OP_GREATER;
                    }
                    case CompareOp::NotEqual:
                    {
                        return VK_COMPARE_OP_NOT_EQUAL;
                    }
                    case CompareOp::GreaterOrEqual:
                    {
                        return VK_COMPARE_OP_GREATER_OR_EQUAL;
                    }
                    default:
                    {
                        return VK_COMPARE_OP_ALWAYS;
                    }
                }
            }

            VkStencilOp ToVkStencilOp(StencilOp operation)
            {
                switch (operation)
                {
                    case StencilOp::Zero:
                    {
                        return VK_STENCIL_OP_ZERO;
                    }
                    case StencilOp::Replace:
                    {
                        return VK_STENCIL_OP_REPLACE;
                    }
                    case StencilOp::IncrementClamp:
                    {
                        return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
                    }
                    case StencilOp::DecrementClamp:
                    {
                        return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
                    }
                    default:
                    {
                        return VK_STENCIL_OP_KEEP;
                    }
                }
            }
        }

        std::optional<VulkanPipeline> VulkanPipeline::CreateGraphics(const VulkanDevice& device, const VulkanBindless& bindless, const GraphicsPipelineDescription& description)
        {
            TR_CORE_ASSERT(!description.VertexShader.Code.empty() && !description.PixelShader.Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);
            TR_CORE_ASSERT(description.ColorFormats.size() <= c_MaxColorAttachments, "Pipeline '{}' has too many color formats.", description.DebugName);
            TR_CORE_ASSERT(!description.StencilTest || HasStencil(description.DepthFormat), "Pipeline '{}' tests stencil without a depth format that has it.", description.DebugName);

            const std::array<const ShaderDescription*, 2> l_Shaders{ &description.VertexShader, &description.PixelShader };
            const std::array<VkShaderStageFlagBits, 2> l_StageBits{ VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT };
            std::array<std::string, 2> l_EntryPoints{};
            std::array<VkShaderModule, 2> l_Modules{};
            std::array<VkPipelineShaderStageCreateInfo, 2> l_Stages{};
            VkResult l_Result = VK_SUCCESS;
            for (std::size_t it_Stage = 0; it_Stage < l_Shaders.size() && l_Result == VK_SUCCESS; ++it_Stage)
            {
                const std::span<const std::byte> l_Code = l_Shaders[it_Stage]->Code;
                TR_CORE_ASSERT(l_Code.size() % 4 == 0 && reinterpret_cast<std::uintptr_t>(l_Code.data()) % 4 == 0, "Pipeline '{}' has SPIR-V that is not whole, aligned 32-bit words.", description.DebugName);

                VkShaderModuleCreateInfo l_Module = MakeInfo<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
                l_Module.codeSize = l_Code.size();
                l_Module.pCode = reinterpret_cast<const std::uint32_t*>(l_Code.data());
                l_Result = vkCreateShaderModule(device.GetHandle(), &l_Module, nullptr, &l_Modules[it_Stage]);

                l_EntryPoints[it_Stage] = l_Shaders[it_Stage]->EntryPoint;
                l_Stages[it_Stage] = MakeInfo<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
                l_Stages[it_Stage].stage = l_StageBits[it_Stage];
                l_Stages[it_Stage].module = l_Modules[it_Stage];
                l_Stages[it_Stage].pName = l_EntryPoints[it_Stage].c_str();
            }

            const VkPipelineVertexInputStateCreateInfo l_VertexInput = MakeInfo<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);

            VkPipelineInputAssemblyStateCreateInfo l_InputAssembly = MakeInfo<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
            l_InputAssembly.topology = ToVkTopology(description.Topology);

            VkPipelineViewportStateCreateInfo l_Viewport = MakeInfo<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
            l_Viewport.viewportCount = 1;
            l_Viewport.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo l_Rasterization = MakeInfo<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
            l_Rasterization.polygonMode = VK_POLYGON_MODE_FILL;
            l_Rasterization.cullMode = ToVkCullMode(description.Cull);
            l_Rasterization.frontFace = description.FrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
            l_Rasterization.depthClampEnable = description.DepthClamp ? VK_TRUE : VK_FALSE;
            l_Rasterization.depthBiasEnable = description.DepthBiasConstant != 0 || description.DepthBiasSlope != 0.0f ? VK_TRUE : VK_FALSE;
            l_Rasterization.depthBiasConstantFactor = static_cast<float>(description.DepthBiasConstant);
            l_Rasterization.depthBiasSlopeFactor = description.DepthBiasSlope;
            l_Rasterization.depthBiasClamp = description.DepthBiasClamp;
            l_Rasterization.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo l_Multisample = MakeInfo<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
            l_Multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(description.SampleCount);

            VkPipelineDepthStencilStateCreateInfo l_DepthStencil = MakeInfo<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
            l_DepthStencil.depthTestEnable = description.DepthTest ? VK_TRUE : VK_FALSE;
            l_DepthStencil.depthWriteEnable = description.DepthWrite ? VK_TRUE : VK_FALSE;
            l_DepthStencil.depthCompareOp = ToVkCompareOp(description.DepthCompare);
            l_DepthStencil.stencilTestEnable = description.StencilTest ? VK_TRUE : VK_FALSE;
            l_DepthStencil.front = { VK_STENCIL_OP_KEEP, ToVkStencilOp(description.StencilPass), VK_STENCIL_OP_KEEP, ToVkCompareOp(description.StencilCompare), description.StencilReadMask, description.StencilWriteMask, 0 };
            l_DepthStencil.back = l_DepthStencil.front;

            std::array<VkPipelineColorBlendAttachmentState, c_MaxColorAttachments> l_Blends{};
            std::array<VkFormat, c_MaxColorAttachments> l_ColorFormats{};
            const std::uint32_t l_ColorCount = static_cast<std::uint32_t>(std::min<std::size_t>(description.ColorFormats.size(), c_MaxColorAttachments));
            for (std::uint32_t it_Color = 0; it_Color < l_ColorCount; ++it_Color)
            {
                // An integer target is never blended
                VkPipelineColorBlendAttachmentState& l_Blend = l_Blends[it_Color];
                l_Blend.blendEnable = description.AlphaBlend && !IsIntegerFormat(description.ColorFormats[it_Color]) ? VK_TRUE : VK_FALSE;
                l_Blend.srcColorBlendFactor = description.PremultipliedAlpha ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
                l_Blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                l_Blend.colorBlendOp = VK_BLEND_OP_ADD;
                l_Blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                l_Blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                l_Blend.alphaBlendOp = VK_BLEND_OP_ADD;
                l_Blend.colorWriteMask = description.ColorWrite ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT : 0;

                l_ColorFormats[it_Color] = ToVkFormat(description.ColorFormats[it_Color]);
            }

            VkPipelineColorBlendStateCreateInfo l_ColorBlend = MakeInfo<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
            l_ColorBlend.attachmentCount = l_ColorCount;
            l_ColorBlend.pAttachments = l_Blends.data();

            // The stencil reference is set on the command list, as D3D12 sets it
            const std::array<VkDynamicState, 3> l_DynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE };
            VkPipelineDynamicStateCreateInfo l_Dynamic = MakeInfo<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
            l_Dynamic.dynamicStateCount = static_cast<std::uint32_t>(description.StencilTest ? l_DynamicStates.size() : l_DynamicStates.size() - 1);
            l_Dynamic.pDynamicStates = l_DynamicStates.data();

            VkPipelineRenderingCreateInfo l_Rendering = MakeInfo<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
            l_Rendering.colorAttachmentCount = l_ColorCount;
            l_Rendering.pColorAttachmentFormats = l_ColorFormats.data();
            l_Rendering.depthAttachmentFormat = description.DepthFormat != Format::Unknown ? ToVkFormat(description.DepthFormat) : VK_FORMAT_UNDEFINED;
            l_Rendering.stencilAttachmentFormat = HasStencil(description.DepthFormat) ? ToVkFormat(description.DepthFormat) : VK_FORMAT_UNDEFINED;

            VkGraphicsPipelineCreateInfo l_Create = MakeInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
            l_Create.pNext = &l_Rendering;
            l_Create.stageCount = static_cast<std::uint32_t>(l_Stages.size());
            l_Create.pStages = l_Stages.data();
            l_Create.pVertexInputState = &l_VertexInput;
            l_Create.pInputAssemblyState = &l_InputAssembly;
            l_Create.pViewportState = &l_Viewport;
            l_Create.pRasterizationState = &l_Rasterization;
            l_Create.pMultisampleState = &l_Multisample;
            l_Create.pDepthStencilState = &l_DepthStencil;
            l_Create.pColorBlendState = &l_ColorBlend;
            l_Create.pDynamicState = &l_Dynamic;
            l_Create.layout = bindless.GetPipelineLayout();

            VulkanPipeline l_Pipeline;
            if (l_Result == VK_SUCCESS)
            {
                l_Result = vkCreateGraphicsPipelines(device.GetHandle(), VK_NULL_HANDLE, 1, &l_Create, nullptr, &l_Pipeline.Pipeline);
            }

            for (VkShaderModule it_Module : l_Modules)
            {
                vkDestroyShaderModule(device.GetHandle(), it_Module, nullptr);
            }

            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: pipeline '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            device.SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(l_Pipeline.Pipeline), description.DebugName);

            return l_Pipeline;
        }

        std::optional<VulkanPipeline> VulkanPipeline::CreateCompute(const VulkanDevice& device, const VulkanBindless& bindless, const ComputePipelineDescription& description)
        {
            const std::span<const std::byte> l_Code = description.ComputeShader.Code;
            TR_CORE_ASSERT(!l_Code.empty(), "Pipeline '{}' is missing shader code.", description.DebugName);
            TR_CORE_ASSERT(l_Code.size() % 4 == 0 && reinterpret_cast<std::uintptr_t>(l_Code.data()) % 4 == 0, "Pipeline '{}' has SPIR-V that is not whole, aligned 32-bit words.", description.DebugName);

            VkShaderModuleCreateInfo l_Module = MakeInfo<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
            l_Module.codeSize = l_Code.size();
            l_Module.pCode = reinterpret_cast<const std::uint32_t*>(l_Code.data());

            VkShaderModule l_ShaderModule = VK_NULL_HANDLE;
            VkResult l_Result = vkCreateShaderModule(device.GetHandle(), &l_Module, nullptr, &l_ShaderModule);

            const std::string l_EntryPoint(description.ComputeShader.EntryPoint);

            VkComputePipelineCreateInfo l_Create = MakeInfo<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
            l_Create.stage = MakeInfo<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            l_Create.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            l_Create.stage.module = l_ShaderModule;
            l_Create.stage.pName = l_EntryPoint.c_str();
            l_Create.layout = bindless.GetPipelineLayout();

            VulkanPipeline l_Pipeline;
            l_Pipeline.BindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
            if (l_Result == VK_SUCCESS)
            {
                l_Result = vkCreateComputePipelines(device.GetHandle(), VK_NULL_HANDLE, 1, &l_Create, nullptr, &l_Pipeline.Pipeline);
            }

            vkDestroyShaderModule(device.GetHandle(), l_ShaderModule, nullptr);

            if (l_Result != VK_SUCCESS)
            {
                TR_CORE_ERROR("Vulkan: pipeline '{}' could not be created ({})", description.DebugName, FormatResult(l_Result));

                return {};
            }

            device.SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(l_Pipeline.Pipeline), description.DebugName);

            return l_Pipeline;
        }

        VulkanRelease VulkanPipeline::ToRelease() const
        {
            VulkanRelease l_Release;
            l_Release.Pipeline = Pipeline;

            return l_Release;
        }
    }
}