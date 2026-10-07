#include "Trinity/Asset/MeshAsset.hpp"

#include "Trinity/Asset/MeshLoader.hpp"
#include "Trinity/Project/Project.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>

namespace Trinity
{
    namespace
    {
        // A 64-byte header, the submesh table, then from the next 16-byte boundary the GPU buffer's bytes, all little-endian
        constexpr std::array<char, 4> c_Magic{ 'T', 'R', 'M', 'H' };
        constexpr std::uint32_t c_Version = 1;
        constexpr std::uint64_t c_HeaderSize = 64;
        constexpr std::uint64_t c_SubmeshSize = 40;
        constexpr std::uint64_t c_StreamAlignment = 16;
        constexpr std::uint32_t c_KnownStreams = static_cast<std::uint32_t>(MeshStreams::NormalTangent | MeshStreams::TexCoord0 | MeshStreams::TexCoord1 | MeshStreams::Color);

        static_assert(sizeof(glm::vec3) == 12 && sizeof(glm::vec2) == 8, "Positions and UVs are copied as they lie in memory");

        std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        template<typename T>
        void Append(std::vector<std::byte>& bytes, const T& value)
        {
            const std::size_t l_Offset = bytes.size();
            bytes.resize(l_Offset + sizeof(T));
            std::memcpy(bytes.data() + l_Offset, &value, sizeof(T));
        }

        template<typename T>
        void Store(std::span<std::byte> bytes, std::uint64_t offset, const T& value)
        {
            std::memcpy(bytes.data() + offset, &value, sizeof(T));
        }

        template<typename T>
        T Load(std::span<const std::byte> bytes, std::uint64_t offset)
        {
            T l_Value{};
            std::memcpy(&l_Value, bytes.data() + offset, sizeof(T));

            return l_Value;
        }

        void AppendBounds(std::vector<std::byte>& bytes, const MeshBounds& bounds)
        {
            for (const glm::vec3& it_Corner : { bounds.Min, bounds.Max })
            {
                Append(bytes, it_Corner.x);
                Append(bytes, it_Corner.y);
                Append(bytes, it_Corner.z);
            }
        }

        MeshBounds LoadBounds(std::span<const std::byte> bytes, std::uint64_t offset)
        {
            MeshBounds l_Bounds;
            for (glm::length_t it_Axis = 0; it_Axis < 3; ++it_Axis)
            {
                l_Bounds.Min[it_Axis] = Load<float>(bytes, offset + static_cast<std::uint64_t>(it_Axis) * 4);
                l_Bounds.Max[it_Axis] = Load<float>(bytes, offset + 12 + static_cast<std::uint64_t>(it_Axis) * 4);
            }

            return l_Bounds;
        }

        // A unit vector perpendicular to the normal, for a mesh with normals and no tangents
        glm::vec3 GetPerpendicular(glm::vec3 normal)
        {
            const glm::vec3 l_Axis = std::abs(normal.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 l_Perpendicular = glm::cross(l_Axis, normal);
            const float l_Length = glm::length(l_Perpendicular);

            return l_Length > 0.0f ? l_Perpendicular / l_Length : glm::vec3(1.0f, 0.0f, 0.0f);
        }

        std::uint16_t ToUnorm16(float value)
        {
            return static_cast<std::uint16_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 65535.0f));
        }
    }

    std::string GetCookedMeshPath(UUID id)
    {
        return std::format("{}/Meshes/{}.trmesh", Project::c_CacheMount, id);
    }

    MeshLayout GetMeshLayout(std::uint32_t vertexCount, std::uint32_t indexCount, MeshStreams streams)
    {
        MeshLayout l_Layout;
        l_Layout.VertexCount = vertexCount;
        l_Layout.IndexCount = indexCount;
        l_Layout.IndexFormat = vertexCount <= 65536 ? RHI::IndexFormat::UInt16 : RHI::IndexFormat::UInt32;
        l_Layout.Streams = streams;

        std::uint64_t l_Offset = 0;
        const auto a_Place = [&l_Offset](std::uint64_t size)
        {
            const std::uint64_t l_Start = l_Offset;
            l_Offset = AlignUp(l_Offset + size, c_StreamAlignment);

            return l_Start;
        };

        const std::uint64_t l_Vertices = vertexCount;
        l_Layout.Positions = a_Place(l_Vertices * 12);
        l_Layout.NormalTangents = HasFlag(streams, MeshStreams::NormalTangent) ? a_Place(l_Vertices * 8) : MeshLayout::c_Absent;
        l_Layout.TexCoords0 = HasFlag(streams, MeshStreams::TexCoord0) ? a_Place(l_Vertices * 8) : MeshLayout::c_Absent;
        l_Layout.TexCoords1 = HasFlag(streams, MeshStreams::TexCoord1) ? a_Place(l_Vertices * 8) : MeshLayout::c_Absent;
        l_Layout.Colors = HasFlag(streams, MeshStreams::Color) ? a_Place(l_Vertices * 8) : MeshLayout::c_Absent;
        l_Layout.Indices = a_Place(std::uint64_t{ indexCount } * RHI::GetIndexSize(l_Layout.IndexFormat));
        l_Layout.Size = l_Offset;

        return l_Layout;
    }

    Expected<std::vector<std::byte>, std::string> CookMesh(const MeshData& mesh)
    {
        const std::size_t l_VertexCount = mesh.Positions.size();
        if (l_VertexCount == 0 || mesh.Indices.empty() || mesh.Indices.size() % 3 != 0 || l_VertexCount > UINT32_MAX || mesh.Indices.size() > UINT32_MAX)
        {
            return Unexpected{ std::format("{} vertices and {} indices are not a mesh of whole triangles", l_VertexCount, mesh.Indices.size()) };
        }

        const auto a_Fits = [l_VertexCount](std::size_t size) { return size == 0 || size == l_VertexCount; };
        if (!a_Fits(mesh.Normals.size()) || !a_Fits(mesh.Tangents.size()) || !a_Fits(mesh.TexCoords0.size()) || !a_Fits(mesh.TexCoords1.size()) || !a_Fits(mesh.Colors.size()) || (!mesh.Tangents.empty() && mesh.Normals.empty()))
        {
            return Unexpected{ std::format("a stream is neither empty nor one entry for each of the {} vertices, or there are tangents without normals", l_VertexCount) };
        }

        if (const auto a_Past = std::ranges::find_if(mesh.Indices, [l_VertexCount](std::uint32_t index) { return index >= l_VertexCount; }); a_Past != mesh.Indices.end())
        {
            return Unexpected{ std::format("index {} names vertex {} of {}", a_Past - mesh.Indices.begin(), *a_Past, l_VertexCount) };
        }

        const auto a_Bounds = [&mesh](auto&& indices)
        {
            MeshBounds l_Bounds{ glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest()) };
            for (const std::uint32_t it_Index : indices)
            {
                l_Bounds.Min = glm::min(l_Bounds.Min, mesh.Positions[it_Index]);
                l_Bounds.Max = glm::max(l_Bounds.Max, mesh.Positions[it_Index]);
            }

            return l_Bounds;
        };

        const std::uint32_t l_IndexCount = static_cast<std::uint32_t>(mesh.Indices.size());
        std::vector<Submesh> l_Submeshes = mesh.Submeshes.empty() ? std::vector<Submesh>{ Submesh{ 0, l_IndexCount, 0, {} } } : mesh.Submeshes;
        for (Submesh& it_Submesh : l_Submeshes)
        {
            if (it_Submesh.IndexCount == 0 || it_Submesh.IndexCount % 3 != 0 || it_Submesh.FirstIndex % 3 != 0 || std::uint64_t{ it_Submesh.FirstIndex } + it_Submesh.IndexCount > l_IndexCount)
            {
                return Unexpected{ std::format("a submesh of {} indices from index {} is not whole triangles within the mesh's {}", it_Submesh.IndexCount, it_Submesh.FirstIndex, l_IndexCount) };
            }

            it_Submesh.Bounds = a_Bounds(std::span(mesh.Indices).subspan(it_Submesh.FirstIndex, it_Submesh.IndexCount));
        }

        MeshStreams l_Streams = MeshStreams::None;
        l_Streams = mesh.Normals.empty() ? l_Streams : l_Streams | MeshStreams::NormalTangent;
        l_Streams = mesh.TexCoords0.empty() ? l_Streams : l_Streams | MeshStreams::TexCoord0;
        l_Streams = mesh.TexCoords1.empty() ? l_Streams : l_Streams | MeshStreams::TexCoord1;
        l_Streams = mesh.Colors.empty() ? l_Streams : l_Streams | MeshStreams::Color;

        const MeshLayout l_Layout = GetMeshLayout(static_cast<std::uint32_t>(l_VertexCount), l_IndexCount, l_Streams);
        MeshBounds l_Bounds{ glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest()) };
        for (const glm::vec3& it_Position : mesh.Positions)
        {
            l_Bounds.Min = glm::min(l_Bounds.Min, it_Position);
            l_Bounds.Max = glm::max(l_Bounds.Max, it_Position);
        }

        std::vector<std::byte> l_File;
        l_File.reserve(static_cast<std::size_t>(c_HeaderSize + l_Submeshes.size() * c_SubmeshSize + c_StreamAlignment + l_Layout.Size));
        Append(l_File, c_Magic);
        Append(l_File, c_Version);
        Append(l_File, l_Layout.VertexCount);
        Append(l_File, l_Layout.IndexCount);
        Append(l_File, static_cast<std::uint32_t>(l_Streams));
        Append(l_File, RHI::GetIndexSize(l_Layout.IndexFormat));
        Append(l_File, static_cast<std::uint32_t>(l_Submeshes.size()));
        Append(l_File, std::uint32_t{ 0 });
        AppendBounds(l_File, l_Bounds);
        Append(l_File, l_Layout.Size);

        for (const Submesh& it_Submesh : l_Submeshes)
        {
            Append(l_File, it_Submesh.FirstIndex);
            Append(l_File, it_Submesh.IndexCount);
            Append(l_File, it_Submesh.MaterialSlot);
            Append(l_File, std::uint32_t{ 0 });
            AppendBounds(l_File, it_Submesh.Bounds);
        }

        const std::size_t l_DataOffset = static_cast<std::size_t>(AlignUp(l_File.size(), c_StreamAlignment));
        l_File.resize(l_DataOffset + static_cast<std::size_t>(l_Layout.Size));
        const std::span<std::byte> l_Data = std::span(l_File).subspan(l_DataOffset);

        std::memcpy(l_Data.data() + l_Layout.Positions, mesh.Positions.data(), l_VertexCount * sizeof(glm::vec3));
        for (std::size_t it_Vertex = 0; it_Vertex < l_VertexCount && HasFlag(l_Streams, MeshStreams::NormalTangent); ++it_Vertex)
        {
            const glm::vec3 l_Normal = mesh.Normals[it_Vertex];
            const glm::vec4 l_Tangent = mesh.Tangents.empty() ? glm::vec4(GetPerpendicular(l_Normal), 1.0f) : mesh.Tangents[it_Vertex];
            const std::array<std::int16_t, 2> l_EncodedNormal = EncodeOctahedral(l_Normal);
            std::array<std::int16_t, 2> l_EncodedTangent = EncodeOctahedral(glm::vec3(l_Tangent));
            l_EncodedTangent[1] = static_cast<std::int16_t>((static_cast<std::uint16_t>(l_EncodedTangent[1]) & 0xFFFEu) | (l_Tangent.w < 0.0f ? 1u : 0u));

            const std::uint64_t l_Offset = l_Layout.NormalTangents + it_Vertex * 8;
            Store(l_Data, l_Offset, l_EncodedNormal);
            Store(l_Data, l_Offset + 4, l_EncodedTangent);
        }

        if (HasFlag(l_Streams, MeshStreams::TexCoord0))
        {
            std::memcpy(l_Data.data() + l_Layout.TexCoords0, mesh.TexCoords0.data(), l_VertexCount * sizeof(glm::vec2));
        }

        if (HasFlag(l_Streams, MeshStreams::TexCoord1))
        {
            std::memcpy(l_Data.data() + l_Layout.TexCoords1, mesh.TexCoords1.data(), l_VertexCount * sizeof(glm::vec2));
        }

        for (std::size_t it_Vertex = 0; it_Vertex < l_VertexCount && HasFlag(l_Streams, MeshStreams::Color); ++it_Vertex)
        {
            const glm::vec4& l_Color = mesh.Colors[it_Vertex];
            const std::array<std::uint16_t, 4> l_Packed{ ToUnorm16(l_Color.r), ToUnorm16(l_Color.g), ToUnorm16(l_Color.b), ToUnorm16(l_Color.a) };
            Store(l_Data, l_Layout.Colors + it_Vertex * 8, l_Packed);
        }

        for (std::size_t it_Index = 0; it_Index < mesh.Indices.size(); ++it_Index)
        {
            if (l_Layout.IndexFormat == RHI::IndexFormat::UInt16)
            {
                Store(l_Data, l_Layout.Indices + it_Index * 2, static_cast<std::uint16_t>(mesh.Indices[it_Index]));
            }
            else
            {
                Store(l_Data, l_Layout.Indices + it_Index * 4, mesh.Indices[it_Index]);
            }
        }

        return l_File;
    }

    Expected<MeshFile, std::string> ReadMeshFile(std::span<const std::byte> file)
    {
        if (file.size() < c_HeaderSize || std::memcmp(file.data(), c_Magic.data(), c_Magic.size()) != 0)
        {
            return Unexpected{ std::string("not a cooked mesh") };
        }

        const std::uint32_t l_Version = Load<std::uint32_t>(file, 4);
        if (l_Version != c_Version)
        {
            return Unexpected{ std::format("mesh format {}, where this build reads {}", l_Version, c_Version) };
        }

        const std::uint32_t l_VertexCount = Load<std::uint32_t>(file, 8);
        const std::uint32_t l_IndexCount = Load<std::uint32_t>(file, 12);
        const std::uint32_t l_Streams = Load<std::uint32_t>(file, 16);
        const std::uint32_t l_IndexSize = Load<std::uint32_t>(file, 20);
        const std::uint32_t l_SubmeshCount = Load<std::uint32_t>(file, 24);
        const std::uint64_t l_DataSize = Load<std::uint64_t>(file, 56);
        if (l_VertexCount == 0 || l_IndexCount == 0 || l_IndexCount % 3 != 0 || l_SubmeshCount == 0 || (l_Streams & ~c_KnownStreams) != 0)
        {
            return Unexpected{ std::format("{} vertices, {} indices, {} submeshes and streams {:#x} are not a mesh this build reads", l_VertexCount, l_IndexCount, l_SubmeshCount, l_Streams) };
        }

        MeshFile l_File;
        l_File.Layout = GetMeshLayout(l_VertexCount, l_IndexCount, static_cast<MeshStreams>(l_Streams));
        l_File.Bounds = LoadBounds(file, 32);

        const std::uint64_t l_DataOffset = AlignUp(c_HeaderSize + std::uint64_t{ l_SubmeshCount } * c_SubmeshSize, c_StreamAlignment);
        if (l_IndexSize != RHI::GetIndexSize(l_File.Layout.IndexFormat) || l_DataSize != l_File.Layout.Size || file.size() != l_DataOffset + l_DataSize)
        {
            return Unexpected{ std::format("{} bytes with {}-byte indices, where the layout needs {} bytes of data after {} and {}-byte indices", file.size(), l_IndexSize, l_File.Layout.Size, l_DataOffset, RHI::GetIndexSize(l_File.Layout.IndexFormat)) };
        }

        l_File.Submeshes.reserve(l_SubmeshCount);
        for (std::uint32_t it_Submesh = 0; it_Submesh < l_SubmeshCount; ++it_Submesh)
        {
            const std::uint64_t l_Offset = c_HeaderSize + std::uint64_t{ it_Submesh } * c_SubmeshSize;
            Submesh l_Submesh;
            l_Submesh.FirstIndex = Load<std::uint32_t>(file, l_Offset);
            l_Submesh.IndexCount = Load<std::uint32_t>(file, l_Offset + 4);
            l_Submesh.MaterialSlot = Load<std::uint32_t>(file, l_Offset + 8);
            l_Submesh.Bounds = LoadBounds(file, l_Offset + 16);
            if (l_Submesh.IndexCount == 0 || std::uint64_t{ l_Submesh.FirstIndex } + l_Submesh.IndexCount > l_IndexCount)
            {
                return Unexpected{ std::format("submesh {} runs past the mesh's {} indices", it_Submesh, l_IndexCount) };
            }

            l_File.Submeshes.push_back(l_Submesh);
        }

        l_File.Data = file.subspan(static_cast<std::size_t>(l_DataOffset), static_cast<std::size_t>(l_DataSize));
        for (std::uint32_t it_Index = 0; it_Index < l_IndexCount; ++it_Index)
        {
            const std::uint32_t l_Index = l_File.Layout.IndexFormat == RHI::IndexFormat::UInt16 ? Load<std::uint16_t>(l_File.Data, l_File.Layout.Indices + std::uint64_t{ it_Index } * 2) : Load<std::uint32_t>(l_File.Data, l_File.Layout.Indices + std::uint64_t{ it_Index } * 4);
            if (l_Index >= l_VertexCount)
            {
                return Unexpected{ std::format("index {} names vertex {} of {}", it_Index, l_Index, l_VertexCount) };
            }
        }

        return l_File;
    }

    // The fold puts the lower half of the sphere into the square's corners
    std::array<std::int16_t, 2> EncodeOctahedral(glm::vec3 direction)
    {
        const float l_Sum = std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z);
        glm::vec2 l_Folded = l_Sum > 0.0f ? glm::vec2(direction.x, direction.y) / l_Sum : glm::vec2(0.0f);
        if (l_Sum > 0.0f && direction.z < 0.0f)
        {
            const glm::vec2 l_Sign(l_Folded.x >= 0.0f ? 1.0f : -1.0f, l_Folded.y >= 0.0f ? 1.0f : -1.0f);
            l_Folded = (glm::vec2(1.0f) - glm::abs(glm::vec2(l_Folded.y, l_Folded.x))) * l_Sign;
        }

        const auto a_Snorm = [](float value) { return static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f)); };

        return { a_Snorm(l_Folded.x), a_Snorm(l_Folded.y) };
    }

    glm::vec3 DecodeOctahedral(std::array<std::int16_t, 2> encoded)
    {
        const glm::vec2 l_Folded(std::max(static_cast<float>(encoded[0]) / 32767.0f, -1.0f), std::max(static_cast<float>(encoded[1]) / 32767.0f, -1.0f));
        glm::vec3 l_Direction(l_Folded.x, l_Folded.y, 1.0f - std::abs(l_Folded.x) - std::abs(l_Folded.y));
        const float l_Fold = std::max(-l_Direction.z, 0.0f);
        l_Direction.x += l_Direction.x >= 0.0f ? -l_Fold : l_Fold;
        l_Direction.y += l_Direction.y >= 0.0f ? -l_Fold : l_Fold;

        return glm::normalize(l_Direction);
    }

    MeshAsset::~MeshAsset()
    {
        if (m_Loader != nullptr)
        {
            m_Loader->Release(*this);
        }
    }
}