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

// An equirectangular .hdr, prefiltered on the CPU exactly as glTF Sample Viewer does on the GPU
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

    // glTF Sample Viewer's cubemap size and mips. Irradiance and the lookup table are kept smaller
    static constexpr std::uint32_t c_CubeSize = 256;
    static constexpr std::uint32_t c_SpecularLevels = 5;
    static constexpr std::uint32_t c_IrradianceSize = 64;
    static constexpr std::uint32_t c_LookupSize = 512;
    static constexpr std::uint32_t c_SpecularSamples = 1024;
    static constexpr std::uint32_t c_IrradianceSamples = 2048;
    static constexpr std::uint32_t c_LookupSamples = 512;

    // RGB radiance on six faces, +X, -X, +Y, -Y, +Z, -Z, rows from the first in memory, with every mip
    struct Cubemap
    {
        std::uint32_t Size = 0;
        std::vector<std::vector<glm::vec3>> Levels;

        // Trilinear, at a level clamped to the mips there are, and bilinear within a face, clamped at edges
        [[nodiscard]] glm::vec3 Sample(const glm::vec3& direction, float level) const;
    };

    // The direction through the centre of a texel, as a cubemap is sampled
    [[nodiscard]] static glm::vec3 GetTexelDirection(std::uint32_t face, std::uint32_t x, std::uint32_t y, std::uint32_t size);
    // As glTF Sample Viewer's panorama pass: bilinear, mirrored at the edges, with up and down swapped
    [[nodiscard]] static Cubemap FromPanorama(std::span<const float> rgb, std::uint32_t width, std::uint32_t height, std::uint32_t size);
    // The cooked file the engine's environment loader reads
    [[nodiscard]] static Trinity::Expected<std::vector<std::byte>, std::string> Filter(const Cubemap& radiance);

    [[nodiscard]] static std::string GetCacheKey(std::span<const std::byte> source);
    [[nodiscard]] static std::string GetCacheKeyPath(Trinity::UUID id);
    [[nodiscard]] static bool IsCached(Trinity::UUID id, std::string_view key);
    [[nodiscard]] static Result Import(const Trinity::AssetRecord& record);
};