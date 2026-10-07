#include "SandboxLayer.hpp"

#include <glm/gtc/packing.hpp>

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

    // The Renderer's display format, which its tonemap pass writes
    constexpr Trinity::RHI::Format c_SpriteReadbackFormat = Trinity::RHI::Format::RGBA8Unorm;
    constexpr std::array<float, 4> c_SpriteReadbackClear{ 0.0f, 0.0f, 0.0f, 1.0f };

    // Exposure 1 and no curve, so unlit sprites reach the readback as they were drawn
    constexpr Trinity::ToneMapping c_SpriteReadbackToneMapping{ Trinity::c_NeutralEV100, Trinity::Tonemapper::None };

    // An sRGB texture of one grey, which must come back from the readback as the same 8-bit value
    constexpr std::string_view c_GreyTextureName = "Grey.png";
    constexpr std::uint8_t c_GreyValue = 128;

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

    // FNV-1a over the texels of each row of a readback, leaving out the padding after each row
    std::uint64_t HashRows(std::span<const std::byte> data, std::uint64_t rowPitch, std::uint64_t rowSize, std::uint32_t rows)
    {
        std::uint64_t l_Hash = 14695981039346656037ull;
        for (std::uint32_t it_Row = 0; it_Row < rows && (it_Row * rowPitch + rowSize) <= data.size(); ++it_Row)
        {
            for (const std::byte it_Byte : data.subspan(static_cast<std::size_t>(it_Row* rowPitch), static_cast<std::size_t>(rowSize)))
            {
                l_Hash = (l_Hash ^ std::to_integer<std::uint64_t>(it_Byte)) * 1099511628211ull;
            }
        }

        return l_Hash;
    }

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

    // A single-mip RGBA8 UNORM or sRGB KTX2 file: the header, the level index, a basic data format descriptor with four 8-bit samples, the filter key when asked for, then the texels
    std::vector<std::byte> MakeSpriteKtx2(std::span<const std::uint8_t> texels, std::uint32_t size, bool nearest, bool srgb)
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

        AppendU32(l_File, srgb ? 43 : 37);
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

        // RGBSDA colour model, BT.709 primaries and a linear or sRGB transfer function, then R, G, B and A, with alpha as channel 15, marked linear in an sRGB texture
        AppendU32(l_File, c_DfdSize);
        AppendU32(l_File, 0);
        AppendU32(l_File, 2 | ((c_DfdSize - 4) << 16));
        AppendU32(l_File, 1 | (1 << 8) | ((srgb ? 2u : 1u) << 16));
        AppendU32(l_File, 0);
        AppendU32(l_File, 4);
        AppendU32(l_File, 0);
        for (std::uint32_t it_Sample = 0; it_Sample < 4; ++it_Sample)
        {
            const std::uint32_t l_Channel = it_Sample < 3 ? it_Sample : (srgb ? 15 | 0x10 : 15);
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
    Trinity::ConsoleVariable<bool> s_ResizeTest("sandbox.resize_test", false, "Once the sprites run, gives the window a new size every frame for 200 frames and checks that Renderer memory stays flat", Trinity::ConsoleVariableFlags::ReadOnly);

    // Added to the Renderer's graph every frame, writing a texture nobody reads, so the graph culls it
    constexpr std::string_view c_UnusedPassName = "Sandbox unused";

    // The window cycles through 25 sizes, so after 100 and after 200 resizes it has been through the same ones
    constexpr std::uint32_t c_ResizeCount = 200;
    constexpr std::uint32_t c_ResizeCycle = 25;
    constexpr std::uint32_t c_ResizeStep = 16;
    constexpr std::uint32_t c_ResizeBaseWidth = 640;
    constexpr std::uint32_t c_ResizeBaseHeight = 360;

    // The file whose sub-assets are the test meshes, standing in for a model an importer reads
    constexpr std::string_view c_ModelName = "Shapes.model";
    constexpr std::uint32_t c_MeshSeed = 20261015;
    constexpr std::uint32_t c_GridSize = 260;
    constexpr std::uint32_t c_MeshChurnLoads = 1000;
    constexpr std::uint32_t c_MeshChurnPerFrame = 12;
    constexpr std::uint32_t c_MeshChurnMaxLifetime = 3;
    constexpr std::uint64_t c_MeshSettleFrames = 120;
    constexpr float c_MeshDirectionDot = 0.9999f;

    // Six faces of four vertices, so each face has its own normal and tangent. The bottom and back faces have left-handed tangents, and the last two faces draw in material slot 1
    Trinity::MeshData MakeCubeMesh()
    {
        struct Face
        {
            glm::vec3 Normal;
            glm::vec3 Tangent;
            float Handedness = 1.0f;
        };

        const std::array<Face, 6> l_Faces
        { {
            { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 1.0f },
            { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 1.0f },
            { { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 1.0f },
            { { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, -1.0f },
            { { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, 1.0f },
            { { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, -1.0f }
        } };

        Trinity::MeshData l_Mesh;
        for (const Face& it_Face : l_Faces)
        {
            const std::uint32_t l_Base = static_cast<std::uint32_t>(l_Mesh.Positions.size());
            const glm::vec3 l_Bitangent = glm::cross(it_Face.Normal, it_Face.Tangent) * it_Face.Handedness;
            for (std::uint32_t it_Corner = 0; it_Corner < 4; ++it_Corner)
            {
                const glm::vec2 l_UV(static_cast<float>(it_Corner & 1), static_cast<float>(it_Corner >> 1));
                l_Mesh.Positions.push_back(it_Face.Normal * 0.5f + it_Face.Tangent * (l_UV.x - 0.5f) + l_Bitangent * (l_UV.y - 0.5f));
                l_Mesh.Normals.push_back(it_Face.Normal);
                l_Mesh.Tangents.push_back(glm::vec4(it_Face.Tangent, it_Face.Handedness));
                l_Mesh.TexCoords0.push_back(l_UV);
            }

            for (const std::uint32_t it_Corner : { 0u, 1u, 2u, 2u, 1u, 3u })
            {
                l_Mesh.Indices.push_back(l_Base + it_Corner);
            }
        }

        l_Mesh.Submeshes = { { 0, 24, 0, {} }, { 24, 12, 1, {} } };

        return l_Mesh;
    }

    // A rippled square of more vertices than 16-bit indices can name, with two UV sets and colours, and normals without tangents
    Trinity::MeshData MakeGridMesh()
    {
        Trinity::MeshData l_Mesh;
        const float l_Last = static_cast<float>(c_GridSize - 1);
        for (std::uint32_t it_Z = 0; it_Z < c_GridSize; ++it_Z)
        {
            for (std::uint32_t it_X = 0; it_X < c_GridSize; ++it_X)
            {
                const glm::vec2 l_Fraction(static_cast<float>(it_X) / l_Last, static_cast<float>(it_Z) / l_Last);
                const float l_Height = 0.1f * std::sin(l_Fraction.x * 12.0f) * std::cos(l_Fraction.y * 9.0f);
                const float l_SlopeX = 0.1f * 12.0f * std::cos(l_Fraction.x * 12.0f) * std::cos(l_Fraction.y * 9.0f) / 10.0f;
                const float l_SlopeZ = -0.1f * 9.0f * std::sin(l_Fraction.x * 12.0f) * std::sin(l_Fraction.y * 9.0f) / 10.0f;

                l_Mesh.Positions.push_back({ l_Fraction.x * 10.0f - 5.0f, l_Height, l_Fraction.y * 10.0f - 5.0f });
                l_Mesh.Normals.push_back(glm::normalize(glm::vec3(-l_SlopeX, 1.0f, -l_SlopeZ)));
                l_Mesh.TexCoords0.push_back(l_Fraction);
                l_Mesh.TexCoords1.push_back({ l_Fraction.x * 2.0f, 1.0f - l_Fraction.y });
                l_Mesh.Colors.push_back({ l_Fraction.x, l_Fraction.y, 0.5f, 1.0f });
            }
        }

        for (std::uint32_t it_Z = 0; it_Z + 1 < c_GridSize; ++it_Z)
        {
            for (std::uint32_t it_X = 0; it_X + 1 < c_GridSize; ++it_X)
            {
                const std::uint32_t l_Corner = it_Z * c_GridSize + it_X;
                for (const std::uint32_t it_Offset : { 0u, c_GridSize, 1u, 1u, c_GridSize, c_GridSize + 1 })
                {
                    l_Mesh.Indices.push_back(l_Corner + it_Offset);
                }
            }
        }

        return l_Mesh;
    }

    // Positions alone
    Trinity::MeshData MakeTriangleMesh()
    {
        Trinity::MeshData l_Mesh;
        l_Mesh.Positions = { { -1.0f, -1.0f, 0.0f }, { 1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 2.0f } };
        l_Mesh.Indices = { 0, 1, 2 };

        return l_Mesh;
    }

    template<typename T>
    T LoadBytes(std::span<const std::byte> data, std::uint64_t offset)
    {
        T l_Value{};
        std::memcpy(&l_Value, data.data() + offset, sizeof(T));

        return l_Value;
    }

    // What the first difference is between a mesh and a GPU buffer laid out by its cooked layout, or nothing. Positions, UVs and indices are exact, directions keep within c_MeshDirectionDot, and colours within one 16-bit step
    std::string CompareMesh(const Trinity::MeshData& mesh, const Trinity::MeshLayout& layout, std::span<const std::byte> data)
    {
        if (data.size() < layout.Size || layout.VertexCount != mesh.Positions.size() || layout.IndexCount != mesh.Indices.size())
        {
            return std::format("{} bytes for {} vertices and {} indices, where the mesh has {} and {} in {} bytes", data.size(), layout.VertexCount, layout.IndexCount, mesh.Positions.size(), mesh.Indices.size(), layout.Size);
        }

        for (std::size_t it_Vertex = 0; it_Vertex < mesh.Positions.size(); ++it_Vertex)
        {
            if (LoadBytes<glm::vec3>(data, layout.Positions + it_Vertex * 12) != mesh.Positions[it_Vertex])
            {
                return std::format("position {} differs", it_Vertex);
            }

            if (layout.NormalTangents != Trinity::MeshLayout::c_Absent)
            {
                const std::array<std::int16_t, 4> l_Packed = LoadBytes<std::array<std::int16_t, 4>>(data, layout.NormalTangents + it_Vertex * 8);
                const glm::vec3 l_Normal = Trinity::DecodeOctahedral({ l_Packed[0], l_Packed[1] });
                const glm::vec3 l_Tangent = Trinity::DecodeOctahedral({ l_Packed[2], l_Packed[3] });
                const float l_Handedness = (static_cast<std::uint16_t>(l_Packed[3]) & 1u) != 0 ? -1.0f : 1.0f;
                const glm::vec3 l_SourceNormal = glm::normalize(mesh.Normals[it_Vertex]);
                const bool l_TangentMatches = mesh.Tangents.empty() ? std::abs(glm::dot(l_Tangent, l_Normal)) < 1.0f - c_MeshDirectionDot + 1e-3f : glm::dot(l_Tangent, glm::normalize(glm::vec3(mesh.Tangents[it_Vertex]))) >= c_MeshDirectionDot && l_Handedness == (mesh.Tangents[it_Vertex].w < 0.0f ? -1.0f : 1.0f);
                if (glm::dot(l_Normal, l_SourceNormal) < c_MeshDirectionDot || !l_TangentMatches)
                {
                    return std::format("the normal or tangent of vertex {} differs", it_Vertex);
                }
            }

            if ((layout.TexCoords0 != Trinity::MeshLayout::c_Absent && LoadBytes<glm::vec2>(data, layout.TexCoords0 + it_Vertex * 8) != mesh.TexCoords0[it_Vertex]) || (layout.TexCoords1 != Trinity::MeshLayout::c_Absent && LoadBytes<glm::vec2>(data, layout.TexCoords1 + it_Vertex * 8) != mesh.TexCoords1[it_Vertex]))
            {
                return std::format("a UV of vertex {} differs", it_Vertex);
            }

            if (layout.Colors != Trinity::MeshLayout::c_Absent)
            {
                const std::array<std::uint16_t, 4> l_Packed = LoadBytes<std::array<std::uint16_t, 4>>(data, layout.Colors + it_Vertex * 8);
                for (glm::length_t it_Channel = 0; it_Channel < 4; ++it_Channel)
                {
                    if (std::abs(static_cast<float>(l_Packed[static_cast<std::size_t>(it_Channel)]) / 65535.0f - mesh.Colors[it_Vertex][it_Channel]) > 1.0f / 65535.0f)
                    {
                        return std::format("the colour of vertex {} differs", it_Vertex);
                    }
                }
            }
        }

        for (std::size_t it_Index = 0; it_Index < mesh.Indices.size(); ++it_Index)
        {
            const std::uint32_t l_Index = layout.IndexFormat == Trinity::RHI::IndexFormat::UInt16 ? LoadBytes<std::uint16_t>(data, layout.Indices + it_Index * 2) : LoadBytes<std::uint32_t>(data, layout.Indices + it_Index * 4);
            if (l_Index != mesh.Indices[it_Index])
            {
                return std::format("index {} is {} where {} was cooked", it_Index, l_Index, mesh.Indices[it_Index]);
            }
        }

        return {};
    }

    Trinity::MeshBounds GetPositionBounds(const Trinity::MeshData& mesh, std::span<const std::uint32_t> indices)
    {
        Trinity::MeshBounds l_Bounds{ glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest()) };
        for (const std::uint32_t it_Index : indices)
        {
            l_Bounds.Min = glm::min(l_Bounds.Min, mesh.Positions[it_Index]);
            l_Bounds.Max = glm::max(l_Bounds.Max, mesh.Positions[it_Index]);
        }

        return l_Bounds;
    }
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

    CreateTestAssets();

    Trinity::Memory::LogUsage();
}

void SandboxLayer::OnDetach()
{
    DestroyTestAssets();
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
        TestFrameGraph();
        TestToneMapping();
        TestMeshRendererScene();
    }

    CheckRendererGraph();
    CheckGpuTimes();
    UpdateMeshes();
    UpdateSprites();
    UpdateResizes();

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
    if (m_SpritePhase != SpritePhase::Idle)
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

// Every frame adds a pass the graph culls, and on the one frame each is wanted the mesh and sprite readbacks, which so go through the Renderer's own graph
void SandboxLayer::OnBuildFrameGraph(Trinity::FrameGraph& graph, Trinity::FrameGraphTexture sceneColor)
{
    Trinity::RHI::TextureDescription l_UnusedDescription;
    l_UnusedDescription.Width = 64;
    l_UnusedDescription.Height = 64;

    const Trinity::FrameGraphTexture l_Unused = graph.CreateTexture(c_UnusedPassName, l_UnusedDescription);
    graph.AddPass(c_UnusedPassName, Trinity::FrameGraphPassType::Raster, [l_Unused, sceneColor](Trinity::FrameGraphPassBuilder& builder)
    {
        builder.AddColorAttachment({ l_Unused });
        if (sceneColor)
        {
            builder.Read(sceneColor, Trinity::RHI::ResourceState::ShaderResource);
        }
    }, []([[maybe_unused]] const Trinity::FrameGraphContext& context)
    {

    });

    if (m_MeshPhase == MeshPhase::Reading && !m_MeshReadbackAdded)
    {
        AddMeshReadbacks(graph);
    }

    if (m_SpritePhase == SpritePhase::Reading && !m_SpriteReadbackAdded)
    {
        AddSpriteReadback(graph);
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

// Four passes over four frames: a compute pass fills a transient texture with ComputeTest's pattern, a raster pass copies it into a transient 4x target and draws a triangle over it, resolving into an imported texture, and a copy pass reads that back. A fourth pass draws into a texture nobody reads and must be culled. From the second frame the transient textures come from the pool, Renderer memory holds still from the third frame to the fourth, once what earlier tests destroyed has been released, and probes read the pattern and the triangle
void SandboxLayer::TestFrameGraph()
{
    TR_PROFILE_FUNCTION();

    struct Probe
    {
        std::uint32_t X = 0;
        std::uint32_t Y = 0;
        std::array<std::uint8_t, 4> Expected{};
    };

    constexpr Trinity::RHI::Format c_Format = Trinity::RHI::Format::RGBA8Unorm;
    constexpr std::uint32_t c_Size = 64;
    constexpr std::uint32_t c_SampleCount = 4;
    constexpr std::uint32_t c_FrameCount = 4;
    constexpr std::array<std::uint8_t, 4> c_Yellow{ 255, 255, 0, 255 };

    // The triangle covers the corner of the lower-left quadrant below its diagonal, and everywhere else shows the pattern
    const std::array<Probe, 6> l_Probes
    { {
        { 40, 10, MakeComputeTexel(40, 10) },
        { 10, 20, MakeComputeTexel(10, 20) },
        { 60, 60, MakeComputeTexel(60, 60) },
        { 20, 50, MakeComputeTexel(20, 50) },
        { 4, 59, c_Yellow },
        { 2, 40, c_Yellow }
    } };

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();

    const std::string_view l_Extension = l_Info.API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";
    const auto a_ReadShader = [l_Extension](std::string_view name) { return Trinity::FileSystem::ReadFile(std::format("/engine/shaders/{}.{}", name, l_Extension)); };
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_FillShader = a_ReadShader("ComputeTest.FillTexture");
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_CopyVertexShader = a_ReadShader("SceneCopy.VertexMain");
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_CopyPixelShader = a_ReadShader("SceneCopy.PixelMain");
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_TriangleVertexShader = a_ReadShader("RasterTest.VertexMain");
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_TrianglePixelShader = a_ReadShader("RasterTest.PixelMain");
    if (!l_FillShader || !l_CopyVertexShader || !l_CopyPixelShader || !l_TriangleVertexShader || !l_TrianglePixelShader)
    {
        TR_ERROR("Frame graph: the ComputeTest, SceneCopy and RasterTest {} shaders could not be read from /engine/shaders", l_Extension);

        return;
    }

    Trinity::RHI::ComputePipelineDescription l_FillDescription;
    l_FillDescription.ComputeShader = { *l_FillShader, "FillTexture" };
    l_FillDescription.DebugName = "Sandbox graph pattern";
    const Trinity::RHI::PipelineHandle l_Fill = l_Device.CreateComputePipeline(l_FillDescription);

    const std::array<Trinity::RHI::Format, 1> l_ColorFormats{ c_Format };
    Trinity::RHI::GraphicsPipelineDescription l_CopyDescription;
    l_CopyDescription.VertexShader = { *l_CopyVertexShader, "VertexMain" };
    l_CopyDescription.PixelShader = { *l_CopyPixelShader, "PixelMain" };
    l_CopyDescription.ColorFormats = l_ColorFormats;
    l_CopyDescription.SampleCount = c_SampleCount;
    l_CopyDescription.Cull = Trinity::RHI::CullMode::None;
    l_CopyDescription.DebugName = "Sandbox graph copy";
    const Trinity::RHI::PipelineHandle l_Copy = l_Device.CreateGraphicsPipeline(l_CopyDescription);

    Trinity::RHI::GraphicsPipelineDescription l_TriangleDescription = l_CopyDescription;
    l_TriangleDescription.VertexShader = { *l_TriangleVertexShader, "VertexMain" };
    l_TriangleDescription.PixelShader = { *l_TrianglePixelShader, "PixelMain" };
    l_TriangleDescription.DebugName = "Sandbox graph triangle";
    const Trinity::RHI::PipelineHandle l_Triangle = l_Device.CreateGraphicsPipeline(l_TriangleDescription);

    Trinity::RHI::TextureDescription l_OutputDescription;
    l_OutputDescription.Width = c_Size;
    l_OutputDescription.Height = c_Size;
    l_OutputDescription.TextureFormat = c_Format;
    l_OutputDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget | Trinity::RHI::TextureUsage::CopySource;
    l_OutputDescription.DebugName = "Sandbox graph output";
    const Trinity::RHI::TextureHandle l_Output = l_Device.CreateTexture(l_OutputDescription);

    const std::uint64_t l_RowPitch = Trinity::RHI::GetTextureCopyRowPitch(c_Format, c_Size);

    Trinity::RHI::BufferDescription l_ReadbackDescription;
    l_ReadbackDescription.Size = l_RowPitch * c_Size;
    l_ReadbackDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_ReadbackDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_ReadbackDescription.DebugName = "Sandbox graph readback";
    const Trinity::RHI::BufferHandle l_Readback = l_Device.CreateBuffer(l_ReadbackDescription);

    const auto a_Destroy = [&]()
    {
        l_Device.DestroyBuffer(l_Readback);
        l_Device.DestroyTexture(l_Output);
        l_Device.DestroyPipeline(l_Triangle);
        l_Device.DestroyPipeline(l_Copy);
        l_Device.DestroyPipeline(l_Fill);
    };

    if (!l_Fill || !l_Copy || !l_Triangle || !l_Output || !l_Readback)
    {
        TR_ERROR("Frame graph: could not create the pipelines, the output texture or the readback");
        a_Destroy();

        return;
    }

    Trinity::RHI::TextureDescription l_PatternDescription;
    l_PatternDescription.Width = c_Size;
    l_PatternDescription.Height = c_Size;
    l_PatternDescription.TextureFormat = c_Format;

    Trinity::RHI::TextureDescription l_MultisampledDescription = l_PatternDescription;
    l_MultisampledDescription.SampleCount = c_SampleCount;

    Trinity::FrameGraph::Statistics l_First;
    std::array<std::uint64_t, c_FrameCount> l_RendererBytes{};
    {
        Trinity::FrameGraph l_Graph(l_Device);
        for (std::uint32_t it_Frame = 0; it_Frame < c_FrameCount; ++it_Frame)
        {
            l_Graph.Reset();

            const Trinity::FrameGraphTexture l_GraphOutput = l_Graph.ImportTexture("Output", l_Output, l_OutputDescription, it_Frame == 0 ? Trinity::RHI::ResourceState::Undefined : Trinity::RHI::ResourceState::CopySource, Trinity::RHI::ResourceState::CopySource);
            const Trinity::FrameGraphBuffer l_GraphReadback = l_Graph.ImportBuffer("Readback", l_Readback, l_ReadbackDescription.Size, Trinity::RHI::ResourceState::CopyDestination, Trinity::RHI::ResourceState::CopyDestination);
            const Trinity::FrameGraphTexture l_Pattern = l_Graph.CreateTexture("Pattern", l_PatternDescription);
            const Trinity::FrameGraphTexture l_Multisampled = l_Graph.CreateTexture("Multisampled", l_MultisampledDescription);
            const Trinity::FrameGraphTexture l_Unused = l_Graph.CreateTexture("Unused", l_PatternDescription);

            l_Graph.AddPass("Pattern", Trinity::FrameGraphPassType::Compute, [l_Pattern](Trinity::FrameGraphPassBuilder& builder) { builder.Write(l_Pattern, Trinity::RHI::ResourceState::UnorderedAccess); }, [l_Pattern, l_Fill](const Trinity::FrameGraphContext& context)
            {
                ComputePushData l_Push;
                l_Push.Texture = { context.GetDevice().GetUnorderedAccessIndex(context.GetTexture(l_Pattern)), 0 };
                l_Push.Size = c_Size;

                context.GetCommands().SetPipeline(l_Fill);
                context.GetCommands().PushConstants(std::as_bytes(std::span(&l_Push, 1)));
                context.GetCommands().Dispatch(c_Size / c_ComputeTextureGroupSize, c_Size / c_ComputeTextureGroupSize, 1);
            });

            l_Graph.AddPass("Unused", Trinity::FrameGraphPassType::Raster, [l_Unused](Trinity::FrameGraphPassBuilder& builder) { builder.AddColorAttachment({ l_Unused }); }, []([[maybe_unused]] const Trinity::FrameGraphContext& context)
            {

            });

            l_Graph.AddPass("Composite", Trinity::FrameGraphPassType::Raster, [l_Multisampled, l_GraphOutput, l_Pattern](Trinity::FrameGraphPassBuilder& builder)
            {
                Trinity::FrameGraphColorAttachment l_Attachment{ l_Multisampled };
                l_Attachment.Resolve = l_GraphOutput;
                builder.AddColorAttachment(l_Attachment);
                builder.Read(l_Pattern, Trinity::RHI::ResourceState::ShaderResource);
            }, [l_Pattern, l_Copy, l_Triangle](const Trinity::FrameGraphContext& context)
            {
                const std::array<std::uint32_t, 2> l_CopyPush{ context.GetDevice().GetShaderResourceIndex(context.GetTexture(l_Pattern)), 0 };
                context.GetCommands().SetPipeline(l_Copy);
                context.GetCommands().PushConstants(std::as_bytes(std::span(l_CopyPush)));
                context.GetCommands().Draw(3, 1, 0, 0);

                const RasterPushData l_TrianglePush{ { glm::vec4(-1.0f, -1.0f, 0.5f, 1.0f), glm::vec4(0.0f, -1.0f, 0.5f, 1.0f), glm::vec4(-1.0f, 0.0f, 0.5f, 1.0f) }, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) };
                context.GetCommands().SetPipeline(l_Triangle);
                context.GetCommands().PushConstants(std::as_bytes(std::span(&l_TrianglePush, 1)));
                context.GetCommands().Draw(3, 1, 0, 0);
            });

            l_Graph.AddPass("Readback", Trinity::FrameGraphPassType::Copy, [l_GraphOutput, l_GraphReadback](Trinity::FrameGraphPassBuilder& builder)
            {
                builder.Read(l_GraphOutput, Trinity::RHI::ResourceState::CopySource);
                builder.Write(l_GraphReadback, Trinity::RHI::ResourceState::CopyDestination);
            }, [l_GraphOutput, l_GraphReadback](const Trinity::FrameGraphContext& context)
            {
                context.GetCommands().CopyTextureToBuffer(context.GetTexture(l_GraphOutput), 0, 0, context.GetBuffer(l_GraphReadback), 0);
            });

            Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();
            l_Graph.Execute(l_Commands);
            l_Device.EndFrame();

            l_First = it_Frame == 0 ? l_Graph.GetStatistics() : l_First;
            l_RendererBytes[it_Frame] = Trinity::Memory::GetStats(Trinity::MemoryTag::Renderer).CurrentBytes;
        }

        const Trinity::FrameGraph::Statistics& l_Last = l_Graph.GetStatistics();
        const bool l_Shape = l_First.Passes == 4 && l_First.CulledPasses == 1 && !l_Graph.IsPassCulled(0) && l_Graph.IsPassCulled(1) && !l_Graph.IsPassCulled(2) && !l_Graph.IsPassCulled(3);
        const bool l_Pooled = l_First.TransientTextures == 2 && l_First.PooledTextures == 2 && l_Last.TransientTextures == 2 && l_Last.PooledTextures == 2;
        if (!l_Shape || !l_Pooled || l_RendererBytes[2] != l_RendererBytes[3])
        {
            TR_ERROR("Frame graph: the first frame ran {} pass(es) with {} culled, {} transient and {} pooled texture(s), the last {} transient and {} pooled, and Renderer went from {} to {} between the last two frames", l_First.Passes, l_First.CulledPasses, l_First.TransientTextures, l_First.PooledTextures, l_Last.TransientTextures, l_Last.PooledTextures, Trinity::Memory::FormatBytes(l_RendererBytes[2]), Trinity::Memory::FormatBytes(l_RendererBytes[3]));
            l_Device.WaitIdle();
            a_Destroy();

            return;
        }

        l_Device.WaitIdle();
    }

    if (l_Info.API == Trinity::GraphicsAPI::None)
    {
        TR_INFO("Frame graph: None ran 3 of 4 passes with the unused one culled, and reused 2 pooled textures over {} frames", c_FrameCount);
        a_Destroy();

        return;
    }

    const std::span<const std::byte> l_Data = l_Device.GetMappedData(l_Readback);
    std::string l_Wrong;
    for (const Probe& it_Probe : l_Probes)
    {
        const std::size_t l_Offset = static_cast<std::size_t>(it_Probe.Y * l_RowPitch + it_Probe.X * 4);
        if (l_Offset + 4 > l_Data.size() || std::memcmp(l_Data.data() + l_Offset, it_Probe.Expected.data(), it_Probe.Expected.size()) != 0)
        {
            const auto a_Channel = [&l_Data, l_Offset](std::size_t channel) { return l_Offset + channel < l_Data.size() ? std::to_integer<std::uint32_t>(l_Data[l_Offset + channel]) : 0u; };
            l_Wrong += std::format("{}({}, {}) is ({}, {}, {}, {}) where ({}, {}, {}, {}) was expected", l_Wrong.empty() ? "" : "; ", it_Probe.X, it_Probe.Y, a_Channel(0), a_Channel(1), a_Channel(2), a_Channel(3), it_Probe.Expected[0], it_Probe.Expected[1], it_Probe.Expected[2], it_Probe.Expected[3]);
        }
    }

    a_Destroy();

    if (!l_Wrong.empty())
    {
        TR_ERROR("Frame graph: the readback differs: {}", l_Wrong);

        return;
    }

    TR_INFO("Frame graph: {} ran 3 of 4 passes with the unused one culled and {} barriers a frame, reused 2 pooled textures over {} frames with Renderer holding {} over the last two, and all {} probes for the pattern and the triangle hold", Trinity::ToString(l_Info.API), l_First.Barriers, c_FrameCount, Trinity::Memory::FormatBytes(l_RendererBytes[3]), l_Probes.size());
}

// Linear colours from black to far past white go through the Renderer's tonemap pass three ways, and each 8-bit result must be within one step of ApplyToneMapping on the CPU
void SandboxLayer::TestToneMapping()
{
    TR_PROFILE_FUNCTION();

    constexpr std::array<glm::vec3, 24> c_Colors
    { {
        { 0.0f, 0.0f, 0.0f }, { 0.001f, 0.001f, 0.001f }, { 0.003f, 0.003f, 0.003f }, { 0.01f, 0.01f, 0.01f },
        { 0.05f, 0.05f, 0.05f }, { 0.1f, 0.1f, 0.1f }, { 0.18f, 0.18f, 0.18f }, { 0.2159f, 0.2159f, 0.2159f },
        { 0.5f, 0.5f, 0.5f }, { 0.75f, 0.75f, 0.75f }, { 0.9f, 0.9f, 0.9f }, { 1.0f, 1.0f, 1.0f },
        { 1.5f, 1.5f, 1.5f }, { 2.0f, 2.0f, 2.0f }, { 4.0f, 4.0f, 4.0f }, { 16.0f, 16.0f, 16.0f },
        { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 2.0f, 0.5f, 0.1f },
        { 0.2f, 0.6f, 1.2f }, { 0.05f, 0.03f, 0.01f }, { 10.0f, 5.0f, 1.0f }, { 0.5f, 0.25f, 0.125f }
    } };

    constexpr std::size_t c_MidGrey = 6;
    constexpr std::uint32_t c_Width = static_cast<std::uint32_t>(c_Colors.size());
    constexpr Trinity::RHI::Format c_InputFormat = Trinity::RHI::Format::RGBA16Float;
    constexpr Trinity::RHI::Format c_OutputFormat = Trinity::RHI::Format::RGBA8Unorm;

    // No change, half the light with no curve, and four times the light through the curve
    constexpr std::array<Trinity::ToneMapping, 3> c_Settings
    { {
        { Trinity::c_NeutralEV100, Trinity::Tonemapper::PBRNeutral },
        { Trinity::c_NeutralEV100 + 1.0f, Trinity::Tonemapper::None },
        { Trinity::c_NeutralEV100 - 2.0f, Trinity::Tonemapper::PBRNeutral }
    } };

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();
    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();

    const std::uint64_t l_InputPitch = Trinity::RHI::GetTextureCopyRowPitch(c_InputFormat, c_Width);
    const std::uint64_t l_OutputStride = (Trinity::RHI::GetTextureCopyRowPitch(c_OutputFormat, c_Width) + Trinity::RHI::c_TextureCopyOffsetAlignment - 1) / Trinity::RHI::c_TextureCopyOffsetAlignment * Trinity::RHI::c_TextureCopyOffsetAlignment;

    Trinity::RHI::BufferDescription l_ReadbackDescription;
    l_ReadbackDescription.Size = l_OutputStride * c_Settings.size();
    l_ReadbackDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_ReadbackDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_ReadbackDescription.DebugName = "Sandbox tone mapping readback";
    const Trinity::RHI::BufferHandle l_Readback = l_Device.CreateBuffer(l_ReadbackDescription);
    if (!l_Readback)
    {
        TR_ERROR("Tone mapping: could not create the readback");

        return;
    }

    // The CPU reference starts from the colours as the half floats hold them
    std::array<glm::vec3, c_Colors.size()> l_Inputs{};
    {
        Trinity::FrameGraph l_Graph(l_Device);
        Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();

        const Trinity::RHI::UploadAllocation l_Upload = l_Device.AllocateUpload(l_InputPitch, Trinity::RHI::c_TextureCopyOffsetAlignment);
        for (std::size_t it_Color = 0; it_Color < c_Colors.size(); ++it_Color)
        {
            const glm::vec4 l_Color(c_Colors[it_Color], 1.0f);
            std::array<std::uint16_t, 4> l_Halves{};
            for (glm::length_t it_Channel = 0; it_Channel < 4; ++it_Channel)
            {
                l_Halves[static_cast<std::size_t>(it_Channel)] = glm::packHalf1x16(l_Color[it_Channel]);
                if (it_Channel < 3)
                {
                    l_Inputs[it_Color][it_Channel] = glm::unpackHalf1x16(l_Halves[static_cast<std::size_t>(it_Channel)]);
                }
            }

            if (l_Upload.Data.size() >= (it_Color + 1) * sizeof(l_Halves))
            {
                std::memcpy(l_Upload.Data.data() + it_Color * sizeof(l_Halves), l_Halves.data(), sizeof(l_Halves));
            }
        }

        Trinity::RHI::TextureDescription l_InputDescription;
        l_InputDescription.Width = c_Width;
        l_InputDescription.Height = 1;
        l_InputDescription.TextureFormat = c_InputFormat;

        Trinity::RHI::TextureDescription l_OutputDescription = l_InputDescription;
        l_OutputDescription.TextureFormat = c_OutputFormat;

        const Trinity::FrameGraphTexture l_Input = l_Graph.CreateTexture("Tone mapping input", l_InputDescription);
        const Trinity::FrameGraphBuffer l_GraphUpload = l_Graph.ImportBuffer("Tone mapping upload", l_Upload.Buffer, l_Upload.Offset + l_InputPitch, Trinity::RHI::ResourceState::CopySource, Trinity::RHI::ResourceState::CopySource);
        const Trinity::FrameGraphBuffer l_GraphReadback = l_Graph.ImportBuffer("Tone mapping readback", l_Readback, l_ReadbackDescription.Size, Trinity::RHI::ResourceState::CopyDestination, Trinity::RHI::ResourceState::CopyDestination);

        const std::uint64_t l_UploadOffset = l_Upload.Offset;
        l_Graph.AddPass("Tone mapping upload", Trinity::FrameGraphPassType::Copy, [l_Input, l_GraphUpload](Trinity::FrameGraphPassBuilder& builder)
        {
            builder.Read(l_GraphUpload, Trinity::RHI::ResourceState::CopySource);
            builder.Write(l_Input, Trinity::RHI::ResourceState::CopyDestination);
        }, [l_Input, l_GraphUpload, l_UploadOffset](const Trinity::FrameGraphContext& context)
        {
            context.GetCommands().CopyBufferToTexture(context.GetBuffer(l_GraphUpload), l_UploadOffset, context.GetTexture(l_Input), 0, 0, { 0, 0, c_Width, 1 });
        });

        std::array<Trinity::FrameGraphTexture, c_Settings.size()> l_Outputs{};
        for (std::size_t it_Setting = 0; it_Setting < c_Settings.size(); ++it_Setting)
        {
            l_Outputs[it_Setting] = l_Graph.CreateTexture("Tone mapping output", l_OutputDescription);
            l_Renderer.AddTonemapPass(l_Graph, l_Input, l_Outputs[it_Setting], c_Settings[it_Setting]);
        }

        l_Graph.AddPass("Tone mapping readback", Trinity::FrameGraphPassType::Copy, [l_Outputs, l_GraphReadback](Trinity::FrameGraphPassBuilder& builder)
        {
            for (const Trinity::FrameGraphTexture it_Output : l_Outputs)
            {
                builder.Read(it_Output, Trinity::RHI::ResourceState::CopySource);
            }

            builder.Write(l_GraphReadback, Trinity::RHI::ResourceState::CopyDestination);
        }, [l_Outputs, l_GraphReadback, l_OutputStride](const Trinity::FrameGraphContext& context)
        {
            for (std::size_t it_Output = 0; it_Output < l_Outputs.size(); ++it_Output)
            {
                context.GetCommands().CopyTextureToBuffer(context.GetTexture(l_Outputs[it_Output]), 0, 0, context.GetBuffer(l_GraphReadback), it_Output * l_OutputStride);
            }
        });

        l_Graph.Execute(l_Commands);
        l_Device.EndFrame();
        l_Device.WaitIdle();
    }

    if (l_Info.API == Trinity::GraphicsAPI::None)
    {
        TR_INFO("Tone mapping: None ran {} colours through the tonemap pass {} ways", c_Colors.size(), c_Settings.size());
        l_Device.DestroyBuffer(l_Readback);

        return;
    }

    const std::span<const std::byte> l_Data = l_Device.GetMappedData(l_Readback);
    std::string l_Wrong;
    std::uint32_t l_MidGrey = 0;
    for (std::size_t it_Setting = 0; it_Setting < c_Settings.size(); ++it_Setting)
    {
        for (std::size_t it_Color = 0; it_Color < c_Colors.size(); ++it_Color)
        {
            const glm::vec3 l_Expected = glm::round(Trinity::ApplyToneMapping(l_Inputs[it_Color], c_Settings[it_Setting]) * 255.0f);
            const std::size_t l_Offset = static_cast<std::size_t>(it_Setting * l_OutputStride + it_Color * 4);
            const auto a_Channel = [&l_Data, l_Offset](std::size_t channel) { return l_Offset + channel < l_Data.size() ? std::to_integer<std::int32_t>(l_Data[l_Offset + channel]) : -1; };

            bool l_Matches = true;
            for (glm::length_t it_Channel = 0; it_Channel < 3; ++it_Channel)
            {
                l_Matches = l_Matches && std::abs(a_Channel(static_cast<std::size_t>(it_Channel)) - static_cast<std::int32_t>(l_Expected[it_Channel])) <= 1;
            }

            if (!l_Matches)
            {
                l_Wrong += std::format("{}setting {} colour ({}, {}, {}) is ({}, {}, {}) where ({}, {}, {}) was expected", l_Wrong.empty() ? "" : "; ", it_Setting, c_Colors[it_Color].r, c_Colors[it_Color].g, c_Colors[it_Color].b, a_Channel(0), a_Channel(1), a_Channel(2), l_Expected.r, l_Expected.g, l_Expected.b);
            }

            l_MidGrey = it_Setting == 0 && it_Color == c_MidGrey ? static_cast<std::uint32_t>(std::max(a_Channel(0), 0)) : l_MidGrey;
        }
    }

    l_Device.DestroyBuffer(l_Readback);

    if (!l_Wrong.empty())
    {
        TR_ERROR("Tone mapping: the tonemap pass differs from the CPU reference: {}", l_Wrong);

        return;
    }

    TR_INFO("Tone mapping: {} matched the CPU reference within one 8-bit step for {} linear colours through PBR Neutral, through no curve at EV100 +1 and through PBR Neutral at EV100 -2, from exposure 1, with linear 0.18 under PBR Neutral at {}", Trinity::ToString(l_Info.API), c_Colors.size(), l_MidGrey);
}
void SandboxLayer::CheckRendererGraph()
{
    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const Trinity::FrameGraph& l_Graph = l_Renderer.GetFrameGraph();
    if (m_GraphChecked || l_Graph.GetPassCount() == 0)
    {
        return;
    }

    m_GraphChecked = true;

    std::string l_Passes;
    bool l_Scene = false;
    bool l_Output = false;
    bool l_UnusedCulled = false;
    for (std::uint32_t it_Pass = 0; it_Pass < l_Graph.GetPassCount(); ++it_Pass)
    {
        const std::string_view l_Name = l_Graph.GetPassName(it_Pass);
        const bool l_Culled = l_Graph.IsPassCulled(it_Pass);
        l_Passes += std::format("{}{}{}", l_Passes.empty() ? "" : ", ", l_Name, l_Culled ? " (culled)" : "");

        l_Scene = l_Scene || (l_Name == "Scene" && !l_Culled);
        l_Output = l_Output || (l_Name == "Output" && !l_Culled);
        l_UnusedCulled = l_UnusedCulled || (l_Name == c_UnusedPassName && l_Culled);
    }

    const Trinity::FrameGraph::Statistics& l_Statistics = l_Graph.GetStatistics();
    if (!l_Scene || !l_Output || !l_UnusedCulled || l_Statistics.CulledPasses != 1)
    {
        TR_ERROR("Renderer graph: the first frame's passes were {}, where Scene and Output run and only {} is culled", l_Passes, c_UnusedPassName);

        return;
    }

    TR_INFO("Renderer graph: {} ran {} with {} barriers", Trinity::ToString(Trinity::Application::Get().GetDevice().GetInfo().API), l_Passes, l_Statistics.Barriers);
}

// Once the Renderer's graph has published its first averages: Scene and Output were timed, and no pass took less than nothing or longer than the whole. The null device's timestamps all read 0
void SandboxLayer::CheckGpuTimes()
{
    const Trinity::FrameGraph& l_Graph = Trinity::Application::Get().GetRenderer().GetFrameGraph();
    const std::span<const Trinity::FrameGraph::PassTime> l_Times = l_Graph.GetPassTimes();
    if (m_GpuTimesChecked || l_Times.empty())
    {
        return;
    }

    m_GpuTimesChecked = true;

    const Trinity::GraphicsAPI l_API = Trinity::Application::Get().GetDevice().GetInfo().API;
    const float l_Total = l_Graph.GetGpuMilliseconds();

    std::string l_Passes;
    float l_Sum = 0.0f;
    bool l_Scene = false;
    bool l_Output = false;
    bool l_InRange = true;
    for (const Trinity::FrameGraph::PassTime& it_Time : l_Times)
    {
        l_Passes += std::format("{}{} {:.3f} ms", l_Passes.empty() ? "" : ", ", it_Time.Name, it_Time.Milliseconds);
        l_Sum += it_Time.Milliseconds;
        l_Scene = l_Scene || it_Time.Name == "Scene";
        l_Output = l_Output || it_Time.Name == "Output";
        l_InRange = l_InRange && it_Time.Milliseconds >= 0.0f && it_Time.Milliseconds <= l_Total * 1.001f + 0.001f;
    }

    const bool l_Timed = l_API == Trinity::GraphicsAPI::None ? l_Total == 0.0f : l_Total > 0.0f;
    if (!l_Scene || !l_Output || !l_InRange || !l_Timed)
    {
        TR_ERROR("GPU times: {} timed {} with {:.3f} ms in all, where Scene and Output are timed and each pass takes from 0 to the whole", Trinity::ToString(l_API), l_Passes, l_Total);

        return;
    }

    TR_INFO("GPU times: {} averaged over 30 frames {}, adding up to {:.3f} ms of {:.3f} ms in all", Trinity::ToString(l_API), l_Passes, l_Sum, l_Total);
}

// Each resize recreates the scene target and the swap chain or offscreen target between frames, and Renderer memory after 100 resizes must match that after 200. The window gets its size back afterwards
void SandboxLayer::UpdateResizes()
{
    if (!s_ResizeTest.Get() || m_SpritePhase != SpritePhase::Running || m_Resizes > c_ResizeCount)
    {
        return;
    }

    Trinity::Window& l_Window = Trinity::Application::Get().GetWindow();
    if (m_Resizes == 0)
    {
        m_ResizeWidth = l_Window.GetWidth();
        m_ResizeHeight = l_Window.GetHeight();
    }

    const std::uint64_t l_Bytes = Trinity::Memory::GetStats(Trinity::MemoryTag::Renderer).CurrentBytes;
    if (m_Resizes == c_ResizeCount / 2)
    {
        m_ResizeBytesMiddle = l_Bytes;
    }

    if (m_Resizes == c_ResizeCount)
    {
        ++m_Resizes;
        l_Window.SetSize(m_ResizeWidth, m_ResizeHeight);
        if (l_Bytes != m_ResizeBytesMiddle)
        {
            TR_ERROR("Resizes: Renderer went from {} after {} resizes to {} after {}", Trinity::Memory::FormatBytes(m_ResizeBytesMiddle), c_ResizeCount / 2, Trinity::Memory::FormatBytes(l_Bytes), c_ResizeCount);

            return;
        }

        TR_INFO("Resizes: {} resized the window {} times with Renderer holding {} after {} and after {}", Trinity::ToString(Trinity::Application::Get().GetDevice().GetInfo().API), c_ResizeCount, Trinity::Memory::FormatBytes(l_Bytes), c_ResizeCount / 2, c_ResizeCount);

        return;
    }

    const std::uint32_t l_Step = m_Resizes % c_ResizeCycle;
    l_Window.SetSize(c_ResizeBaseWidth + l_Step * c_ResizeStep, c_ResizeBaseHeight + (l_Step * 7 % c_ResizeCycle) * c_ResizeStep);
    ++m_Resizes;
}

// Entities with MeshRenderers holding none to three materials, some invalid, an invalid mesh and shadows on and off, saved, loaded and saved again: the two files match, and every component comes back equal
void SandboxLayer::TestMeshRendererScene()
{
    TR_PROFILE_FUNCTION();

    constexpr std::uint32_t c_EntityCount = 12;
    constexpr std::string_view c_FirstPath = "/saves/sandbox/meshes/MeshRenderers.trscene";
    constexpr std::string_view c_SecondPath = "/saves/sandbox/meshes/MeshRenderers.Again.trscene";

    Trinity::Scene l_Scene;
    std::mt19937 l_Random(c_MeshSeed);
    std::uniform_real_distribution<float> l_Position(-10.0f, 10.0f);
    std::vector<Trinity::UUID> l_Entities;
    Trinity::Entity l_Parent;
    for (std::uint32_t it_Entity = 0; it_Entity < c_EntityCount; ++it_Entity)
    {
        const std::string l_Name = std::format("Mesh {}", it_Entity);
        Trinity::Entity l_Entity = it_Entity % 4 == 1 ? l_Scene.CreateEntity(l_Name, l_Parent) : l_Scene.CreateEntity(l_Name);
        l_Entity.Get<Trinity::TransformComponent>().Position = { l_Position(l_Random), l_Position(l_Random), l_Position(l_Random) };

        Trinity::MeshRendererComponent& l_Renderer = l_Entity.Add<Trinity::MeshRendererComponent>();
        l_Renderer.Mesh = it_Entity % 5 == 4 ? Trinity::UUID() : Trinity::UUID::Generate();
        for (std::uint32_t it_Material = 0; it_Material < it_Entity % 4; ++it_Material)
        {
            l_Renderer.Materials.push_back(it_Material == 1 ? Trinity::UUID() : Trinity::UUID::Generate());
        }

        l_Renderer.CastShadows = it_Entity % 3 != 0;
        l_Entities.push_back(l_Entity.GetUUID());
        l_Parent = l_Entity;
    }

    Trinity::Scene l_Loaded;
    const Trinity::Expected<void, std::string> l_Saved = Trinity::SceneSerializer::Save(l_Scene, c_FirstPath);
    const Trinity::Expected<void, std::string> l_Read = l_Saved ? Trinity::SceneSerializer::Load(l_Loaded, c_FirstPath) : l_Saved;
    const Trinity::Expected<void, std::string> l_SavedAgain = l_Read ? Trinity::SceneSerializer::Save(l_Loaded, c_SecondPath) : l_Read;
    if (!l_SavedAgain)
    {
        TR_ERROR("Mesh scene: {}", l_SavedAgain.GetError());

        return;
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_First = Trinity::FileSystem::ReadText(c_FirstPath);
    const Trinity::Expected<std::string, Trinity::FileError> l_Second = Trinity::FileSystem::ReadText(c_SecondPath);
    const bool l_Same = l_First && l_Second && *l_First == *l_Second;
    const std::size_t l_Equal = static_cast<std::size_t>(std::ranges::count_if(l_Entities, [&l_Scene, &l_Loaded](Trinity::UUID id)
    {
        const Trinity::Entity l_Original = l_Scene.FindEntityByUUID(id);
        const Trinity::Entity l_Copy = l_Loaded.FindEntityByUUID(id);

        return l_Copy && l_Copy.Has<Trinity::MeshRendererComponent>() && l_Copy.Get<Trinity::MeshRendererComponent>() == l_Original.Get<Trinity::MeshRendererComponent>();
    }));

    if (!l_Same || l_Equal != l_Entities.size())
    {
        TR_ERROR("Mesh scene: the second save {} the first, and {} of {} MeshRenderers came back equal", l_Same ? "matched" : "differed from", l_Equal, l_Entities.size());

        return;
    }

    TR_INFO("Mesh scene: {} MeshRenderers with 0 to 3 materials saved, loaded and saved again into two identical files of {} bytes, and every component came back equal", l_Entities.size(), l_First->size());
}

// The model stands in for a file an importer reads: the registry lists a mesh sub-asset per key in its .meta, and each mesh is cooked into the cache where an import would write it. Listing the keys again in another order, and scanning the folder with a second registry, must find the same UUIDs
bool SandboxLayer::AddTestMeshes(Trinity::MemorySource& cache)
{
    const std::string l_ModelPath = std::format("{}/{}", c_SpriteTestRoot, c_ModelName);
    const Trinity::AssetRecord* l_Model = m_SpriteRegistry->FindByPath(l_ModelPath);
    if (l_Model == nullptr)
    {
        TR_ERROR("Meshes: the registry has no record for {}", l_ModelPath);

        return false;
    }

    const Trinity::UUID l_ModelID = l_Model->ID;
    std::vector<std::pair<std::string_view, Trinity::MeshData>> l_Meshes;
    l_Meshes.emplace_back("Cube", MakeCubeMesh());
    l_Meshes.emplace_back("Grid", MakeGridMesh());
    l_Meshes.emplace_back("Triangle", MakeTriangleMesh());

    std::vector<Trinity::SubAsset> l_SubAssets;
    for (const auto& [it_Name, it_Data] : l_Meshes)
    {
        l_SubAssets.push_back({ std::string(it_Name), std::string(Trinity::MeshAsset::c_AssetType), {} });
    }

    if (!m_SpriteRegistry->SetSubAssets(l_ModelID, l_SubAssets))
    {
        TR_ERROR("Meshes: the sub-assets of {} could not be recorded", l_ModelPath);

        return false;
    }

    const std::vector<Trinity::SubAsset> l_First = m_SpriteRegistry->Find(l_ModelID)->SubAssets;
    std::ranges::reverse(l_SubAssets);
    bool l_Kept = m_SpriteRegistry->SetSubAssets(l_ModelID, l_SubAssets);

    Trinity::AssetRegistry l_Rescan(c_SpriteTestRoot);
    static_cast<void>(l_Rescan.Scan());
    for (const Trinity::SubAsset& it_SubAsset : l_First)
    {
        const Trinity::AssetRecord* l_Child = m_SpriteRegistry->Find(it_SubAsset.ID);
        const Trinity::AssetRecord* l_Rescanned = l_Rescan.Find(it_SubAsset.ID);
        l_Kept = l_Kept && l_Child != nullptr && l_Child->Parent == l_ModelID && l_Child->Importer == Trinity::MeshAsset::c_AssetType && l_Child->Path == std::format("{}#{}", l_ModelPath, it_SubAsset.Key) && l_Rescanned != nullptr && l_Rescanned->Path == l_Child->Path && l_Rescanned->Parent == l_ModelID;
    }

    if (!l_Kept)
    {
        TR_ERROR("Meshes: the sub-assets of {} changed UUID when listed again or scanned by a second registry", l_ModelPath);

        return false;
    }

    for (auto& [it_Name, it_Data] : l_Meshes)
    {
        Trinity::Expected<std::vector<std::byte>, std::string> l_Cooked = Trinity::CookMesh(it_Data);
        if (!l_Cooked)
        {
            TR_ERROR("Meshes: {} could not be cooked: {}", it_Name, l_Cooked.GetError());

            return false;
        }

        const Trinity::UUID l_ID = std::ranges::find(l_First, std::string(it_Name), &Trinity::SubAsset::Key)->ID;
        const std::string l_Path = Trinity::GetCookedMeshPath(l_ID);
        static_cast<void>(cache.AddFile(std::string_view(l_Path).substr(Trinity::Project::c_CacheMount.size() + 1), *l_Cooked));
        m_TestMeshes.push_back({ it_Name, l_ID, std::move(it_Data), std::move(*l_Cooked) });
    }

    TR_INFO("Meshes: {} recorded {} mesh sub-assets, whose UUIDs held when listed again and when a second registry scanned the folder", c_ModelName, m_TestMeshes.size());

    return true;
}

// Loads the test meshes and reads their GPU buffers back, then runs the loads and releases, and starts the sprites once Assets is back to 0 B
void SandboxLayer::UpdateMeshes()
{
    const std::uint64_t l_Frame = Trinity::Application::Get().GetFrameCount();
    if (m_MeshPhase == MeshPhase::Loading)
    {
        if (m_MeshRefs.empty())
        {
            for (const TestMesh& it_Mesh : m_TestMeshes)
            {
                m_MeshRefs.emplace_back(it_Mesh.ID);
            }
        }

        const bool l_Failed = std::ranges::any_of(m_MeshRefs, [](const Trinity::AssetRef<Trinity::MeshAsset>& mesh) { return mesh.GetState() == Trinity::AssetState::Failed; });
        const bool l_Ready = std::ranges::all_of(m_MeshRefs, [](const Trinity::AssetRef<Trinity::MeshAsset>& mesh) { return mesh.IsReady(); });
        if (l_Failed || (!l_Ready && l_Frame - m_MeshPhaseFrame > c_SpriteLoadTimeoutFrames))
        {
            TR_ERROR("Meshes: the {} test meshes {}", m_TestMeshes.size(), l_Failed ? "did not all load" : std::format("were not all loaded after {} frames", c_SpriteLoadTimeoutFrames));
            FinishMeshes();

            return;
        }

        if (!l_Ready)
        {
            return;
        }

        // Uploaded at the start of this frame, then copied out by a pass in the Renderer's graph
        for (const Trinity::AssetRef<Trinity::MeshAsset>& it_Mesh : m_MeshRefs)
        {
            Trinity::RHI::BufferDescription l_Description;
            l_Description.Size = it_Mesh.Get()->GetLayout().Size;
            l_Description.Usage = Trinity::RHI::BufferUsage::CopyDestination;
            l_Description.Memory = Trinity::RHI::MemoryType::Readback;
            l_Description.DebugName = "Sandbox mesh readback";
            m_MeshReadbacks.push_back(Trinity::Application::Get().GetDevice().CreateBuffer(l_Description));
        }

        if (std::ranges::any_of(m_MeshReadbacks, [](Trinity::RHI::BufferHandle readback) { return !readback; }))
        {
            TR_ERROR("Meshes: could not create the readback buffers");
            FinishMeshes();

            return;
        }

        m_MeshPhase = MeshPhase::Reading;

        return;
    }

    if (m_MeshPhase == MeshPhase::Reading)
    {
        if (m_MeshReadbackAdded)
        {
            CheckMeshReadbacks();
            m_MeshRefs.clear();
            m_MeshRandom.seed(c_MeshSeed);
            m_MeshPhase = MeshPhase::Churning;
            m_MeshPhaseFrame = l_Frame;
        }

        return;
    }

    if (m_MeshPhase == MeshPhase::Churning)
    {
        UpdateMeshChurn(l_Frame);

        return;
    }

    if (m_MeshPhase == MeshPhase::Settling)
    {
        // Reads cancelled on release and decodes still running let go of their memory as they finish
        const std::uint64_t l_Bytes = Trinity::Memory::GetStats(Trinity::MemoryTag::Assets).CurrentBytes;
        if (l_Bytes == 0 && Trinity::AssetManager::GetEntryCount() == 0)
        {
            TR_INFO("Meshes: {} asynchronous loads and releases, {} of them released once loaded and the rest cancelled while reading or decoding, left Assets at 0 B in {} frame(s) after the last release", m_MeshChurnLoads, m_MeshChurnFinished, l_Frame - m_MeshPhaseFrame);
            FinishMeshes();
        }
        else if (l_Frame - m_MeshPhaseFrame > c_MeshSettleFrames)
        {
            TR_ERROR("Meshes: Assets still held {} in {} entries {} frames after the last of {} loads was released", Trinity::Memory::FormatBytes(l_Bytes), Trinity::AssetManager::GetEntryCount(), c_MeshSettleFrames, m_MeshChurnLoads);
            FinishMeshes();
        }
    }
}

// One pass copies every mesh's buffer into its readback buffer, moving each from Geometry to CopySource and back
void SandboxLayer::AddMeshReadbacks(Trinity::FrameGraph& graph)
{
    m_MeshReadbackAdded = true;

    std::vector<std::pair<Trinity::FrameGraphBuffer, Trinity::FrameGraphBuffer>> l_Copies;
    for (std::size_t it_Mesh = 0; it_Mesh < m_MeshRefs.size(); ++it_Mesh)
    {
        const Trinity::MeshAsset* l_Mesh = m_MeshRefs[it_Mesh].Get();
        const std::uint64_t l_Size = l_Mesh->GetLayout().Size;
        l_Copies.emplace_back(graph.ImportBuffer("Sandbox mesh", l_Mesh->GetBuffer(), l_Size, Trinity::RHI::ResourceState::Geometry, Trinity::RHI::ResourceState::Geometry), graph.ImportBuffer("Sandbox mesh readback", m_MeshReadbacks[it_Mesh], l_Size, Trinity::RHI::ResourceState::CopyDestination, Trinity::RHI::ResourceState::CopyDestination));
    }

    std::vector<std::uint64_t> l_Sizes;
    for (const Trinity::AssetRef<Trinity::MeshAsset>& it_Mesh : m_MeshRefs)
    {
        l_Sizes.push_back(it_Mesh.Get()->GetLayout().Size);
    }

    graph.AddPass("Sandbox mesh readback", Trinity::FrameGraphPassType::Copy, [&l_Copies](Trinity::FrameGraphPassBuilder& builder)
    {
        for (const auto& [it_Mesh, it_Readback] : l_Copies)
        {
            builder.Read(it_Mesh, Trinity::RHI::ResourceState::CopySource);
            builder.Write(it_Readback, Trinity::RHI::ResourceState::CopyDestination);
        }
    }, [l_Copies, l_Sizes](const Trinity::FrameGraphContext& context)
    {
        for (std::size_t it_Copy = 0; it_Copy < l_Copies.size(); ++it_Copy)
        {
            context.GetCommands().CopyBuffer(context.GetBuffer(l_Copies[it_Copy].first), 0, context.GetBuffer(l_Copies[it_Copy].second), 0, l_Sizes[it_Copy]);
        }
    });
}

// Each loaded mesh must hold what its file held, its bounds must be those of its positions, and its GPU buffer must read back as the cooked bytes and decode to the mesh it was cooked from. The null device reads back nothing, so there the cooked bytes are decoded instead
void SandboxLayer::CheckMeshReadbacks()
{
    TR_PROFILE_FUNCTION();

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    l_Device.WaitIdle();

    const Trinity::GraphicsAPI l_API = l_Device.GetInfo().API;
    std::string l_Wrong;
    std::uint64_t l_Vertices = 0;
    std::uint64_t l_Indices = 0;
    std::string l_Summary;
    for (std::size_t it_Mesh = 0; it_Mesh < m_TestMeshes.size() && l_Wrong.empty(); ++it_Mesh)
    {
        const TestMesh& l_Test = m_TestMeshes[it_Mesh];
        const Trinity::MeshAsset* l_Mesh = m_MeshRefs[it_Mesh].Get();
        const Trinity::Expected<Trinity::MeshFile, std::string> l_File = Trinity::ReadMeshFile(l_Test.Cooked);
        if (!l_File)
        {
            l_Wrong = std::format("{}'s cooked file does not read back: {}", l_Test.Name, l_File.GetError());

            break;
        }

        const std::vector<Trinity::Submesh> l_Submeshes(l_Mesh->GetSubmeshes().begin(), l_Mesh->GetSubmeshes().end());
        const bool l_Matches = l_Mesh->GetLayout() == l_File->Layout && l_Mesh->GetBounds() == l_File->Bounds && l_Submeshes == l_File->Submeshes;
        bool l_Bounds = l_Mesh->GetBounds() == GetPositionBounds(l_Test.Data, l_Test.Data.Indices);
        for (const Trinity::Submesh& it_Submesh : l_Submeshes)
        {
            l_Bounds = l_Bounds && it_Submesh.Bounds == GetPositionBounds(l_Test.Data, std::span(l_Test.Data.Indices).subspan(it_Submesh.FirstIndex, it_Submesh.IndexCount));
        }

        const std::span<const std::byte> l_Gpu = l_Device.GetMappedData(m_MeshReadbacks[it_Mesh]);
        const bool l_ReadBack = l_API == Trinity::GraphicsAPI::None || (l_Gpu.size() >= l_File->Data.size() && std::memcmp(l_Gpu.data(), l_File->Data.data(), l_File->Data.size()) == 0);
        const std::string l_Decoded = CompareMesh(l_Test.Data, l_File->Layout, l_API == Trinity::GraphicsAPI::None ? l_File->Data : l_Gpu);
        if (!l_Matches || !l_Bounds || !l_ReadBack || !l_Decoded.empty())
        {
            l_Wrong = std::format("{} {}", l_Test.Name, !l_Matches ? "loaded with a layout, bounds or submeshes other than its file's" : !l_Bounds ? "has bounds other than its positions'" : !l_ReadBack ? "read back from the GPU other than as cooked" : l_Decoded);

            break;
        }

        l_Vertices += l_File->Layout.VertexCount;
        l_Indices += l_File->Layout.IndexCount;
        l_Summary += std::format("{}{} with {} vertices, {}-bit indices and {} submesh(es)", l_Summary.empty() ? "" : ", ", l_Test.Name, l_File->Layout.VertexCount, Trinity::RHI::GetIndexSize(l_File->Layout.IndexFormat) * 8, l_Submeshes.size());
    }

    for (const Trinity::RHI::BufferHandle it_Readback : m_MeshReadbacks)
    {
        l_Device.DestroyBuffer(it_Readback);
    }

    m_MeshReadbacks.clear();

    if (!l_Wrong.empty())
    {
        TR_ERROR("Meshes: {}", l_Wrong);

        return;
    }

    TR_INFO("Meshes: {} loaded {} ({} vertices and {} indices in all), and every vertex, index, bound and submesh {} as cooked", Trinity::ToString(l_API), l_Summary, l_Vertices, l_Indices, l_API == Trinity::GraphicsAPI::None ? "decoded" : "read back from the GPU");
}

// Every frame acquires meshes at random and releases what is due. A quarter are released at once, so their reads are cancelled, and the rest live up to c_MeshChurnMaxLifetime frames. Only an acquisition that starts a load counts
void SandboxLayer::UpdateMeshChurn(std::uint64_t frame)
{
    std::erase_if(m_MeshChurn, [this, frame](const ChurnRef& churn)
    {
        if (churn.ReleaseFrame > frame)
        {
            return false;
        }

        m_MeshChurnFinished += churn.Ref.IsReady() ? 1 : 0;

        return true;
    });

    std::uniform_int_distribution<std::size_t> l_Mesh(0, m_TestMeshes.size() - 1);
    std::uniform_int_distribution<std::uint32_t> l_Lifetimes(0, c_MeshChurnMaxLifetime);
    for (std::uint32_t it_Acquire = 0; it_Acquire < c_MeshChurnPerFrame && m_MeshChurnLoads < c_MeshChurnLoads; ++it_Acquire)
    {
        const Trinity::UUID l_ID = m_TestMeshes[l_Mesh(m_MeshRandom)].ID;
        const std::uint32_t l_Lifetime = l_Lifetimes(m_MeshRandom);
        m_MeshChurnLoads += Trinity::AssetManager::GetState(l_ID) == Trinity::AssetState::None ? 1 : 0;
        if (l_Lifetime == 0)
        {
            const Trinity::AssetRef<Trinity::MeshAsset> l_Released(l_ID);

            continue;
        }

        m_MeshChurn.push_back({ Trinity::AssetRef<Trinity::MeshAsset>(l_ID), frame + l_Lifetime });
    }

    if (m_MeshChurnLoads >= c_MeshChurnLoads)
    {
        m_MeshChurn.clear();
        m_MeshPhase = MeshPhase::Settling;
        m_MeshPhaseFrame = frame;
    }
}

// Lets go of everything the mesh test held, and starts the sprites
void SandboxLayer::FinishMeshes()
{
    m_MeshChurn.clear();
    m_MeshRefs.clear();
    for (const Trinity::RHI::BufferHandle it_Readback : m_MeshReadbacks)
    {
        Trinity::Application::Get().GetDevice().DestroyBuffer(it_Readback);
    }

    m_MeshReadbacks.clear();
    m_MeshPhase = MeshPhase::Done;
    if (m_SpritesReady)
    {
        m_SpritePhase = SpritePhase::Loading;
        m_SpritePhaseFrame = Trinity::Application::Get().GetFrameCount();
    }
}

// 64 placeholder source files give the registry 64 texture assets, a grey one and a model whose sub-assets are the test meshes, and a memory source serves what they cook into at /cache. The meshes run first
void SandboxLayer::CreateTestAssets()
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

    if (!Trinity::FileSystem::WriteText(std::format("{}/{}", c_SpriteTestRoot, c_GreyTextureName), "A grey sRGB texture; the cooked KTX2 is made in memory") || !Trinity::FileSystem::WriteText(std::format("{}/{}", c_SpriteTestRoot, c_ModelName), "A model; its meshes are made in memory"))
    {
        TR_ERROR("Sprites: the source files under {} could not be written", c_SpriteTestRoot);

        return;
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
        const std::vector<std::byte> l_File = MakeSpriteKtx2(l_Texels, c_SpriteTextureSize, it_Index == 0, false);
        const std::string l_Path = Trinity::GetCookedTexturePath(l_Record->ID);
        static_cast<void>(l_Cache->AddFile(std::string_view(l_Path).substr(Trinity::Project::c_CacheMount.size() + 1), l_File));
        m_SpriteTextures.push_back(l_Record->ID);
    }

    const Trinity::AssetRecord* l_GreyRecord = m_SpriteRegistry->FindByPath(std::format("{}/{}", c_SpriteTestRoot, c_GreyTextureName));
    if (l_GreyRecord == nullptr)
    {
        TR_ERROR("Sprites: the registry has no record for the grey texture");

        return;
    }

    std::vector<std::uint8_t> l_GreyTexels(std::size_t{ c_SpriteTextureSize } * c_SpriteTextureSize * 4, c_GreyValue);
    for (std::size_t it_Alpha = 3; it_Alpha < l_GreyTexels.size(); it_Alpha += 4)
    {
        l_GreyTexels[it_Alpha] = 255;
    }

    const std::string l_GreyPath = Trinity::GetCookedTexturePath(l_GreyRecord->ID);
    static_cast<void>(l_Cache->AddFile(std::string_view(l_GreyPath).substr(Trinity::Project::c_CacheMount.size() + 1), MakeSpriteKtx2(l_GreyTexels, c_SpriteTextureSize, true, true)));
    m_GreyTexture = l_GreyRecord->ID;

    if (!AddTestMeshes(*l_Cache))
    {
        m_TestMeshes.clear();
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
    Trinity::CameraComponent& l_ReadbackCamera = l_Scene.CreateEntity("Camera").Add<Trinity::CameraComponent>();
    l_ReadbackCamera.OrthographicSize = static_cast<float>(c_SpriteReadbackSize);
    l_ReadbackCamera.ExposureEV100 = c_SpriteReadbackToneMapping.ExposureEV100;
    l_ReadbackCamera.Tonemap = c_SpriteReadbackToneMapping.Curve;

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

    Trinity::Entity l_Grey = a_Sprite("Grey", { -55.0f, -55.0f }, { 10.0f, 10.0f }, glm::vec4(1.0f), 0, 0);
    l_Grey.Get<Trinity::SpriteRendererComponent>().Texture = m_GreyTexture;

    l_Scene.UpdateWorldTransforms();

    Trinity::AssetManager::SetRegistry(m_SpriteRegistry.get());
    m_SpritesReady = true;
    m_MeshPhase = MeshPhase::Loading;
    m_MeshPhaseFrame = Trinity::Application::Get().GetFrameCount();
    if (m_TestMeshes.empty())
    {
        FinishMeshes();
    }
}

// Waits for every texture to load, reads a frame's readback back, and from then on watches Renderer memory
void SandboxLayer::UpdateSprites()
{
    // The readback passes went into a frame that has been submitted since
    if (m_SpritePhase == SpritePhase::Reading)
    {
        if (m_SpriteReadbackAdded)
        {
            CheckSpriteReadback();
            m_SpritePhase = SpritePhase::Running;
        }

        return;
    }

    if (m_SpritePhase != SpritePhase::Loading)
    {
        return;
    }

    // A texture that became ready this frame is uploaded before the graph runs, so the readback can draw it in this frame
    const std::uint64_t l_Frame = Trinity::Application::Get().GetFrameCount();
    const auto a_Ready = [](Trinity::UUID id) { return Trinity::AssetManager::GetState(id) == Trinity::AssetState::Ready; };
    const bool l_Loaded = std::ranges::all_of(m_SpriteTextures, a_Ready) && a_Ready(m_GreyTexture);
    if (!l_Loaded)
    {
        if (l_Frame - m_SpritePhaseFrame > c_SpriteLoadTimeoutFrames)
        {
            TR_ERROR("Sprites: the {} textures and the grey one were not all loaded after {} frames", c_SpriteTextureCount, c_SpriteLoadTimeoutFrames);
            m_SpritePhase = SpritePhase::Running;
        }

        return;
    }

    TR_INFO("Sprites: {} textures and the grey one loaded in {} frame(s)", c_SpriteTextureCount, l_Frame - m_SpritePhaseFrame);

    Trinity::RHI::BufferDescription l_ReadbackDescription;
    l_ReadbackDescription.Size = Trinity::RHI::GetTextureCopyRowPitch(c_SpriteReadbackFormat, c_SpriteReadbackSize) * c_SpriteReadbackSize;
    l_ReadbackDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_ReadbackDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_ReadbackDescription.DebugName = "Sandbox sprite readback";

    m_SpriteReadback = Trinity::Application::Get().GetDevice().CreateBuffer(l_ReadbackDescription);
    if (!m_SpriteReadback)
    {
        TR_ERROR("Sprites: could not create the readback buffer");
        m_SpritePhase = SpritePhase::Running;

        return;
    }

    m_SpritePhase = SpritePhase::Reading;
}

// As the Renderer draws a frame: the readback scene into a transient linear target cleared to black, the tonemap pass with the readback camera's exposure 1 and no curve into an 8-bit target, then a copy into the readback buffer
void SandboxLayer::AddSpriteReadback(Trinity::FrameGraph& graph)
{
    m_SpriteReadbackAdded = true;

    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const Trinity::RHI::Format l_SceneFormat = l_Renderer.GetSceneFormat();

    Trinity::RHI::TextureDescription l_SceneDescription;
    l_SceneDescription.Width = c_SpriteReadbackSize;
    l_SceneDescription.Height = c_SpriteReadbackSize;
    l_SceneDescription.TextureFormat = l_SceneFormat;
    l_SceneDescription.ClearColor = c_SpriteReadbackClear;

    Trinity::RHI::TextureDescription l_TargetDescription = l_SceneDescription;
    l_TargetDescription.TextureFormat = c_SpriteReadbackFormat;

    const std::uint64_t l_Size = Trinity::RHI::GetTextureCopyRowPitch(c_SpriteReadbackFormat, c_SpriteReadbackSize) * c_SpriteReadbackSize;
    const Trinity::FrameGraphTexture l_Scene = graph.CreateTexture("Sandbox sprite scene", l_SceneDescription);
    const Trinity::FrameGraphTexture l_Target = graph.CreateTexture("Sandbox sprite target", l_TargetDescription);
    const Trinity::FrameGraphBuffer l_Readback = graph.ImportBuffer("Sandbox sprite readback", m_SpriteReadback, l_Size, Trinity::RHI::ResourceState::CopyDestination, Trinity::RHI::ResourceState::CopyDestination);

    graph.AddPass("Sandbox sprites", Trinity::FrameGraphPassType::Raster, [l_Scene](Trinity::FrameGraphPassBuilder& builder) { builder.AddColorAttachment({ l_Scene, Trinity::RHI::LoadOp::Clear, c_SpriteReadbackClear }); }, [this, l_SceneFormat](const Trinity::FrameGraphContext& context)
    {
        Trinity::Renderer2D& l_Renderer2D = Trinity::Application::Get().GetRenderer().GetRenderer2D();
        m_SpriteReadbackDrawn = l_Renderer2D.DrawScene(context.GetCommands(), *m_SpriteReadbackScene, l_SceneFormat, c_SpriteReadbackSize, c_SpriteReadbackSize);
        m_SpriteReadbackStatistics = l_Renderer2D.GetStatistics();
    });

    l_Renderer.AddTonemapPass(graph, l_Scene, l_Target, c_SpriteReadbackToneMapping);

    graph.AddPass("Sandbox sprite copy", Trinity::FrameGraphPassType::Copy, [l_Target, l_Readback](Trinity::FrameGraphPassBuilder& builder)
    {
        builder.Read(l_Target, Trinity::RHI::ResourceState::CopySource);
        builder.Write(l_Readback, Trinity::RHI::ResourceState::CopyDestination);
    }, [l_Target, l_Readback](const Trinity::FrameGraphContext& context)
    {
        context.GetCommands().CopyTextureToBuffer(context.GetTexture(l_Target), 0, 0, context.GetBuffer(l_Readback), 0);
    });
}

// Probes inside each sprite of a small scene, chosen so that a wrong sort, transform, flip, filter or blend each changes at least one of them
void SandboxLayer::CheckSpriteReadback()
{
    TR_PROFILE_FUNCTION();

    struct Probe
    {
        std::string_view Name;
        glm::vec2 Position;
        std::array<std::uint8_t, 3> Expected;
        std::int32_t Tolerance = 2;
    };

    constexpr std::array<Probe, 17> c_Probes
    { {
        { "red alone", { -48.0f, 48.0f }, { 255, 0, 0 } },
        { "green over red, order 1 over 0", { -26.0f, 26.0f }, { 0, 255, 0 } },
        { "blue alone on layer -1", { 5.0f, 15.0f }, { 0, 0, 255 } },
        { "green over blue, layer 0 over -1 despite order 9", { -15.0f, 15.0f }, { 0, 255, 0 } },
        { "half-transparent blue over red, blended in linear light", { -20.0f, 46.0f }, { 187, 0, 188 }, 3 },
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
        { "background", { 55.0f, -55.0f }, { 0, 0, 0 } },
        { "a mid-grey sRGB texture comes back as it went in", { -55.0f, -55.0f }, { c_GreyValue, c_GreyValue, c_GreyValue }, 0 }
    } };

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    l_Device.WaitIdle();

    const std::uint64_t l_RowPitch = Trinity::RHI::GetTextureCopyRowPitch(c_SpriteReadbackFormat, c_SpriteReadbackSize);
    const bool l_Drawn = m_SpriteReadbackDrawn;
    const Trinity::Renderer2D::Statistics l_Statistics = m_SpriteReadbackStatistics;

    // The null device draws nothing, so only a GPU's readback has pixels to check
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();
    const std::span<const std::byte> l_Data = l_Device.GetMappedData(m_SpriteReadback);
    const bool l_Check = l_Info.API != Trinity::GraphicsAPI::None && l_Data.size() == l_RowPitch * c_SpriteReadbackSize;

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

    const std::uint64_t l_Hash = l_Check ? HashRows(l_Data, l_RowPitch, std::uint64_t{ c_SpriteReadbackSize } * 4, c_SpriteReadbackSize) : 0;

    l_Device.DestroyBuffer(m_SpriteReadback);
    m_SpriteReadback = {};

    if (!l_Drawn || l_Statistics.Sprites != 9 || l_Statistics.DrawCalls != (l_Info.API != Trinity::GraphicsAPI::None ? 1u : l_Statistics.DrawCalls))
    {
        TR_ERROR("Sprites: the readback scene drew {} sprite(s) in {} draw call(s){}", l_Statistics.Sprites, l_Statistics.DrawCalls, l_Drawn ? "" : ", without finding its camera");

        return;
    }

    if (!l_Wrong.empty())
    {
        TR_ERROR("Sprites: the readback differs: {}", l_Wrong);

        return;
    }

    TR_INFO("Sprites: {} drew 9 sprites in one draw call{}", Trinity::ToString(l_Info.API), l_Check ? std::format(", and all {} probes for sort order, transforms, flips, filtering, blending in linear light and the sRGB round trip hold the expected colour, with the readback hashing to 0x{:016x}", c_Probes.size(), l_Hash) : "");
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

void SandboxLayer::DestroyTestAssets()
{
    m_MeshChurn.clear();
    m_MeshRefs.clear();
    for (const Trinity::RHI::BufferHandle it_Readback : m_MeshReadbacks)
    {
        Trinity::Application::Get().GetDevice().DestroyBuffer(it_Readback);
    }

    m_MeshReadbacks.clear();
    if (m_SpritePhase == SpritePhase::Running)
    {
        ReportSprites();
    }

    if (m_SpriteRegistry)
    {
        Trinity::AssetManager::SetRegistry(nullptr);
    }

    if (m_SpriteRegistry)
    {
        static_cast<void>(Trinity::FileSystem::Unmount(Trinity::Project::c_CacheMount));
    }

    Trinity::Application::Get().GetDevice().DestroyBuffer(m_SpriteReadback);
    m_SpriteReadback = {};
    m_SpriteScene.reset();
    m_SpriteReadbackScene.reset();
    m_SpriteRegistry.reset();
    m_SpritePhase = SpritePhase::Idle;
}