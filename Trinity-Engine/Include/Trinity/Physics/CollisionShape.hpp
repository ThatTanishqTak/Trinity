#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Asset/MeshAsset.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace JPH
{
    class Shape;
}

namespace Trinity
{
    // A model's mesh as a physics shape, cooked at import: the convex hull around its vertices, which any body can have, or its triangles, which only a static body can
    enum class CollisionShapeKind : std::uint8_t
    {
        ConvexHull,
        Mesh
    };

    [[nodiscard]] TRINITY_API std::string GetCookedCollisionShapePath(UUID id);
    // The cooked format's version with Jolt's own in it, since a shape is kept as Jolt saves it, so an importer that keys its cache on it cooks again for a new Jolt
    [[nodiscard]] TRINITY_API std::uint32_t GetCollisionShapeVersion();

    // A shape that could not be made, such as the hull of points all in a line, still gives a file, which holds the reason and fails to load with it
    struct CookedCollisionShape
    {
        std::vector<std::byte> File;
        std::string Error;
    };

    // From the mesh's positions and, for a triangle mesh, its indices, with vertices at the same position welded so neighbouring triangles share their edges. Refuses an index past the last vertex and anything that is not whole triangles
    [[nodiscard]] TRINITY_API CookedCollisionShape CookCollisionShape(CollisionShapeKind kind, const MeshData& mesh);

    // A cooked shape, in its mesh's own space, so it fits an entity drawing that mesh. Jolt's state is held under Physics and the rest under Assets
    class TRINITY_API CollisionShapeAsset : public Asset
    {
    public:
        // A shape with more edges than this keeps none for drawing, and draws as its bounds
        static constexpr std::uint32_t c_MaxWireEdges = 1u << 16;

        ~CollisionShapeAsset() override;

        CollisionShapeAsset(const CollisionShapeAsset&) = delete;
        CollisionShapeAsset& operator=(const CollisionShapeAsset&) = delete;

        [[nodiscard]] CollisionShapeKind GetKind() const { return m_Kind; }
        [[nodiscard]] const MeshBounds& GetBounds() const { return m_Bounds; }
        // Each edge as its two ends, a hull's around its faces and a mesh's around its triangles. Empty in Distribution, and for a shape with more than c_MaxWireEdges edges
        [[nodiscard]] std::span<const glm::vec3> GetWireframe() const { return m_Wireframe; }
        // For the physics sources, which alone see Jolt's types
        [[nodiscard]] const JPH::Shape* GetShape() const { return m_Shape; }

    protected:
        explicit CollisionShapeAsset(CollisionShapeKind kind) : m_Kind(kind)
        {

        }

    private:
        friend class CollisionShapeLoader;

        CollisionShapeKind m_Kind;
        // A reference of its own, and one on Jolt's types, both let go with the asset
        const JPH::Shape* m_Shape = nullptr;
        MeshBounds m_Bounds;
        std::vector<glm::vec3, TaggedAllocator<glm::vec3, MemoryTag::Assets>> m_Wireframe;
    };

    class TRINITY_API ConvexHullAsset final : public CollisionShapeAsset
    {
    public:
        static constexpr std::string_view c_AssetType = "ConvexHull";

        ConvexHullAsset() : CollisionShapeAsset(CollisionShapeKind::ConvexHull)
        {

        }

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }
    };

    class TRINITY_API CollisionMeshAsset final : public CollisionShapeAsset
    {
    public:
        static constexpr std::string_view c_AssetType = "CollisionMesh";

        CollisionMeshAsset() : CollisionShapeAsset(CollisionShapeKind::Mesh)
        {

        }

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }
    };

    [[nodiscard]] constexpr std::string_view GetCollisionShapeAssetType(CollisionShapeKind kind)
    {
        return kind == CollisionShapeKind::ConvexHull ? ConvexHullAsset::c_AssetType : CollisionMeshAsset::c_AssetType;
    }
}