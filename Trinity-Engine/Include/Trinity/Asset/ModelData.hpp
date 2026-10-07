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

        [[nodiscard]] bool operator==(const ModelNode&) const = default;
    };

    // The node hierarchy an importer found in a model, parents before their children, as dropping the model creates it
    struct ModelData
    {
        static constexpr std::uint32_t c_FormatVersion = 1;

        std::vector<ModelNode> Nodes;

        [[nodiscard]] bool operator==(const ModelData&) const = default;
    };

    [[nodiscard]] TRINITY_API std::string GetCookedModelPath(UUID id);

    [[nodiscard]] TRINITY_API std::string WriteModelData(const ModelData& model);
    [[nodiscard]] TRINITY_API Expected<ModelData, std::string> ParseModelData(std::string_view text);
}