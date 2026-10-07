#include "Importers/ModelImporter.hpp"

#include "Importers/TextureImporter.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <meshoptimizer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <initializer_list>
#include <map>
#include <numeric>
#include <optional>
#include <utility>
#include <variant>

struct ModelImporter::Source
{
    // A material's use of a glTF texture: the sub-asset it became, or the texture beside the model it names
    struct TextureUse
    {
        std::string Key;
        std::size_t External = 0;
    };

    fastgltf::Asset Asset;
    // By glTF image, the bytes of each the model holds itself, and nothing for one beside it
    std::vector<std::span<const std::byte>> EmbeddedImages;
    // By glTF mesh and material, the sub-asset each became, and an empty key for a mesh with nothing to import
    std::vector<std::string> MeshKeys;
    std::vector<std::string> MaterialKeys;
    // By glTF texture and how it is used
    std::map<std::pair<std::size_t, TextureUsage>, TextureUse> Textures;
};

namespace
{
    constexpr std::size_t c_MaxNameLength = 48;

    struct PrimitiveData
    {
        std::vector<glm::vec3> Positions;
        std::vector<glm::vec3> Normals;
        std::vector<glm::vec4> Tangents;
        std::vector<glm::vec2> TexCoords0;
        std::vector<glm::vec2> TexCoords1;
        std::vector<glm::vec4> Colors;
        std::vector<std::uint32_t> Indices;
    };

    // A glTF mesh as one mesh asset with a submesh for each primitive, and the material in each submesh's slot
    struct BuiltMesh
    {
        Trinity::MeshData Data;
        std::vector<std::optional<std::size_t>> SlotMaterials;
    };

    // FNV-1a, continued from a previous hash so several files make one
    std::uint64_t Hash(std::span<const std::byte> bytes, std::uint64_t hash = 14695981039346656037ull)
    {
        for (const std::byte it_Byte : bytes)
        {
            hash ^= std::to_integer<std::uint64_t>(it_Byte);
            hash *= 1099511628211ull;
        }

        return hash;
    }

    std::uint64_t Hash(std::string_view text, std::uint64_t hash)
    {
        return Hash(std::as_bytes(std::span(text)), hash);
    }

    bool IsKeyCharacter(char character)
    {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_' || character == '-' || character == ' ' || character == '(' || character == ')';
    }

    // The glTF index, then how it is used, then its name in characters that read plainly in a .meta and after the '#' of a path
    std::string MakeKey(std::string_view kind, std::size_t index, std::string_view usage, std::string_view name)
    {
        std::string l_Key = usage.empty() ? std::format("{}.{}", kind, index) : std::format("{}.{}.{}", kind, index, usage);
        std::string l_Name;
        for (const char it_Character : name.substr(0, c_MaxNameLength))
        {
            l_Name += IsKeyCharacter(it_Character) ? it_Character : '_';
        }

        while (!l_Name.empty() && l_Name.back() == ' ')
        {
            l_Name.pop_back();
        }

        return l_Name.empty() ? l_Key : std::format("{}.{}", l_Key, l_Name);
    }

    // A relative URI from the model's folder, refused when it is absolute or leaves the mount it starts in
    std::optional<std::string> ResolvePath(std::string_view modelPath, std::string_view uri)
    {
        if (uri.empty() || uri.starts_with('/') || uri.starts_with('\\') || uri.find(':') != std::string_view::npos)
        {
            return std::nullopt;
        }

        std::vector<std::string> l_Parts;
        const auto a_Split = [&l_Parts](std::string_view path, bool allowParent)
        {
            std::size_t l_Start = 0;
            while (l_Start <= path.size())
            {
                const std::size_t l_End = std::min(path.find_first_of("/\\", l_Start), path.size());
                const std::string_view l_Part = path.substr(l_Start, l_End - l_Start);
                l_Start = l_End + 1;
                if (l_Part.empty() || l_Part == ".")
                {
                    continue;
                }

                if (l_Part == "..")
                {
                    if (!allowParent || l_Parts.size() <= 1)
                    {
                        return false;
                    }

                    l_Parts.pop_back();

                    continue;
                }

                l_Parts.emplace_back(l_Part);
            }

            return true;
        };

        if (!a_Split(modelPath.substr(0, modelPath.find_last_of('/')), false) || !a_Split(uri, true) || l_Parts.size() <= 1)
        {
            return std::nullopt;
        }

        std::string l_Path;
        for (const std::string& it_Part : l_Parts)
        {
            l_Path += '/';
            l_Path += it_Part;
        }

        return l_Path;
    }

    std::span<const std::byte> GetBytes(const fastgltf::DataSource& data)
    {
        if (const auto* l_Array = std::get_if<fastgltf::sources::Array>(&data))
        {
            return { l_Array->bytes.data(), l_Array->bytes.size_bytes() };
        }

        if (const auto* l_Vector = std::get_if<fastgltf::sources::Vector>(&data))
        {
            return l_Vector->bytes;
        }

        if (const auto* l_View = std::get_if<fastgltf::sources::ByteView>(&data))
        {
            return { l_View->bytes.data(), l_View->bytes.size() };
        }

        return {};
    }

    // Every element inside its buffer view, which PlanImport has checked lies inside its buffer, as fastgltf reads without checking
    bool IsReadable(const fastgltf::Asset& asset, std::size_t accessorIndex, std::initializer_list<fastgltf::AccessorType> types)
    {
        if (accessorIndex >= asset.accessors.size())
        {
            return false;
        }

        const fastgltf::Accessor& l_Accessor = asset.accessors[accessorIndex];
        if (std::ranges::find(types, l_Accessor.type) == types.end() || (l_Accessor.sparse && l_Accessor.sparse->count > 0))
        {
            return false;
        }

        if (!l_Accessor.bufferViewIndex.has_value() || l_Accessor.count == 0)
        {
            return true;
        }

        if (l_Accessor.bufferViewIndex.value() >= asset.bufferViews.size())
        {
            return false;
        }

        const fastgltf::BufferView& l_View = asset.bufferViews[l_Accessor.bufferViewIndex.value()];
        const std::size_t l_ElementSize = fastgltf::getElementByteSize(l_Accessor.type, l_Accessor.componentType);
        const std::size_t l_Stride = l_View.byteStride.value_or(l_ElementSize);
        if (l_ElementSize == 0 || l_Stride == 0 || l_View.meshoptCompression != nullptr || l_Accessor.byteOffset > l_View.byteLength || l_ElementSize > l_View.byteLength - l_Accessor.byteOffset)
        {
            return false;
        }

        return l_Accessor.count - 1 <= (l_View.byteLength - l_Accessor.byteOffset - l_ElementSize) / l_Stride;
    }

    template<typename T>
    std::vector<T> ReadAccessor(const fastgltf::Asset& asset, std::size_t accessorIndex)
    {
        const fastgltf::Accessor& l_Accessor = asset.accessors[accessorIndex];
        std::vector<T> l_Values;
        l_Values.reserve(l_Accessor.count);
        fastgltf::iterateAccessor<T>(asset, l_Accessor, [&l_Values](T value) { l_Values.push_back(value); });

        return l_Values;
    }

    bool IsImported(const fastgltf::Primitive& primitive)
    {
        const bool l_Triangles = primitive.type == fastgltf::PrimitiveType::Triangles || primitive.type == fastgltf::PrimitiveType::TriangleStrip || primitive.type == fastgltf::PrimitiveType::TriangleFan;

        return l_Triangles && primitive.findAttribute("POSITION") != primitive.attributes.end();
    }

    glm::vec3 GetPerpendicular(glm::vec3 normal)
    {
        const glm::vec3 l_Axis = std::abs(normal.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 l_Perpendicular = glm::cross(l_Axis, normal);
        const float l_Length = glm::length(l_Perpendicular);

        return l_Length > 0.0f ? l_Perpendicular / l_Length : glm::vec3(1.0f, 0.0f, 0.0f);
    }

    // Each triangle's tangent and bitangent from its UVs, summed at its corners and made perpendicular to each normal. V runs down the image in glTF and up in tangent space, so it is flipped, as glTF's own tangents are made
    std::vector<glm::vec4> GenerateTangents(const PrimitiveData& primitive, const std::vector<glm::vec2>& texCoords)
    {
        std::vector<glm::vec3> l_Tangents(primitive.Positions.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> l_Bitangents(primitive.Positions.size(), glm::vec3(0.0f));
        for (std::size_t it_Index = 0; it_Index + 2 < primitive.Indices.size(); it_Index += 3)
        {
            const std::uint32_t l_A = primitive.Indices[it_Index];
            const std::uint32_t l_B = primitive.Indices[it_Index + 1];
            const std::uint32_t l_C = primitive.Indices[it_Index + 2];
            const glm::vec3 l_Edge1 = primitive.Positions[l_B] - primitive.Positions[l_A];
            const glm::vec3 l_Edge2 = primitive.Positions[l_C] - primitive.Positions[l_A];
            const glm::vec2 l_Delta1 = (texCoords[l_B] - texCoords[l_A]) * glm::vec2(1.0f, -1.0f);
            const glm::vec2 l_Delta2 = (texCoords[l_C] - texCoords[l_A]) * glm::vec2(1.0f, -1.0f);
            const float l_Determinant = l_Delta1.x * l_Delta2.y - l_Delta2.x * l_Delta1.y;
            if (std::abs(l_Determinant) < 1e-12f)
            {
                continue;
            }

            const glm::vec3 l_Tangent = (l_Edge1 * l_Delta2.y - l_Edge2 * l_Delta1.y) / l_Determinant;
            const glm::vec3 l_Bitangent = (l_Edge2 * l_Delta1.x - l_Edge1 * l_Delta2.x) / l_Determinant;
            for (const std::uint32_t it_Corner : { l_A, l_B, l_C })
            {
                l_Tangents[it_Corner] += l_Tangent;
                l_Bitangents[it_Corner] += l_Bitangent;
            }
        }

        std::vector<glm::vec4> l_Result(primitive.Positions.size());
        for (std::size_t it_Vertex = 0; it_Vertex < l_Result.size(); ++it_Vertex)
        {
            const glm::vec3 l_Normal = primitive.Normals[it_Vertex];
            glm::vec3 l_Tangent = l_Tangents[it_Vertex] - l_Normal * glm::dot(l_Normal, l_Tangents[it_Vertex]);
            const float l_Length = glm::length(l_Tangent);
            l_Tangent = l_Length > 1e-12f ? l_Tangent / l_Length : GetPerpendicular(l_Normal);
            l_Result[it_Vertex] = glm::vec4(l_Tangent, glm::dot(glm::cross(l_Normal, l_Tangent), l_Bitangents[it_Vertex]) < 0.0f ? -1.0f : 1.0f);
        }

        return l_Result;
    }

    // Whole triangles in a list, normals made flat where the primitive has none, and tangents made where it has none, from the UVs the normal map uses
    Trinity::Expected<PrimitiveData, std::string> ReadPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive, std::size_t normalTexCoord)
    {
        std::string l_Error;
        const auto a_Find = [&](std::string_view name, std::initializer_list<fastgltf::AccessorType> types) -> std::optional<std::size_t>
        {
            const auto* l_Attribute = primitive.findAttribute(name);
            if (l_Attribute == primitive.attributes.end())
            {
                return std::nullopt;
            }

            if (!IsReadable(asset, l_Attribute->accessorIndex, types))
            {
                l_Error = std::format("its {} cannot be read", name);

                return std::nullopt;
            }

            return l_Attribute->accessorIndex;
        };

        const std::optional<std::size_t> l_Positions = a_Find("POSITION", { fastgltf::AccessorType::Vec3 });
        const std::optional<std::size_t> l_Normals = a_Find("NORMAL", { fastgltf::AccessorType::Vec3 });
        const std::optional<std::size_t> l_Tangents = a_Find("TANGENT", { fastgltf::AccessorType::Vec4 });
        const std::optional<std::size_t> l_TexCoords0 = a_Find("TEXCOORD_0", { fastgltf::AccessorType::Vec2 });
        const std::optional<std::size_t> l_TexCoords1 = a_Find("TEXCOORD_1", { fastgltf::AccessorType::Vec2 });
        const std::optional<std::size_t> l_Colors = a_Find("COLOR_0", { fastgltf::AccessorType::Vec3, fastgltf::AccessorType::Vec4 });
        if (!l_Error.empty() || !l_Positions)
        {
            return Trinity::Unexpected{ l_Error.empty() ? std::string("it has no positions") : l_Error };
        }

        const std::size_t l_Count = asset.accessors[*l_Positions].count;
        for (const std::optional<std::size_t>& it_Accessor : { l_Normals, l_Tangents, l_TexCoords0, l_TexCoords1, l_Colors })
        {
            if (it_Accessor && asset.accessors[*it_Accessor].count != l_Count)
            {
                return Trinity::Unexpected{ std::string("its attributes hold different numbers of vertices") };
            }
        }

        PrimitiveData l_Data;
        l_Data.Positions = ReadAccessor<glm::vec3>(asset, *l_Positions);
        l_Data.Normals = l_Normals ? ReadAccessor<glm::vec3>(asset, *l_Normals) : std::vector<glm::vec3>();
        l_Data.Tangents = l_Tangents ? ReadAccessor<glm::vec4>(asset, *l_Tangents) : std::vector<glm::vec4>();
        l_Data.TexCoords0 = l_TexCoords0 ? ReadAccessor<glm::vec2>(asset, *l_TexCoords0) : std::vector<glm::vec2>();
        l_Data.TexCoords1 = l_TexCoords1 ? ReadAccessor<glm::vec2>(asset, *l_TexCoords1) : std::vector<glm::vec2>();
        if (l_Colors && asset.accessors[*l_Colors].type == fastgltf::AccessorType::Vec3)
        {
            for (const glm::vec3& it_Color : ReadAccessor<glm::vec3>(asset, *l_Colors))
            {
                l_Data.Colors.emplace_back(it_Color, 1.0f);
            }
        }
        else if (l_Colors)
        {
            l_Data.Colors = ReadAccessor<glm::vec4>(asset, *l_Colors);
        }

        std::vector<std::uint32_t> l_Indices(l_Count);
        if (primitive.indicesAccessor.has_value())
        {
            const std::size_t l_Accessor = primitive.indicesAccessor.value();
            const bool l_Unsigned = l_Accessor < asset.accessors.size() && (asset.accessors[l_Accessor].componentType == fastgltf::ComponentType::UnsignedByte || asset.accessors[l_Accessor].componentType == fastgltf::ComponentType::UnsignedShort || asset.accessors[l_Accessor].componentType == fastgltf::ComponentType::UnsignedInt);
            if (!l_Unsigned || !IsReadable(asset, l_Accessor, { fastgltf::AccessorType::Scalar }))
            {
                return Trinity::Unexpected{ std::string("its indices cannot be read") };
            }

            l_Indices = ReadAccessor<std::uint32_t>(asset, l_Accessor);
        }
        else
        {
            std::iota(l_Indices.begin(), l_Indices.end(), 0u);
        }

        if (std::ranges::any_of(l_Indices, [l_Count](std::uint32_t index) { return index >= l_Count; }))
        {
            return Trinity::Unexpected{ std::format("an index names a vertex past its {}", l_Count) };
        }

        switch (primitive.type)
        {
            case fastgltf::PrimitiveType::TriangleStrip:
            {
                for (std::size_t it_Index = 0; it_Index + 2 < l_Indices.size(); ++it_Index)
                {
                    const std::size_t l_Odd = it_Index % 2;
                    l_Data.Indices.insert(l_Data.Indices.end(), { l_Indices[it_Index], l_Indices[it_Index + 1 + l_Odd], l_Indices[it_Index + 2 - l_Odd] });
                }

                break;
            }
            case fastgltf::PrimitiveType::TriangleFan:
            {
                for (std::size_t it_Index = 0; it_Index + 2 < l_Indices.size(); ++it_Index)
                {
                    l_Data.Indices.insert(l_Data.Indices.end(), { l_Indices[it_Index + 1], l_Indices[it_Index + 2], l_Indices[0] });
                }

                break;
            }
            default:
            {
                if (l_Indices.size() % 3 != 0)
                {
                    return Trinity::Unexpected{ std::format("its {} indices are not whole triangles", l_Indices.size()) };
                }

                l_Data.Indices = std::move(l_Indices);
                break;
            }
        }

        if (l_Data.Indices.empty())
        {
            return Trinity::Unexpected{ std::string("it has no triangles") };
        }

        // Flat normals need a vertex for each corner of each triangle
        if (l_Data.Normals.empty())
        {
            const auto a_Unweld = [&l_Data](auto& stream)
            {
                if (stream.empty())
                {
                    return;
                }

                std::remove_reference_t<decltype(stream)> l_Corners;
                l_Corners.reserve(l_Data.Indices.size());
                for (const std::uint32_t it_Index : l_Data.Indices)
                {
                    l_Corners.push_back(stream[it_Index]);
                }

                stream = std::move(l_Corners);
            };

            a_Unweld(l_Data.Positions);
            a_Unweld(l_Data.Tangents);
            a_Unweld(l_Data.TexCoords0);
            a_Unweld(l_Data.TexCoords1);
            a_Unweld(l_Data.Colors);
            std::iota(l_Data.Indices.begin(), l_Data.Indices.end(), 0u);

            l_Data.Normals.resize(l_Data.Positions.size());
            for (std::size_t it_Corner = 0; it_Corner + 2 < l_Data.Positions.size(); it_Corner += 3)
            {
                const glm::vec3 l_Cross = glm::cross(l_Data.Positions[it_Corner + 1] - l_Data.Positions[it_Corner], l_Data.Positions[it_Corner + 2] - l_Data.Positions[it_Corner]);
                const float l_Length = glm::length(l_Cross);
                const glm::vec3 l_Normal = l_Length > 0.0f ? l_Cross / l_Length : glm::vec3(0.0f, 0.0f, 1.0f);
                std::fill_n(l_Data.Normals.begin() + static_cast<std::ptrdiff_t>(it_Corner), 3, l_Normal);
            }
        }

        // Exporters leave normals that are not unit length and tangents that are not perpendicular to them, which the octahedral encoding cannot keep
        for (glm::vec3& it_Normal : l_Data.Normals)
        {
            const float l_Length = glm::length(it_Normal);
            it_Normal = l_Length > 1e-12f ? it_Normal / l_Length : glm::vec3(0.0f, 0.0f, 1.0f);
        }

        if (l_Data.Tangents.empty())
        {
            const std::vector<glm::vec2>& l_TexCoords = normalTexCoord == 1 ? l_Data.TexCoords1 : l_Data.TexCoords0;
            if (!l_TexCoords.empty())
            {
                l_Data.Tangents = GenerateTangents(l_Data, l_TexCoords);
            }
            else
            {
                std::ranges::transform(l_Data.Normals, std::back_inserter(l_Data.Tangents), [](const glm::vec3& normal) { return glm::vec4(GetPerpendicular(normal), 1.0f); });
            }
        }

        for (std::size_t it_Vertex = 0; it_Vertex < l_Data.Tangents.size(); ++it_Vertex)
        {
            const glm::vec3 l_Normal = l_Data.Normals[it_Vertex];
            const glm::vec3 l_Given(l_Data.Tangents[it_Vertex]);
            const glm::vec3 l_Tangent = l_Given - l_Normal * glm::dot(l_Normal, l_Given);
            const float l_Length = glm::length(l_Tangent);
            l_Data.Tangents[it_Vertex] = glm::vec4(l_Length > 1e-6f ? l_Tangent / l_Length : GetPerpendicular(l_Normal), l_Data.Tangents[it_Vertex].w < 0.0f ? -1.0f : 1.0f);
        }

        return l_Data;
    }

    // Each submesh's triangles in vertex cache order, then the vertices in the order the triangles first use them, which drops any that none use
    void Optimize(Trinity::MeshData& mesh)
    {
        const std::size_t l_VertexCount = mesh.Positions.size();
        std::vector<std::uint32_t> l_Ordered(mesh.Indices.size());
        for (const Trinity::Submesh& it_Submesh : mesh.Submeshes)
        {
            meshopt_optimizeVertexCache(l_Ordered.data() + it_Submesh.FirstIndex, mesh.Indices.data() + it_Submesh.FirstIndex, it_Submesh.IndexCount, l_VertexCount);
        }

        std::vector<std::uint32_t> l_Remap(l_VertexCount);
        const std::size_t l_Used = meshopt_optimizeVertexFetchRemap(l_Remap.data(), l_Ordered.data(), l_Ordered.size(), l_VertexCount);
        meshopt_remapIndexBuffer(mesh.Indices.data(), l_Ordered.data(), l_Ordered.size(), l_Remap.data());

        const auto a_Remap = [&](auto& stream)
        {
            if (stream.empty())
            {
                return;
            }

            std::remove_reference_t<decltype(stream)> l_Remapped(l_Used);
            meshopt_remapVertexBuffer(l_Remapped.data(), stream.data(), l_VertexCount, sizeof(stream[0]), l_Remap.data());
            stream = std::move(l_Remapped);
        };

        a_Remap(mesh.Positions);
        a_Remap(mesh.Normals);
        a_Remap(mesh.Tangents);
        a_Remap(mesh.TexCoords0);
        a_Remap(mesh.TexCoords1);
        a_Remap(mesh.Colors);
    }

    // A stream that only some primitives have is filled for the others, with zero UVs and white
    Trinity::Expected<BuiltMesh, std::string> BuildMesh(const fastgltf::Asset& asset, const fastgltf::Mesh& mesh)
    {
        std::vector<PrimitiveData> l_Primitives;
        BuiltMesh l_Mesh;
        for (std::size_t it_Primitive = 0; it_Primitive < mesh.primitives.size(); ++it_Primitive)
        {
            const fastgltf::Primitive& l_Primitive = mesh.primitives[it_Primitive];
            if (!IsImported(l_Primitive))
            {
                continue;
            }

            const std::optional<std::size_t> l_Material = l_Primitive.materialIndex.has_value() && l_Primitive.materialIndex.value() < asset.materials.size() ? std::optional(l_Primitive.materialIndex.value()) : std::nullopt;
            const std::size_t l_NormalTexCoord = l_Material && asset.materials[*l_Material].normalTexture ? asset.materials[*l_Material].normalTexture->texCoordIndex : 0;
            Trinity::Expected<PrimitiveData, std::string> l_Data = ReadPrimitive(asset, l_Primitive, l_NormalTexCoord);
            if (!l_Data)
            {
                return Trinity::Unexpected{ std::format("primitive {} cannot be imported, since {}", it_Primitive, l_Data.GetError()) };
            }

            l_Primitives.push_back(std::move(*l_Data));
            l_Mesh.SlotMaterials.push_back(l_Material);
        }

        const auto a_Any = [&l_Primitives](auto member) { return std::ranges::any_of(l_Primitives, [member](const PrimitiveData& primitive) { return !(primitive.*member).empty(); }); };
        const auto a_Append = [](auto& stream, const auto& values, std::size_t count, auto fill)
        {
            if (values.empty())
            {
                stream.insert(stream.end(), count, fill);
            }
            else
            {
                stream.insert(stream.end(), values.begin(), values.end());
            }
        };

        const bool l_TexCoords0 = a_Any(&PrimitiveData::TexCoords0);
        const bool l_TexCoords1 = a_Any(&PrimitiveData::TexCoords1);
        const bool l_Colors = a_Any(&PrimitiveData::Colors);

        Trinity::MeshData& l_Data = l_Mesh.Data;
        for (std::size_t it_Slot = 0; it_Slot < l_Primitives.size(); ++it_Slot)
        {
            const PrimitiveData& l_Primitive = l_Primitives[it_Slot];
            const std::size_t l_Base = l_Data.Positions.size();
            const std::size_t l_Count = l_Primitive.Positions.size();
            if (l_Base + l_Count > UINT32_MAX || l_Data.Indices.size() + l_Primitive.Indices.size() > UINT32_MAX)
            {
                return Trinity::Unexpected{ std::string("it has more vertices or indices than 32-bit indices can name") };
            }

            Trinity::Submesh l_Submesh;
            l_Submesh.FirstIndex = static_cast<std::uint32_t>(l_Data.Indices.size());
            l_Submesh.IndexCount = static_cast<std::uint32_t>(l_Primitive.Indices.size());
            l_Submesh.MaterialSlot = static_cast<std::uint32_t>(it_Slot);
            l_Data.Submeshes.push_back(l_Submesh);

            l_Data.Positions.insert(l_Data.Positions.end(), l_Primitive.Positions.begin(), l_Primitive.Positions.end());
            l_Data.Normals.insert(l_Data.Normals.end(), l_Primitive.Normals.begin(), l_Primitive.Normals.end());
            l_Data.Tangents.insert(l_Data.Tangents.end(), l_Primitive.Tangents.begin(), l_Primitive.Tangents.end());
            if (l_TexCoords0)
            {
                a_Append(l_Data.TexCoords0, l_Primitive.TexCoords0, l_Count, glm::vec2(0.0f));
            }

            if (l_TexCoords1)
            {
                a_Append(l_Data.TexCoords1, l_Primitive.TexCoords1, l_Count, glm::vec2(0.0f));
            }

            if (l_Colors)
            {
                a_Append(l_Data.Colors, l_Primitive.Colors, l_Count, glm::vec4(1.0f));
            }

            for (const std::uint32_t it_Index : l_Primitive.Indices)
            {
                l_Data.Indices.push_back(static_cast<std::uint32_t>(l_Base + it_Index));
            }
        }

        Optimize(l_Data);

        return l_Mesh;
    }

    // Parents before children from the default scene's roots, or every node no other node holds when the model has no scenes. A node reached twice is created once
    Trinity::ModelData BuildHierarchy(const fastgltf::Asset& asset, std::span<const Trinity::UUID> meshes, std::span<const std::vector<Trinity::UUID>> meshMaterials)
    {
        std::vector<std::size_t> l_Roots;
        if (!asset.scenes.empty())
        {
            const std::size_t l_Scene = asset.defaultScene.value_or(0) < asset.scenes.size() ? asset.defaultScene.value_or(0) : 0;
            l_Roots.assign(asset.scenes[l_Scene].nodeIndices.begin(), asset.scenes[l_Scene].nodeIndices.end());
        }
        else
        {
            std::vector<bool> l_IsChild(asset.nodes.size(), false);
            for (const fastgltf::Node& it_Node : asset.nodes)
            {
                for (const std::size_t it_Child : it_Node.children)
                {
                    if (it_Child < l_IsChild.size())
                    {
                        l_IsChild[it_Child] = true;
                    }
                }
            }

            for (std::size_t it_Node = 0; it_Node < asset.nodes.size(); ++it_Node)
            {
                if (!l_IsChild[it_Node])
                {
                    l_Roots.push_back(it_Node);
                }
            }
        }

        Trinity::ModelData l_Model;
        std::vector<bool> l_Visited(asset.nodes.size(), false);
        std::vector<std::pair<std::size_t, std::int32_t>> l_Stack;
        for (auto it_Root = l_Roots.rbegin(); it_Root != l_Roots.rend(); ++it_Root)
        {
            l_Stack.emplace_back(*it_Root, Trinity::ModelNode::c_NoParent);
        }

        while (!l_Stack.empty())
        {
            const auto [l_NodeIndex, l_Parent] = l_Stack.back();
            l_Stack.pop_back();
            if (l_NodeIndex >= asset.nodes.size() || l_Visited[l_NodeIndex])
            {
                continue;
            }

            l_Visited[l_NodeIndex] = true;
            const fastgltf::Node& l_Source = asset.nodes[l_NodeIndex];
            const std::optional<std::size_t> l_Mesh = l_Source.meshIndex.has_value() && l_Source.meshIndex.value() < meshes.size() ? std::optional(l_Source.meshIndex.value()) : std::nullopt;

            Trinity::ModelNode l_Node;
            l_Node.Name = !l_Source.name.empty() ? std::string(std::string_view(l_Source.name)) : (l_Mesh && !asset.meshes[*l_Mesh].name.empty() ? std::string(std::string_view(asset.meshes[*l_Mesh].name)) : std::format("Node {}", l_NodeIndex));
            l_Node.Parent = l_Parent;
            l_Node.SourceNode = static_cast<std::uint32_t>(l_NodeIndex);
            if (const auto* l_TRS = std::get_if<fastgltf::TRS>(&l_Source.transform))
            {
                l_Node.Translation = glm::vec3(l_TRS->translation[0], l_TRS->translation[1], l_TRS->translation[2]);
                l_Node.Rotation = glm::quat(l_TRS->rotation[3], l_TRS->rotation[0], l_TRS->rotation[1], l_TRS->rotation[2]);
                l_Node.Scale = glm::vec3(l_TRS->scale[0], l_TRS->scale[1], l_TRS->scale[2]);
            }
            else if (const auto* l_Matrix = std::get_if<fastgltf::math::fmat4x4>(&l_Source.transform))
            {
                glm::mat4 l_Value(1.0f);
                for (glm::length_t it_Column = 0; it_Column < 4; ++it_Column)
                {
                    for (glm::length_t it_Row = 0; it_Row < 4; ++it_Row)
                    {
                        l_Value[it_Column][it_Row] = (*l_Matrix)[static_cast<std::size_t>(it_Column)][static_cast<std::size_t>(it_Row)];
                    }
                }

                Trinity::TransformComponent l_Transform;
                l_Transform.SetMatrix(l_Value);
                l_Node.Translation = l_Transform.Position;
                l_Node.Rotation = l_Transform.Rotation;
                l_Node.Scale = l_Transform.Scale;
            }

            if (l_Mesh && meshes[*l_Mesh].IsValid())
            {
                l_Node.Mesh = meshes[*l_Mesh];
                l_Node.Materials = meshMaterials[*l_Mesh];
            }

            const std::int32_t l_Index = static_cast<std::int32_t>(l_Model.Nodes.size());
            l_Model.Nodes.push_back(std::move(l_Node));
            for (std::size_t it_Child = l_Source.children.size(); it_Child > 0; --it_Child)
            {
                l_Stack.emplace_back(l_Source.children[it_Child - 1], l_Index);
            }
        }

        return l_Model;
    }

    Trinity::MaterialAlphaMode ToAlphaMode(fastgltf::AlphaMode mode)
    {
        switch (mode)
        {
            case fastgltf::AlphaMode::Mask:
            {
                return Trinity::MaterialAlphaMode::Mask;
            }
            case fastgltf::AlphaMode::Blend:
            {
                return Trinity::MaterialAlphaMode::Blend;
            }
            default:
            {
                return Trinity::MaterialAlphaMode::Opaque;
            }
        }
    }

    TextureImportSettings GetTextureSettings(const fastgltf::Asset& asset, const fastgltf::Texture& texture, ModelImporter::TextureUsage usage)
    {
        TextureImportSettings l_Settings;
        l_Settings.Srgb = usage == ModelImporter::TextureUsage::Color;
        l_Settings.NormalMap = usage == ModelImporter::TextureUsage::Normal;
        if (texture.samplerIndex.has_value() && texture.samplerIndex.value() < asset.samplers.size() && asset.samplers[texture.samplerIndex.value()].magFilter.value_or(fastgltf::Filter::Linear) == fastgltf::Filter::Nearest)
        {
            l_Settings.Filter = Trinity::RHI::Filter::Nearest;
        }

        return l_Settings;
    }
}

std::string ModelImporter::GetCacheKeyPath(Trinity::UUID id)
{
    return std::format("{}/Models/{}.key", Trinity::Project::c_CacheMount, id);
}

// The model's files, its sub-assets' keys and UUIDs, the textures beside it, and both importers' versions, since a texture it embeds is encoded as the texture importer encodes
std::string ModelImporter::GetCacheKey(const Trinity::AssetRecord& record, const Plan& plan, std::span<const Trinity::UUID> externalTextures)
{
    std::uint64_t l_SubAssets = Hash(std::span<const std::byte>());
    for (const Trinity::SubAsset& it_SubAsset : record.SubAssets)
    {
        l_SubAssets = Hash(std::format("{}/{}/{};", it_SubAsset.Key, it_SubAsset.Importer, it_SubAsset.ID), l_SubAssets);
    }

    std::uint64_t l_Textures = Hash(std::span<const std::byte>());
    for (const Trinity::UUID it_Texture : externalTextures)
    {
        l_Textures = Hash(std::format("{};", it_Texture), l_Textures);
    }

    return std::format("content {:016x}, sub-assets {:016x}, textures {:016x}, importer {}, texture importer {}", plan.ContentHash, l_SubAssets, l_Textures, c_Version, TextureImporter::c_Version);
}

std::string_view ModelImporter::ToString(TextureUsage usage)
{
    switch (usage)
    {
        case TextureUsage::Color:
        {
            return "Color";
        }
        case TextureUsage::Data:
        {
            return "Data";
        }
        case TextureUsage::Normal:
        {
            return "Normal";
        }
    }

    return "Unknown";
}

std::string ModelImporter::GetCookedPath(const Trinity::SubAsset& subAsset)
{
    if (subAsset.Importer == Trinity::MeshAsset::c_AssetType)
    {
        return Trinity::GetCookedMeshPath(subAsset.ID);
    }

    if (subAsset.Importer == c_MaterialImporter)
    {
        return Trinity::GetCookedMaterialPath(subAsset.ID);
    }

    return subAsset.Importer == Trinity::TextureAsset::c_AssetType ? Trinity::GetCookedTexturePath(subAsset.ID) : std::string();
}

// The file and every buffer beside it are read, and checked as far as fastgltf trusts them, so cooking reads nothing it has not checked. Images beside it are only named, since they are textures of their own
ModelImporter::Plan ModelImporter::PlanImport(const Trinity::AssetRecord& record)
{
    TR_PROFILE_FUNCTION();

    Plan l_Plan;
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_File = Trinity::FileSystem::ReadFile(record.Path);
    if (!l_File)
    {
        TR_ERROR("Models: {} could not be read: {}", record.Path, Trinity::ToString(l_File.GetError()));

        return l_Plan;
    }

    fastgltf::Expected<fastgltf::GltfDataBuffer> l_Data = fastgltf::GltfDataBuffer::FromBytes(l_File->data(), l_File->size());
    if (l_Data.error() != fastgltf::Error::None)
    {
        TR_ERROR("Models: {} could not be read: {}", record.Path, fastgltf::getErrorMessage(l_Data.error()));

        return l_Plan;
    }

    const fastgltf::Extensions l_Extensions = fastgltf::Extensions::KHR_materials_emissive_strength | fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_transform;
    fastgltf::Parser l_Parser(l_Extensions);
    fastgltf::Expected<fastgltf::Asset> l_Parsed = l_Parser.loadGltf(l_Data.get(), {}, fastgltf::Options::DecomposeNodeMatrices);
    if (l_Parsed.error() != fastgltf::Error::None)
    {
        TR_ERROR("Models: {} is not a glTF that can be imported: {} ({})", record.Path, fastgltf::getErrorMessage(l_Parsed.error()), fastgltf::getErrorName(l_Parsed.error()));

        return l_Plan;
    }

    const std::shared_ptr<Source> l_Source = std::make_shared<Source>();
    l_Source->Asset = std::move(l_Parsed.get());
    fastgltf::Asset& l_Asset = l_Source->Asset;

    std::uint64_t l_Hash = Hash(*l_File);
    for (std::size_t it_Buffer = 0; it_Buffer < l_Asset.buffers.size(); ++it_Buffer)
    {
        fastgltf::Buffer& l_Buffer = l_Asset.buffers[it_Buffer];
        if (const auto* l_URI = std::get_if<fastgltf::sources::URI>(&l_Buffer.data))
        {
            const std::optional<std::string> l_Path = l_URI->uri.isLocalPath() ? ResolvePath(record.Path, l_URI->uri.path()) : std::nullopt;
            const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Bytes = l_Path ? Trinity::FileSystem::ReadFile(*l_Path) : Trinity::Expected<Trinity::FileBuffer, Trinity::FileError>(Trinity::Unexpected{ Trinity::FileError::NotFound });
            if (!l_Bytes || l_Bytes->size() < l_URI->fileByteOffset || l_Bytes->size() - l_URI->fileByteOffset < l_Buffer.byteLength)
            {
                TR_ERROR("Models: {} names {} for buffer {}, which is not a file of at least {} bytes beside it", record.Path, l_URI->uri.string(), it_Buffer, l_Buffer.byteLength);

                return l_Plan;
            }

            const auto a_First = l_Bytes->begin() + static_cast<std::ptrdiff_t>(l_URI->fileByteOffset);
            std::vector<std::byte> l_Contents(a_First, a_First + static_cast<std::ptrdiff_t>(l_Buffer.byteLength));
            l_Hash = Hash(l_Contents, l_Hash);
            l_Buffer.data = fastgltf::sources::Vector{ std::move(l_Contents), fastgltf::MimeType::GltfBuffer };
        }

        if (GetBytes(l_Buffer.data).size() < l_Buffer.byteLength)
        {
            TR_ERROR("Models: buffer {} of {} holds fewer bytes than it says, or none", it_Buffer, record.Path);

            return l_Plan;
        }
    }

    for (std::size_t it_View = 0; it_View < l_Asset.bufferViews.size(); ++it_View)
    {
        const fastgltf::BufferView& l_View = l_Asset.bufferViews[it_View];
        const std::size_t l_Size = l_View.bufferIndex < l_Asset.buffers.size() ? l_Asset.buffers[l_View.bufferIndex].byteLength : 0;
        if (l_View.byteOffset > l_Size || l_View.byteLength > l_Size - l_View.byteOffset)
        {
            TR_ERROR("Models: buffer view {} of {} reaches past its buffer", it_View, record.Path);

            return l_Plan;
        }
    }

    l_Source->EmbeddedImages.resize(l_Asset.images.size());
    for (std::size_t it_Image = 0; it_Image < l_Asset.images.size(); ++it_Image)
    {
        const fastgltf::DataSource& l_Image = l_Asset.images[it_Image].data;
        if (const auto* l_View = std::get_if<fastgltf::sources::BufferView>(&l_Image); l_View != nullptr && l_View->bufferViewIndex < l_Asset.bufferViews.size())
        {
            const fastgltf::BufferView& l_BufferView = l_Asset.bufferViews[l_View->bufferViewIndex];
            l_Source->EmbeddedImages[it_Image] = GetBytes(l_Asset.buffers[l_BufferView.bufferIndex].data).subspan(l_BufferView.byteOffset, l_BufferView.byteLength);
        }
        else
        {
            l_Source->EmbeddedImages[it_Image] = GetBytes(l_Image);
        }
    }

    l_Source->MeshKeys.resize(l_Asset.meshes.size());
    for (std::size_t it_Mesh = 0; it_Mesh < l_Asset.meshes.size(); ++it_Mesh)
    {
        const fastgltf::Mesh& l_Mesh = l_Asset.meshes[it_Mesh];
        if (std::ranges::any_of(l_Mesh.primitives, IsImported))
        {
            l_Source->MeshKeys[it_Mesh] = MakeKey("Mesh", it_Mesh, {}, l_Mesh.name);
            l_Plan.SubAssets.push_back({ l_Source->MeshKeys[it_Mesh], std::string(Trinity::MeshAsset::c_AssetType), {} });
        }
        else
        {
            TR_WARN("Models: mesh {} of {} has no triangles, so it is left out", it_Mesh, record.Path);
        }
    }

    bool l_Transformed = false;
    const auto a_Use = [&](const fastgltf::TextureInfo& info, TextureUsage usage)
    {
        l_Transformed = l_Transformed || info.transform != nullptr;
        const std::pair<std::size_t, TextureUsage> l_Use{ info.textureIndex, usage };
        if (l_Source->Textures.contains(l_Use))
        {
            return;
        }

        const fastgltf::Texture* l_Texture = info.textureIndex < l_Asset.textures.size() ? &l_Asset.textures[info.textureIndex] : nullptr;
        const std::size_t l_ImageIndex = l_Texture != nullptr ? l_Texture->imageIndex.value_or(l_Asset.images.size()) : l_Asset.images.size();
        if (l_ImageIndex >= l_Asset.images.size())
        {
            TR_WARN("Models: texture {} of {} has no PNG or JPEG image, so materials using it go without", info.textureIndex, record.Path);

            return;
        }

        const fastgltf::Image& l_Image = l_Asset.images[l_ImageIndex];
        if (const auto* l_URI = std::get_if<fastgltf::sources::URI>(&l_Image.data))
        {
            const std::optional<std::string> l_Path = l_URI->uri.isLocalPath() ? ResolvePath(record.Path, l_URI->uri.path()) : std::nullopt;
            if (!l_Path)
            {
                TR_WARN("Models: {} names {} for image {}, which is not a file inside the project's Assets, so materials using it go without", record.Path, l_URI->uri.string(), l_ImageIndex);

                return;
            }

            const auto a_Existing = std::ranges::find(l_Plan.ExternalTextures, *l_Path, &ExternalTexture::Path);
            if (a_Existing != l_Plan.ExternalTextures.end() && a_Existing->Usage != usage)
            {
                TR_WARN("Models: {} uses {} as {} and as {}, and it can be encoded only one way, so it stays {}", record.Path, *l_Path, ToString(a_Existing->Usage), ToString(usage), ToString(a_Existing->Usage));
            }

            if (a_Existing == l_Plan.ExternalTextures.end())
            {
                l_Plan.ExternalTextures.push_back({ *l_Path, usage });
            }

            l_Source->Textures[l_Use] = { {}, static_cast<std::size_t>(std::ranges::find(l_Plan.ExternalTextures, *l_Path, &ExternalTexture::Path) - l_Plan.ExternalTextures.begin()) };

            return;
        }

        if (l_Source->EmbeddedImages[l_ImageIndex].empty())
        {
            TR_WARN("Models: image {} of {} could not be found in the file, so materials using it go without", l_ImageIndex, record.Path);

            return;
        }

        const std::string_view l_Name = !l_Texture->name.empty() ? std::string_view(l_Texture->name) : std::string_view(l_Image.name);
        l_Source->Textures[l_Use] = { MakeKey("Texture", info.textureIndex, ToString(usage), l_Name), 0 };
    };

    l_Source->MaterialKeys.resize(l_Asset.materials.size());
    for (std::size_t it_Material = 0; it_Material < l_Asset.materials.size(); ++it_Material)
    {
        const fastgltf::Material& l_Material = l_Asset.materials[it_Material];
        l_Source->MaterialKeys[it_Material] = MakeKey("Material", it_Material, {}, l_Material.name);
        l_Plan.SubAssets.push_back({ l_Source->MaterialKeys[it_Material], std::string(c_MaterialImporter), {} });

        if (l_Material.pbrData.baseColorTexture)
        {
            a_Use(*l_Material.pbrData.baseColorTexture, TextureUsage::Color);
        }

        if (l_Material.pbrData.metallicRoughnessTexture)
        {
            a_Use(*l_Material.pbrData.metallicRoughnessTexture, TextureUsage::Data);
        }

        if (l_Material.normalTexture)
        {
            a_Use(*l_Material.normalTexture, TextureUsage::Normal);
        }

        if (l_Material.occlusionTexture)
        {
            a_Use(*l_Material.occlusionTexture, TextureUsage::Data);
        }

        if (l_Material.emissiveTexture)
        {
            a_Use(*l_Material.emissiveTexture, TextureUsage::Color);
        }
    }

    for (const auto& [it_Use, it_Texture] : l_Source->Textures)
    {
        if (!it_Texture.Key.empty())
        {
            l_Plan.SubAssets.push_back({ it_Texture.Key, std::string(Trinity::TextureAsset::c_AssetType), {} });
        }
    }

    if (l_Transformed)
    {
        TR_WARN("Models: {} moves or scales some textures with KHR_texture_transform, which is not imported yet, so they are sampled as they are", record.Path);
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_CachedKey = Trinity::FileSystem::ReadText(GetCacheKeyPath(record.ID));
    l_Plan.CachedKey = l_CachedKey ? *l_CachedKey : std::string();
    l_Plan.CookedFilesExist = Trinity::FileSystem::Exists(Trinity::GetCookedModelPath(record.ID)) && std::ranges::all_of(record.SubAssets, [](const Trinity::SubAsset& subAsset)
    {
        const std::string l_Path = GetCookedPath(subAsset);

        return !l_Path.empty() && Trinity::FileSystem::Exists(l_Path);
    });

    l_Plan.ContentHash = l_Hash;
    l_Plan.Model = l_Source;
    l_Plan.Read = true;

    return l_Plan;
}

// Embedded textures first, since they take the time, each skipped when its own key says the cache already holds it. The key is written last, so a cook cut short is cooked again
ModelImporter::Cooked ModelImporter::Cook(const Trinity::AssetRecord& record, const Plan& plan, std::span<const Trinity::UUID> externalTextures, const std::atomic<bool>& stop)
{
    TR_PROFILE_FUNCTION();

    const auto l_Start = std::chrono::steady_clock::now();
    const Source& l_Source = *plan.Model;
    const fastgltf::Asset& l_Asset = l_Source.Asset;

    Cooked l_Cooked;
    bool l_Failed = false;
    const auto a_Find = [&record](std::string_view key)
    {
        const auto a_Found = std::ranges::find(record.SubAssets, key, &Trinity::SubAsset::Key);

        return a_Found != record.SubAssets.end() ? a_Found->ID : Trinity::UUID();
    };

    const auto a_Write = [&](Trinity::UUID id, const std::string& path, std::span<const std::byte> bytes)
    {
        const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteFile(path, bytes);
        if (!l_Written)
        {
            TR_ERROR("Models: {} could not be written for {}: {}", path, record.Path, Trinity::ToString(l_Written.GetError()));
            l_Failed = true;

            return false;
        }

        if (id.IsValid())
        {
            l_Cooked.Written.push_back(id);
        }

        return true;
    };

    for (const auto& [it_Use, it_Texture] : l_Source.Textures)
    {
        if (stop.load(std::memory_order_relaxed))
        {
            l_Cooked.Outcome = Result::Stopped;

            return l_Cooked;
        }

        const Trinity::UUID l_ID = a_Find(it_Texture.Key);
        if (it_Texture.Key.empty() || !l_ID.IsValid())
        {
            continue;
        }

        const fastgltf::Texture& l_Texture = l_Asset.textures[it_Use.first];
        const std::span<const std::byte> l_Image = l_Source.EmbeddedImages[l_Texture.imageIndex.value()];
        const TextureImportSettings l_Settings = GetTextureSettings(l_Asset, l_Texture, it_Use.second);
        const std::string l_Key = TextureImporter::GetCacheKey(l_Image, l_Settings);
        if (TextureImporter::IsCached(l_ID, l_Key))
        {
            ++l_Cooked.TexturesCached;

            continue;
        }

        const Trinity::Expected<TextureImporter::EncodedTexture, std::string> l_Encoded = TextureImporter::Encode(l_Image, l_Settings);
        if (!l_Encoded)
        {
            TR_ERROR("Models: {} of {} was not imported, since {}", it_Texture.Key, record.Path, l_Encoded.GetError());
            l_Failed = true;

            continue;
        }

        if (a_Write(l_ID, Trinity::GetCookedTexturePath(l_ID), l_Encoded->File) && a_Write({}, TextureImporter::GetCacheKeyPath(l_ID), std::as_bytes(std::span(l_Key))))
        {
            ++l_Cooked.TexturesEncoded;
        }
    }

    std::vector<Trinity::UUID> l_Materials(l_Source.MaterialKeys.size());
    std::ranges::transform(l_Source.MaterialKeys, l_Materials.begin(), a_Find);

    std::vector<Trinity::UUID> l_Meshes(l_Source.MeshKeys.size());
    std::vector<std::vector<Trinity::UUID>> l_MeshMaterials(l_Source.MeshKeys.size());
    for (std::size_t it_Mesh = 0; it_Mesh < l_Source.MeshKeys.size(); ++it_Mesh)
    {
        const Trinity::UUID l_ID = a_Find(l_Source.MeshKeys[it_Mesh]);
        if (!l_ID.IsValid())
        {
            continue;
        }

        const Trinity::Expected<BuiltMesh, std::string> l_Mesh = BuildMesh(l_Asset, l_Asset.meshes[it_Mesh]);
        const Trinity::Expected<std::vector<std::byte>, std::string> l_File = l_Mesh ? Trinity::CookMesh(l_Mesh->Data) : Trinity::Expected<std::vector<std::byte>, std::string>(Trinity::Unexpected{ l_Mesh.GetError() });
        if (!l_File)
        {
            TR_ERROR("Models: {} of {} was not imported, since {}", l_Source.MeshKeys[it_Mesh], record.Path, l_File.GetError());
            l_Failed = true;

            continue;
        }

        if (a_Write(l_ID, Trinity::GetCookedMeshPath(l_ID), *l_File))
        {
            l_Meshes[it_Mesh] = l_ID;
            for (const std::optional<std::size_t>& it_Material : l_Mesh->SlotMaterials)
            {
                l_MeshMaterials[it_Mesh].push_back(it_Material ? l_Materials[*it_Material] : Trinity::UUID());
            }
        }
    }

    for (std::size_t it_Material = 0; it_Material < l_Asset.materials.size(); ++it_Material)
    {
        const fastgltf::Material& l_Gltf = l_Asset.materials[it_Material];
        const auto a_Texture = [&](const auto& info, TextureUsage usage)
        {
            if (!info)
            {
                return Trinity::MaterialTexture();
            }

            const auto a_Use = l_Source.Textures.find({ info->textureIndex, usage });
            if (a_Use == l_Source.Textures.end())
            {
                return Trinity::MaterialTexture();
            }

            const Trinity::UUID l_ID = a_Use->second.Key.empty() ? externalTextures[a_Use->second.External] : a_Find(a_Use->second.Key);

            return Trinity::MaterialTexture{ l_ID, static_cast<std::uint32_t>(info->texCoordIndex) };
        };

        Trinity::MaterialData l_Material;
        const auto& l_Factor = l_Gltf.pbrData.baseColorFactor;
        l_Material.BaseColorFactor = glm::vec4(l_Factor[0], l_Factor[1], l_Factor[2], l_Factor[3]);
        l_Material.MetallicFactor = l_Gltf.pbrData.metallicFactor;
        l_Material.RoughnessFactor = l_Gltf.pbrData.roughnessFactor;
        l_Material.EmissiveFactor = glm::vec3(l_Gltf.emissiveFactor[0], l_Gltf.emissiveFactor[1], l_Gltf.emissiveFactor[2]);
        l_Material.EmissiveStrength = l_Gltf.emissiveStrength;
        l_Material.NormalScale = l_Gltf.normalTexture ? l_Gltf.normalTexture->scale : 1.0f;
        l_Material.OcclusionStrength = l_Gltf.occlusionTexture ? l_Gltf.occlusionTexture->strength : 1.0f;
        l_Material.AlphaMode = ToAlphaMode(l_Gltf.alphaMode);
        l_Material.AlphaCutoff = l_Gltf.alphaCutoff;
        l_Material.DoubleSided = l_Gltf.doubleSided;
        l_Material.BaseColorTexture = a_Texture(l_Gltf.pbrData.baseColorTexture, TextureUsage::Color);
        l_Material.MetallicRoughnessTexture = a_Texture(l_Gltf.pbrData.metallicRoughnessTexture, TextureUsage::Data);
        l_Material.NormalTexture = a_Texture(l_Gltf.normalTexture, TextureUsage::Normal);
        l_Material.OcclusionTexture = a_Texture(l_Gltf.occlusionTexture, TextureUsage::Data);
        l_Material.EmissiveTexture = a_Texture(l_Gltf.emissiveTexture, TextureUsage::Color);

        const std::string l_Text = Trinity::WriteMaterialData(l_Material);
        static_cast<void>(a_Write(l_Materials[it_Material], Trinity::GetCookedMaterialPath(l_Materials[it_Material]), std::as_bytes(std::span(l_Text))));
    }

    const Trinity::ModelData l_Model = BuildHierarchy(l_Asset, l_Meshes, l_MeshMaterials);
    const std::string l_ModelText = Trinity::WriteModelData(l_Model);
    static_cast<void>(a_Write({}, Trinity::GetCookedModelPath(record.ID), std::as_bytes(std::span(l_ModelText))));

    if (l_Failed)
    {
        TR_ERROR("Models: {} was not fully imported, and is imported again at the next refresh", record.Path);

        return l_Cooked;
    }

    const std::string l_Key = GetCacheKey(record, plan, externalTextures);
    l_Cooked.Outcome = a_Write({}, GetCacheKeyPath(record.ID), std::as_bytes(std::span(l_Key))) ? Result::Imported : Result::Failed;

    const auto l_Milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - l_Start).count();
    TR_INFO("Models: imported {} into {} mesh(es), {} material(s) and {} node(s), with {} texture(s) encoded, {} from the cache and {} beside it, in {} ms", record.Path, std::ranges::count_if(l_Meshes, [](Trinity::UUID id) { return id.IsValid(); }), l_Materials.size(), l_Model.Nodes.size(), l_Cooked.TexturesEncoded, l_Cooked.TexturesCached, externalTextures.size(), l_Milliseconds);

    return l_Cooked;
}