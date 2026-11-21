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

// The collision shapes cooked for each mesh: none, a convex hull, a collision mesh, or both
enum class ModelCollision : std::uint8_t
{
    None,
    ConvexHulls,
    TriangleMeshes,
    Both
};

// Unit scale is metres in one of the file's units. Z up is turned to Y up as a Z-up tool exports
struct ModelImportSettings
{
    std::optional<float> UnitScale;
    ModelUpAxis UpAxis = ModelUpAxis::Auto;
    ModelCollision Collision = ModelCollision::Both;

    [[nodiscard]] bool operator==(const ModelImportSettings&) const = default;
};

struct ModelImportReport
{
    std::size_t Models = 0;
    std::size_t Imported = 0;
    std::size_t Cached = 0;
    std::size_t Failed = 0;
    std::size_t Stopped = 0;
    // Models whose cooked hierarchy took their file's name, as after a rename
    std::size_t Renamed = 0;
    std::size_t TexturesEncoded = 0;
    std::size_t TexturesCached = 0;
};

// A .gltf, .glb, .fbx, .obj or .dae becomes sub-assets of its file, read by fastgltf or assimp
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

    // How a material samples a texture, which decides its encoding: sRGB colour, linear data, normals
    enum class TextureUsage : std::uint8_t
    {
        Color,
        Data,
        Normal
    };

    static constexpr std::string_view c_Importer = Trinity::c_ModelAssetType;
    static constexpr std::string_view c_MaterialImporter = "Material";
    static constexpr std::uint32_t c_Version = 5;
    // Between a material's key and a field's name in a .meta override, as Material.0.x/RoughnessFactor
    static constexpr char c_OverrideSeparator = '/';

    struct ExternalTexture
    {
        std::string Path;
        TextureUsage Usage = TextureUsage::Color;
    };

    // Read on a worker, then taken up on the main thread, which gives UUIDs and decides whether to cook
    struct Plan
    {
        bool Read = false;
        // In the order they are made, with no UUIDs yet
        std::vector<Trinity::SubAsset> SubAssets;
        std::vector<ExternalTexture> ExternalTextures;
        std::uint64_t ContentHash = 0;
        // What the last cook left: its key, and whether every sub-asset's file and the model's own is there
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
    // The settings with the record's material overrides kept
    [[nodiscard]] static Trinity::AssetSettings MakeSettings(const ModelImportSettings& settings, const Trinity::AssetRecord& record);
    [[nodiscard]] static ModelImportSettings ReadSettings(const Trinity::AssetRecord& record);

    // A model's material as imported, before the .meta's overrides, cooked so an edit needs no import
    [[nodiscard]] static std::string GetImportedMaterialPath(Trinity::UUID id);
    [[nodiscard]] static std::vector<Trinity::MaterialField> ReadMaterialOverrides(const Trinity::AssetRecord& model, std::string_view material);
    [[nodiscard]] static Trinity::MaterialData ApplyMaterialOverrides(const Trinity::AssetRecord& model, std::string_view material, const Trinity::MaterialData& imported);
    // The model's settings with the material's overrides replaced by every field the edit changes
    [[nodiscard]] static Trinity::AssetSettings MakeMaterialOverrides(const Trinity::AssetRecord& model, std::string_view material, const Trinity::MaterialData& imported, const Trinity::MaterialData& edited);
    // For a model whose cache is whole: each cooked material is brought in line with the .meta
    [[nodiscard]] static std::vector<Trinity::UUID> RefreshMaterials(const Trinity::AssetRecord& model);
    // For a model whose cache is whole: the cooked hierarchy takes the file's name. Whether written
    [[nodiscard]] static bool RefreshName(const Trinity::AssetRecord& model);
    // The settings without overrides of materials the model no longer has, or nothing when none
    [[nodiscard]] static std::optional<Trinity::AssetSettings> PruneMaterialOverrides(const Trinity::AssetRecord& model);
    [[nodiscard]] static std::string GetCacheKeyPath(Trinity::UUID id);
    [[nodiscard]] static std::string GetCacheKey(const Trinity::AssetRecord& record, const Plan& plan, std::span<const Trinity::UUID> externalTextures);
    [[nodiscard]] static std::string_view ToString(TextureUsage usage);
    [[nodiscard]] static std::string GetCookedPath(const Trinity::SubAsset& subAsset);
    // A mesh's key with the shape's asset type for its kind, as ConvexHull.2.Crate for Mesh.2.Crate
    [[nodiscard]] static std::string GetCollisionKey(std::string_view meshKey, Trinity::CollisionShapeKind kind);

    [[nodiscard]] static Plan PlanImport(const Trinity::AssetRecord& record);
    [[nodiscard]] static Cooked Cook(const Trinity::AssetRecord& record, const Plan& plan, std::span<const Trinity::UUID> externalTextures, const std::atomic<bool>& stop);
};