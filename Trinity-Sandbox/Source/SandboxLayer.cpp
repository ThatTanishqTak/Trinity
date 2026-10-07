#include "SandboxLayer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    // 100,000 sprites over 64 textures, each 16x16 RGBA8 KTX2 written in memory and served from /cache, as Forge's importer would have cooked them
    constexpr std::string_view c_SpriteTestRoot = "/saves/sandbox/sprites";
    constexpr std::uint32_t c_SpriteTextureCount = 64;
    constexpr std::uint32_t c_SpriteTextureSize = 16;
    constexpr std::uint32_t c_SpriteCount = 100000;
    constexpr std::uint32_t c_SpriteGroupSize = 10;
    constexpr std::uint32_t c_SpriteSeed = 20261008;
    constexpr std::uint64_t c_SpriteCheckFrame = 100;
    constexpr std::uint64_t c_SpriteReportFrame = 1000;
    constexpr std::uint64_t c_SpriteLoadTimeoutFrames = 600;
    constexpr std::uint32_t c_SpriteReadbackSize = 128;
    constexpr float c_ClearCycleSeconds = 10.0f;

    // The storage buffer holds c_ComputeWordCount words from one dispatch and as many again from the next, and the storage texture is c_ComputeTextureSize texels square
    constexpr std::uint32_t c_ComputeWordCount = 4096;
    constexpr std::uint32_t c_ComputeBufferGroupSize = 64;
    constexpr std::uint32_t c_ComputeTextureSize = 64;
    constexpr std::uint32_t c_ComputeTextureGroupSize = 8;

    // ComputeTest.slang's push constants: two descriptor handles, then the word count and texture size
    struct ComputePushData
    {
        std::array<std::uint32_t, 2> Buffer{};
        std::array<std::uint32_t, 2> Texture{};
        std::uint32_t Count = 0;
        std::uint32_t Size = 0;
    };

    static_assert(sizeof(ComputePushData) == 24);

    // The words FillBuffer and then ReverseBuffer write, as ComputeTest.slang computes them
    std::vector<std::uint32_t> MakeComputeWords()
    {
        std::vector<std::uint32_t> l_Words(std::size_t{ c_ComputeWordCount } * 2);
        for (std::uint32_t it_Index = 0; it_Index < c_ComputeWordCount; ++it_Index)
        {
            l_Words[it_Index] = it_Index * 2654435761u + 0x9E3779B9u;
        }

        for (std::uint32_t it_Index = 0; it_Index < c_ComputeWordCount; ++it_Index)
        {
            const std::uint32_t l_Word = l_Words[c_ComputeWordCount - 1 - it_Index];
            l_Words[c_ComputeWordCount + it_Index] = ((l_Word << 7) | (l_Word >> 25)) ^ it_Index;
        }

        return l_Words;
    }

    // The RGBA8 texel FillTexture writes at a column and row
    std::array<std::uint8_t, 4> MakeComputeTexel(std::uint32_t x, std::uint32_t y)
    {
        return { static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), static_cast<std::uint8_t>((x * 3 + y * 5) & 0xFF), 255 };
    }

    // The first word of a readback that differs from the expected words, or nothing when all match
    std::optional<std::size_t> FindWrongWord(std::span<const std::byte> data, std::span<const std::uint32_t> expected)
    {
        for (std::size_t it_Word = 0; it_Word < expected.size(); ++it_Word)
        {
            if ((it_Word + 1) * 4 > data.size() || std::memcmp(data.data() + it_Word * 4, &expected[it_Word], 4) != 0)
            {
                return it_Word;
            }
        }

        return std::nullopt;
    }

    // The first texel of a readback, at a row pitch, that differs from the texture FillTexture writes, or nothing when all match
    std::optional<std::array<std::uint32_t, 2>> FindWrongTexel(std::span<const std::byte> data, std::uint64_t rowPitch)
    {
        if (data.size() < rowPitch * c_ComputeTextureSize)
        {
            return std::array<std::uint32_t, 2>{ 0, 0 };
        }

        for (std::uint32_t it_Y = 0; it_Y < c_ComputeTextureSize; ++it_Y)
        {
            for (std::uint32_t it_X = 0; it_X < c_ComputeTextureSize; ++it_X)
            {
                const std::array<std::uint8_t, 4> l_Expected = MakeComputeTexel(it_X, it_Y);
                if (std::memcmp(data.data() + static_cast<std::size_t>(it_Y * rowPitch + it_X * 4), l_Expected.data(), l_Expected.size()) != 0)
                {
                    return std::array<std::uint32_t, 2>{ it_X, it_Y };
                }
            }
        }

        return std::nullopt;
    }

    // A cube and an array, each with two mips, every mip of every layer cleared to its own colour
    constexpr std::uint32_t c_LayeredCubeSize = 16;
    constexpr std::uint32_t c_LayeredArrayWidth = 32;
    constexpr std::uint32_t c_LayeredArrayHeight = 16;
    constexpr std::uint32_t c_LayeredArrayLayers = 4;
    constexpr std::uint32_t c_LayeredMipLevels = 2;

    // LayeredTest.slang's push constants: four descriptor handles, then the mip and layer counts
    struct LayeredPushData
    {
        std::array<std::uint32_t, 2> Cube{};
        std::array<std::uint32_t, 2> Array{};
        std::array<std::uint32_t, 2> Sampler{};
        std::array<std::uint32_t, 2> Output{};
        std::uint32_t MipLevels = 0;
        std::uint32_t ArrayLayers = 0;
    };

    static_assert(sizeof(LayeredPushData) == 40);

    // Red counts layers, green mips, and blue tells the cube from the array, each a whole 8-bit value that RGBA8 stores exactly
    std::array<std::uint8_t, 4> MakeLayerColor(bool cube, std::uint32_t layer, std::uint32_t mipLevel)
    {
        return { static_cast<std::uint8_t>((layer + 1) * 32), static_cast<std::uint8_t>((mipLevel + 1) * 64), static_cast<std::uint8_t>(cube ? 0x40 : 0xC0), 255 };
    }

    // Nine 32x32 cells side by side, each drawn through its own viewport with its own pipeline
    constexpr std::uint32_t c_RasterCellSize = 32;
    constexpr std::uint32_t c_RasterCellCount = 9;
    constexpr std::uint32_t c_RasterFloatSize = 4;

    // RasterTest.slang's push constants: a triangle in clip space and its colour
    struct RasterPushData
    {
        std::array<glm::vec4, 3> Positions{};
        glm::vec4 Color{};
    };

    static_assert(sizeof(RasterPushData) == 64);

    // A hue in [0, 1) at saturation 0.6 and value 0.5, so the window never gets too bright to look at
    std::array<float, 4> HueToColor(float hue)
    {
        const auto a_Channel = [hue](float offset)
        {
            const float l_K = std::fmod(offset + hue * 6.0f, 6.0f);

            return 0.5f - 0.5f * 0.6f * std::clamp(std::min(l_K, 4.0f - l_K), 0.0f, 1.0f);
        };

        return { a_Channel(5.0f), a_Channel(3.0f), a_Channel(1.0f), 1.0f };
    }

    void AppendU32(std::vector<std::byte>& bytes, std::uint32_t value)
    {
        for (std::uint32_t it_Byte = 0; it_Byte < 4; ++it_Byte)
        {
            bytes.push_back(static_cast<std::byte>((value >> (it_Byte * 8)) & 0xFF));
        }
    }

    void AppendU64(std::vector<std::byte>& bytes, std::uint64_t value)
    {
        AppendU32(bytes, static_cast<std::uint32_t>(value & 0xFFFFFFFF));
        AppendU32(bytes, static_cast<std::uint32_t>(value >> 32));
    }

    // A single-mip RGBA8 UNORM KTX2 file: the header, the level index, a basic data format descriptor with four 8-bit samples, the filter key when asked for, then the texels
    std::vector<std::byte> MakeSpriteKtx2(std::span<const std::uint8_t> texels, std::uint32_t size, bool nearest)
    {
        constexpr std::uint32_t c_HeaderSize = 80;
        constexpr std::uint32_t c_LevelIndexSize = 24;
        constexpr std::uint32_t c_DfdSize = 92;
        constexpr std::array<std::uint8_t, 12> c_Identifier{ 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };

        std::vector<std::byte> l_KeyValue;
        if (nearest)
        {
            const std::string l_Pair = std::format("{}{}Nearest{}", Trinity::c_TextureFilterKey, '\0', '\0');
            AppendU32(l_KeyValue, static_cast<std::uint32_t>(l_Pair.size()));
            for (const char it_Character : l_Pair)
            {
                l_KeyValue.push_back(static_cast<std::byte>(it_Character));
            }

            while (l_KeyValue.size() % 4 != 0)
            {
                l_KeyValue.push_back(std::byte{ 0 });
            }
        }

        const std::uint32_t l_DfdOffset = c_HeaderSize + c_LevelIndexSize;
        const std::uint32_t l_KeyValueOffset = l_DfdOffset + c_DfdSize;
        const std::uint32_t l_DataOffset = l_KeyValueOffset + static_cast<std::uint32_t>(l_KeyValue.size());

        std::vector<std::byte> l_File;
        for (const std::uint8_t it_Byte : c_Identifier)
        {
            l_File.push_back(static_cast<std::byte>(it_Byte));
        }

        AppendU32(l_File, 37);
        AppendU32(l_File, 1);
        AppendU32(l_File, size);
        AppendU32(l_File, size);
        AppendU32(l_File, 0);
        AppendU32(l_File, 0);
        AppendU32(l_File, 1);
        AppendU32(l_File, 1);
        AppendU32(l_File, 0);
        AppendU32(l_File, l_DfdOffset);
        AppendU32(l_File, c_DfdSize);
        AppendU32(l_File, l_KeyValue.empty() ? 0 : l_KeyValueOffset);
        AppendU32(l_File, static_cast<std::uint32_t>(l_KeyValue.size()));
        AppendU64(l_File, 0);
        AppendU64(l_File, 0);

        AppendU64(l_File, l_DataOffset);
        AppendU64(l_File, texels.size());
        AppendU64(l_File, texels.size());

        // RGBSDA colour model, BT.709 primaries and a linear transfer function, then R, G, B and A, with alpha as channel 15
        AppendU32(l_File, c_DfdSize);
        AppendU32(l_File, 0);
        AppendU32(l_File, 2 | ((c_DfdSize - 4) << 16));
        AppendU32(l_File, 1 | (1 << 8) | (1 << 16));
        AppendU32(l_File, 0);
        AppendU32(l_File, 4);
        AppendU32(l_File, 0);
        for (std::uint32_t it_Sample = 0; it_Sample < 4; ++it_Sample)
        {
            const std::uint32_t l_Channel = it_Sample < 3 ? it_Sample : 15;
            AppendU32(l_File, (it_Sample * 8) | (7 << 16) | (l_Channel << 24));
            AppendU32(l_File, 0);
            AppendU32(l_File, 0);
            AppendU32(l_File, 255);
        }

        l_File.insert(l_File.end(), l_KeyValue.begin(), l_KeyValue.end());
        for (const std::uint8_t it_Byte : texels)
        {
            l_File.push_back(static_cast<std::byte>(it_Byte));
        }

        return l_File;
    }

    // Texture 0 has a red, green, blue and yellow quadrant clockwise from the top left, so a flip shows. The rest are a hue each with a darker diagonal
    std::vector<std::uint8_t> MakeSpriteTexels(std::uint32_t index)
    {
        std::vector<std::uint8_t> l_Texels(std::size_t{ c_SpriteTextureSize } * c_SpriteTextureSize * 4);
        const std::array<float, 4> l_Hue = HueToColor(static_cast<float>(index) / static_cast<float>(c_SpriteTextureCount));
        for (std::uint32_t it_Y = 0; it_Y < c_SpriteTextureSize; ++it_Y)
        {
            for (std::uint32_t it_X = 0; it_X < c_SpriteTextureSize; ++it_X)
            {
                std::array<std::uint8_t, 4> l_Texel{ 255, 255, 255, 255 };
                if (index == 0)
                {
                    const bool l_Right = it_X >= c_SpriteTextureSize / 2;
                    const bool l_Bottom = it_Y >= c_SpriteTextureSize / 2;
                    l_Texel = !l_Bottom ? (l_Right ? std::array<std::uint8_t, 4>{ 0, 255, 0, 255 } : std::array<std::uint8_t, 4>{ 255, 0, 0, 255 }) : (l_Right ? std::array<std::uint8_t, 4>{ 255, 255, 0, 255 } : std::array<std::uint8_t, 4>{ 0, 0, 255, 255 });
                }
                else
                {
                    const float l_Shade = it_X == it_Y ? 0.5f : 1.0f;
                    for (std::size_t it_Channel = 0; it_Channel < 3; ++it_Channel)
                    {
                        l_Texel[it_Channel] = static_cast<std::uint8_t>(std::lround((1.0f - l_Hue[it_Channel]) * l_Shade * 255.0f));
                    }
                }

                std::memcpy(l_Texels.data() + (std::size_t{ it_Y } * c_SpriteTextureSize + it_X) * 4, l_Texel.data(), 4);
            }
        }

        return l_Texels;
    }

    // Files only. Emptied folders stay, which the registry ignores
    void RemoveFiles(std::string_view directory)
    {
        const Trinity::Expected<std::vector<Trinity::DirectoryEntry>, Trinity::FileError> l_Entries = Trinity::FileSystem::List(directory);
        if (!l_Entries)
        {
            return;
        }

        for (const Trinity::DirectoryEntry& it_Entry : *l_Entries)
        {
            const std::string l_Path = std::format("{}/{}", directory, it_Entry.Name);
            if (it_Entry.Type == Trinity::FileType::Directory)
            {
                RemoveFiles(l_Path);
            }
            else
            {
                static_cast<void>(Trinity::FileSystem::RemoveFile(l_Path));
            }
        }
    }

    Trinity::ConsoleVariable<float> s_ReportInterval("sandbox.report_interval", 1.0f, "Seconds between Sandbox fps reports");
    Trinity::ConsoleVariable<bool> s_ListConsoleVariables("sandbox.list_cvars", false, "Log every console variable when the Sandbox starts", Trinity::ConsoleVariableFlags::ReadOnly);
}

SandboxLayer::SandboxLayer() : Layer("Sandbox")
{

}

void SandboxLayer::OnAttach()
{
    TR_INFO("Sandbox attached. Escape closes the window, M prints memory use, C lists console variables, V toggles vsync.");
    TR_INFO("Reporting fps every {} s (sandbox.report_interval)", s_ReportInterval.Get());

    if (s_ListConsoleVariables.Get())
    {
        Trinity::ConsoleVariables::LogAll();
    }

    Trinity::Memory::LogUsage();
}

void SandboxLayer::OnDetach()
{
    DestroySprites();
}

void SandboxLayer::OnUpdate(Trinity::Timestep timestep)
{
    TR_PROFILE_FUNCTION();

    if (!m_RHITested)
    {
        m_RHITested = true;
        TestCompute();
        TestLayeredTextures();
        TestRasterState();
        TestMultisampling();
        TestTimestamps();
    }

    m_ClearHue = std::fmod(m_ClearHue + timestep.GetSeconds() / c_ClearCycleSeconds, 1.0f);
    Trinity::Application::Get().GetRenderer().SetClearColor(HueToColor(m_ClearHue));

    m_SecondsSinceReport += timestep;
    ++m_FramesSinceReport;

    if (m_SecondsSinceReport >= s_ReportInterval.Get())
    {
        const Trinity::FrameAllocator& l_FrameAllocator = Trinity::Application::Get().GetFrameAllocator();
        TR_TRACE("{:.1f} fps, frame memory {} of {} (peak {})", static_cast<float>(m_FramesSinceReport) / m_SecondsSinceReport, Trinity::Memory::FormatBytes(l_FrameAllocator.GetUsed()), Trinity::Memory::FormatBytes(l_FrameAllocator.GetCapacity()), Trinity::Memory::FormatBytes(l_FrameAllocator.GetPeakUsed()));
        m_SecondsSinceReport = 0.0f;
        m_FramesSinceReport = 0;
    }
}

void SandboxLayer::OnEvent(Trinity::Event& event)
{
    if (event.GetEventType() != Trinity::EventType::MouseMoved)
    {
        TR_TRACE("{}", event);
    }

    Trinity::EventDispatcher l_Dispatcher(event);
    l_Dispatcher.Dispatch<Trinity::KeyPressedEvent>(TR_BIND_EVENT_FN(OnKeyPressed));
}

void SandboxLayer::OnRender(Trinity::RHI::CommandList& commands)
{
    // Renderer memory is watched from the first frame after the readback
    if (m_SpritePhase == SpritePhase::Loading || m_SpritePhase == SpritePhase::Running)
    {
        Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
        Trinity::Renderer2D& l_Renderer2D = l_Renderer.GetRenderer2D();
        static_cast<void>(l_Renderer2D.DrawScene(commands, *m_SpriteScene, l_Renderer.GetSceneFormat(), l_Renderer.GetSceneWidth(), l_Renderer.GetSceneHeight()));
        if (m_SpritePhase == SpritePhase::Running && !m_SpriteReported)
        {
            const Trinity::Renderer2D::Statistics& l_Statistics = l_Renderer2D.GetStatistics();
            const bool l_Drawn = l_Statistics.Sprites == c_SpriteCount && (l_Statistics.DrawCalls == 1 || Trinity::Application::Get().GetDevice().GetInfo().API == Trinity::GraphicsAPI::None);
            m_SpriteBadFrames += l_Drawn ? 0 : 1;

            ++m_SpriteFrames;
            m_SpriteBytesLast = Trinity::Memory::GetStats(Trinity::MemoryTag::Renderer).CurrentBytes;
            if (m_SpriteFrames == c_SpriteCheckFrame)
            {
                m_SpriteBytesAtCheck = m_SpriteBytesLast;
            }

            if (m_SpriteFrames == c_SpriteReportFrame)
            {
                ReportSprites();
            }
        }
    }
}

bool SandboxLayer::OnKeyPressed(Trinity::KeyPressedEvent& event)
{
    if (event.GetKeyCode() == Trinity::KeyCode::TR_ESCAPE)
    {
        Trinity::Application::Get().Close();

        return true;
    }

    if (event.GetKeyCode() == Trinity::KeyCode::TR_M)
    {
        Trinity::Memory::LogUsage();

        return true;
    }

    if (event.GetKeyCode() == Trinity::KeyCode::TR_C)
    {
        Trinity::ConsoleVariables::LogAll();

        return true;
    }

    if (event.GetKeyCode() == Trinity::KeyCode::TR_V)
    {
        Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
        l_Renderer.SetVSync(!l_Renderer.IsVSync());

        return true;
    }

    return false;
}

// Three dispatches and a draw in one frame, outside the renderer's: FillBuffer, then ReverseBuffer behind an UnorderedAccess to UnorderedAccess barrier, then FillTexture, whose texture the scene copy pipeline draws into a render target. The buffer, the texture and the target are read back and compared byte for byte
void SandboxLayer::TestCompute()
{
    TR_PROFILE_FUNCTION();

    constexpr Trinity::RHI::Format c_Format = Trinity::RHI::Format::RGBA8Unorm;
    constexpr std::uint64_t c_BufferSize = std::uint64_t{ c_ComputeWordCount } * 2 * 4;

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();

    const std::string_view l_Extension = l_Info.API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";
    const auto a_ReadShader = [l_Extension](std::string_view name) { return Trinity::FileSystem::ReadFile(std::format("/engine/shaders/{}.{}", name, l_Extension)); };
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_FillBufferShader = a_ReadShader("ComputeTest.FillBuffer");
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_ReverseBufferShader = a_ReadShader("ComputeTest.ReverseBuffer");
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_FillTextureShader = a_ReadShader("ComputeTest.FillTexture");
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_CopyVertexShader = a_ReadShader("SceneCopy.VertexMain");
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_CopyPixelShader = a_ReadShader("SceneCopy.PixelMain");
    if (!l_FillBufferShader || !l_ReverseBufferShader || !l_FillTextureShader || !l_CopyVertexShader || !l_CopyPixelShader)
    {
        TR_ERROR("Compute: the ComputeTest and SceneCopy {} shaders could not be read from /engine/shaders", l_Extension);

        return;
    }

    Trinity::RHI::ComputePipelineDescription l_ComputeDescription;
    l_ComputeDescription.ComputeShader = { *l_FillBufferShader, "FillBuffer" };
    l_ComputeDescription.DebugName = "Sandbox fill buffer";
    const Trinity::RHI::PipelineHandle l_FillBuffer = l_Device.CreateComputePipeline(l_ComputeDescription);

    l_ComputeDescription.ComputeShader = { *l_ReverseBufferShader, "ReverseBuffer" };
    l_ComputeDescription.DebugName = "Sandbox reverse buffer";
    const Trinity::RHI::PipelineHandle l_ReverseBuffer = l_Device.CreateComputePipeline(l_ComputeDescription);

    l_ComputeDescription.ComputeShader = { *l_FillTextureShader, "FillTexture" };
    l_ComputeDescription.DebugName = "Sandbox fill texture";
    const Trinity::RHI::PipelineHandle l_FillTexture = l_Device.CreateComputePipeline(l_ComputeDescription);

    const std::array<Trinity::RHI::Format, 1> l_ColorFormats{ c_Format };
    Trinity::RHI::GraphicsPipelineDescription l_CopyDescription;
    l_CopyDescription.VertexShader = { *l_CopyVertexShader, "VertexMain" };
    l_CopyDescription.PixelShader = { *l_CopyPixelShader, "PixelMain" };
    l_CopyDescription.ColorFormats = l_ColorFormats;
    l_CopyDescription.Cull = Trinity::RHI::CullMode::None;
    l_CopyDescription.DebugName = "Sandbox compute copy";
    const Trinity::RHI::PipelineHandle l_Copy = l_Device.CreateGraphicsPipeline(l_CopyDescription);

    Trinity::RHI::BufferDescription l_BufferDescription;
    l_BufferDescription.Size = c_BufferSize;
    l_BufferDescription.Usage = Trinity::RHI::BufferUsage::UnorderedAccess | Trinity::RHI::BufferUsage::CopySource;
    l_BufferDescription.DebugName = "Sandbox storage buffer";
    const Trinity::RHI::BufferHandle l_Buffer = l_Device.CreateBuffer(l_BufferDescription);

    Trinity::RHI::TextureDescription l_TextureDescription;
    l_TextureDescription.Width = c_ComputeTextureSize;
    l_TextureDescription.Height = c_ComputeTextureSize;
    l_TextureDescription.TextureFormat = c_Format;
    l_TextureDescription.Usage = Trinity::RHI::TextureUsage::UnorderedAccess | Trinity::RHI::TextureUsage::ShaderResource | Trinity::RHI::TextureUsage::CopySource;
    l_TextureDescription.DebugName = "Sandbox storage texture";
    const Trinity::RHI::TextureHandle l_Texture = l_Device.CreateTexture(l_TextureDescription);

    Trinity::RHI::TextureDescription l_TargetDescription = l_TextureDescription;
    l_TargetDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget | Trinity::RHI::TextureUsage::CopySource;
    l_TargetDescription.DebugName = "Sandbox compute copy target";
    const Trinity::RHI::TextureHandle l_Target = l_Device.CreateTexture(l_TargetDescription);

    const std::uint64_t l_RowPitch = Trinity::RHI::GetTextureCopyRowPitch(c_Format, c_ComputeTextureSize);

    Trinity::RHI::BufferDescription l_ReadbackDescription;
    l_ReadbackDescription.Size = c_BufferSize;
    l_ReadbackDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_ReadbackDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_ReadbackDescription.DebugName = "Sandbox storage buffer readback";
    const Trinity::RHI::BufferHandle l_BufferReadback = l_Device.CreateBuffer(l_ReadbackDescription);

    l_ReadbackDescription.Size = l_RowPitch * c_ComputeTextureSize;
    l_ReadbackDescription.DebugName = "Sandbox storage texture readback";
    const Trinity::RHI::BufferHandle l_TextureReadback = l_Device.CreateBuffer(l_ReadbackDescription);

    l_ReadbackDescription.DebugName = "Sandbox compute copy readback";
    const Trinity::RHI::BufferHandle l_TargetReadback = l_Device.CreateBuffer(l_ReadbackDescription);

    const auto a_Destroy = [&]()
    {
        l_Device.DestroyBuffer(l_TargetReadback);
        l_Device.DestroyBuffer(l_TextureReadback);
        l_Device.DestroyBuffer(l_BufferReadback);
        l_Device.DestroyTexture(l_Target);
        l_Device.DestroyTexture(l_Texture);
        l_Device.DestroyBuffer(l_Buffer);
        l_Device.DestroyPipeline(l_Copy);
        l_Device.DestroyPipeline(l_FillTexture);
        l_Device.DestroyPipeline(l_ReverseBuffer);
        l_Device.DestroyPipeline(l_FillBuffer);
    };

    if (!l_FillBuffer || !l_ReverseBuffer || !l_FillTexture || !l_Copy || !l_Buffer || !l_Texture || !l_Target || !l_BufferReadback || !l_TextureReadback || !l_TargetReadback)
    {
        TR_ERROR("Compute: could not create the pipelines, the storage buffer and texture, or their readbacks");
        a_Destroy();

        return;
    }

    ComputePushData l_Push;
    l_Push.Buffer = { l_Device.GetUnorderedAccessIndex(l_Buffer), 0 };
    l_Push.Texture = { l_Device.GetUnorderedAccessIndex(l_Texture), 0 };
    l_Push.Count = c_ComputeWordCount;
    l_Push.Size = c_ComputeTextureSize;

    Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();
    l_Commands.BufferBarrier(l_Buffer, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::UnorderedAccess);
    l_Commands.TextureBarrier(l_Texture, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::UnorderedAccess);

    l_Commands.SetPipeline(l_FillBuffer);
    l_Commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
    l_Commands.Dispatch(c_ComputeWordCount / c_ComputeBufferGroupSize, 1, 1);

    l_Commands.BufferBarrier(l_Buffer, Trinity::RHI::ResourceState::UnorderedAccess, Trinity::RHI::ResourceState::UnorderedAccess);
    l_Commands.SetPipeline(l_ReverseBuffer);
    l_Commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
    l_Commands.Dispatch(c_ComputeWordCount / c_ComputeBufferGroupSize, 1, 1);

    l_Commands.SetPipeline(l_FillTexture);
    l_Commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
    l_Commands.Dispatch(c_ComputeTextureSize / c_ComputeTextureGroupSize, c_ComputeTextureSize / c_ComputeTextureGroupSize, 1);

    l_Commands.BufferBarrier(l_Buffer, Trinity::RHI::ResourceState::UnorderedAccess, Trinity::RHI::ResourceState::CopySource);
    l_Commands.CopyBuffer(l_Buffer, 0, l_BufferReadback, 0, c_BufferSize);

    l_Commands.TextureBarrier(l_Texture, Trinity::RHI::ResourceState::UnorderedAccess, Trinity::RHI::ResourceState::ShaderResource);
    l_Commands.TextureBarrier(l_Target, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::RenderTarget);

    const std::array<Trinity::RHI::ColorAttachment, 1> l_Attachments{ Trinity::RHI::ColorAttachment{ l_Target, Trinity::RHI::LoadOp::DontCare, Trinity::RHI::StoreOp::Store } };
    Trinity::RHI::RenderingDescription l_Rendering;
    l_Rendering.ColorAttachments = l_Attachments;
    l_Rendering.RenderArea = { 0, 0, c_ComputeTextureSize, c_ComputeTextureSize };
    l_Commands.BeginRendering(l_Rendering);
    l_Commands.SetPipeline(l_Copy);
    l_Commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(c_ComputeTextureSize), static_cast<float>(c_ComputeTextureSize), 0.0f, 1.0f });
    l_Commands.SetScissor({ 0, 0, c_ComputeTextureSize, c_ComputeTextureSize });
    const std::array<std::uint32_t, 2> l_CopyPush{ l_Device.GetShaderResourceIndex(l_Texture), 0 };
    l_Commands.PushConstants(std::as_bytes(std::span(l_CopyPush)));
    l_Commands.Draw(3, 1, 0, 0);
    l_Commands.EndRendering();

    l_Commands.TextureBarrier(l_Texture, Trinity::RHI::ResourceState::ShaderResource, Trinity::RHI::ResourceState::CopySource);
    l_Commands.CopyTextureToBuffer(l_Texture, 0, 0, l_TextureReadback, 0);
    l_Commands.TextureBarrier(l_Target, Trinity::RHI::ResourceState::RenderTarget, Trinity::RHI::ResourceState::CopySource);
    l_Commands.CopyTextureToBuffer(l_Target, 0, 0, l_TargetReadback, 0);
    l_Device.EndFrame();
    l_Device.WaitIdle();

    // The null device runs nothing, so only a GPU's readbacks have contents to check
    if (l_Info.API == Trinity::GraphicsAPI::None)
    {
        TR_INFO("Compute: None recorded 3 dispatches and a draw reading the storage texture");
        a_Destroy();

        return;
    }

    const std::vector<std::uint32_t> l_Words = MakeComputeWords();
    const std::span<const std::byte> l_BufferData = l_Device.GetMappedData(l_BufferReadback);
    const std::span<const std::byte> l_TextureData = l_Device.GetMappedData(l_TextureReadback);
    const std::span<const std::byte> l_TargetData = l_Device.GetMappedData(l_TargetReadback);

    std::string l_Wrong;
    const auto a_Mismatch = [&l_Wrong](std::string message) { l_Wrong += std::format("{}{}", l_Wrong.empty() ? "" : "; ", message); };
    if (const std::optional<std::size_t> l_Word = FindWrongWord(l_BufferData, l_Words))
    {
        a_Mismatch(std::format("the storage buffer differs at word {}, which {} wrote", *l_Word, *l_Word < c_ComputeWordCount ? "FillBuffer" : "ReverseBuffer"));
    }

    if (const std::optional<std::array<std::uint32_t, 2>> l_Texel = FindWrongTexel(l_TextureData, l_RowPitch))
    {
        a_Mismatch(std::format("the storage texture differs at ({}, {})", (*l_Texel)[0], (*l_Texel)[1]));
    }

    if (const std::optional<std::array<std::uint32_t, 2>> l_Texel = FindWrongTexel(l_TargetData, l_RowPitch))
    {
        a_Mismatch(std::format("the draw reading the storage texture differs at ({}, {})", (*l_Texel)[0], (*l_Texel)[1]));
    }

    a_Destroy();

    if (!l_Wrong.empty())
    {
        TR_ERROR("Compute: the readback differs: {}", l_Wrong);

        return;
    }

    TR_INFO("Compute: {} filled a storage buffer of {} and a {}x{} storage texture, and all {} bytes of the buffer, the texture and a draw reading the texture read back as expected", Trinity::ToString(l_Info.API), Trinity::Memory::FormatBytes(c_BufferSize), c_ComputeTextureSize, c_ComputeTextureSize, c_BufferSize + 2 * std::uint64_t{ c_ComputeTextureSize } * c_ComputeTextureSize * 4);
}

// Clears every mip of every face of a cube and every layer of an array to its own colour, each moved in and out of RenderTarget by a barrier on that mip and layer alone, and reads each back by a copy. A dispatch then reads the same colours through the cube's and the array's shader views
void SandboxLayer::TestLayeredTextures()
{
    TR_PROFILE_FUNCTION();

    struct Layered
    {
        bool Cube = false;
        Trinity::RHI::TextureHandle Texture;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t Layers = 0;
    };

    constexpr Trinity::RHI::Format c_Format = Trinity::RHI::Format::RGBA8Unorm;
    constexpr std::uint32_t c_WordCount = (Trinity::RHI::c_CubeFaceCount + c_LayeredArrayLayers) * c_LayeredMipLevels;

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();

    const std::string_view l_Extension = l_Info.API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_ReadLayersShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/LayeredTest.ReadLayers.{}", l_Extension));
    if (!l_ReadLayersShader)
    {
        TR_ERROR("Layers: the LayeredTest {} shader could not be read from /engine/shaders", l_Extension);

        return;
    }

    Trinity::RHI::ComputePipelineDescription l_PipelineDescription;
    l_PipelineDescription.ComputeShader = { *l_ReadLayersShader, "ReadLayers" };
    l_PipelineDescription.DebugName = "Sandbox read layers";
    const Trinity::RHI::PipelineHandle l_ReadLayers = l_Device.CreateComputePipeline(l_PipelineDescription);

    Trinity::RHI::SamplerDescription l_SamplerDescription;
    l_SamplerDescription.MinFilter = Trinity::RHI::Filter::Nearest;
    l_SamplerDescription.MagFilter = Trinity::RHI::Filter::Nearest;
    l_SamplerDescription.MipFilter = Trinity::RHI::Filter::Nearest;
    l_SamplerDescription.AddressU = Trinity::RHI::AddressMode::ClampToEdge;
    l_SamplerDescription.AddressV = Trinity::RHI::AddressMode::ClampToEdge;
    l_SamplerDescription.AddressW = Trinity::RHI::AddressMode::ClampToEdge;
    l_SamplerDescription.DebugName = "Sandbox layers sampler";
    const Trinity::RHI::SamplerHandle l_Sampler = l_Device.CreateSampler(l_SamplerDescription);

    // Clear colours change from layer to layer, so no single optimized clear value fits
    Trinity::RHI::TextureDescription l_TextureDescription;
    l_TextureDescription.MipLevels = c_LayeredMipLevels;
    l_TextureDescription.TextureFormat = c_Format;
    l_TextureDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget | Trinity::RHI::TextureUsage::ShaderResource | Trinity::RHI::TextureUsage::CopySource;
    l_TextureDescription.OptimizedClear = false;

    l_TextureDescription.Width = c_LayeredCubeSize;
    l_TextureDescription.Height = c_LayeredCubeSize;
    l_TextureDescription.ArrayLayers = Trinity::RHI::c_CubeFaceCount;
    l_TextureDescription.Dimension = Trinity::RHI::TextureDimension::TextureCube;
    l_TextureDescription.DebugName = "Sandbox cube";
    const Layered l_Cube{ true, l_Device.CreateTexture(l_TextureDescription), c_LayeredCubeSize, c_LayeredCubeSize, Trinity::RHI::c_CubeFaceCount };

    l_TextureDescription.Width = c_LayeredArrayWidth;
    l_TextureDescription.Height = c_LayeredArrayHeight;
    l_TextureDescription.ArrayLayers = c_LayeredArrayLayers;
    l_TextureDescription.Dimension = Trinity::RHI::TextureDimension::Texture2DArray;
    l_TextureDescription.DebugName = "Sandbox array";
    const Layered l_Array{ false, l_Device.CreateTexture(l_TextureDescription), c_LayeredArrayWidth, c_LayeredArrayHeight, c_LayeredArrayLayers };

    // Every mip of every layer gets a slot of the same size, the largest mip's rounded up to the copy offset alignment
    const std::uint64_t l_LargestMip = std::max(Trinity::RHI::GetTextureCopySize(c_Format, c_LayeredCubeSize, c_LayeredCubeSize), Trinity::RHI::GetTextureCopySize(c_Format, c_LayeredArrayWidth, c_LayeredArrayHeight));
    const std::uint64_t l_SlotSize = (l_LargestMip + Trinity::RHI::c_TextureCopyOffsetAlignment - 1) / Trinity::RHI::c_TextureCopyOffsetAlignment * Trinity::RHI::c_TextureCopyOffsetAlignment;

    Trinity::RHI::BufferDescription l_BufferDescription;
    l_BufferDescription.Size = l_SlotSize * c_WordCount;
    l_BufferDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_BufferDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_BufferDescription.DebugName = "Sandbox layers readback";
    const Trinity::RHI::BufferHandle l_Readback = l_Device.CreateBuffer(l_BufferDescription);

    l_BufferDescription.Size = std::uint64_t{ c_WordCount } * 4;
    l_BufferDescription.Usage = Trinity::RHI::BufferUsage::UnorderedAccess | Trinity::RHI::BufferUsage::CopySource;
    l_BufferDescription.Memory = Trinity::RHI::MemoryType::GPU;
    l_BufferDescription.DebugName = "Sandbox layers words";
    const Trinity::RHI::BufferHandle l_Words = l_Device.CreateBuffer(l_BufferDescription);

    l_BufferDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_BufferDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_BufferDescription.DebugName = "Sandbox layers words readback";
    const Trinity::RHI::BufferHandle l_WordsReadback = l_Device.CreateBuffer(l_BufferDescription);

    const auto a_Destroy = [&]()
    {
        l_Device.DestroyBuffer(l_WordsReadback);
        l_Device.DestroyBuffer(l_Words);
        l_Device.DestroyBuffer(l_Readback);
        l_Device.DestroyTexture(l_Array.Texture);
        l_Device.DestroyTexture(l_Cube.Texture);
        l_Device.DestroySampler(l_Sampler);
        l_Device.DestroyPipeline(l_ReadLayers);
    };

    if (!l_ReadLayers || !l_Sampler || !l_Cube.Texture || !l_Array.Texture || !l_Readback || !l_Words || !l_WordsReadback)
    {
        TR_ERROR("Layers: could not create the pipeline, the sampler, the cube and array, or their readbacks");
        a_Destroy();

        return;
    }

    const std::array<Layered, 2> l_Textures{ l_Cube, l_Array };

    Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();
    std::uint32_t l_Slot = 0;
    for (const Layered& it_Texture : l_Textures)
    {
        for (std::uint32_t it_Layer = 0; it_Layer < it_Texture.Layers; ++it_Layer)
        {
            for (std::uint32_t it_Mip = 0; it_Mip < c_LayeredMipLevels; ++it_Mip)
            {
                const Trinity::RHI::TextureSubresourceRange l_Range{ it_Mip, 1, it_Layer, 1 };
                const std::array<std::uint8_t, 4> l_Color = MakeLayerColor(it_Texture.Cube, it_Layer, it_Mip);

                Trinity::RHI::ColorAttachment l_Attachment{ it_Texture.Texture, Trinity::RHI::LoadOp::Clear, Trinity::RHI::StoreOp::Store };
                std::ranges::transform(l_Color, l_Attachment.ClearColor.begin(), [](std::uint8_t channel) { return static_cast<float>(channel) / 255.0f; });
                l_Attachment.MipLevel = it_Mip;
                l_Attachment.ArrayLayer = it_Layer;

                Trinity::RHI::RenderingDescription l_Rendering;
                l_Rendering.ColorAttachments = std::span(&l_Attachment, 1);

                l_Commands.TextureBarrier(it_Texture.Texture, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::RenderTarget, l_Range);
                l_Commands.BeginRendering(l_Rendering);
                l_Commands.EndRendering();
                l_Commands.TextureBarrier(it_Texture.Texture, Trinity::RHI::ResourceState::RenderTarget, Trinity::RHI::ResourceState::CopySource, l_Range);
                l_Commands.CopyTextureToBuffer(it_Texture.Texture, it_Mip, it_Layer, l_Readback, l_Slot++ * l_SlotSize);
            }
        }

        l_Commands.TextureBarrier(it_Texture.Texture, Trinity::RHI::ResourceState::CopySource, Trinity::RHI::ResourceState::ShaderResource);
    }

    LayeredPushData l_Push;
    l_Push.Cube = { l_Device.GetShaderResourceIndex(l_Cube.Texture), 0 };
    l_Push.Array = { l_Device.GetShaderResourceIndex(l_Array.Texture), 0 };
    l_Push.Sampler = { l_Device.GetSamplerIndex(l_Sampler), 0 };
    l_Push.Output = { l_Device.GetUnorderedAccessIndex(l_Words), 0 };
    l_Push.MipLevels = c_LayeredMipLevels;
    l_Push.ArrayLayers = c_LayeredArrayLayers;

    l_Commands.BufferBarrier(l_Words, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::UnorderedAccess);
    l_Commands.SetPipeline(l_ReadLayers);
    l_Commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
    l_Commands.Dispatch(1, 1, 2);
    l_Commands.BufferBarrier(l_Words, Trinity::RHI::ResourceState::UnorderedAccess, Trinity::RHI::ResourceState::CopySource);
    l_Commands.CopyBuffer(l_Words, 0, l_WordsReadback, 0, std::uint64_t{ c_WordCount } * 4);
    l_Device.EndFrame();
    l_Device.WaitIdle();

    if (l_Info.API == Trinity::GraphicsAPI::None)
    {
        TR_INFO("Layers: None recorded a clear and a copy for each of {} mips of {} cube faces and {} array layers", c_LayeredMipLevels, Trinity::RHI::c_CubeFaceCount, c_LayeredArrayLayers);
        a_Destroy();

        return;
    }

    const std::span<const std::byte> l_Texels = l_Device.GetMappedData(l_Readback);
    const std::span<const std::byte> l_ReadWords = l_Device.GetMappedData(l_WordsReadback);

    std::string l_Wrong;
    l_Slot = 0;
    for (const Layered& it_Texture : l_Textures)
    {
        for (std::uint32_t it_Layer = 0; it_Layer < it_Texture.Layers; ++it_Layer)
        {
            for (std::uint32_t it_Mip = 0; it_Mip < c_LayeredMipLevels; ++it_Mip)
            {
                const std::array<std::uint8_t, 4> l_Color = MakeLayerColor(it_Texture.Cube, it_Layer, it_Mip);
                const std::uint32_t l_Width = Trinity::RHI::GetMipSize(it_Texture.Width, it_Mip);
                const std::uint32_t l_Height = Trinity::RHI::GetMipSize(it_Texture.Height, it_Mip);
                const std::uint64_t l_RowPitch = Trinity::RHI::GetTextureCopyRowPitch(c_Format, l_Width);
                const std::uint64_t l_Base = l_Slot * l_SlotSize;

                bool l_Cleared = l_Texels.size() >= l_Base + l_RowPitch * l_Height;
                for (std::uint32_t it_Y = 0; it_Y < l_Height && l_Cleared; ++it_Y)
                {
                    for (std::uint32_t it_X = 0; it_X < l_Width && l_Cleared; ++it_X)
                    {
                        l_Cleared = std::memcmp(l_Texels.data() + static_cast<std::size_t>(l_Base + it_Y * l_RowPitch + it_X * 4), l_Color.data(), l_Color.size()) == 0;
                    }
                }

                std::uint32_t l_Expected = 0;
                std::memcpy(&l_Expected, l_Color.data(), sizeof(l_Expected));
                const bool l_Read = l_ReadWords.size() >= (std::size_t{ l_Slot } + 1) * 4 && std::memcmp(l_ReadWords.data() + std::size_t{ l_Slot } * 4, &l_Expected, 4) == 0;

                if (!l_Cleared || !l_Read)
                {
                    l_Wrong += std::format("{}{} {} {} mip {}{}", l_Wrong.empty() ? "" : "; ", it_Texture.Cube ? "cube" : "array", it_Texture.Cube ? "face" : "layer", it_Layer, it_Mip, !l_Cleared ? " reads back the wrong texels" : " reads wrong through its shader view");
                }

                ++l_Slot;
            }
        }
    }

    a_Destroy();

    if (!l_Wrong.empty())
    {
        TR_ERROR("Layers: {}", l_Wrong);

        return;
    }

    TR_INFO("Layers: {} cleared each of {} mips of {} cube faces and {} array layers to its own colour, and every one reads back by a copy and through its texture's shader view", Trinity::ToString(l_Info.API), c_LayeredMipLevels, Trinity::RHI::c_CubeFaceCount, c_LayeredArrayLayers);
}

// Culling with both windings, constant depth bias of 0, +16 and -16 over a plane at the same depth, depth with and without clamping beyond the far end of the range, then R11G11B10Float and RGBA16Float targets cleared to values each format stores exactly. Every cell is probed and both float targets read back bit for bit
void SandboxLayer::TestRasterState()
{
    TR_PROFILE_FUNCTION();

    enum RasterPipeline : std::uint32_t
    {
        CullBack,
        CullFront,
        CullBackClockwise,
        CullNone,
        DepthPlane,
        Overlay,
        OverlayBiasUp,
        OverlayBiasDown,
        Unclamped,
        Clamped,
        PipelineCount
    };

    struct Probe
    {
        std::string_view Name;
        std::uint32_t Cell = 0;
        std::uint32_t X = 0;
        std::uint32_t Y = 0;
        std::array<std::uint8_t, 3> Expected{};
    };

    constexpr std::array<std::uint8_t, 3> c_Black{ 0, 0, 0 };
    constexpr std::array<std::uint8_t, 3> c_Red{ 255, 0, 0 };
    constexpr std::array<std::uint8_t, 3> c_Green{ 0, 255, 0 };
    constexpr std::array<std::uint8_t, 3> c_Blue{ 0, 0, 255 };
    constexpr std::array<std::uint8_t, 3> c_Yellow{ 255, 255, 0 };

    // The left triangle winds counter-clockwise and the right one clockwise, as seen with Y up
    constexpr std::array<Probe, 18> c_Probes
    { {
        { "back culling keeps the counter-clockwise triangle", 0, 8, 20, c_Red },
        { "back culling drops the clockwise triangle", 0, 24, 20, c_Black },
        { "front culling drops the counter-clockwise triangle", 1, 8, 20, c_Black },
        { "front culling keeps the clockwise triangle", 1, 24, 20, c_Green },
        { "back culling with clockwise front faces drops the counter-clockwise triangle", 2, 8, 20, c_Black },
        { "back culling with clockwise front faces keeps the clockwise triangle", 2, 24, 20, c_Green },
        { "no culling keeps the counter-clockwise triangle", 3, 8, 20, c_Red },
        { "no culling keeps the clockwise triangle", 3, 24, 20, c_Green },
        { "outside both triangles", 3, 16, 2, c_Black },
        { "a coplanar overlay without bias fails a greater test", 4, 16, 16, c_Blue },
        { "a coplanar overlay biased by +16 passes a greater test", 5, 16, 16, c_Green },
        { "a coplanar overlay biased by -16 fails a greater-or-equal test", 6, 16, 16, c_Blue },
        { "a triangle beyond the depth range is clipped without depth clamp", 7, 16, 16, c_Black },
        { "a triangle beyond the depth range is drawn with depth clamp", 8, 16, 16, c_Yellow },
        { "the biased overlay covers its whole cell", 5, 1, 30, c_Green },
        { "the clamped triangle covers its whole cell", 8, 30, 1, c_Yellow },
        { "the first cell's background", 0, 16, 2, c_Black },
        { "the last cell's corner", 8, 31, 31, c_Yellow }
    } };

    constexpr Trinity::RHI::Format c_Format = Trinity::RHI::Format::RGBA8Unorm;
    constexpr Trinity::RHI::Format c_DepthFormat = Trinity::RHI::Format::D32Float;
    constexpr std::uint32_t c_Width = c_RasterCellSize * c_RasterCellCount;

    // 0.5, 2 and 0.25 as R11G11B10's 6-bit and 5-bit mantissa floats with an exponent bias of 15, red in the low bits
    constexpr std::uint32_t c_PackedR11G11B10 = (14u << 6) | ((16u << 6) << 11) | ((13u << 5) << 22);
    constexpr std::array<std::uint16_t, 4> c_HalfRGBA16{ 0x3800, 0x4000, 0x3400, 0x3C00 };
    constexpr std::array<float, 4> c_FloatClear{ 0.5f, 2.0f, 0.25f, 1.0f };

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();

    const std::string_view l_Extension = l_Info.API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_VertexShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/RasterTest.VertexMain.{}", l_Extension));
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_PixelShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/RasterTest.PixelMain.{}", l_Extension));
    if (!l_VertexShader || !l_PixelShader)
    {
        TR_ERROR("Raster: the RasterTest {} shaders could not be read from /engine/shaders", l_Extension);

        return;
    }

    const std::array<Trinity::RHI::Format, 1> l_ColorFormats{ c_Format };
    std::array<Trinity::RHI::GraphicsPipelineDescription, PipelineCount> l_Descriptions{};
    for (Trinity::RHI::GraphicsPipelineDescription& it_Description : l_Descriptions)
    {
        it_Description.VertexShader = { *l_VertexShader, "VertexMain" };
        it_Description.PixelShader = { *l_PixelShader, "PixelMain" };
        it_Description.ColorFormats = l_ColorFormats;
        it_Description.DepthFormat = c_DepthFormat;
        it_Description.Cull = Trinity::RHI::CullMode::None;
        it_Description.DebugName = "Sandbox raster state";
    }

    l_Descriptions[CullBack].Cull = Trinity::RHI::CullMode::Back;
    l_Descriptions[CullFront].Cull = Trinity::RHI::CullMode::Front;
    l_Descriptions[CullBackClockwise].Cull = Trinity::RHI::CullMode::Back;
    l_Descriptions[CullBackClockwise].FrontCounterClockwise = false;

    l_Descriptions[DepthPlane].DepthTest = true;
    l_Descriptions[DepthPlane].DepthWrite = true;
    for (const RasterPipeline it_Overlay : { Overlay, OverlayBiasUp, OverlayBiasDown })
    {
        l_Descriptions[it_Overlay].DepthTest = true;
        l_Descriptions[it_Overlay].DepthCompare = Trinity::RHI::CompareOp::Greater;
    }

    l_Descriptions[OverlayBiasUp].DepthBiasConstant = 16;
    l_Descriptions[OverlayBiasDown].DepthBiasConstant = -16;
    l_Descriptions[OverlayBiasDown].DepthCompare = Trinity::RHI::CompareOp::GreaterOrEqual;
    l_Descriptions[Clamped].DepthClamp = true;

    std::array<Trinity::RHI::PipelineHandle, PipelineCount> l_Pipelines{};
    std::ranges::transform(l_Descriptions, l_Pipelines.begin(), [&l_Device](const Trinity::RHI::GraphicsPipelineDescription& description) { return l_Device.CreateGraphicsPipeline(description); });

    Trinity::RHI::TextureDescription l_TargetDescription;
    l_TargetDescription.Width = c_Width;
    l_TargetDescription.Height = c_RasterCellSize;
    l_TargetDescription.TextureFormat = c_Format;
    l_TargetDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget | Trinity::RHI::TextureUsage::CopySource;
    l_TargetDescription.DebugName = "Sandbox raster target";
    const Trinity::RHI::TextureHandle l_Target = l_Device.CreateTexture(l_TargetDescription);

    Trinity::RHI::TextureDescription l_DepthDescription = l_TargetDescription;
    l_DepthDescription.TextureFormat = c_DepthFormat;
    l_DepthDescription.Usage = Trinity::RHI::TextureUsage::DepthStencil;
    l_DepthDescription.DebugName = "Sandbox raster depth";
    const Trinity::RHI::TextureHandle l_Depth = l_Device.CreateTexture(l_DepthDescription);

    Trinity::RHI::TextureDescription l_FloatDescription;
    l_FloatDescription.Width = c_RasterFloatSize;
    l_FloatDescription.Height = c_RasterFloatSize;
    l_FloatDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget | Trinity::RHI::TextureUsage::CopySource;
    l_FloatDescription.ClearColor = c_FloatClear;
    l_FloatDescription.TextureFormat = Trinity::RHI::Format::R11G11B10Float;
    l_FloatDescription.DebugName = "Sandbox R11G11B10Float target";
    const Trinity::RHI::TextureHandle l_SmallFloat = l_Device.CreateTexture(l_FloatDescription);

    l_FloatDescription.TextureFormat = Trinity::RHI::Format::RGBA16Float;
    l_FloatDescription.DebugName = "Sandbox RGBA16Float target";
    const Trinity::RHI::TextureHandle l_HalfFloat = l_Device.CreateTexture(l_FloatDescription);

    // The cells, then each float target, every part starting on the copy offset alignment
    const auto a_Align = [](std::uint64_t size) { return (size + Trinity::RHI::c_TextureCopyOffsetAlignment - 1) / Trinity::RHI::c_TextureCopyOffsetAlignment * Trinity::RHI::c_TextureCopyOffsetAlignment; };
    const std::uint64_t l_RowPitch = Trinity::RHI::GetTextureCopyRowPitch(c_Format, c_Width);
    const std::uint64_t l_SmallFloatOffset = a_Align(Trinity::RHI::GetTextureCopySize(c_Format, c_Width, c_RasterCellSize));
    const std::uint64_t l_HalfFloatOffset = l_SmallFloatOffset + a_Align(Trinity::RHI::GetTextureCopySize(Trinity::RHI::Format::R11G11B10Float, c_RasterFloatSize, c_RasterFloatSize));

    Trinity::RHI::BufferDescription l_ReadbackDescription;
    l_ReadbackDescription.Size = l_HalfFloatOffset + Trinity::RHI::GetTextureCopySize(Trinity::RHI::Format::RGBA16Float, c_RasterFloatSize, c_RasterFloatSize);
    l_ReadbackDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_ReadbackDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_ReadbackDescription.DebugName = "Sandbox raster readback";
    const Trinity::RHI::BufferHandle l_Readback = l_Device.CreateBuffer(l_ReadbackDescription);

    const auto a_Destroy = [&]()
    {
        l_Device.DestroyBuffer(l_Readback);
        l_Device.DestroyTexture(l_HalfFloat);
        l_Device.DestroyTexture(l_SmallFloat);
        l_Device.DestroyTexture(l_Depth);
        l_Device.DestroyTexture(l_Target);
        for (const Trinity::RHI::PipelineHandle it_Pipeline : l_Pipelines)
        {
            l_Device.DestroyPipeline(it_Pipeline);
        }
    };

    if (!std::ranges::all_of(l_Pipelines, [](Trinity::RHI::PipelineHandle pipeline) { return pipeline.IsValid(); }) || !l_Target || !l_Depth || !l_SmallFloat || !l_HalfFloat || !l_Readback)
    {
        TR_ERROR("Raster: could not create the pipelines, the targets or the readback");
        a_Destroy();

        return;
    }

    Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();
    l_Commands.TextureBarrier(l_Target, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::RenderTarget);
    l_Commands.TextureBarrier(l_Depth, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::DepthWrite);

    const std::array<Trinity::RHI::ColorAttachment, 1> l_Attachments{ Trinity::RHI::ColorAttachment{ l_Target, Trinity::RHI::LoadOp::Clear, Trinity::RHI::StoreOp::Store } };
    Trinity::RHI::RenderingDescription l_Rendering;
    l_Rendering.ColorAttachments = l_Attachments;
    l_Rendering.Depth = { l_Depth, Trinity::RHI::LoadOp::Clear, Trinity::RHI::StoreOp::DontCare, 0.0f };
    l_Commands.BeginRendering(l_Rendering);

    const auto a_Draw = [&l_Commands, &l_Pipelines](std::uint32_t cell, RasterPipeline pipeline, std::array<glm::vec4, 3> positions, glm::vec4 color)
    {
        const float l_X = static_cast<float>(cell * c_RasterCellSize);
        l_Commands.SetPipeline(l_Pipelines[pipeline]);
        l_Commands.SetViewport({ l_X, 0.0f, static_cast<float>(c_RasterCellSize), static_cast<float>(c_RasterCellSize), 0.0f, 1.0f });
        l_Commands.SetScissor({ static_cast<std::int32_t>(cell * c_RasterCellSize), 0, c_RasterCellSize, c_RasterCellSize });

        const RasterPushData l_Push{ positions, color };
        l_Commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
        l_Commands.Draw(3, 1, 0, 0);
    };

    const std::array<glm::vec4, 3> l_CounterClockwise{ glm::vec4(-0.9f, -0.9f, 0.5f, 1.0f), glm::vec4(-0.1f, -0.9f, 0.5f, 1.0f), glm::vec4(-0.5f, 0.9f, 0.5f, 1.0f) };
    const std::array<glm::vec4, 3> l_Clockwise{ glm::vec4(0.1f, -0.9f, 0.5f, 1.0f), glm::vec4(0.5f, 0.9f, 0.5f, 1.0f), glm::vec4(0.9f, -0.9f, 0.5f, 1.0f) };
    const auto a_Cover = [](float depth) { return std::array<glm::vec4, 3>{ glm::vec4(-1.0f, -1.0f, depth, 1.0f), glm::vec4(3.0f, -1.0f, depth, 1.0f), glm::vec4(-1.0f, 3.0f, depth, 1.0f) }; };
    const glm::vec4 l_Red(1.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec4 l_Green(0.0f, 1.0f, 0.0f, 1.0f);
    const glm::vec4 l_Blue(0.0f, 0.0f, 1.0f, 1.0f);
    const glm::vec4 l_Yellow(1.0f, 1.0f, 0.0f, 1.0f);

    for (const RasterPipeline it_Pipeline : { CullBack, CullFront, CullBackClockwise, CullNone })
    {
        a_Draw(it_Pipeline, it_Pipeline, l_CounterClockwise, l_Red);
        a_Draw(it_Pipeline, it_Pipeline, l_Clockwise, l_Green);
    }

    for (const RasterPipeline it_Overlay : { Overlay, OverlayBiasUp, OverlayBiasDown })
    {
        const std::uint32_t l_Cell = 4 + static_cast<std::uint32_t>(it_Overlay - Overlay);
        a_Draw(l_Cell, DepthPlane, a_Cover(0.5f), l_Blue);
        a_Draw(l_Cell, it_Overlay, a_Cover(0.5f), l_Green);
    }

    a_Draw(7, Unclamped, a_Cover(1.5f), l_Yellow);
    a_Draw(8, Clamped, a_Cover(1.5f), l_Yellow);
    l_Commands.EndRendering();

    l_Commands.TextureBarrier(l_Target, Trinity::RHI::ResourceState::RenderTarget, Trinity::RHI::ResourceState::CopySource);
    l_Commands.CopyTextureToBuffer(l_Target, 0, 0, l_Readback, 0);

    for (const Trinity::RHI::TextureHandle it_Float : { l_SmallFloat, l_HalfFloat })
    {
        const std::array<Trinity::RHI::ColorAttachment, 1> l_FloatAttachments{ Trinity::RHI::ColorAttachment{ it_Float, Trinity::RHI::LoadOp::Clear, Trinity::RHI::StoreOp::Store, c_FloatClear } };
        Trinity::RHI::RenderingDescription l_FloatRendering;
        l_FloatRendering.ColorAttachments = l_FloatAttachments;

        l_Commands.TextureBarrier(it_Float, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::RenderTarget);
        l_Commands.BeginRendering(l_FloatRendering);
        l_Commands.EndRendering();
        l_Commands.TextureBarrier(it_Float, Trinity::RHI::ResourceState::RenderTarget, Trinity::RHI::ResourceState::CopySource);
        l_Commands.CopyTextureToBuffer(it_Float, 0, 0, l_Readback, it_Float == l_SmallFloat ? l_SmallFloatOffset : l_HalfFloatOffset);
    }

    l_Device.EndFrame();
    l_Device.WaitIdle();

    if (l_Info.API == Trinity::GraphicsAPI::None)
    {
        TR_INFO("Raster: None recorded {} draws through {} pipelines and cleared R11G11B10Float and RGBA16Float targets", 16, static_cast<std::uint32_t>(PipelineCount));
        a_Destroy();

        return;
    }

    const std::span<const std::byte> l_Data = l_Device.GetMappedData(l_Readback);
    std::string l_Wrong;
    for (const Probe& it_Probe : c_Probes)
    {
        const std::size_t l_Offset = static_cast<std::size_t>(it_Probe.Y * l_RowPitch + (it_Probe.Cell * c_RasterCellSize + it_Probe.X) * 4);
        if (l_Offset + 4 > l_Data.size() || std::memcmp(l_Data.data() + l_Offset, it_Probe.Expected.data(), it_Probe.Expected.size()) != 0)
        {
            const auto a_Channel = [&l_Data, l_Offset](std::size_t channel) { return l_Offset + channel < l_Data.size() ? std::to_integer<std::uint32_t>(l_Data[l_Offset + channel]) : 0u; };
            l_Wrong += std::format("{}{} at ({}, {}) is ({}, {}, {}) where ({}, {}, {}) was expected", l_Wrong.empty() ? "" : "; ", it_Probe.Name, it_Probe.Cell * c_RasterCellSize + it_Probe.X, it_Probe.Y, a_Channel(0), a_Channel(1), a_Channel(2), it_Probe.Expected[0], it_Probe.Expected[1], it_Probe.Expected[2]);
        }
    }

    const auto a_Matches = [&l_Data](std::uint64_t offset, std::span<const std::byte> texel)
    {
        const std::uint64_t l_FloatPitch = Trinity::RHI::GetTextureCopyRowPitch(texel.size() == 4 ? Trinity::RHI::Format::R11G11B10Float : Trinity::RHI::Format::RGBA16Float, c_RasterFloatSize);
        for (std::uint32_t it_Y = 0; it_Y < c_RasterFloatSize; ++it_Y)
        {
            for (std::uint32_t it_X = 0; it_X < c_RasterFloatSize; ++it_X)
            {
                const std::uint64_t l_Texel = offset + it_Y * l_FloatPitch + it_X * texel.size();
                if (l_Texel + texel.size() > l_Data.size() || std::memcmp(l_Data.data() + l_Texel, texel.data(), texel.size()) != 0)
                {
                    return false;
                }
            }
        }

        return true;
    };

    if (!a_Matches(l_SmallFloatOffset, std::as_bytes(std::span(&c_PackedR11G11B10, 1))))
    {
        l_Wrong += std::format("{}the R11G11B10Float target does not read back as 0x{:08x}", l_Wrong.empty() ? "" : "; ", c_PackedR11G11B10);
    }

    if (!a_Matches(l_HalfFloatOffset, std::as_bytes(std::span(c_HalfRGBA16))))
    {
        l_Wrong += std::format("{}the RGBA16Float target does not read back as half floats 0.5, 2, 0.25 and 1", l_Wrong.empty() ? "" : "; ");
    }

    a_Destroy();

    if (!l_Wrong.empty())
    {
        TR_ERROR("Raster: {}", l_Wrong);

        return;
    }

    TR_INFO("Raster: {} passed all {} probes for culling with both windings, depth bias of 0, +16 and -16 and depth clamp, and R11G11B10Float and RGBA16Float targets read back bit for bit", Trinity::ToString(l_Info.API), c_Probes.size());
}

// A white triangle on black, drawn with 4x multisampled colour and depth and resolved into a single-sampled target as rendering ends. Pixels the edge crosses must resolve to a quarter, half or three quarters of white, and every other pixel to black or white
void SandboxLayer::TestMultisampling()
{
    TR_PROFILE_FUNCTION();

    constexpr Trinity::RHI::Format c_Format = Trinity::RHI::Format::RGBA8Unorm;
    constexpr Trinity::RHI::Format c_DepthFormat = Trinity::RHI::Format::D32Float;
    constexpr std::uint32_t c_Size = 16;
    constexpr std::uint32_t c_SampleCount = 4;
    constexpr std::int32_t c_Tolerance = 2;

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();

    if (!l_Device.IsFormatSupported(c_Format, Trinity::RHI::TextureUsage::RenderTarget, c_SampleCount) || !l_Device.IsFormatSupported(c_DepthFormat, Trinity::RHI::TextureUsage::DepthStencil, c_SampleCount))
    {
        TR_ERROR("Multisampling: {} cannot render {} or {} with {} samples", Trinity::ToString(l_Info.API), Trinity::RHI::ToString(c_Format), Trinity::RHI::ToString(c_DepthFormat), c_SampleCount);

        return;
    }

    const std::string_view l_Extension = l_Info.API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_VertexShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/RasterTest.VertexMain.{}", l_Extension));
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_PixelShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/RasterTest.PixelMain.{}", l_Extension));
    if (!l_VertexShader || !l_PixelShader)
    {
        TR_ERROR("Multisampling: the RasterTest {} shaders could not be read from /engine/shaders", l_Extension);

        return;
    }

    const std::array<Trinity::RHI::Format, 1> l_ColorFormats{ c_Format };
    Trinity::RHI::GraphicsPipelineDescription l_PipelineDescription;
    l_PipelineDescription.VertexShader = { *l_VertexShader, "VertexMain" };
    l_PipelineDescription.PixelShader = { *l_PixelShader, "PixelMain" };
    l_PipelineDescription.ColorFormats = l_ColorFormats;
    l_PipelineDescription.DepthFormat = c_DepthFormat;
    l_PipelineDescription.SampleCount = c_SampleCount;
    l_PipelineDescription.Cull = Trinity::RHI::CullMode::None;
    l_PipelineDescription.DepthTest = true;
    l_PipelineDescription.DepthWrite = true;
    l_PipelineDescription.DebugName = "Sandbox multisampled triangle";
    const Trinity::RHI::PipelineHandle l_Pipeline = l_Device.CreateGraphicsPipeline(l_PipelineDescription);

    Trinity::RHI::TextureDescription l_TextureDescription;
    l_TextureDescription.Width = c_Size;
    l_TextureDescription.Height = c_Size;
    l_TextureDescription.TextureFormat = c_Format;
    l_TextureDescription.SampleCount = c_SampleCount;
    l_TextureDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget;
    l_TextureDescription.DebugName = "Sandbox multisampled target";
    const Trinity::RHI::TextureHandle l_Multisampled = l_Device.CreateTexture(l_TextureDescription);

    l_TextureDescription.TextureFormat = c_DepthFormat;
    l_TextureDescription.Usage = Trinity::RHI::TextureUsage::DepthStencil;
    l_TextureDescription.DebugName = "Sandbox multisampled depth";
    const Trinity::RHI::TextureHandle l_Depth = l_Device.CreateTexture(l_TextureDescription);

    l_TextureDescription.TextureFormat = c_Format;
    l_TextureDescription.SampleCount = 1;
    l_TextureDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget | Trinity::RHI::TextureUsage::CopySource;
    l_TextureDescription.DebugName = "Sandbox resolved target";
    const Trinity::RHI::TextureHandle l_Resolved = l_Device.CreateTexture(l_TextureDescription);

    const std::uint64_t l_RowPitch = Trinity::RHI::GetTextureCopyRowPitch(c_Format, c_Size);

    Trinity::RHI::BufferDescription l_ReadbackDescription;
    l_ReadbackDescription.Size = l_RowPitch * c_Size;
    l_ReadbackDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_ReadbackDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_ReadbackDescription.DebugName = "Sandbox resolved readback";
    const Trinity::RHI::BufferHandle l_Readback = l_Device.CreateBuffer(l_ReadbackDescription);

    const auto a_Destroy = [&]()
    {
        l_Device.DestroyBuffer(l_Readback);
        l_Device.DestroyTexture(l_Resolved);
        l_Device.DestroyTexture(l_Depth);
        l_Device.DestroyTexture(l_Multisampled);
        l_Device.DestroyPipeline(l_Pipeline);
    };

    if (!l_Pipeline || !l_Multisampled || !l_Depth || !l_Resolved || !l_Readback)
    {
        TR_ERROR("Multisampling: could not create the pipeline, the targets or the readback");
        a_Destroy();

        return;
    }

    Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();
    l_Commands.TextureBarrier(l_Multisampled, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::RenderTarget);
    l_Commands.TextureBarrier(l_Depth, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::DepthWrite);
    l_Commands.TextureBarrier(l_Resolved, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::ResolveDestination);

    // The multisampled target is only resolved, never stored
    Trinity::RHI::ColorAttachment l_Attachment{ l_Multisampled, Trinity::RHI::LoadOp::Clear, Trinity::RHI::StoreOp::DontCare };
    l_Attachment.ResolveTexture = l_Resolved;

    Trinity::RHI::RenderingDescription l_Rendering;
    l_Rendering.ColorAttachments = std::span(&l_Attachment, 1);
    l_Rendering.Depth = { l_Depth, Trinity::RHI::LoadOp::Clear, Trinity::RHI::StoreOp::DontCare, 0.0f };
    l_Commands.BeginRendering(l_Rendering);
    l_Commands.SetPipeline(l_Pipeline);
    l_Commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(c_Size), static_cast<float>(c_Size), 0.0f, 1.0f });
    l_Commands.SetScissor({ 0, 0, c_Size, c_Size });

    // The long edge runs from the bottom right to two-thirds of the way up the left side, so it crosses pixels at many different offsets
    const RasterPushData l_Push{ { glm::vec4(-1.0f, -1.0f, 0.5f, 1.0f), glm::vec4(1.0f, -1.0f, 0.5f, 1.0f), glm::vec4(-1.0f, 0.6f, 0.5f, 1.0f) }, glm::vec4(1.0f) };
    l_Commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
    l_Commands.Draw(3, 1, 0, 0);
    l_Commands.EndRendering();

    l_Commands.TextureBarrier(l_Resolved, Trinity::RHI::ResourceState::ResolveDestination, Trinity::RHI::ResourceState::CopySource);
    l_Commands.CopyTextureToBuffer(l_Resolved, 0, 0, l_Readback, 0);
    l_Device.EndFrame();
    l_Device.WaitIdle();

    if (l_Info.API == Trinity::GraphicsAPI::None)
    {
        TR_INFO("Multisampling: None recorded a draw into {}x {} colour and depth resolved into a single-sampled target", c_SampleCount, c_Size);
        a_Destroy();

        return;
    }

    const std::span<const std::byte> l_Data = l_Device.GetMappedData(l_Readback);
    const auto a_Channel = [&l_Data, l_RowPitch](std::uint32_t x, std::uint32_t y, std::uint32_t channel) { return std::to_integer<std::int32_t>(l_Data[static_cast<std::size_t>(y * l_RowPitch + x * 4 + channel)]); };

    std::string l_Wrong;
    std::uint32_t l_Partial = 0;
    for (std::uint32_t it_Y = 0; it_Y < c_Size && l_Data.size() >= l_RowPitch * c_Size; ++it_Y)
    {
        for (std::uint32_t it_X = 0; it_X < c_Size; ++it_X)
        {
            const std::int32_t l_Value = a_Channel(it_X, it_Y, 0);
            const bool l_Grey = a_Channel(it_X, it_Y, 1) == l_Value && a_Channel(it_X, it_Y, 2) == l_Value && a_Channel(it_X, it_Y, 3) == 255;
            const std::int32_t l_Quarters = (l_Value * static_cast<std::int32_t>(c_SampleCount) + 127) / 255;
            const bool l_Covered = std::abs(l_Value - (l_Quarters * 255 + 2) / 4) <= c_Tolerance;
            if (!l_Grey || !l_Covered)
            {
                l_Wrong += std::format("{}({}, {}) is ({}, {}, {}, {})", l_Wrong.empty() ? "" : ", ", it_X, it_Y, l_Value, a_Channel(it_X, it_Y, 1), a_Channel(it_X, it_Y, 2), a_Channel(it_X, it_Y, 3));
            }

            l_Partial += l_Value > c_Tolerance && l_Value < 255 - c_Tolerance ? 1 : 0;
        }
    }

    const bool l_Solid = l_Data.size() >= l_RowPitch * c_Size && a_Channel(1, c_Size - 2, 0) == 255 && a_Channel(c_Size - 2, 1, 0) == 0;
    a_Destroy();

    if (!l_Wrong.empty() || !l_Solid || l_Partial == 0)
    {
        TR_ERROR("Multisampling: the resolved triangle {}{}{}", l_Partial == 0 ? "has no in-between pixels along its edge" : std::format("has {} in-between pixel(s)", l_Partial), l_Solid ? "" : ", and is not white inside and black outside", l_Wrong.empty() ? "" : std::format(", and these are not black, white or a quarter step of grey: {}", l_Wrong));

        return;
    }

    TR_INFO("Multisampling: {} resolved a {}x triangle as rendering ended, with {} edge pixel(s) at a quarter, half or three quarters of white and every other pixel black or white", Trinity::ToString(l_Info.API), c_SampleCount, l_Partial);
}

// Two frames each write four timestamps into the same pool, around and inside a pass that clears and fills a large target, and resolve them. Every timestamp must be non-zero, each frame's in order, and the second frame's after the first's, which also shows the pool can be written again once resolved
void SandboxLayer::TestTimestamps()
{
    TR_PROFILE_FUNCTION();

    constexpr Trinity::RHI::Format c_Format = Trinity::RHI::Format::RGBA16Float;
    constexpr std::uint32_t c_Size = 1024;
    constexpr std::uint32_t c_TimestampCount = 4;
    constexpr std::uint32_t c_FrameCount = 2;

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();
    const std::uint64_t l_Frequency = l_Device.GetTimestampFrequency();

    const std::string_view l_Extension = l_Info.API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_VertexShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/RasterTest.VertexMain.{}", l_Extension));
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_PixelShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/RasterTest.PixelMain.{}", l_Extension));
    if (!l_VertexShader || !l_PixelShader || l_Frequency == 0)
    {
        TR_ERROR("Timestamps: {}", l_Frequency == 0 ? "the device reports no timestamp frequency" : std::format("the RasterTest {} shaders could not be read from /engine/shaders", l_Extension));

        return;
    }

    const std::array<Trinity::RHI::Format, 1> l_ColorFormats{ c_Format };
    Trinity::RHI::GraphicsPipelineDescription l_PipelineDescription;
    l_PipelineDescription.VertexShader = { *l_VertexShader, "VertexMain" };
    l_PipelineDescription.PixelShader = { *l_PixelShader, "PixelMain" };
    l_PipelineDescription.ColorFormats = l_ColorFormats;
    l_PipelineDescription.Cull = Trinity::RHI::CullMode::None;
    l_PipelineDescription.DebugName = "Sandbox timed fill";
    const Trinity::RHI::PipelineHandle l_Pipeline = l_Device.CreateGraphicsPipeline(l_PipelineDescription);

    Trinity::RHI::TextureDescription l_TargetDescription;
    l_TargetDescription.Width = c_Size;
    l_TargetDescription.Height = c_Size;
    l_TargetDescription.TextureFormat = c_Format;
    l_TargetDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget;
    l_TargetDescription.DebugName = "Sandbox timed target";
    const Trinity::RHI::TextureHandle l_Target = l_Device.CreateTexture(l_TargetDescription);

    const Trinity::RHI::QueryPoolHandle l_Pool = l_Device.CreateQueryPool({ c_TimestampCount, "Sandbox timestamps" });

    Trinity::RHI::BufferDescription l_ReadbackDescription;
    l_ReadbackDescription.Size = std::uint64_t{ c_TimestampCount } * c_FrameCount * 8;
    l_ReadbackDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_ReadbackDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_ReadbackDescription.DebugName = "Sandbox timestamp readback";
    const Trinity::RHI::BufferHandle l_Readback = l_Device.CreateBuffer(l_ReadbackDescription);

    const auto a_Destroy = [&]()
    {
        l_Device.DestroyBuffer(l_Readback);
        l_Device.DestroyQueryPool(l_Pool);
        l_Device.DestroyTexture(l_Target);
        l_Device.DestroyPipeline(l_Pipeline);
    };

    if (!l_Pipeline || !l_Target || !l_Pool || !l_Readback)
    {
        TR_ERROR("Timestamps: could not create the pipeline, the target, the query pool or the readback");
        a_Destroy();

        return;
    }

    const RasterPushData l_Push{ { glm::vec4(-1.0f, -1.0f, 0.5f, 1.0f), glm::vec4(3.0f, -1.0f, 0.5f, 1.0f), glm::vec4(-1.0f, 3.0f, 0.5f, 1.0f) }, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f) };
    const std::array<Trinity::RHI::ColorAttachment, 1> l_Attachments{ Trinity::RHI::ColorAttachment{ l_Target, Trinity::RHI::LoadOp::Clear, Trinity::RHI::StoreOp::Store } };
    Trinity::RHI::RenderingDescription l_Rendering;
    l_Rendering.ColorAttachments = l_Attachments;

    // Timestamps before the pass, inside it before and after the draw, and after it
    for (std::uint32_t it_Frame = 0; it_Frame < c_FrameCount; ++it_Frame)
    {
        Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();
        l_Commands.TextureBarrier(l_Target, it_Frame == 0 ? Trinity::RHI::ResourceState::Undefined : Trinity::RHI::ResourceState::RenderTarget, Trinity::RHI::ResourceState::RenderTarget);
        l_Commands.WriteTimestamp(l_Pool, 0);
        l_Commands.BeginRendering(l_Rendering);
        l_Commands.WriteTimestamp(l_Pool, 1);
        l_Commands.SetPipeline(l_Pipeline);
        l_Commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(c_Size), static_cast<float>(c_Size), 0.0f, 1.0f });
        l_Commands.SetScissor({ 0, 0, c_Size, c_Size });
        l_Commands.PushConstants(std::as_bytes(std::span(&l_Push, 1)));
        l_Commands.Draw(3, 1, 0, 0);
        l_Commands.WriteTimestamp(l_Pool, 2);
        l_Commands.EndRendering();
        l_Commands.WriteTimestamp(l_Pool, 3);
        l_Commands.ResolveTimestamps(l_Pool, 0, c_TimestampCount, l_Readback, std::uint64_t{ it_Frame } * c_TimestampCount * 8);
        l_Device.EndFrame();
    }

    l_Device.WaitIdle();

    if (l_Info.API == Trinity::GraphicsAPI::None)
    {
        TR_INFO("Timestamps: None recorded {} timestamps in each of {} frames and resolved them", c_TimestampCount, c_FrameCount);
        a_Destroy();

        return;
    }

    std::array < std::uint64_t, std::size_t{ c_TimestampCount }* c_FrameCount > l_Ticks{};
    const std::span<const std::byte> l_Data = l_Device.GetMappedData(l_Readback);
    if (l_Data.size() >= sizeof(l_Ticks))
    {
        std::memcpy(l_Ticks.data(), l_Data.data(), sizeof(l_Ticks));
    }

    a_Destroy();

    const bool l_NonZero = std::ranges::none_of(l_Ticks, [](std::uint64_t ticks) { return ticks == 0; });
    const bool l_InOrder = std::ranges::is_sorted(l_Ticks);
    if (!l_NonZero || !l_InOrder || l_Ticks.back() == l_Ticks.front())
    {
        std::string l_Values;
        for (const std::uint64_t it_Ticks : l_Ticks)
        {
            l_Values += std::format("{}{}", l_Values.empty() ? "" : ", ", it_Ticks);
        }

        TR_ERROR("Timestamps: {} read back {}, which are not all non-zero and in order", Trinity::ToString(l_Info.API), l_Values);

        return;
    }

    const auto a_Microseconds = [l_Frequency](std::uint64_t ticks) { return static_cast<double>(ticks) * 1000000.0 / static_cast<double>(l_Frequency); };
    TR_INFO("Timestamps: {} wrote {} non-zero timestamps in order over {} frames at {} ticks a second, and the second frame's {}x{} pass took {:.1f} us", Trinity::ToString(l_Info.API), l_Ticks.size(), c_FrameCount, l_Frequency, c_Size, c_Size, a_Microseconds(l_Ticks[7] - l_Ticks[4]));
}

// 64 placeholder source files give the registry 64 texture assets, whose cooked KTX2 a memory source serves at /cache
void SandboxLayer::CreateSprites()
{
    TR_PROFILE_FUNCTION();

    RemoveFiles(c_SpriteTestRoot);
    for (std::uint32_t it_Index = 0; it_Index < c_SpriteTextureCount; ++it_Index)
    {
        if (!Trinity::FileSystem::WriteText(std::format("{}/Sprite{:02}.png", c_SpriteTestRoot, it_Index), std::format("Sprite texture {}; the cooked KTX2 is made in memory", it_Index)))
        {
            TR_ERROR("Sprites: the source files under {} could not be written", c_SpriteTestRoot);

            return;
        }
    }

    m_SpriteRegistry = Trinity::CreateScope<Trinity::AssetRegistry>(c_SpriteTestRoot);
    static_cast<void>(m_SpriteRegistry->Scan());

    Trinity::Scope<Trinity::MemorySource> l_Cache = Trinity::CreateScope<Trinity::MemorySource>("Sandbox sprite textures");
    for (std::uint32_t it_Index = 0; it_Index < c_SpriteTextureCount; ++it_Index)
    {
        const Trinity::AssetRecord* l_Record = m_SpriteRegistry->FindByPath(std::format("{}/Sprite{:02}.png", c_SpriteTestRoot, it_Index));
        if (l_Record == nullptr)
        {
            TR_ERROR("Sprites: the registry has no record for texture {}", it_Index);

            return;
        }

        const std::vector<std::uint8_t> l_Texels = MakeSpriteTexels(it_Index);
        const std::vector<std::byte> l_File = MakeSpriteKtx2(l_Texels, c_SpriteTextureSize, it_Index == 0);
        const std::string l_Path = Trinity::GetCookedTexturePath(l_Record->ID);
        static_cast<void>(l_Cache->AddFile(std::string_view(l_Path).substr(Trinity::Project::c_CacheMount.size() + 1), l_File));
        m_SpriteTextures.push_back(l_Record->ID);
    }

    if (!Trinity::FileSystem::Mount(Trinity::Project::c_CacheMount, std::move(l_Cache)))
    {
        TR_ERROR("Sprites: the textures could not be mounted at {}", Trinity::Project::c_CacheMount);

        return;
    }

    // Groups of ten: a root and nine children, so world transforms and hierarchy order both take part
    std::mt19937 l_Random(c_SpriteSeed);
    std::uniform_real_distribution<float> l_X(-75.0f, 75.0f);
    std::uniform_real_distribution<float> l_Y(-42.0f, 42.0f);
    std::uniform_real_distribution<float> l_Offset(-3.0f, 3.0f);
    std::uniform_real_distribution<float> l_Size(0.4f, 2.5f);
    std::uniform_real_distribution<float> l_Angle(0.0f, 2.0f * std::numbers::pi_v<float>);
    std::uniform_real_distribution<float> l_Tint(0.6f, 1.0f);
    std::uniform_int_distribution<std::uint32_t> l_Texture(0, c_SpriteTextureCount - 1);
    std::uniform_int_distribution<std::int32_t> l_Layer(0, 3);
    std::uniform_int_distribution<std::int32_t> l_Order(-5, 5);

    m_SpriteScene = Trinity::CreateScope<Trinity::Scene>();
    Trinity::Entity l_Camera = m_SpriteScene->CreateEntity("Camera");
    l_Camera.Add<Trinity::CameraComponent>().OrthographicSize = 90.0f;

    Trinity::Entity l_Group;
    for (std::uint32_t it_Sprite = 0; it_Sprite < c_SpriteCount; ++it_Sprite)
    {
        const bool l_Root = it_Sprite % c_SpriteGroupSize == 0;
        Trinity::Entity l_Entity = l_Root ? m_SpriteScene->CreateEntity("Sprite") : m_SpriteScene->CreateEntity("Sprite", l_Group);
        Trinity::TransformComponent& l_Transform = l_Entity.Get<Trinity::TransformComponent>();
        l_Transform.Position = l_Root ? glm::vec3(l_X(l_Random), l_Y(l_Random), 0.0f) : glm::vec3(l_Offset(l_Random), l_Offset(l_Random), 0.0f);
        l_Transform.Rotation = glm::angleAxis(l_Angle(l_Random), glm::vec3(0.0f, 0.0f, 1.0f));
        l_Transform.Scale = l_Root ? glm::vec3(l_Size(l_Random), l_Size(l_Random), 1.0f) : glm::vec3(1.0f);

        Trinity::SpriteRendererComponent& l_Sprite = l_Entity.Add<Trinity::SpriteRendererComponent>();
        l_Sprite.Texture = m_SpriteTextures[l_Texture(l_Random)];
        l_Sprite.Tint = glm::vec4(l_Tint(l_Random), l_Tint(l_Random), l_Tint(l_Random), 1.0f);
        l_Sprite.SortingLayer = l_Layer(l_Random);
        l_Sprite.OrderInLayer = l_Order(l_Random);
        l_Sprite.FlipX = it_Sprite % 3 == 0;

        if (l_Root)
        {
            l_Group = l_Entity;
        }
    }

    m_SpriteScene->UpdateWorldTransforms();

    // One unit is one pixel of the 128x128 readback target, centred on the origin
    m_SpriteReadbackScene = Trinity::CreateScope<Trinity::Scene>();
    Trinity::Scene& l_Scene = *m_SpriteReadbackScene;
    l_Scene.CreateEntity("Camera").Add<Trinity::CameraComponent>().OrthographicSize = static_cast<float>(c_SpriteReadbackSize);

    const auto a_Sprite = [&l_Scene](std::string_view name, glm::vec2 position, glm::vec2 size, glm::vec4 tint, std::int32_t layer, std::int32_t order, Trinity::Entity parent = {})
    {
        Trinity::Entity l_Entity = parent ? l_Scene.CreateEntity(name, parent) : l_Scene.CreateEntity(name);
        l_Entity.Get<Trinity::TransformComponent>().Position = glm::vec3(position, 0.0f);
        l_Entity.Get<Trinity::TransformComponent>().Scale = glm::vec3(size, 1.0f);
        Trinity::SpriteRendererComponent& l_Sprite = l_Entity.Add<Trinity::SpriteRendererComponent>();
        l_Sprite.Tint = tint;
        l_Sprite.SortingLayer = layer;
        l_Sprite.OrderInLayer = order;

        return l_Entity;
    };

    static_cast<void>(a_Sprite("Red", { -32.0f, 32.0f }, { 40.0f, 40.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, 0, 0));
    static_cast<void>(a_Sprite("Green", { -20.0f, 20.0f }, { 40.0f, 40.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }, 0, 1));
    static_cast<void>(a_Sprite("Blue below", { -10.0f, 10.0f }, { 40.0f, 40.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, -1, 9));
    static_cast<void>(a_Sprite("Half blue", { -20.0f, 46.0f }, { 6.0f, 6.0f }, { 0.0f, 0.0f, 1.0f, 0.5f }, 5, 0));

    // Created yellow first, then moved after cyan, so only hierarchy order puts yellow on top
    Trinity::Entity l_Yellow = a_Sprite("Yellow", { 30.0f, 30.0f }, { 30.0f, 30.0f }, { 1.0f, 1.0f, 0.0f, 1.0f }, 2, 0);
    Trinity::Entity l_Cyan = a_Sprite("Cyan", { 38.0f, 38.0f }, { 30.0f, 30.0f }, { 0.0f, 1.0f, 1.0f, 1.0f }, 2, 0);
    static_cast<void>(l_Scene.MoveBefore(l_Cyan, l_Yellow));

    Trinity::Entity l_Pivot = l_Scene.CreateEntity("Pivot");
    l_Pivot.Get<Trinity::TransformComponent>().Position = glm::vec3(30.0f, -30.0f, 0.0f);
    l_Pivot.Get<Trinity::TransformComponent>().Rotation = glm::angleAxis(std::numbers::pi_v<float> *0.25f, glm::vec3(0.0f, 0.0f, 1.0f));
    static_cast<void>(a_Sprite("Magenta bar", { 0.0f, 0.0f }, { 40.0f, 6.0f }, { 1.0f, 0.0f, 1.0f, 1.0f }, 0, 0, l_Pivot));

    Trinity::Entity l_Quadrants = a_Sprite("Quadrants", { -30.0f, -30.0f }, { 32.0f, 32.0f }, glm::vec4(1.0f), 0, 0);
    l_Quadrants.Get<Trinity::SpriteRendererComponent>().Texture = m_SpriteTextures[0];
    l_Quadrants.Get<Trinity::SpriteRendererComponent>().FlipX = true;

    l_Scene.UpdateWorldTransforms();

    Trinity::AssetManager::SetRegistry(m_SpriteRegistry.get());
    m_SpritePhase = SpritePhase::Loading;
    m_SpritePhaseFrame = Trinity::Application::Get().GetFrameCount();
}

// Waits for every texture to load, checks a readback, and from then on watches Renderer memory
void SandboxLayer::UpdateSprites()
{
    const std::uint64_t l_Frame = Trinity::Application::Get().GetFrameCount();
    if (m_SpritePhase != SpritePhase::Loading)
    {
        return;
    }

    // A texture becomes ready at the start of a frame and is uploaded when that frame renders, so the readback, which draws outside the renderer's frame, waits one more frame
    const bool l_Loaded = std::ranges::all_of(m_SpriteTextures, [](Trinity::UUID id) { return Trinity::AssetManager::GetState(id) == Trinity::AssetState::Ready; });
    if (l_Loaded && m_SpriteLoadedFrame == 0)
    {
        m_SpriteLoadedFrame = l_Frame;
    }

    if (!l_Loaded || l_Frame == m_SpriteLoadedFrame)
    {
        if (l_Frame - m_SpritePhaseFrame > c_SpriteLoadTimeoutFrames)
        {
            TR_ERROR("Sprites: the {} textures were not all loaded after {} frames", c_SpriteTextureCount, c_SpriteLoadTimeoutFrames);
            m_SpritePhase = SpritePhase::Running;
        }

        return;
    }

    TR_INFO("Sprites: {} textures loaded in {} frame(s)", c_SpriteTextureCount, m_SpriteLoadedFrame - m_SpritePhaseFrame);
    TestSpriteReadback();
    m_SpritePhase = SpritePhase::Running;
}

// Probes inside each sprite of a small scene, chosen so that a wrong sort, transform, flip, filter or blend each changes at least one of them
void SandboxLayer::TestSpriteReadback()
{
    TR_PROFILE_FUNCTION();

    struct Probe
    {
        std::string_view Name;
        glm::vec2 Position;
        std::array<std::uint8_t, 3> Expected;
        std::int32_t Tolerance = 2;
    };

    constexpr std::array<Probe, 16> c_Probes
    { {
        { "red alone", { -48.0f, 48.0f }, { 255, 0, 0 } },
        { "green over red, order 1 over 0", { -26.0f, 26.0f }, { 0, 255, 0 } },
        { "blue alone on layer -1", { 5.0f, 15.0f }, { 0, 0, 255 } },
        { "green over blue, layer 0 over -1 despite order 9", { -15.0f, 15.0f }, { 0, 255, 0 } },
        { "half-transparent blue over red", { -20.0f, 46.0f }, { 128, 0, 128 }, 3 },
        { "yellow over cyan, later in hierarchy order", { 35.0f, 35.0f }, { 255, 255, 0 } },
        { "cyan alone", { 50.0f, 50.0f }, { 0, 255, 255 } },
        { "yellow alone", { 20.0f, 20.0f }, { 255, 255, 0 } },
        { "bar along its parent's 45 degree turn", { 37.0f, -23.0f }, { 255, 0, 255 } },
        { "outside the turned bar", { 40.0f, -30.0f }, { 0, 0, 0 } },
        { "flipped top left shows green", { -38.0f, -22.0f }, { 0, 255, 0 } },
        { "flipped top right shows red", { -22.0f, -22.0f }, { 255, 0, 0 } },
        { "flipped bottom left shows yellow", { -38.0f, -38.0f }, { 255, 255, 0 } },
        { "flipped bottom right shows blue", { -22.0f, -38.0f }, { 0, 0, 255 } },
        { "a quarter texel from the edge, nearest filtering keeps green unmixed", { -30.5f, -22.0f }, { 0, 255, 0 } },
        { "background", { 55.0f, -55.0f }, { 0, 0, 0 } }
    } };

    constexpr Trinity::RHI::Format c_Format = Trinity::RHI::Format::RGBA8Unorm;
    constexpr std::array<float, 4> c_Black{ 0.0f, 0.0f, 0.0f, 1.0f };

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    Trinity::Renderer2D& l_Renderer2D = Trinity::Application::Get().GetRenderer().GetRenderer2D();

    Trinity::RHI::TextureDescription l_TargetDescription;
    l_TargetDescription.Width = c_SpriteReadbackSize;
    l_TargetDescription.Height = c_SpriteReadbackSize;
    l_TargetDescription.TextureFormat = c_Format;
    l_TargetDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget | Trinity::RHI::TextureUsage::CopySource;
    l_TargetDescription.ClearColor = c_Black;
    l_TargetDescription.DebugName = "Sandbox sprite target";

    const std::uint64_t l_RowPitch = Trinity::RHI::GetTextureCopyRowPitch(c_Format, c_SpriteReadbackSize);

    Trinity::RHI::BufferDescription l_ReadbackDescription;
    l_ReadbackDescription.Size = l_RowPitch * c_SpriteReadbackSize;
    l_ReadbackDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_ReadbackDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_ReadbackDescription.DebugName = "Sandbox sprite readback";

    const Trinity::RHI::TextureHandle l_Target = l_Device.CreateTexture(l_TargetDescription);
    const Trinity::RHI::BufferHandle l_Readback = l_Device.CreateBuffer(l_ReadbackDescription);
    if (!l_Target || !l_Readback)
    {
        TR_ERROR("Sprites: could not create the readback target or buffer");
        l_Device.DestroyTexture(l_Target);
        l_Device.DestroyBuffer(l_Readback);

        return;
    }

    Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();
    l_Commands.TextureBarrier(l_Target, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::RenderTarget);

    const std::array<Trinity::RHI::ColorAttachment, 1> l_Attachments{ Trinity::RHI::ColorAttachment{ l_Target, Trinity::RHI::LoadOp::Clear, Trinity::RHI::StoreOp::Store, c_Black } };
    Trinity::RHI::RenderingDescription l_Rendering;
    l_Rendering.ColorAttachments = l_Attachments;
    l_Rendering.RenderArea = { 0, 0, c_SpriteReadbackSize, c_SpriteReadbackSize };
    l_Commands.BeginRendering(l_Rendering);
    const bool l_Drawn = l_Renderer2D.DrawScene(l_Commands, *m_SpriteReadbackScene, c_Format, c_SpriteReadbackSize, c_SpriteReadbackSize);
    const Trinity::Renderer2D::Statistics l_Statistics = l_Renderer2D.GetStatistics();
    l_Commands.EndRendering();

    l_Commands.TextureBarrier(l_Target, Trinity::RHI::ResourceState::RenderTarget, Trinity::RHI::ResourceState::CopySource);
    l_Commands.CopyTextureToBuffer(l_Target, 0, 0, l_Readback, 0);
    l_Device.EndFrame();
    l_Device.WaitIdle();

    // The null device draws nothing, so only a GPU's readback has pixels to check
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();
    const std::span<const std::byte> l_Data = l_Device.GetMappedData(l_Readback);
    const bool l_Check = l_Info.API != Trinity::GraphicsAPI::None && l_Data.size() == l_ReadbackDescription.Size;

    std::string l_Wrong;
    for (const Probe& it_Probe : c_Probes)
    {
        const std::uint32_t l_PixelX = static_cast<std::uint32_t>(std::floor(it_Probe.Position.x + static_cast<float>(c_SpriteReadbackSize / 2)));
        const std::uint32_t l_PixelY = static_cast<std::uint32_t>(std::floor(static_cast<float>(c_SpriteReadbackSize / 2) - it_Probe.Position.y));
        if (!l_Check)
        {
            continue;
        }

        const std::byte* l_Pixel = l_Data.data() + static_cast<std::size_t>(l_PixelY * l_RowPitch + l_PixelX * 4);
        bool l_Matches = true;
        for (std::size_t it_Channel = 0; it_Channel < 3; ++it_Channel)
        {
            l_Matches = l_Matches && std::abs(std::to_integer<std::int32_t>(l_Pixel[it_Channel]) - static_cast<std::int32_t>(it_Probe.Expected[it_Channel])) <= it_Probe.Tolerance;
        }

        if (!l_Matches)
        {
            l_Wrong += std::format("{}{} at ({}, {}) is ({}, {}, {}) where ({}, {}, {}) was expected", l_Wrong.empty() ? "" : "; ", it_Probe.Name, l_PixelX, l_PixelY, std::to_integer<std::uint32_t>(l_Pixel[0]), std::to_integer<std::uint32_t>(l_Pixel[1]), std::to_integer<std::uint32_t>(l_Pixel[2]), it_Probe.Expected[0], it_Probe.Expected[1], it_Probe.Expected[2]);
        }
    }

    l_Device.DestroyBuffer(l_Readback);
    l_Device.DestroyTexture(l_Target);

    if (!l_Drawn || l_Statistics.Sprites != 8 || l_Statistics.DrawCalls != (l_Info.API != Trinity::GraphicsAPI::None ? 1u : l_Statistics.DrawCalls))
    {
        TR_ERROR("Sprites: the readback scene drew {} sprite(s) in {} draw call(s){}", l_Statistics.Sprites, l_Statistics.DrawCalls, l_Drawn ? "" : ", without finding its camera");

        return;
    }

    if (!l_Wrong.empty())
    {
        TR_ERROR("Sprites: the readback differs: {}", l_Wrong);

        return;
    }

    TR_INFO("Sprites: {} drew 8 sprites in one draw call{}", Trinity::ToString(l_Info.API), l_Check ? std::format(", and all {} probes for sort order, transforms, flips, filtering and blending hold the expected colour", c_Probes.size()) : "");
}

// Whether Renderer memory moved between sprite frame c_SpriteCheckFrame and c_SpriteReportFrame, or the last frame when the run ends sooner
void SandboxLayer::ReportSprites()
{
    if (m_SpriteReported)
    {
        return;
    }

    m_SpriteReported = true;
    if (m_SpriteFrames > 0)
    {
        if (m_SpriteFrames >= c_SpriteCheckFrame && m_SpriteBytesLast != m_SpriteBytesAtCheck)
        {
            TR_ERROR("Sprites: Renderer went from {} at sprite frame {} to {} at frame {}", Trinity::Memory::FormatBytes(m_SpriteBytesAtCheck), c_SpriteCheckFrame, Trinity::Memory::FormatBytes(m_SpriteBytesLast), m_SpriteFrames);
        }
        else
        {
            TR_INFO("Sprites: drew {} sprites from {} textures in each of {} frame(s){}", c_SpriteCount, c_SpriteTextureCount, m_SpriteFrames, m_SpriteFrames >= c_SpriteCheckFrame ? std::format(", and Renderer held {} at sprite frame {} and {} at the last", Trinity::Memory::FormatBytes(m_SpriteBytesAtCheck), c_SpriteCheckFrame, Trinity::Memory::FormatBytes(m_SpriteBytesLast)) : "");
        }
    }

    if (m_SpriteBadFrames != 0)
    {
        TR_ERROR("Sprites: {} frame(s) did not draw all {} sprites in one draw call", m_SpriteBadFrames, c_SpriteCount);
    }
}

void SandboxLayer::DestroySprites()
{
    if (m_SpritePhase == SpritePhase::Running)
    {
        ReportSprites();
    }

    if (m_SpritePhase != SpritePhase::Idle)
    {
        Trinity::AssetManager::SetRegistry(nullptr);
    }

    if (m_SpriteRegistry)
    {
        static_cast<void>(Trinity::FileSystem::Unmount(Trinity::Project::c_CacheMount));
    }

    m_SpriteScene.reset();
    m_SpriteReadbackScene.reset();
    m_SpriteRegistry.reset();
    m_SpritePhase = SpritePhase::Idle;
}