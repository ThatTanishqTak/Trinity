#include "EditorGrid3D.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>
#include <string_view>

namespace
{
    // The eye's height over the plane is taken as at least this, so the cells stop shrinking just above it
    constexpr float c_MinimumHeight = 0.01f;
    // The grid fades out this many times the eye's height away, and never nearer than the minimum
    constexpr float c_FadeHeights = 100.0f;
    constexpr float c_MinimumFadeDistance = 50.0f;

    // Laid out as EditorGrid3D.slang reads it
    struct PushData
    {
        std::array<glm::vec4, 4> InverseColumns{};
        glm::vec4 DepthRow{ 0.0f };
        glm::vec4 WRow{ 0.0f };
        glm::vec3 Eye{ 0.0f };
        float Spacing = 1.0f;
        float MinorFade = 1.0f;
        float FadeDistance = c_MinimumFadeDistance;
        glm::vec2 Padding{ 0.0f };
    };

    static_assert(sizeof(PushData) == Trinity::RHI::c_MaxPushConstantSize);

    float GetLogHeight(const glm::vec3& eye)
    {
        return std::log10(std::max(std::abs(eye.y), c_MinimumHeight));
    }
}

EditorGrid3D::~EditorGrid3D()
{
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    for (const PipelineEntry& it_Entry : m_Pipelines)
    {
        l_Device.DestroyPipeline(it_Entry.Pipeline);
    }
}

// Tested against the scene's depth and never writing it, with the depth each pixel writes being the plane's
void EditorGrid3D::Draw(Trinity::RHI::CommandList& commands, const Trinity::RenderView& view, Trinity::RHI::Format colorFormat, std::uint32_t sampleCount)
{
    const Trinity::RHI::PipelineHandle l_Pipeline = GetPipeline(colorFormat, sampleCount);
    if (!l_Pipeline)
    {
        return;
    }

    const glm::mat4 l_Inverse = glm::inverse(view.ViewProjection);
    const float l_LogHeight = GetLogHeight(view.Position);

    PushData l_Push;
    for (glm::length_t it_Column = 0; it_Column < 4; ++it_Column)
    {
        l_Push.InverseColumns[static_cast<std::size_t>(it_Column)] = l_Inverse[it_Column];
    }

    l_Push.DepthRow = glm::vec4(view.ViewProjection[0][2], view.ViewProjection[1][2], view.ViewProjection[2][2], view.ViewProjection[3][2]);
    l_Push.WRow = glm::vec4(view.ViewProjection[0][3], view.ViewProjection[1][3], view.ViewProjection[2][3], view.ViewProjection[3][3]);
    l_Push.Eye = view.Position;
    l_Push.Spacing = GetSpacing(view.Position);
    l_Push.MinorFade = 1.0f - (l_LogHeight - std::floor(l_LogHeight));
    l_Push.FadeDistance = std::max(std::abs(view.Position.y) * c_FadeHeights, c_MinimumFadeDistance);

    commands.SetPipeline(l_Pipeline);
    commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
    commands.Draw(3, 1, 0, 0);
}

// The smallest cell: a tenth of the power of ten at or below the eye's height over the plane
float EditorGrid3D::GetSpacing(const glm::vec3& eye)
{
    return std::pow(10.0f, std::floor(GetLogHeight(eye)) - 1.0f);
}

// One for each colour format and sample count the scene is drawn at, made when first needed. Missing shaders are reported once, and the grid is not drawn
Trinity::RHI::PipelineHandle EditorGrid3D::GetPipeline(Trinity::RHI::Format colorFormat, std::uint32_t sampleCount)
{
    const auto a_Found = std::ranges::find_if(m_Pipelines, [&](const PipelineEntry& entry) { return entry.Format == colorFormat && entry.SampleCount == sampleCount; });
    if (a_Found != m_Pipelines.end())
    {
        return a_Found->Pipeline;
    }

    if (m_Missing)
    {
        return {};
    }

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const std::string_view l_Extension = l_Device.GetInfo().API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_VertexShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/EditorGrid3D.VertexMain.{}", l_Extension));
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_PixelShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/EditorGrid3D.PixelMain.{}", l_Extension));
    if (!l_VertexShader || !l_PixelShader)
    {
        TR_INFO("Scene view: no EditorGrid3D shaders under /engine/shaders, so the 3D grid is not drawn");
        m_Missing = true;

        return {};
    }

    const std::array<Trinity::RHI::Format, 1> l_ColorFormats{ colorFormat };

    Trinity::RHI::GraphicsPipelineDescription l_Description;
    l_Description.VertexShader = { *l_VertexShader, "VertexMain" };
    l_Description.PixelShader = { *l_PixelShader, "PixelMain" };
    l_Description.ColorFormats = l_ColorFormats;
    l_Description.DepthFormat = Trinity::Renderer3D::c_DepthFormat;
    l_Description.SampleCount = sampleCount;
    l_Description.Cull = Trinity::RHI::CullMode::None;
    l_Description.DepthTest = true;
    l_Description.DepthWrite = false;
    l_Description.DepthCompare = Trinity::RHI::CompareOp::GreaterOrEqual;
    l_Description.AlphaBlend = true;
    l_Description.DebugName = "Scene view grid 3D";

    const Trinity::RHI::PipelineHandle l_Pipeline = l_Device.CreateGraphicsPipeline(l_Description);
    if (!l_Pipeline)
    {
        TR_ERROR("Scene view: the 3D grid pipeline could not be created");
        m_Missing = true;

        return {};
    }

    m_Pipelines.push_back({ colorFormat, sampleCount, l_Pipeline });

    return l_Pipeline;
}