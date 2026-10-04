#include "SceneLayer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <span>
#include <string_view>

namespace
{
    struct TriangleVertex
    {
        std::array<float, 4> Position;
        std::array<float, 4> Color;
    };

    // Counter-clockwise with Y up, laid out as Triangle.slang reads it
    constexpr std::array<TriangleVertex, 3> c_TriangleVertices
    { {
        { { 0.0f, 0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { 0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
    } };
}

SceneLayer::SceneLayer() : Layer("Scene")
{

}

void SceneLayer::OnAttach()
{
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const std::string_view l_Extension = l_Device.GetInfo().API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";

    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_VertexShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/Triangle.VertexMain.{}", l_Extension));
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_PixelShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/Triangle.PixelMain.{}", l_Extension));
    if (!l_VertexShader || !l_PixelShader)
    {
        TR_INFO("Scene: no {} triangle shaders under /engine/shaders, so the viewport shows only the clear colour", l_Extension);

        return;
    }

    const std::array<Trinity::RHI::Format, 1> l_ColorFormats{ Trinity::Application::Get().GetRenderer().GetSceneFormat() };

    Trinity::RHI::GraphicsPipelineDescription l_PipelineDescription;
    l_PipelineDescription.VertexShader = { *l_VertexShader, "VertexMain" };
    l_PipelineDescription.PixelShader = { *l_PixelShader, "PixelMain" };
    l_PipelineDescription.ColorFormats = l_ColorFormats;
    l_PipelineDescription.DebugName = "Forge triangle";

    Trinity::RHI::BufferDescription l_VertexDescription;
    l_VertexDescription.Size = sizeof(c_TriangleVertices);
    l_VertexDescription.Usage = Trinity::RHI::BufferUsage::ShaderResource | Trinity::RHI::BufferUsage::CopyDestination;
    l_VertexDescription.DebugName = "Forge triangle vertices";

    Trinity::RHI::BufferDescription l_StagingDescription;
    l_StagingDescription.Size = sizeof(c_TriangleVertices);
    l_StagingDescription.Memory = Trinity::RHI::MemoryType::Upload;
    l_StagingDescription.DebugName = "Forge triangle staging";

    m_Pipeline = l_Device.CreateGraphicsPipeline(l_PipelineDescription);
    m_Vertices = l_Device.CreateBuffer(l_VertexDescription);
    const Trinity::RHI::BufferHandle l_Staging = l_Device.CreateBuffer(l_StagingDescription);
    m_VertexIndex = m_Vertices ? l_Device.GetShaderResourceIndex(m_Vertices) : Trinity::RHI::c_NoBindlessIndex;
    if (!m_Pipeline || !l_Staging || m_VertexIndex == Trinity::RHI::c_NoBindlessIndex)
    {
        TR_ERROR("Scene: could not create the triangle pipeline, its vertex buffer or its bindless index");

        l_Device.DestroyBuffer(l_Staging);
        OnDetach();

        return;
    }

    const std::span<std::byte> l_Mapped = l_Device.GetMappedData(l_Staging);
    std::memcpy(l_Mapped.data(), c_TriangleVertices.data(), sizeof(c_TriangleVertices));

    Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();
    l_Commands.BufferBarrier(m_Vertices, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::CopyDestination);
    l_Commands.CopyBuffer(l_Staging, 0, m_Vertices, 0, sizeof(c_TriangleVertices));
    l_Commands.BufferBarrier(m_Vertices, Trinity::RHI::ResourceState::CopyDestination, Trinity::RHI::ResourceState::ShaderResource);
    l_Device.EndFrame();

    l_Device.DestroyBuffer(l_Staging);
}

void SceneLayer::OnDetach()
{
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();

    l_Device.DestroyPipeline(m_Pipeline);
    l_Device.DestroyBuffer(m_Vertices);
    m_Pipeline = {};
    m_Vertices = {};
    m_VertexIndex = Trinity::RHI::c_NoBindlessIndex;
}

// Releases are left out, since ImGuiLayer lets them through even when the scene has no input, so no layer is left holding a key or button
void SceneLayer::OnEvent(Trinity::Event& event)
{
    switch (event.GetEventType())
    {
        case Trinity::EventType::MouseMoved:
        case Trinity::EventType::MouseButtonPressed:
        case Trinity::EventType::MouseScrolled:
        {
            ++m_MouseEvents;

            break;
        }
        case Trinity::EventType::KeyPressed:
        case Trinity::EventType::KeyTyped:
        {
            ++m_KeyEvents;

            break;
        }
        default:
        {
            break;
        }
    }
}

// The largest square that fits the scene target, in its middle
void SceneLayer::OnRender(Trinity::RHI::CommandList& commands)
{
    if (!m_Pipeline)
    {
        return;
    }

    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const std::uint32_t l_Width = l_Renderer.GetSceneWidth();
    const std::uint32_t l_Height = l_Renderer.GetSceneHeight();
    const std::uint32_t l_Side = std::min(l_Width, l_Height);
    const std::int32_t l_X = static_cast<std::int32_t>((l_Width - l_Side) / 2);
    const std::int32_t l_Y = static_cast<std::int32_t>((l_Height - l_Side) / 2);

    commands.SetViewport({ static_cast<float>(l_X), static_cast<float>(l_Y), static_cast<float>(l_Side), static_cast<float>(l_Side), 0.0f, 1.0f });
    commands.SetScissor({ l_X, l_Y, l_Side, l_Side });

    // The push constants hold a Slang DescriptorHandle, two 32-bit values of which the first is the index
    const std::array<std::uint32_t, 2> l_PushData{ m_VertexIndex, 0 };
    commands.SetPipeline(m_Pipeline);
    commands.PushConstants(std::as_bytes(std::span(l_PushData)));
    commands.Draw(3, 1, 0, 0);
}