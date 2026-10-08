#pragma once

#include <Trinity.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class ModelSource;

enum class ModelUpAxis : std::uint8_t
{
    Auto,
    Y,
    Z
};

// What the file says of its units and axes, unless these say otherwise. A unit scale is metres in one of the file's units, and Z up is turned to Y up as a right-handed Z-up tool exports it
struct ModelImportSettings
{
    std::optional<float> UnitScale;
    ModelUpAxis UpAxis = ModelUpAxis::Auto;

    [[nodiscard]] bool operator==(const ModelImportSettings&) const = default;
};

struct ModelImportReport
{
    std::size_t Models = 0;
    std::size_t Imported = 0;
    std::size_t Cached = 0;
    std::size_t Failed = 0;
    std::size_t Stopped = 0;
    std::size_t TexturesEncoded = 0;
    std::size_t TexturesCached = 0;
};

// A .gltf, .glb, .fbx, .obj or .dae becomes sub-assets of its file: a mesh for each mesh, a material for each material, and a texture for each image it embeds and each way a material uses it. An image beside the model is its own texture asset, which the model sets to be encoded as it uses it. The node hierarchy is cooked beside them, in Trinity's metres with Y up. glTF is read with fastgltf and the rest with assimp
class ModelImporter
{
public:
    enum class Result : std::uint8_t
    {
        Cached,
        Imported,
        Failed,
        Stopped
    };

    // How a material samples a texture, which decides how it is encoded: colour as sRGB, data as linear, and normals as a normal map
    enum class TextureUsage : std::uint8_t
    {
        Color,
        Data,
        Normal
    };

    static constexpr std::string_view c_Importer = "Model";
    static constexpr std::string_view c_MaterialImporter = "Material";
    static constexpr std::uint32_t c_Version = 3;

    struct ExternalTexture
    {
        std::string Path;
        TextureUsage Usage = TextureUsage::Color;
    };

    // Read on a worker, then taken up on the main thread, which gives the sub-assets their UUIDs, finds the textures beside the model, and decides whether to cook
    struct Plan
    {
        bool Read = false;
        // In the order they are made, with no UUIDs yet
        std::vector<Trinity::SubAsset> SubAssets;
        std::vector<ExternalTexture> ExternalTextures;
        std::uint64_t ContentHash = 0;
        // What the last cook left: its key, and whether the file of every sub-asset the record lists, and the model's own, is there
        std::string CachedKey;
        bool CookedFilesExist = false;
        std::shared_ptr<const ModelSource> Model;
    };

    struct Cooked
    {
        Result Outcome = Result::Failed;
        // Sub-assets whose files were written, which anything holding them loads again
        std::vector<Trinity::UUID> Written;
        std::size_t TexturesEncoded = 0;
        std::size_t TexturesCached = 0;
    };

    [[nodiscard]] static Trinity::AssetSettings GetDefaultSettings();
    [[nodiscard]] static Trinity::AssetSettings MakeSettings(const ModelImportSettings& settings);
    [[nodiscard]] static ModelImportSettings ReadSettings(const Trinity::AssetRecord& record);
    [[nodiscard]] static std::string GetCacheKeyPath(Trinity::UUID id);
    [[nodiscard]] static std::string GetCacheKey(const Trinity::AssetRecord& record, const Plan& plan, std::span<const Trinity::UUID> externalTextures);
    [[nodiscard]] static std::string_view ToString(TextureUsage usage);
    [[nodiscard]] static std::string GetCookedPath(const Trinity::SubAsset& subAsset);

    [[nodiscard]] static Plan PlanImport(const Trinity::AssetRecord& record);
    [[nodiscard]] static Cooked Cook(const Trinity::AssetRecord& record, const Plan& plan, std::span<const Trinity::UUID> externalTextures, const std::atomic<bool>& stop);
};