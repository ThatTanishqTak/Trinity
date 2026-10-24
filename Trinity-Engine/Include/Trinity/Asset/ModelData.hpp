#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // One node of a model's hierarchy, relative to its parent as a Transform is
    struct ModelNode
    {
        static constexpr std::int32_t c_NoParent = -1;

        std::string Name;
        // An earlier node in the model, or c_NoParent for a root
        std::int32_t Parent = c_NoParent;
        glm::vec3 Translation{ 0.0f };
        glm::quat Rotation = glm::identity<glm::quat>();
        glm::vec3 Scale{ 1.0f };
        // A mesh asset and the material in each of its slots, or no mesh
        UUID Mesh;
        std::vector<UUID> Materials;
        // The node's index in the file it came from, so the hierarchy can be checked against that file
        std::uint32_t SourceNode = 0;

        [[nodiscard]] bool operator==(const ModelNode&) const = default;
    };

    // The node hierarchy an importer found in a model, parents before their children, as an instance of the model has it under a root named for the model. Format 2 added the name, so a format 1 model reads with none
    struct ModelData
    {
        static constexpr std::uint32_t c_FormatVersion = 2;

        std::string Name;
        std::vector<ModelNode> Nodes;

        [[nodiscard]] bool operator==(const ModelData&) const = default;
    };

    // What the asset registry imports a model file as, which loads as a prefab of its hierarchy
    constexpr std::string_view c_ModelAssetType = "Model";

    [[nodiscard]] TRINITY_API std::string GetCookedModelPath(UUID id);

    [[nodiscard]] TRINITY_API std::string WriteModelData(const ModelData& model);
    [[nodiscard]] TRINITY_API Expected<ModelData, std::string> ParseModelData(std::string_view text);
}