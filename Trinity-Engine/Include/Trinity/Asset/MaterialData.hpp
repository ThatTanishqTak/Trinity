#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"

#include <glm/glm.hpp>

#include <array>
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

    // An invalid UUID leaves the slot empty
    struct MaterialTexture
    {
        UUID Texture;
        std::uint32_t TexCoord = 0;

        [[nodiscard]] bool operator==(const MaterialTexture&) const = default;
    };

    // Linear colours. Metallic is read from blue, roughness from green, occlusion from red
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

        // An empty slot's is an invalid UUID. A scene holds them while it holds the material
        [[nodiscard]] std::array<UUID, 5> GetTextures() const
        {
            return { BaseColorTexture.Texture, MetallicRoughnessTexture.Texture, NormalTexture.Texture, OcclusionTexture.Texture, EmissiveTexture.Texture };
        }

        [[nodiscard]] bool operator==(const MaterialData&) const = default;
    };

    // One key of a material file and its value, such as BaseColorFactor and [1, 1, 1, 1]
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

    // Every key in file order, empty texture slots too, so two materials compare key by key
    [[nodiscard]] TRINITY_API std::vector<MaterialField> GetMaterialFields(const MaterialData& material);
    // A name that is not a key, or a value that does not read, refuses them all
    [[nodiscard]] TRINITY_API Expected<MaterialData, std::string> ApplyMaterialFields(MaterialData material, std::span<const MaterialField> fields);

    [[nodiscard]] TRINITY_API std::string_view ToString(MaterialAlphaMode mode);
    [[nodiscard]] TRINITY_API std::optional<MaterialAlphaMode> ParseMaterialAlphaMode(std::string_view text);
}