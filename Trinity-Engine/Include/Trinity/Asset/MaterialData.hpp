#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    enum class MaterialAlphaMode : std::uint8_t
    {
        Opaque,
        Mask,
        Blend
    };

    // A texture asset's UUID and the UV set it is sampled with. An invalid UUID leaves the slot empty
    struct MaterialTexture
    {
        UUID Texture;
        std::uint32_t TexCoord = 0;

        [[nodiscard]] bool operator==(const MaterialTexture&) const = default;
    };

    // glTF's metallic-roughness material as importers cook it, with linear colours. Metallic is read from blue and roughness from green, occlusion from red, and the normal map's X and Y from red and green
    struct MaterialData
    {
        static constexpr std::uint32_t c_FormatVersion = 1;

        glm::vec4 BaseColorFactor{ 1.0f };
        float MetallicFactor = 1.0f;
        float RoughnessFactor = 1.0f;
        glm::vec3 EmissiveFactor{ 0.0f };
        float EmissiveStrength = 1.0f;
        float NormalScale = 1.0f;
        float OcclusionStrength = 1.0f;
        MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
        float AlphaCutoff = 0.5f;
        bool DoubleSided = false;

        MaterialTexture BaseColorTexture;
        MaterialTexture MetallicRoughnessTexture;
        MaterialTexture NormalTexture;
        MaterialTexture OcclusionTexture;
        MaterialTexture EmissiveTexture;

        [[nodiscard]] bool operator==(const MaterialData&) const = default;
    };

    // One key of a material file and its value as the file writes it, such as BaseColorFactor and [1, 1, 1, 1]
    struct MaterialField
    {
        std::string Name;
        std::string Value;

        [[nodiscard]] bool operator==(const MaterialField&) const = default;
    };

    [[nodiscard]] TRINITY_API std::string GetCookedMaterialPath(UUID id);

    // YAML, with an empty slot left out
    [[nodiscard]] TRINITY_API std::string WriteMaterialData(const MaterialData& material);
    [[nodiscard]] TRINITY_API Expected<MaterialData, std::string> ParseMaterialData(std::string_view text);

    // Every key of the material in file order, an empty texture slot's too as an invalid UUID, so two materials compare key by key
    [[nodiscard]] TRINITY_API std::vector<MaterialField> GetMaterialFields(const MaterialData& material);
    // The material with the fields set as a file would set them. A name that is not a key, or a value that does not read, refuses them all
    [[nodiscard]] TRINITY_API Expected<MaterialData, std::string> ApplyMaterialFields(MaterialData material, std::span<const MaterialField> fields);

    [[nodiscard]] TRINITY_API std::string_view ToString(MaterialAlphaMode mode);
    [[nodiscard]] TRINITY_API std::optional<MaterialAlphaMode> ParseMaterialAlphaMode(std::string_view text);
}