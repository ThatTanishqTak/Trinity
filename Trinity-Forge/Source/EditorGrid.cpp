#include "EditorGrid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>
#include <string_view>

namespace
{
    // Minor cells are never smaller than this on screen, and are fully drawn by the time they reach ten times it, when the next spacing up takes over
    constexpr float c_MinimumCellPixels = 8.0f;

    // Laid out as EditorGrid.slang reads it
    struct PushData
    {
        glm::vec2 Center{ 0.0f };
        glm::vec2 HalfExtent{ 0.0f };
        float Spacing = 1.0f;
        float MinorFade = 1.0f;
        glm::vec2 Padding{ 0.0f };
    };

    static_assert(sizeof(PushData) == 32);

    float GetPixelsPerUnit(const EditorCamera& camera, glm::vec2 viewportSize)
    {
        return viewportSize.y / camera.GetHeight();
    }
}

// Over the scene target, so the pipeline is built for its format
EditorGrid::EditorGrid()
{
    Trinity::Application& l_Application = Trinity::Application::Get();
    Trinity::RHI::Device& l_Device = l_Application.GetDevice();
    const std::string_view l_Extension = l_Device.GetInfo().API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";

    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_VertexShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/EditorGrid.VertexMain.{}", l_Extension));
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_PixelShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/EditorGrid.PixelMain.{}", l_Extension));
    if (!l_VertexShader || !l_PixelShader)
    {
        TR_INFO("Scene view: no EditorGrid shaders under /engine/shaders, so the grid is not drawn");

        return;
    }

    const std::array<Trinity::RHI::Format, 1> l_ColorFormats{ l_Application.GetRenderer().GetSceneFormat() };

    Trinity::RHI::GraphicsPipelineDescription l_Description;
    l_Description.VertexShader = { *l_VertexShader, "VertexMain" };
    l_Description.PixelShader = { *l_PixelShader, "PixelMain" };
    l_Description.ColorFormats = l_ColorFormats;
    l_Description.Cull = Trinity::RHI::CullMode::None;
    l_Description.AlphaBlend = true;
    l_Description.DebugName = "Scene view grid";

    m_Pipeline = l_Device.CreateGraphicsPipeline(l_Description);
    if (!m_Pipeline)
    {
        TR_ERROR("Scene view: the grid pipeline could not be created");
    }
}

EditorGrid::~EditorGrid()
{
    Trinity::Application::Get().GetDevice().DestroyPipeline(m_Pipeline);
}

void EditorGrid::Draw(Trinity::RHI::CommandList& commands, const EditorCamera& camera, glm::vec2 viewportSize)
{
    if (!m_Pipeline || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
    {
        return;
    }

    const float l_Spacing = GetSpacing(camera, viewportSize);
    const float l_CellPixels = l_Spacing * GetPixelsPerUnit(camera, viewportSize);

    PushData l_Push;
    l_Push.Center = camera.GetPosition();
    l_Push.HalfExtent = camera.GetHalfExtent(viewportSize);
    l_Push.Spacing = l_Spacing;
    l_Push.MinorFade = std::clamp((l_CellPixels - c_MinimumCellPixels) / (c_MinimumCellPixels * 9.0f), 0.0f, 1.0f);

    commands.SetPipeline(m_Pipeline);
    commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
    commands.Draw(3, 1, 0, 0);
}

// The smallest power of ten whose cells are at least c_MinimumCellPixels across
float EditorGrid::GetSpacing(const EditorCamera& camera, glm::vec2 viewportSize)
{
    const float l_PixelsPerUnit = std::max(GetPixelsPerUnit(camera, viewportSize), 1e-6f);

    return std::pow(10.0f, std::ceil(std::log10(c_MinimumCellPixels / l_PixelsPerUnit)));
}