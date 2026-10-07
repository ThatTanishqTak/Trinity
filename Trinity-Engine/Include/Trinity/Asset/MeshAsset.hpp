#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/RHI/Types.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    class MeshLoader;

    [[nodiscard]] TRINITY_API std::string GetCookedMeshPath(UUID id);

    // The vertex streams beside the positions, which every mesh has. A stream holds one entry for every vertex or is absent
    enum class MeshStreams : std::uint32_t
    {
        None = 0,
        NormalTangent = 1u << 0,
        TexCoord0 = 1u << 1,
        TexCoord1 = 1u << 2,
        Color = 1u << 3
    };

    [[nodiscard]] constexpr MeshStreams operator|(MeshStreams left, MeshStreams right)
    {
        return static_cast<MeshStreams>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(MeshStreams flags, MeshStreams flag)
    {
        return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
    }

    struct MeshBounds
    {
        glm::vec3 Min{ 0.0f };
        glm::vec3 Max{ 0.0f };

        [[nodiscard]] bool operator==(const MeshBounds&) const = default;
    };

    // A run of triangles drawn with one material, the one in the MeshRenderer's slot. Its bounds cover the vertices its indices use
    struct Submesh
    {
        std::uint32_t FirstIndex = 0;
        std::uint32_t IndexCount = 0;
        std::uint32_t MaterialSlot = 0;
        MeshBounds Bounds;

        [[nodiscard]] bool operator==(const Submesh&) const = default;
    };

    // Where each stream starts in a mesh's GPU buffer, in bytes, each on a 16-byte boundary in the order below. Positions are three floats, a normal and tangent are both octahedral pairs of 16-bit SNORMs with the tangent's handedness in the lowest bit of its last value, UVs are two floats and a colour is linear RGBA as 16-bit UNORMs
    struct MeshLayout
    {
        static constexpr std::uint64_t c_Absent = UINT64_MAX;

        std::uint32_t VertexCount = 0;
        std::uint32_t IndexCount = 0;
        RHI::IndexFormat IndexFormat = RHI::IndexFormat::UInt16;
        MeshStreams Streams = MeshStreams::None;
        std::uint64_t Positions = 0;
        std::uint64_t NormalTangents = c_Absent;
        std::uint64_t TexCoords0 = c_Absent;
        std::uint64_t TexCoords1 = c_Absent;
        std::uint64_t Colors = c_Absent;
        std::uint64_t Indices = 0;
        std::uint64_t Size = 0;

        [[nodiscard]] bool operator==(const MeshLayout&) const = default;
    };

    // 16-bit indices whenever every vertex can be named by one
    [[nodiscard]] TRINITY_API MeshLayout GetMeshLayout(std::uint32_t vertexCount, std::uint32_t indexCount, MeshStreams streams);

    // A mesh as an importer builds it, in triangles. Every stream but the positions may be empty, and one that is not has an entry for every vertex
    struct MeshData
    {
        std::vector<glm::vec3> Positions;
        std::vector<glm::vec3> Normals;
        // The direction of increasing U, with the bitangent's handedness in w as +1 or -1. Normals without tangents get one perpendicular to each normal
        std::vector<glm::vec4> Tangents;
        std::vector<glm::vec2> TexCoords0;
        std::vector<glm::vec2> TexCoords1;
        std::vector<glm::vec4> Colors;
        std::vector<std::uint32_t> Indices;
        // Their bounds are worked out when cooking. None at all is one submesh over every index, in material slot 0
        std::vector<Submesh> Submeshes;
    };

    // What the cooked file holds: the layout, the bounds, the submeshes, and the GPU buffer's bytes, which still belong to the file
    struct MeshFile
    {
        MeshLayout Layout;
        MeshBounds Bounds;
        std::vector<Submesh> Submeshes;
        std::span<const std::byte> Data;
    };

    // Refuses a stream of the wrong length, an index past the last vertex, a submesh past the last index, and anything that is not whole triangles
    [[nodiscard]] TRINITY_API Expected<std::vector<std::byte>, std::string> CookMesh(const MeshData& mesh);

    // Checks every size, offset and index of a cooked mesh without copying its data
    [[nodiscard]] TRINITY_API Expected<MeshFile, std::string> ReadMeshFile(std::span<const std::byte> file);

    // A unit direction folded onto an octahedron and stored in two 16-bit SNORMs, as the vertex shaders unfold it
    [[nodiscard]] TRINITY_API std::array<std::int16_t, 2> EncodeOctahedral(glm::vec3 direction);
    [[nodiscard]] TRINITY_API glm::vec3 DecodeOctahedral(std::array<std::int16_t, 2> encoded);

    class TRINITY_API MeshAsset final : public Asset
    {
    public:
        static constexpr std::string_view c_AssetType = "Mesh";

        MeshAsset() = default;
        ~MeshAsset() override;

        MeshAsset(const MeshAsset&) = delete;
        MeshAsset& operator=(const MeshAsset&) = delete;

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }

        // Read in shaders as a ByteAddressBuffer at the layout's offsets, and bound as the index buffer at its Indices offset
        [[nodiscard]] RHI::BufferHandle GetBuffer() const { return m_Buffer; }
        [[nodiscard]] std::uint32_t GetShaderResourceIndex() const { return m_ShaderResourceIndex; }
        [[nodiscard]] const MeshLayout& GetLayout() const { return m_Layout; }
        [[nodiscard]] const MeshBounds& GetBounds() const { return m_Bounds; }
        [[nodiscard]] std::span<const Submesh> GetSubmeshes() const { return m_Submeshes; }

    private:
        friend class MeshLoader;

        const MeshLoader* m_Loader = nullptr;
        RHI::BufferHandle m_Buffer;
        std::uint32_t m_ShaderResourceIndex = RHI::c_NoBindlessIndex;
        MeshLayout m_Layout;
        MeshBounds m_Bounds;
        std::vector<Submesh, TaggedAllocator<Submesh, MemoryTag::Assets>> m_Submeshes;

        // Kept only until the renderer uploads it
        std::vector<std::byte, TaggedAllocator<std::byte, MemoryTag::Assets>> m_Data;
    };
}