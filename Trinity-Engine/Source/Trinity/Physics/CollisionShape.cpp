#include "Trinity/Physics/JoltContext.hpp"
#include "Trinity/Physics/CollisionShape.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Physics/CollisionShapeLoaders.hpp"
#include "Trinity/Project/Project.hpp"
#include "Trinity/Renderer/DebugDraw.hpp"

#include <Jolt/Core/StreamIn.h>
#include <Jolt/Core/StreamOut.h>
#include <Jolt/Geometry/AABox.h>
#include <Jolt/Geometry/IndexedTriangle.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace Trinity
{
    namespace
    {
        // A 24-byte header, then the shape as Jolt saves it, or the reason it could not be made, all little-endian
        constexpr std::array<char, 4> c_Magic{ 'T', 'R', 'C', 'S' };
        constexpr std::uint32_t c_FormatVersion = 1;
        constexpr std::size_t c_HeaderSize = 24;

        struct FileHeader
        {
            std::array<char, 4> Magic = c_Magic;
            std::uint32_t Version = 0;
            std::uint32_t Kind = 0;
            std::uint32_t Failed = 0;
            std::uint64_t Size = 0;
        };

        static_assert(sizeof(FileHeader) == c_HeaderSize, "The header is copied as it lies in memory");

        std::string_view GetKindName(CollisionShapeKind kind)
        {
            return kind == CollisionShapeKind::ConvexHull ? "convex hull" : "triangle mesh";
        }

        // Held while Jolt's types are needed, and let go after the shapes made under it
        class JoltTypes
        {
        public:
            JoltTypes()
            {
                JoltContext::AcquireTypes();
            }

            ~JoltTypes()
            {
                JoltContext::ReleaseTypes();
            }

            JoltTypes(const JoltTypes&) = delete;
            JoltTypes& operator=(const JoltTypes&) = delete;
        };

        class ByteStreamOut final : public JPH::StreamOut
        {
        public:
            explicit ByteStreamOut(std::vector<std::byte>& bytes) : m_Bytes(bytes)
            {

            }

            void WriteBytes(const void* data, std::size_t size) override
            {
                const std::byte* l_Data = static_cast<const std::byte*>(data);
                m_Bytes.insert(m_Bytes.end(), l_Data, l_Data + size);
            }

            [[nodiscard]] bool IsFailed() const override
            {
                return false;
            }

        private:
            std::vector<std::byte>& m_Bytes;
        };

        // A read past the end fails the stream, which Jolt also takes as its end, and reads zeros
        class ByteStreamIn final : public JPH::StreamIn
        {
        public:
            explicit ByteStreamIn(std::span<const std::byte> bytes) : m_Bytes(bytes)
            {

            }

            void ReadBytes(void* data, std::size_t size) override
            {
                if (m_Failed || size > m_Bytes.size() - m_Offset)
                {
                    m_Failed = true;
                    std::memset(data, 0, size);

                    return;
                }

                std::memcpy(data, m_Bytes.data() + m_Offset, size);
                m_Offset += size;
            }

            [[nodiscard]] bool IsEOF() const override
            {
                return m_Failed;
            }

            [[nodiscard]] bool IsFailed() const override
            {
                return m_Failed;
            }

            [[nodiscard]] bool IsWhollyRead() const
            {
                return !m_Failed && m_Offset == m_Bytes.size();
            }

        private:
            std::span<const std::byte> m_Bytes;
            std::size_t m_Offset = 0;
            bool m_Failed = false;
        };

        // A position by its bits, with -0 taken as 0, so equal positions are one key
        using PositionKey = std::array<std::uint32_t, 3>;

        struct PositionKeyHash
        {
            [[nodiscard]] std::size_t operator()(const PositionKey& key) const
            {
                std::uint64_t l_Hash = 14695981039346656037ull;
                for (const std::uint32_t it_Part : key)
                {
                    l_Hash = (l_Hash ^ it_Part) * 1099511628211ull;
                }

                return static_cast<std::size_t>(l_Hash);
            }
        };

        PositionKey GetPositionKey(const glm::vec3& position)
        {
            return { std::bit_cast<std::uint32_t>(position.x + 0.0f), std::bit_cast<std::uint32_t>(position.y + 0.0f), std::bit_cast<std::uint32_t>(position.z + 0.0f) };
        }

        // Each position once, in the order first met, and for each of the mesh's vertices the welded vertex it became
        struct WeldedVertices
        {
            JPH::VertexList Positions;
            std::vector<std::uint32_t> Remap;
        };

        WeldedVertices Weld(std::span<const glm::vec3> positions)
        {
            WeldedVertices l_Welded;
            l_Welded.Remap.reserve(positions.size());

            std::unordered_map<PositionKey, std::uint32_t, PositionKeyHash> l_Seen;
            l_Seen.reserve(positions.size());
            for (const glm::vec3& it_Position : positions)
            {
                const auto [a_Found, a_Added] = l_Seen.try_emplace(GetPositionKey(it_Position), static_cast<std::uint32_t>(l_Welded.Positions.size()));
                if (a_Added)
                {
                    l_Welded.Positions.push_back(JPH::Float3(it_Position.x, it_Position.y, it_Position.z));
                }

                l_Welded.Remap.push_back(a_Found->second);
            }

            return l_Welded;
        }

        template<typename F>
        std::vector<std::byte> MakeFile(CollisionShapeKind kind, bool failed, F writeBody)
        {
            std::vector<std::byte> l_File(c_HeaderSize);
            writeBody(l_File);

            FileHeader l_Header;
            l_Header.Version = GetCollisionShapeVersion();
            l_Header.Kind = static_cast<std::uint32_t>(kind);
            l_Header.Failed = failed ? 1u : 0u;
            l_Header.Size = l_File.size() - c_HeaderSize;
            std::memcpy(l_File.data(), &l_Header, c_HeaderSize);

            return l_File;
        }

        glm::vec3 ToGlm(JPH::Vec3Arg vector)
        {
            return { vector.GetX(), vector.GetY(), vector.GetZ() };
        }

#if TR_DEBUG_DRAW
        // Triangles read from a mesh shape at a time, for its wireframe
        constexpr int c_TriangleBatch = 256;

        // Around each face, each edge once though two faces share it, in the mesh's space rather than about the centre of mass
        void AddHullWireframe(const JPH::ConvexHullShape& hull, std::vector<glm::vec3, TaggedAllocator<glm::vec3, MemoryTag::Assets>>& wireframe)
        {
            const JPH::Vec3 l_Center = hull.GetCenterOfMass();
            std::vector<JPH::uint> l_Face(hull.GetNumPoints());
            std::unordered_set<std::uint32_t> l_Edges;
            for (JPH::uint it_Face = 0; it_Face < hull.GetNumFaces(); ++it_Face)
            {
                const JPH::uint l_Count = std::min(hull.GetFaceVertices(it_Face, static_cast<JPH::uint>(l_Face.size()), l_Face.data()), static_cast<JPH::uint>(l_Face.size()));
                for (JPH::uint it_Vertex = 0; it_Vertex < l_Count; ++it_Vertex)
                {
                    const JPH::uint l_From = l_Face[it_Vertex];
                    const JPH::uint l_To = l_Face[(it_Vertex + 1) % l_Count];
                    if (l_Edges.insert((std::min(l_From, l_To) << 16) | std::max(l_From, l_To)).second)
                    {
                        wireframe.push_back(ToGlm(hull.GetPoint(l_From) + l_Center));
                        wireframe.push_back(ToGlm(hull.GetPoint(l_To) + l_Center));
                    }
                }
            }
        }

        // Each triangle's edges, the ones two triangles share once. Past c_MaxWireEdges none are kept
        void AddMeshWireframe(const JPH::Shape& mesh, std::vector<glm::vec3, TaggedAllocator<glm::vec3, MemoryTag::Assets>>& wireframe)
        {
            using EdgeKey = std::array<std::uint32_t, 6>;
            struct EdgeKeyHash
            {
                [[nodiscard]] std::size_t operator()(const EdgeKey& key) const
                {
                    return PositionKeyHash()({ key[0], key[1], key[2] }) * 31 + PositionKeyHash()({ key[3], key[4], key[5] });
                }
            };

            // A little past the shape's bounds, so no triangle on them is left out
            JPH::AABox l_Box = mesh.GetLocalBounds();
            l_Box.ExpandBy(JPH::Vec3::sReplicate(1.0e-3f) + l_Box.GetExtent() * 1.0e-3f);
            l_Box.Translate(mesh.GetCenterOfMass());

            JPH::Shape::GetTrianglesContext l_Context;
            mesh.GetTrianglesStart(l_Context, l_Box, mesh.GetCenterOfMass(), JPH::Quat::sIdentity(), JPH::Vec3::sOne());

            std::array<JPH::Float3, c_TriangleBatch * 3> l_Vertices;
            std::unordered_set<EdgeKey, EdgeKeyHash> l_Edges;
            for (int l_Count = mesh.GetTrianglesNext(l_Context, c_TriangleBatch, l_Vertices.data()); l_Count > 0; l_Count = mesh.GetTrianglesNext(l_Context, c_TriangleBatch, l_Vertices.data()))
            {
                for (int it_Corner = 0; it_Corner < l_Count * 3; ++it_Corner)
                {
                    const JPH::Float3& l_From = l_Vertices[static_cast<std::size_t>(it_Corner)];
                    const JPH::Float3& l_To = l_Vertices[static_cast<std::size_t>(it_Corner % 3 == 2 ? it_Corner - 2 : it_Corner + 1)];
                    const PositionKey l_FromKey = GetPositionKey({ l_From.x, l_From.y, l_From.z });
                    const PositionKey l_ToKey = GetPositionKey({ l_To.x, l_To.y, l_To.z });
                    const bool l_Ordered = l_FromKey < l_ToKey;
                    const PositionKey& l_First = l_Ordered ? l_FromKey : l_ToKey;
                    const PositionKey& l_Second = l_Ordered ? l_ToKey : l_FromKey;
                    if (!l_Edges.insert({ l_First[0], l_First[1], l_First[2], l_Second[0], l_Second[1], l_Second[2] }).second)
                    {
                        continue;
                    }

                    if (l_Edges.size() > CollisionShapeAsset::c_MaxWireEdges)
                    {
                        wireframe.clear();
                        wireframe.shrink_to_fit();

                        return;
                    }

                    wireframe.emplace_back(l_From.x, l_From.y, l_From.z);
                    wireframe.emplace_back(l_To.x, l_To.y, l_To.z);
                }
            }
        }
#endif
    }

    // A loader for each kind, so a collider's picker and a dropped asset name the kind it takes. A worker checks the header and restores Jolt's shape, which holds Jolt's types until the asset goes
    class CollisionShapeLoader final : public AssetLoader
    {
    public:
        explicit CollisionShapeLoader(CollisionShapeKind kind) : m_Kind(kind)
        {

        }

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return GetCollisionShapeAssetType(m_Kind);
        }

        [[nodiscard]] std::string GetLoadPath(const AssetRecord& record) const override
        {
            return GetCookedCollisionShapePath(record.ID);
        }

        [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override;

    private:
        CollisionShapeKind m_Kind;
    };

    std::string GetCookedCollisionShapePath(UUID id)
    {
        return std::format("{}/Collision/{}.trshape", Project::c_CacheMount, id);
    }

    std::uint32_t GetCollisionShapeVersion()
    {
        return (c_FormatVersion << 24) | (JPH_VERSION_MAJOR << 16) | (JPH_VERSION_MINOR << 8) | JPH_VERSION_PATCH;
    }

    CookedCollisionShape CookCollisionShape(CollisionShapeKind kind, const MeshData& mesh)
    {
        TR_PROFILE_FUNCTION();

        CookedCollisionShape l_Cooked;
        const auto a_Fail = [&l_Cooked, kind](std::string reason)
        {
            l_Cooked.Error = std::move(reason);
            l_Cooked.File = MakeFile(kind, true, [&l_Cooked](std::vector<std::byte>& file)
            {
                const std::span<const std::byte> l_Reason = std::as_bytes(std::span(l_Cooked.Error));
                file.insert(file.end(), l_Reason.begin(), l_Reason.end());
            });

            return l_Cooked;
        };

        const std::size_t l_VertexCount = mesh.Positions.size();
        if (mesh.Indices.size() % 3 != 0)
        {
            return a_Fail(std::format("{} indices are not whole triangles", mesh.Indices.size()));
        }

        if (const auto a_Past = std::ranges::find_if(mesh.Indices, [l_VertexCount](std::uint32_t index) { return index >= l_VertexCount; }); a_Past != mesh.Indices.end())
        {
            return a_Fail(std::format("index {} names vertex {} of {}", a_Past - mesh.Indices.begin(), *a_Past, l_VertexCount));
        }

        if (!std::ranges::all_of(mesh.Positions, [](const glm::vec3& position) { return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z); }))
        {
            return a_Fail("a vertex is not at a finite position");
        }

        const JoltTypes l_Types;
        WeldedVertices l_Welded = Weld(mesh.Positions);
        JPH::Shape::ShapeResult l_Result;
        if (kind == CollisionShapeKind::ConvexHull)
        {
            JPH::Array<JPH::Vec3> l_Points;
            l_Points.reserve(l_Welded.Positions.size());
            for (const JPH::Float3& it_Position : l_Welded.Positions)
            {
                l_Points.push_back(JPH::Vec3(it_Position));
            }

            l_Result = JPH::ConvexHullShapeSettings(l_Points).Create();
        }
        else
        {
            JPH::IndexedTriangleList l_Triangles;
            l_Triangles.reserve(mesh.Indices.size() / 3);
            for (std::size_t it_Index = 0; it_Index < mesh.Indices.size(); it_Index += 3)
            {
                l_Triangles.push_back(JPH::IndexedTriangle(l_Welded.Remap[mesh.Indices[it_Index]], l_Welded.Remap[mesh.Indices[it_Index + 1]], l_Welded.Remap[mesh.Indices[it_Index + 2]]));
            }

            l_Result = JPH::MeshShapeSettings(std::move(l_Welded.Positions), std::move(l_Triangles)).Create();
        }

        if (l_Result.HasError())
        {
            return a_Fail(std::format("Jolt could not make its {}: {}", GetKindName(kind), l_Result.GetError().c_str()));
        }

        const JPH::Ref<JPH::Shape> l_Shape = l_Result.Get();
        l_Cooked.File = MakeFile(kind, false, [&l_Shape](std::vector<std::byte>& file)
        {
            ByteStreamOut l_Stream(file);
            l_Shape->SaveBinaryState(l_Stream);
        });

        return l_Cooked;
    }

    // Jolt's types go after the shape that needs them
    CollisionShapeAsset::~CollisionShapeAsset()
    {
        if (m_Shape != nullptr)
        {
            m_Shape->Release();
            JoltContext::ReleaseTypes();
        }
    }

    // On a worker. The kind byte Jolt reads first is checked before Jolt sees it, since Jolt takes it as an index into its own table
    Expected<Asset*, std::string> CollisionShapeLoader::Load(std::span<const std::byte> data) const
    {
        TR_PROFILE_FUNCTION();

        FileHeader l_Header;
        if (data.size() >= c_HeaderSize)
        {
            std::memcpy(&l_Header, data.data(), c_HeaderSize);
        }

        if (data.size() < c_HeaderSize || l_Header.Magic != c_Magic)
        {
            return Unexpected{ std::string("not a cooked collision shape") };
        }

        if (l_Header.Version != GetCollisionShapeVersion())
        {
            return Unexpected{ std::format("it was cooked as collision shape version {:08x}, and this build reads {:08x}, so its model needs importing again", l_Header.Version, GetCollisionShapeVersion()) };
        }

        if (l_Header.Kind != static_cast<std::uint32_t>(m_Kind) || l_Header.Size != data.size() - c_HeaderSize)
        {
            return Unexpected{ std::format("it is not a whole cooked {}", GetKindName(m_Kind)) };
        }

        const std::span<const std::byte> l_Body = data.subspan(c_HeaderSize);
        if (l_Header.Failed != 0)
        {
            return Unexpected{ std::format("its mesh gave no shape, since {}", std::string_view(reinterpret_cast<const char*>(l_Body.data()), l_Body.size())) };
        }

        const JPH::EShapeSubType l_SubType = m_Kind == CollisionShapeKind::ConvexHull ? JPH::EShapeSubType::ConvexHull : JPH::EShapeSubType::Mesh;
        if (l_Body.empty() || l_Body.front() != static_cast<std::byte>(l_SubType))
        {
            return Unexpected{ std::format("it does not hold a {}", GetKindName(m_Kind)) };
        }

        JoltContext::AcquireTypes();
        ByteStreamIn l_Stream(l_Body);
        const JPH::Shape::ShapeResult l_Result = JPH::Shape::sRestoreFromBinaryState(l_Stream);
        if (l_Result.HasError() || !l_Stream.IsWhollyRead())
        {
            const std::string l_Error = l_Result.HasError() ? std::format("Jolt could not restore it: {}", l_Result.GetError().c_str()) : std::string("Jolt did not read all of it");
            JoltContext::ReleaseTypes();

            return Unexpected{ l_Error };
        }

        const JPH::Shape* l_Shape = l_Result.Get().GetPtr();
        l_Shape->AddRef();

        CollisionShapeAsset* l_Asset = m_Kind == CollisionShapeKind::ConvexHull ? static_cast<CollisionShapeAsset*>(Memory::New<ConvexHullAsset>(MemoryTag::Assets)) : Memory::New<CollisionMeshAsset>(MemoryTag::Assets);
        l_Asset->m_Shape = l_Shape;

        const JPH::AABox l_Bounds = l_Shape->GetLocalBounds();
        const JPH::Vec3 l_Center = l_Shape->GetCenterOfMass();
        l_Asset->m_Bounds = { ToGlm(l_Bounds.mMin + l_Center), ToGlm(l_Bounds.mMax + l_Center) };

#if TR_DEBUG_DRAW
        if (m_Kind == CollisionShapeKind::ConvexHull)
        {
            AddHullWireframe(static_cast<const JPH::ConvexHullShape&>(*l_Shape), l_Asset->m_Wireframe);
        }
        else
        {
            AddMeshWireframe(*l_Shape, l_Asset->m_Wireframe);
        }
#endif

        return l_Asset;
    }

    namespace CollisionShapeLoaders
    {
        namespace
        {
            std::array<CollisionShapeLoader*, 2> s_Loaders{};
        }

        void Register()
        {
            for (const CollisionShapeKind it_Kind : { CollisionShapeKind::ConvexHull, CollisionShapeKind::Mesh })
            {
                CollisionShapeLoader*& l_Loader = s_Loaders[static_cast<std::size_t>(it_Kind)];
                TR_CORE_ASSERT(l_Loader == nullptr, "The collision shape loaders are registered once.");
                l_Loader = Memory::New<CollisionShapeLoader>(MemoryTag::Engine, it_Kind);
                AssetManager::RegisterLoader(*l_Loader);
            }
        }

        void Unregister()
        {
            for (CollisionShapeLoader*& it_Loader : s_Loaders)
            {
                if (it_Loader != nullptr)
                {
                    AssetManager::UnregisterLoader(it_Loader->GetAssetType());
                    Memory::Delete(it_Loader);
                    it_Loader = nullptr;
                }
            }
        }
    }
}