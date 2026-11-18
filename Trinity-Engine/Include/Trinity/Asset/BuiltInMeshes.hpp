#pragma once

#include "Trinity/Asset/MeshAsset.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // Each fits the default collider of the same shape, as one submesh in material slot 0
    enum class BuiltInMesh : std::uint8_t
    {
        Cube,
        Sphere,
        Capsule,
        Cylinder,
        Plane,
        Quad
    };

    inline constexpr std::array<BuiltInMesh, 6> c_BuiltInMeshes{ BuiltInMesh::Cube, BuiltInMesh::Sphere, BuiltInMesh::Capsule, BuiltInMesh::Cylinder, BuiltInMesh::Plane, BuiltInMesh::Quad };

    // The UUID scenes name it by, the same in every project and never changed
    [[nodiscard]] TRINITY_API UUID GetBuiltInMeshID(BuiltInMesh mesh);
    [[nodiscard]] TRINITY_API std::optional<BuiltInMesh> FindBuiltInMesh(UUID id);
    [[nodiscard]] TRINITY_API std::string_view ToString(BuiltInMesh mesh);

    [[nodiscard]] TRINITY_API MeshData MakeBuiltInMesh(BuiltInMesh mesh);

    // What the asset manager makes a built-in mesh from in place of a file
    [[nodiscard]] TRINITY_API Expected<std::vector<std::byte>, std::string> CookBuiltInMesh(UUID id);
}