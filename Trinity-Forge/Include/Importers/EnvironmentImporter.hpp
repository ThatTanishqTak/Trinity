#pragma once

#include <Trinity.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct EnvironmentImportReport
{
    std::size_t Environments = 0;
    std::size_t Filtered = 0;
    std::size_t Cached = 0;
    std::size_t Failed = 0;
    std::size_t Stopped = 0;
};

// An equirectangular .hdr becomes a cooked environment, prefiltered on the CPU exactly as glTF Sample Viewer prefilters on the GPU: the panorama is resampled into a cubemap with box-filtered mips, then importance-sampled into GGX radiance for each roughness, Lambertian irradiance, and the split sum's BRDF lookup table, with the same sizes, sample counts, Hammersley points and mip choices, so the two light a model alike
class EnvironmentImporter
{
public:
    enum class Result : std::uint8_t
    {
        Cached,
        Filtered,
        Failed
    };

    static constexpr std::string_view c_Importer = Trinity::EnvironmentAsset::c_AssetType;
    static constexpr std::uint32_t c_Version = 1;

    // glTF Sample Viewer's: its cubemap size, and the mips it filters, floor(log2(size)) + 1 - 4, for roughness m / (levels - 1). Irradiance is smooth, so it is kept smaller than the 256 the viewer renders, and so is the lookup table, which the viewer renders at 1024
    static constexpr std::uint32_t c_CubeSize = 256;
    static constexpr std::uint32_t c_SpecularLevels = 5;
    static constexpr std::uint32_t c_IrradianceSize = 64;
    static constexpr std::uint32_t c_LookupSize = 512;
    static constexpr std::uint32_t c_SpecularSamples = 1024;
    static constexpr std::uint32_t c_IrradianceSamples = 2048;
    static constexpr std::uint32_t c_LookupSamples = 512;

    // RGB radiance on the six faces of a cube, +X, -X, +Y, -Y, +Z, -Z, each face's rows from the first in memory, as OpenGL, Vulkan and D3D12 all lay a cubemap out, with every box-filtered mip down to 1x1
    struct Cubemap
    {
        std::uint32_t Size = 0;
        std::vector<std::vector<glm::vec3>> Levels;

        // Trilinear, at a level clamped to the mips there are, and bilinear within a face, clamped at its edges
        [[nodiscard]] glm::vec3 Sample(const glm::vec3& direction, float level) const;
    };

    // The direction through the centre of a texel, as a cubemap is sampled
    [[nodiscard]] static glm::vec3 GetTexelDirection(std::uint32_t face, std::uint32_t x, std::uint32_t y, std::uint32_t size);
    // As glTF Sample Viewer's panorama pass resamples it: bilinearly, mirrored at the edges, and with up and down the other way round from the cubemap's own, which its filtering then undoes
    [[nodiscard]] static Cubemap FromPanorama(std::span<const float> rgb, std::uint32_t width, std::uint32_t height, std::uint32_t size);
    // The cooked file the engine's environment loader reads
    [[nodiscard]] static Trinity::Expected<std::vector<std::byte>, std::string> Filter(const Cubemap& radiance);

    [[nodiscard]] static std::string GetCacheKey(std::span<const std::byte> source);
    [[nodiscard]] static std::string GetCacheKeyPath(Trinity::UUID id);
    [[nodiscard]] static bool IsCached(Trinity::UUID id, std::string_view key);
    [[nodiscard]] static Result Import(const Trinity::AssetRecord& record);
};