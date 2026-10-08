#pragma once

#include "Importers/ModelImporter.hpp"
#include "Importers/TextureImporter.hpp"

#include <Trinity.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// What a model file gives the importer, whatever its format. A reader fills it while planning, on a worker, and the cook builds each mesh, material and the hierarchy from it, so nothing the cache already holds is built
class ModelSource
{
public:
    // A material's use of a texture: the sub-asset it became, or the texture beside the model it names
    struct TextureUse
    {
        std::string Key;
        std::size_t External = 0;
    };

    struct EmbeddedTexture
    {
        std::string Key;
        std::span<const std::byte> Bytes;
        TextureImportSettings Settings;
    };

    // One mesh asset with a submesh for each part of the file's mesh, and the material each submesh's slot holds
    struct BuiltMesh
    {
        Trinity::MeshData Data;
        std::vector<std::optional<std::size_t>> SlotMaterials;
    };

    virtual ~ModelSource() = default;

    [[nodiscard]] virtual Trinity::Expected<BuiltMesh, std::string> BuildMesh(std::size_t mesh) const = 0;
    [[nodiscard]] virtual Trinity::MaterialData BuildMaterial(std::size_t material, const std::function<Trinity::UUID(const TextureUse&)>& resolve) const = 0;
    // Each root first goes through the conversion to Trinity's metres and Y up
    [[nodiscard]] virtual Trinity::ModelData BuildHierarchy(const glm::mat4& conversion, std::span<const Trinity::UUID> meshes, std::span<const std::vector<Trinity::UUID>> meshMaterials) const = 0;

    // By mesh, the sub-asset key, or none for a mesh with nothing to import. By material the same
    std::vector<std::string> MeshKeys;
    std::vector<std::string> MaterialKeys;
    std::vector<EmbeddedTexture> EmbeddedTextures;
    // What the file says of itself: metres in one of its units, and the turn that takes its axes to Y up
    float UnitScale = 1.0f;
    glm::mat3 Axes{ 1.0f };
};

// What every reader shares: keys, paths, the finishing of vertex data and the merging of a mesh's parts
namespace ModelReading
{
    constexpr std::uint64_t c_HashSeed = 14695981039346656037ull;

    // One part of a mesh as a reader finds it, in whole triangles
    struct Primitive
    {
        std::vector<glm::vec3> Positions;
        std::vector<glm::vec3> Normals;
        std::vector<glm::vec4> Tangents;
        std::vector<glm::vec2> TexCoords0;
        std::vector<glm::vec2> TexCoords1;
        std::vector<glm::vec4> Colors;
        std::vector<std::uint32_t> Indices;
        std::optional<std::size_t> Material;
        // The UV set the normal map is read with, which tangents are made from
        std::size_t NormalTexCoord = 0;
    };

    [[nodiscard]] std::uint64_t Hash(std::span<const std::byte> bytes, std::uint64_t hash = c_HashSeed);
    [[nodiscard]] std::uint64_t Hash(std::string_view text, std::uint64_t hash);
    [[nodiscard]] std::string MakeKey(std::string_view kind, std::size_t index, std::string_view usage, std::string_view name);
    [[nodiscard]] std::optional<std::string> ResolvePath(std::string_view modelPath, std::string_view uri);
    [[nodiscard]] std::optional<std::string> NormalizePath(std::string_view path);
    [[nodiscard]] std::size_t AddExternalTexture(ModelImporter::Plan& plan, const std::string& modelPath, const std::string& texturePath, ModelImporter::TextureUsage usage);

    void FinishPrimitive(Primitive& primitive);
    [[nodiscard]] Trinity::Expected<ModelSource::BuiltMesh, std::string> MergePrimitives(std::vector<Primitive> primitives);
    void SetTransform(Trinity::ModelNode& node, const glm::mat4& matrix);

    // Each reads the file and everything it names, sets the plan's content hash and external textures, and gives back what the cook builds from, or nothing when the file cannot be imported
    [[nodiscard]] std::shared_ptr<ModelSource> ReadGltf(const Trinity::AssetRecord& record, ModelImporter::Plan& plan);
    [[nodiscard]] std::shared_ptr<ModelSource> ReadWithAssimp(const Trinity::AssetRecord& record, ModelImporter::Plan& plan);
}