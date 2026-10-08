#include "Importers/EnvironmentImporter.hpp"

#include <ktx.h>
#include <stb_image.h>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <numbers>

namespace
{
    // zstd decompresses as fast at any level, and an environment is filtered once, so the level favours size
    constexpr ktx_uint32_t c_ZstdLevel = 18;
    // VK_FORMAT_R16G16B16A16_SFLOAT
    constexpr ktx_uint32_t c_VkFormatRGBA16Float = 97;
    constexpr float c_Pi = std::numbers::pi_v<float>;
    // Rows of texels a worker filters at a time
    constexpr std::size_t c_RowBatch = 4;

    struct KtxDeleter
    {
        void operator()(ktxTexture2* texture) const
        {
            ktxTexture2_Destroy(texture);
        }
    };

    struct MallocDeleter
    {
        void operator()(ktx_uint8_t* data) const
        {
            std::free(data);
        }
    };

    struct StbDeleter
    {
        void operator()(float* pixels) const
        {
            stbi_image_free(pixels);
        }
    };

    // FNV-1a, which is plenty to tell one version of a file from the next
    std::uint64_t HashBytes(std::span<const std::byte> bytes)
    {
        std::uint64_t l_Hash = 14695981039346656037ull;
        for (const std::byte it_Byte : bytes)
        {
            l_Hash ^= std::to_integer<std::uint64_t>(it_Byte);
            l_Hash *= 1099511628211ull;
        }

        return l_Hash;
    }

    // glTF Sample Viewer's uvToXYZ: a face's texel position, from -1 to 1 across, to a point on the unit cube, with y the other way round from the cubemap's own
    glm::vec3 GetScan(std::uint32_t face, float u, float v)
    {
        switch (face)
        {
            case 0:
            {
                return { 1.0f, v, -u };
            }
            case 1:
            {
                return { -1.0f, v, u };
            }
            case 2:
            {
                return { u, -1.0f, v };
            }
            case 3:
            {
                return { u, 1.0f, -v };
            }
            case 4:
            {
                return { u, v, 1.0f };
            }
            default:
            {
                return { -u, v, -1.0f };
            }
        }
    }

    // The bits of i mirrored about the binary point, the second coordinate of the Hammersley points glTF Sample Viewer samples with
    float RadicalInverse(std::uint32_t bits)
    {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);

        return static_cast<float>(bits) * 2.3283064365386963e-10f;
    }

    // A sample around +Z in a frame of its own, and the probability density that chose it
    struct LocalSample
    {
        glm::vec3 Direction{ 0.0f, 0.0f, 1.0f };
        float Density = 0.0f;
    };

    // GGX with alpha the square of perceptual roughness, D as the viewer writes it, and the density of the half vector, D / 4 with the view along the normal
    LocalSample SampleGGX(std::uint32_t index, std::uint32_t count, float roughness)
    {
        const float l_X = static_cast<float>(index) / static_cast<float>(count);
        const float l_Y = RadicalInverse(index);
        const float l_Alpha = roughness * roughness;
        const float l_Cosine = std::clamp(std::sqrt((1.0f - l_Y) / (1.0f + (l_Alpha * l_Alpha - 1.0f) * l_Y)), 0.0f, 1.0f);
        const float l_Sine = std::sqrt(1.0f - l_Cosine * l_Cosine);
        const float l_Phi = 2.0f * c_Pi * l_X;

        const float l_A = l_Cosine * l_Alpha;
        const float l_K = l_Alpha / (1.0f - l_Cosine * l_Cosine + l_A * l_A);
        const float l_Distribution = l_K * l_K / c_Pi;

        return { glm::normalize(glm::vec3(l_Sine * std::cos(l_Phi), l_Sine * std::sin(l_Phi), l_Cosine)), l_Distribution / 4.0f };
    }

    // Cosine-weighted, with density cos / pi
    LocalSample SampleLambertian(std::uint32_t index, std::uint32_t count)
    {
        const float l_X = static_cast<float>(index) / static_cast<float>(count);
        const float l_Y = RadicalInverse(index);
        const float l_Cosine = std::sqrt(1.0f - l_Y);
        const float l_Sine = std::sqrt(l_Y);
        const float l_Phi = 2.0f * c_Pi * l_X;

        return { glm::normalize(glm::vec3(l_Sine * std::cos(l_Phi), l_Sine * std::sin(l_Phi), l_Cosine)), l_Cosine / c_Pi };
    }

    // glTF Sample Viewer's generateTBN: tangent and bitangent about the normal, from +Y, or +Z or -Z when the normal is +Y or -Y
    void GetFrame(const glm::vec3& normal, glm::vec3& tangent, glm::vec3& bitangent)
    {
        glm::vec3 l_Up(0.0f, 1.0f, 0.0f);
        const float l_NormalDotUp = glm::dot(normal, l_Up);
        if (1.0f - std::abs(l_NormalDotUp) <= 0.0000001f)
        {
            l_Up = l_NormalDotUp > 0.0f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 0.0f, -1.0f);
        }

        tangent = glm::normalize(glm::cross(l_Up, normal));
        bitangent = glm::cross(normal, tangent);
    }

    // The mip a sample reads, so its footprint on the cubemap matches the solid angle its density gives it
    float GetSampleLevel(float density, std::uint32_t samples, std::uint32_t size)
    {
        return 0.5f * std::log2(6.0f * static_cast<float>(size) * static_cast<float>(size) / (static_cast<float>(samples) * density));
    }

    float VisibilitySmithGGXCorrelated(float normalDotView, float normalDotLight, float roughness)
    {
        const float l_Alpha2 = std::pow(roughness, 4.0f);
        const float l_View = normalDotLight * std::sqrt(normalDotView * normalDotView * (1.0f - l_Alpha2) + l_Alpha2);
        const float l_Light = normalDotView * std::sqrt(normalDotLight * normalDotLight * (1.0f - l_Alpha2) + l_Alpha2);

        return 0.5f / (l_View + l_Light);
    }

    std::size_t GetFaceTexels(std::uint32_t size)
    {
        return std::size_t{ size } * size;
    }

    // One KTX2 image of RGBA16Float texels, with alpha 1, zstd-supercompressed
    Trinity::Expected<std::vector<std::byte>, std::string> WriteImage(const std::vector<std::vector<glm::vec3>>& levels, std::uint32_t size, std::uint32_t faces)
    {
        ktxTextureCreateInfo l_CreateInfo{};
        l_CreateInfo.vkFormat = c_VkFormatRGBA16Float;
        l_CreateInfo.baseWidth = size;
        l_CreateInfo.baseHeight = size;
        l_CreateInfo.baseDepth = 1;
        l_CreateInfo.numDimensions = 2;
        l_CreateInfo.numLevels = static_cast<ktx_uint32_t>(levels.size());
        l_CreateInfo.numLayers = 1;
        l_CreateInfo.numFaces = faces;
        l_CreateInfo.isArray = KTX_FALSE;
        l_CreateInfo.generateMipmaps = KTX_FALSE;

        ktxTexture2* l_Created = nullptr;
        KTX_error_code l_Result = ktxTexture2_Create(&l_CreateInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &l_Created);
        if (l_Result != KTX_SUCCESS)
        {
            return Trinity::Unexpected{ std::format("a {}x{} KTX2 texture could not be made: {}", size, size, ktxErrorString(l_Result)) };
        }

        const std::unique_ptr<ktxTexture2, KtxDeleter> l_Texture(l_Created);
        std::vector<std::uint16_t> l_Halves;
        for (std::uint32_t it_Level = 0; it_Level < levels.size() && l_Result == KTX_SUCCESS; ++it_Level)
        {
            const std::size_t l_Texels = GetFaceTexels(Trinity::RHI::GetMipSize(size, it_Level));
            for (std::uint32_t it_Face = 0; it_Face < faces && l_Result == KTX_SUCCESS; ++it_Face)
            {
                l_Halves.resize(l_Texels * 4);
                for (std::size_t it_Texel = 0; it_Texel < l_Texels; ++it_Texel)
                {
                    const glm::vec3& l_Value = levels[it_Level][it_Face * l_Texels + it_Texel];
                    l_Halves[it_Texel * 4] = glm::packHalf1x16(l_Value.r);
                    l_Halves[it_Texel * 4 + 1] = glm::packHalf1x16(l_Value.g);
                    l_Halves[it_Texel * 4 + 2] = glm::packHalf1x16(l_Value.b);
                    l_Halves[it_Texel * 4 + 3] = glm::packHalf1x16(1.0f);
                }

                l_Result = ktxTexture_SetImageFromMemory(ktxTexture(l_Texture.get()), it_Level, 0, it_Face, reinterpret_cast<const ktx_uint8_t*>(l_Halves.data()), l_Halves.size() * sizeof(std::uint16_t));
            }
        }

        if (l_Result == KTX_SUCCESS)
        {
            l_Result = ktxTexture2_DeflateZstd(l_Texture.get(), c_ZstdLevel);
        }

        ktx_uint8_t* l_Written = nullptr;
        ktx_size_t l_WrittenSize = 0;
        if (l_Result == KTX_SUCCESS)
        {
            l_Result = ktxTexture2_WriteToMemory(l_Texture.get(), &l_Written, &l_WrittenSize);
        }

        const std::unique_ptr<ktx_uint8_t, MallocDeleter> l_File(l_Written);
        if (l_Result != KTX_SUCCESS)
        {
            return Trinity::Unexpected{ std::format("a {}x{} image could not be written: {}", size, size, ktxErrorString(l_Result)) };
        }

        const std::span<const std::byte> l_Bytes = std::as_bytes(std::span(l_File.get(), l_WrittenSize));

        return std::vector<std::byte>(l_Bytes.begin(), l_Bytes.end());
    }
}

// The face whose axis the direction is most along, and where on it the direction lands, by the table OpenGL, Vulkan and D3D12 share
glm::vec3 EnvironmentImporter::Cubemap::Sample(const glm::vec3& direction, float level) const
{
    const glm::vec3 l_Magnitude = glm::abs(direction);
    std::uint32_t l_Face = 0;
    float l_S = 0.0f;
    float l_T = 0.0f;
    float l_Major = 0.0f;
    if (l_Magnitude.x >= l_Magnitude.y && l_Magnitude.x >= l_Magnitude.z)
    {
        l_Face = direction.x >= 0.0f ? 0 : 1;
        l_S = direction.x >= 0.0f ? -direction.z : direction.z;
        l_T = -direction.y;
        l_Major = l_Magnitude.x;
    }
    else if (l_Magnitude.y >= l_Magnitude.z)
    {
        l_Face = direction.y >= 0.0f ? 2 : 3;
        l_S = direction.x;
        l_T = direction.y >= 0.0f ? direction.z : -direction.z;
        l_Major = l_Magnitude.y;
    }
    else
    {
        l_Face = direction.z >= 0.0f ? 4 : 5;
        l_S = direction.z >= 0.0f ? direction.x : -direction.x;
        l_T = -direction.y;
        l_Major = l_Magnitude.z;
    }

    l_S = (l_S / l_Major + 1.0f) * 0.5f;
    l_T = (l_T / l_Major + 1.0f) * 0.5f;

    const auto a_Bilinear = [&](std::uint32_t mip)
    {
        const std::uint32_t l_Size = Trinity::RHI::GetMipSize(Size, mip);
        const float l_X = std::clamp(l_S * static_cast<float>(l_Size) - 0.5f, 0.0f, static_cast<float>(l_Size - 1));
        const float l_Y = std::clamp(l_T * static_cast<float>(l_Size) - 0.5f, 0.0f, static_cast<float>(l_Size - 1));
        const std::uint32_t l_X0 = static_cast<std::uint32_t>(l_X);
        const std::uint32_t l_Y0 = static_cast<std::uint32_t>(l_Y);
        const std::uint32_t l_X1 = std::min(l_X0 + 1, l_Size - 1);
        const std::uint32_t l_Y1 = std::min(l_Y0 + 1, l_Size - 1);
        const float l_FX = l_X - static_cast<float>(l_X0);
        const float l_FY = l_Y - static_cast<float>(l_Y0);
        const glm::vec3* l_Face0 = Levels[mip].data() + l_Face * GetFaceTexels(l_Size);
        const glm::vec3 l_Top = l_Face0[l_Y0 * l_Size + l_X0] * (1.0f - l_FX) + l_Face0[l_Y0 * l_Size + l_X1] * l_FX;
        const glm::vec3 l_Bottom = l_Face0[l_Y1 * l_Size + l_X0] * (1.0f - l_FX) + l_Face0[l_Y1 * l_Size + l_X1] * l_FX;

        return l_Top * (1.0f - l_FY) + l_Bottom * l_FY;
    };

    const float l_Level = std::clamp(level, 0.0f, static_cast<float>(Levels.size() - 1));
    const std::uint32_t l_Low = static_cast<std::uint32_t>(l_Level);
    const float l_Blend = l_Level - static_cast<float>(l_Low);
    if (l_Blend <= 0.0f || l_Low + 1 >= Levels.size())
    {
        return a_Bilinear(l_Low);
    }

    return a_Bilinear(l_Low) * (1.0f - l_Blend) + a_Bilinear(l_Low + 1) * l_Blend;
}

// The viewer's scan with y turned back, which is the direction OpenGL samples the texel at
glm::vec3 EnvironmentImporter::GetTexelDirection(std::uint32_t face, std::uint32_t x, std::uint32_t y, std::uint32_t size)
{
    const float l_U = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
    const float l_V = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
    glm::vec3 l_Direction = glm::normalize(GetScan(face, l_U, l_V));
    l_Direction.y = -l_Direction.y;

    return l_Direction;
}

EnvironmentImporter::Cubemap EnvironmentImporter::FromPanorama(std::span<const float> rgb, std::uint32_t width, std::uint32_t height, std::uint32_t size)
{
    TR_PROFILE_FUNCTION();

    const auto a_Mirror = [](std::int64_t index, std::int64_t count)
    {
        const std::int64_t l_Period = 2 * count;
        std::int64_t l_Index = index % l_Period;
        l_Index = l_Index < 0 ? l_Index + l_Period : l_Index;

        return l_Index < count ? l_Index : l_Period - 1 - l_Index;
    };

    const auto a_Texel = [&](std::int64_t x, std::int64_t y)
    {
        const std::size_t l_Index = (static_cast<std::size_t>(a_Mirror(y, height)) * width + static_cast<std::size_t>(a_Mirror(x, width))) * 3;

        return glm::vec3(rgb[l_Index], rgb[l_Index + 1], rgb[l_Index + 2]);
    };

    Cubemap l_Cube;
    l_Cube.Size = size;
    const std::uint32_t l_LevelCount = static_cast<std::uint32_t>(std::bit_width(size));
    l_Cube.Levels.resize(l_LevelCount);
    l_Cube.Levels[0].resize(6 * GetFaceTexels(size));
    Trinity::JobSystem::ParallelFor(6 * std::size_t{ size }, [&](std::size_t begin, std::size_t end)
    {
        for (std::size_t it_Row = begin; it_Row < end; ++it_Row)
        {
            const std::uint32_t l_Face = static_cast<std::uint32_t>(it_Row / size);
            const std::uint32_t l_Y = static_cast<std::uint32_t>(it_Row % size);
            for (std::uint32_t it_X = 0; it_X < size; ++it_X)
            {
                const float l_U = (static_cast<float>(it_X) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                const float l_V = (static_cast<float>(l_Y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                const glm::vec3 l_Direction = glm::normalize(GetScan(l_Face, l_U, l_V));
                const float l_PanoramaU = 0.5f + 0.5f * std::atan2(l_Direction.z, l_Direction.x) / c_Pi;
                const float l_PanoramaV = 1.0f - std::acos(std::clamp(l_Direction.y, -1.0f, 1.0f)) / c_Pi;
                const float l_X = l_PanoramaU * static_cast<float>(width) - 0.5f;
                const float l_Y2 = l_PanoramaV * static_cast<float>(height) - 0.5f;
                const std::int64_t l_X0 = static_cast<std::int64_t>(std::floor(l_X));
                const std::int64_t l_Y0 = static_cast<std::int64_t>(std::floor(l_Y2));
                const float l_FX = l_X - static_cast<float>(l_X0);
                const float l_FY = l_Y2 - static_cast<float>(l_Y0);
                const glm::vec3 l_Top = a_Texel(l_X0, l_Y0) * (1.0f - l_FX) + a_Texel(l_X0 + 1, l_Y0) * l_FX;
                const glm::vec3 l_Bottom = a_Texel(l_X0, l_Y0 + 1) * (1.0f - l_FX) + a_Texel(l_X0 + 1, l_Y0 + 1) * l_FX;
                l_Cube.Levels[0][it_Row * size + it_X] = l_Top * (1.0f - l_FY) + l_Bottom * l_FY;
            }
        }
    }, c_RowBatch);

    // Box-filtered, as glGenerateMipmap makes them
    for (std::uint32_t it_Level = 1; it_Level < l_LevelCount; ++it_Level)
    {
        const std::uint32_t l_Size = Trinity::RHI::GetMipSize(size, it_Level);
        const std::uint32_t l_Above = Trinity::RHI::GetMipSize(size, it_Level - 1);
        const std::vector<glm::vec3>& l_Source = l_Cube.Levels[it_Level - 1];
        std::vector<glm::vec3>& l_Level = l_Cube.Levels[it_Level];
        l_Level.resize(6 * GetFaceTexels(l_Size));
        for (std::uint32_t it_Face = 0; it_Face < 6; ++it_Face)
        {
            for (std::uint32_t it_Y = 0; it_Y < l_Size; ++it_Y)
            {
                for (std::uint32_t it_X = 0; it_X < l_Size; ++it_X)
                {
                    const glm::vec3* l_Face = l_Source.data() + it_Face * GetFaceTexels(l_Above);
                    const std::uint32_t l_X0 = std::min(it_X * 2, l_Above - 1);
                    const std::uint32_t l_Y0 = std::min(it_Y * 2, l_Above - 1);
                    const std::uint32_t l_X1 = std::min(it_X * 2 + 1, l_Above - 1);
                    const std::uint32_t l_Y1 = std::min(it_Y * 2 + 1, l_Above - 1);
                    l_Level[it_Face * GetFaceTexels(l_Size) + it_Y * l_Size + it_X] = (l_Face[l_Y0 * l_Above + l_X0] + l_Face[l_Y0 * l_Above + l_X1] + l_Face[l_Y1 * l_Above + l_X0] + l_Face[l_Y1 * l_Above + l_X1]) * 0.25f;
                }
            }
        }
    }

    return l_Cube;
}

// Mip 0 of the specular cubemap is roughness 0, the radiance itself, and every other is filtered from the whole cubemap. The samples, their densities and the mips they read depend only on the roughness, so each level works them out once
Trinity::Expected<std::vector<std::byte>, std::string> EnvironmentImporter::Filter(const Cubemap& radiance)
{
    TR_PROFILE_FUNCTION();

    const std::uint32_t l_Size = radiance.Size;
    const std::uint32_t l_Levels = std::min(c_SpecularLevels, static_cast<std::uint32_t>(radiance.Levels.size()));

    std::vector<std::vector<glm::vec3>> l_Specular(l_Levels);
    l_Specular[0] = radiance.Levels[0];
    for (std::uint32_t it_Level = 1; it_Level < l_Levels; ++it_Level)
    {
        const float l_Roughness = static_cast<float>(it_Level) / static_cast<float>(c_SpecularLevels - 1);
        std::vector<LocalSample> l_Samples(c_SpecularSamples);
        std::vector<float> l_SampleLevels(c_SpecularSamples);
        for (std::uint32_t it_Sample = 0; it_Sample < c_SpecularSamples; ++it_Sample)
        {
            l_Samples[it_Sample] = SampleGGX(it_Sample, c_SpecularSamples, l_Roughness);
            l_SampleLevels[it_Sample] = GetSampleLevel(l_Samples[it_Sample].Density, c_SpecularSamples, l_Size);
        }

        const std::uint32_t l_MipSize = Trinity::RHI::GetMipSize(l_Size, it_Level);
        std::vector<glm::vec3>& l_Output = l_Specular[it_Level];
        l_Output.resize(6 * GetFaceTexels(l_MipSize));
        Trinity::JobSystem::ParallelFor(6 * std::size_t{ l_MipSize }, [&](std::size_t begin, std::size_t end)
        {
            for (std::size_t it_Row = begin; it_Row < end; ++it_Row)
            {
                for (std::uint32_t it_X = 0; it_X < l_MipSize; ++it_X)
                {
                    const glm::vec3 l_Normal = GetTexelDirection(static_cast<std::uint32_t>(it_Row / l_MipSize), it_X, static_cast<std::uint32_t>(it_Row % l_MipSize), l_MipSize);
                    glm::vec3 l_Tangent;
                    glm::vec3 l_Bitangent;
                    GetFrame(l_Normal, l_Tangent, l_Bitangent);

                    glm::vec3 l_Color(0.0f);
                    float l_Weight = 0.0f;
                    for (std::uint32_t it_Sample = 0; it_Sample < c_SpecularSamples; ++it_Sample)
                    {
                        const glm::vec3& l_Local = l_Samples[it_Sample].Direction;
                        const glm::vec3 l_Half = l_Tangent * l_Local.x + l_Bitangent * l_Local.y + l_Normal * l_Local.z;
                        const glm::vec3 l_Light = glm::normalize(2.0f * glm::dot(l_Normal, l_Half) * l_Half - l_Normal);
                        const float l_NormalDotLight = glm::dot(l_Normal, l_Light);
                        if (l_NormalDotLight > 0.0f)
                        {
                            l_Color += radiance.Sample(l_Light, l_SampleLevels[it_Sample]) * l_NormalDotLight;
                            l_Weight += l_NormalDotLight;
                        }
                    }

                    l_Output[it_Row * l_MipSize + it_X] = l_Weight != 0.0f ? l_Color / l_Weight : l_Color / static_cast<float>(c_SpecularSamples);
                }
            }
        }, c_RowBatch);
    }

    std::vector<LocalSample> l_DiffuseSamples(c_IrradianceSamples);
    std::vector<float> l_DiffuseLevels(c_IrradianceSamples);
    for (std::uint32_t it_Sample = 0; it_Sample < c_IrradianceSamples; ++it_Sample)
    {
        l_DiffuseSamples[it_Sample] = SampleLambertian(it_Sample, c_IrradianceSamples);
        l_DiffuseLevels[it_Sample] = GetSampleLevel(l_DiffuseSamples[it_Sample].Density, c_IrradianceSamples, l_Size);
    }

    std::vector<std::vector<glm::vec3>> l_Irradiance(1);
    l_Irradiance[0].resize(6 * GetFaceTexels(c_IrradianceSize));
    Trinity::JobSystem::ParallelFor(6 * std::size_t{ c_IrradianceSize }, [&](std::size_t begin, std::size_t end)
    {
        for (std::size_t it_Row = begin; it_Row < end; ++it_Row)
        {
            for (std::uint32_t it_X = 0; it_X < c_IrradianceSize; ++it_X)
            {
                const glm::vec3 l_Normal = GetTexelDirection(static_cast<std::uint32_t>(it_Row / c_IrradianceSize), it_X, static_cast<std::uint32_t>(it_Row % c_IrradianceSize), c_IrradianceSize);
                glm::vec3 l_Tangent;
                glm::vec3 l_Bitangent;
                GetFrame(l_Normal, l_Tangent, l_Bitangent);

                glm::vec3 l_Color(0.0f);
                for (std::uint32_t it_Sample = 0; it_Sample < c_IrradianceSamples; ++it_Sample)
                {
                    const glm::vec3& l_Local = l_DiffuseSamples[it_Sample].Direction;
                    l_Color += radiance.Sample(l_Tangent * l_Local.x + l_Bitangent * l_Local.y + l_Normal * l_Local.z, l_DiffuseLevels[it_Sample]);
                }

                l_Irradiance[0][it_Row * c_IrradianceSize + it_X] = l_Color / static_cast<float>(c_IrradianceSamples);
            }
        }
    }, c_RowBatch);

    // Columns are the cosine between normal and view, rows the perceptual roughness, each at its texel's centre, holding the scale and bias the split sum applies to the reflectance
    std::vector<std::vector<glm::vec3>> l_Lookup(1);
    l_Lookup[0].resize(GetFaceTexels(c_LookupSize));
    Trinity::JobSystem::ParallelFor(c_LookupSize, [&](std::size_t begin, std::size_t end)
    {
        std::vector<LocalSample> l_Samples(c_LookupSamples);
        for (std::size_t it_Row = begin; it_Row < end; ++it_Row)
        {
            const float l_Roughness = (static_cast<float>(it_Row) + 0.5f) / static_cast<float>(c_LookupSize);
            for (std::uint32_t it_Sample = 0; it_Sample < c_LookupSamples; ++it_Sample)
            {
                l_Samples[it_Sample] = SampleGGX(it_Sample, c_LookupSamples, l_Roughness);
            }

            for (std::uint32_t it_X = 0; it_X < c_LookupSize; ++it_X)
            {
                const float l_NormalDotView = (static_cast<float>(it_X) + 0.5f) / static_cast<float>(c_LookupSize);
                const glm::vec3 l_View(std::sqrt(1.0f - l_NormalDotView * l_NormalDotView), 0.0f, l_NormalDotView);
                float l_Scale = 0.0f;
                float l_Bias = 0.0f;
                for (const LocalSample& it_Sample : l_Samples)
                {
                    const glm::vec3& l_Half = it_Sample.Direction;
                    const glm::vec3 l_Light = glm::normalize(2.0f * glm::dot(l_View, l_Half) * l_Half - l_View);
                    const float l_NormalDotLight = std::clamp(l_Light.z, 0.0f, 1.0f);
                    const float l_NormalDotHalf = std::clamp(l_Half.z, 0.0f, 1.0f);
                    const float l_ViewDotHalf = std::clamp(glm::dot(l_View, l_Half), 0.0f, 1.0f);
                    if (l_NormalDotLight > 0.0f)
                    {
                        const float l_Weighted = VisibilitySmithGGXCorrelated(l_NormalDotView, l_NormalDotLight, l_Roughness) * l_ViewDotHalf * l_NormalDotLight / l_NormalDotHalf;
                        const float l_Fresnel = std::pow(1.0f - l_ViewDotHalf, 5.0f);
                        l_Scale += (1.0f - l_Fresnel) * l_Weighted;
                        l_Bias += l_Fresnel * l_Weighted;
                    }
                }

                l_Lookup[0][it_Row * c_LookupSize + it_X] = glm::vec3(4.0f * l_Scale, 4.0f * l_Bias, 0.0f) / static_cast<float>(c_LookupSamples);
            }
        }
    }, 1);

    std::array<std::vector<std::byte>, static_cast<std::size_t>(Trinity::EnvironmentImage::Count)> l_Images;
    const std::array<std::uint32_t, 3> l_Sizes{ l_Size, c_IrradianceSize, c_LookupSize };
    const std::array<const std::vector<std::vector<glm::vec3>>*, 3> l_Sources{ &l_Specular, &l_Irradiance, &l_Lookup };
    for (std::size_t it_Image = 0; it_Image < l_Images.size(); ++it_Image)
    {
        Trinity::Expected<std::vector<std::byte>, std::string> l_Image = WriteImage(*l_Sources[it_Image], l_Sizes[it_Image], it_Image == static_cast<std::size_t>(Trinity::EnvironmentImage::BrdfLookup) ? 1 : 6);
        if (!l_Image)
        {
            return Trinity::Unexpected{ l_Image.GetError() };
        }

        l_Images[it_Image] = std::move(*l_Image);
    }

    Trinity::EnvironmentFileHeader l_Header;
    std::uint64_t l_Offset = sizeof(l_Header);
    for (std::size_t it_Image = 0; it_Image < l_Images.size(); ++it_Image)
    {
        l_Header.Offsets[it_Image] = l_Offset;
        l_Header.Sizes[it_Image] = l_Images[it_Image].size();
        l_Offset += l_Images[it_Image].size();
    }

    std::vector<std::byte> l_File(l_Offset);
    std::memcpy(l_File.data(), &l_Header, sizeof(l_Header));
    for (std::size_t it_Image = 0; it_Image < l_Images.size(); ++it_Image)
    {
        std::memcpy(l_File.data() + l_Header.Offsets[it_Image], l_Images[it_Image].data(), l_Images[it_Image].size());
    }

    return l_File;
}

// Anything that changes the cooked file changes the key: the source and the importer, whose version goes up with any change to how it filters
std::string EnvironmentImporter::GetCacheKey(std::span<const std::byte> source)
{
    return std::format("content {:016x}, importer {}", HashBytes(source), c_Version);
}

std::string EnvironmentImporter::GetCacheKeyPath(Trinity::UUID id)
{
    return std::format("{}/Environments/{}.key", Trinity::Project::c_CacheMount, id);
}

bool EnvironmentImporter::IsCached(Trinity::UUID id, std::string_view key)
{
    if (!Trinity::FileSystem::Exists(Trinity::GetCookedEnvironmentPath(id)))
    {
        return false;
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_CachedKey = Trinity::FileSystem::ReadText(GetCacheKeyPath(id));

    return l_CachedKey && *l_CachedKey == key;
}

// The key is written after the cooked file, so an import cut short leaves no key and is filtered again
EnvironmentImporter::Result EnvironmentImporter::Import(const Trinity::AssetRecord& record)
{
    TR_PROFILE_FUNCTION();

    const auto l_Start = std::chrono::steady_clock::now();
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Source = Trinity::FileSystem::ReadFile(record.Path);
    if (!l_Source)
    {
        TR_ERROR("Environments: {} could not be read: {}", record.Path, Trinity::ToString(l_Source.GetError()));

        return Result::Failed;
    }

    const std::string l_Key = GetCacheKey(*l_Source);
    if (IsCached(record.ID, l_Key))
    {
        return Result::Cached;
    }

    int l_Width = 0;
    int l_Height = 0;
    int l_Channels = 0;
    const std::unique_ptr<float, StbDeleter> l_Pixels(stbi_loadf_from_memory(reinterpret_cast<const stbi_uc*>(l_Source->data()), static_cast<int>(l_Source->size()), &l_Width, &l_Height, &l_Channels, 3));
    if (!l_Pixels || !stbi_is_hdr_from_memory(reinterpret_cast<const stbi_uc*>(l_Source->data()), static_cast<int>(l_Source->size())))
    {
        TR_ERROR("Environments: {} is not a Radiance HDR image stb_image can read: {}", record.Path, l_Pixels ? "it holds 8-bit colour" : stbi_failure_reason());

        return Result::Failed;
    }

    const std::size_t l_Floats = std::size_t{ static_cast<std::uint32_t>(l_Width) } * static_cast<std::uint32_t>(l_Height) * 3;
    const Cubemap l_Cube = FromPanorama(std::span<const float>(l_Pixels.get(), l_Floats), static_cast<std::uint32_t>(l_Width), static_cast<std::uint32_t>(l_Height), c_CubeSize);
    const Trinity::Expected<std::vector<std::byte>, std::string> l_File = Filter(l_Cube);
    if (!l_File)
    {
        TR_ERROR("Environments: {} was not imported, since {}", record.Path, l_File.GetError());

        return Result::Failed;
    }

    const std::string l_CookedPath = Trinity::GetCookedEnvironmentPath(record.ID);
    const std::string l_KeyPath = GetCacheKeyPath(record.ID);
    const Trinity::Expected<void, Trinity::FileError> l_Saved = Trinity::FileSystem::WriteFile(l_CookedPath, *l_File);
    const Trinity::Expected<void, Trinity::FileError> l_KeySaved = l_Saved ? Trinity::FileSystem::WriteText(l_KeyPath, l_Key) : l_Saved;
    if (!l_KeySaved)
    {
        TR_ERROR("Environments: {} could not be written to {}: {}", record.Path, l_Saved ? l_KeyPath : l_CookedPath, Trinity::ToString(l_KeySaved.GetError()));

        return Result::Failed;
    }

    const double l_Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - l_Start).count();
    TR_INFO("Environments: {} ({}x{}) filtered into a {} cubemap with {} specular mips in {:.1f} s, {} KiB", record.Path, l_Width, l_Height, c_CubeSize, c_SpecularLevels, l_Seconds, l_File->size() / 1024);

    return Result::Filtered;
}