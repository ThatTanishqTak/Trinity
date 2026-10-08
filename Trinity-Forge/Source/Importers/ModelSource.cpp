#include "Importers/ModelSource.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace
{
    constexpr std::size_t c_MaxNameLength = 48;

    bool IsKeyCharacter(char character)
    {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_' || character == '-' || character == ' ' || character == '(' || character == ')';
    }

    // Each part of the path onto the parts so far, either slash dividing them. A parent part takes one away, and is refused when allowParent is off or it would leave the mount the path starts in
    bool AppendParts(std::vector<std::string>& parts, std::string_view path, bool allowParent)
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
                if (!allowParent || parts.size() <= 1)
                {
                    return false;
                }

                parts.pop_back();

                continue;
            }

            parts.emplace_back(l_Part);
        }

        return true;
    }

    std::string JoinParts(const std::vector<std::string>& parts)
    {
        std::string l_Path;
        for (const std::string& it_Part : parts)
        {
            l_Path += '/';
            l_Path += it_Part;
        }

        return l_Path;
    }

    glm::vec3 GetPerpendicular(glm::vec3 normal)
    {
        const glm::vec3 l_Axis = std::abs(normal.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 l_Perpendicular = glm::cross(l_Axis, normal);
        const float l_Length = glm::length(l_Perpendicular);

        return l_Length > 0.0f ? l_Perpendicular / l_Length : glm::vec3(1.0f, 0.0f, 0.0f);
    }

    // Each triangle's tangent and bitangent from its UVs, summed at its corners and made perpendicular to each normal. V runs down the image, as in glTF, and up in tangent space, so it is flipped, as glTF's own tangents are made
    std::vector<glm::vec4> GenerateTangents(const ModelReading::Primitive& primitive, const std::vector<glm::vec2>& texCoords)
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
}

namespace ModelReading
{
    // FNV-1a, continued from a previous hash so several files make one
    std::uint64_t Hash(std::span<const std::byte> bytes, std::uint64_t hash)
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

    // The index in the file, then how it is used, then its name in characters that read plainly in a .meta and after the '#' of a path
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

    // A relative path from the model's folder, refused when it is absolute or leaves the mount it starts in
    std::optional<std::string> ResolvePath(std::string_view modelPath, std::string_view uri)
    {
        if (uri.empty() || uri.starts_with('/') || uri.starts_with('\\') || uri.find(':') != std::string_view::npos)
        {
            return std::nullopt;
        }

        std::vector<std::string> l_Parts;
        if (!AppendParts(l_Parts, modelPath.substr(0, modelPath.find_last_of('/')), false) || !AppendParts(l_Parts, uri, true) || l_Parts.size() <= 1)
        {
            return std::nullopt;
        }

        return JoinParts(l_Parts);
    }

    // A path from the root of the file system with either slash, '.' and '..' taken out, refused when it leaves the mount it starts in
    std::optional<std::string> NormalizePath(std::string_view path)
    {
        std::vector<std::string> l_Parts;
        if (path.empty() || (path.front() != '/' && path.front() != '\\') || !AppendParts(l_Parts, path, true) || l_Parts.size() <= 1)
        {
            return std::nullopt;
        }

        return JoinParts(l_Parts);
    }

    // One entry for each file, encoded the way its first use asks, which a later use the other way is warned of
    std::size_t AddExternalTexture(ModelImporter::Plan& plan, const std::string& modelPath, const std::string& texturePath, ModelImporter::TextureUsage usage)
    {
        const auto a_Existing = std::ranges::find(plan.ExternalTextures, texturePath, &ModelImporter::ExternalTexture::Path);
        if (a_Existing != plan.ExternalTextures.end())
        {
            if (a_Existing->Usage != usage)
            {
                TR_WARN("Models: {} uses {} as {} and as {}, and it can be encoded only one way, so it stays {}", modelPath, texturePath, ModelImporter::ToString(a_Existing->Usage), ModelImporter::ToString(usage), ModelImporter::ToString(a_Existing->Usage));
            }

            return static_cast<std::size_t>(a_Existing - plan.ExternalTextures.begin());
        }

        plan.ExternalTextures.push_back({ texturePath, usage });

        return plan.ExternalTextures.size() - 1;
    }

    // Normals made flat where the part has none, which needs a vertex for each corner, then unit length. Tangents are made where it has none, from the UVs the normal map uses, and every tangent is made perpendicular to its normal, since exporters leave them otherwise and the octahedral encoding cannot keep them
    void FinishPrimitive(Primitive& primitive)
    {
        if (primitive.Normals.empty())
        {
            const auto a_Unweld = [&primitive](auto& stream)
            {
                if (stream.empty())
                {
                    return;
                }

                std::remove_reference_t<decltype(stream)> l_Corners;
                l_Corners.reserve(primitive.Indices.size());
                for (const std::uint32_t it_Index : primitive.Indices)
                {
                    l_Corners.push_back(stream[it_Index]);
                }

                stream = std::move(l_Corners);
            };

            a_Unweld(primitive.Positions);
            a_Unweld(primitive.Tangents);
            a_Unweld(primitive.TexCoords0);
            a_Unweld(primitive.TexCoords1);
            a_Unweld(primitive.Colors);
            std::iota(primitive.Indices.begin(), primitive.Indices.end(), 0u);

            primitive.Normals.resize(primitive.Positions.size());
            for (std::size_t it_Corner = 0; it_Corner + 2 < primitive.Positions.size(); it_Corner += 3)
            {
                const glm::vec3 l_Cross = glm::cross(primitive.Positions[it_Corner + 1] - primitive.Positions[it_Corner], primitive.Positions[it_Corner + 2] - primitive.Positions[it_Corner]);
                const float l_Length = glm::length(l_Cross);
                const glm::vec3 l_Normal = l_Length > 0.0f ? l_Cross / l_Length : glm::vec3(0.0f, 0.0f, 1.0f);
                std::fill_n(primitive.Normals.begin() + static_cast<std::ptrdiff_t>(it_Corner), 3, l_Normal);
            }
        }

        for (glm::vec3& it_Normal : primitive.Normals)
        {
            const float l_Length = glm::length(it_Normal);
            it_Normal = l_Length > 1e-12f ? it_Normal / l_Length : glm::vec3(0.0f, 0.0f, 1.0f);
        }

        if (primitive.Tangents.empty())
        {
            const std::vector<glm::vec2>& l_TexCoords = primitive.NormalTexCoord == 1 ? primitive.TexCoords1 : primitive.TexCoords0;
            if (!l_TexCoords.empty())
            {
                primitive.Tangents = GenerateTangents(primitive, l_TexCoords);
            }
            else
            {
                std::ranges::transform(primitive.Normals, std::back_inserter(primitive.Tangents), [](const glm::vec3& normal) { return glm::vec4(GetPerpendicular(normal), 1.0f); });
            }
        }

        for (std::size_t it_Vertex = 0; it_Vertex < primitive.Tangents.size(); ++it_Vertex)
        {
            const glm::vec3 l_Normal = primitive.Normals[it_Vertex];
            const glm::vec3 l_Given(primitive.Tangents[it_Vertex]);
            const glm::vec3 l_Tangent = l_Given - l_Normal * glm::dot(l_Normal, l_Given);
            const float l_Length = glm::length(l_Tangent);
            primitive.Tangents[it_Vertex] = glm::vec4(l_Length > 1e-6f ? l_Tangent / l_Length : GetPerpendicular(l_Normal), primitive.Tangents[it_Vertex].w < 0.0f ? -1.0f : 1.0f);
        }
    }

    // A stream that only some parts have is filled for the others, with zero UVs and white
    Trinity::Expected<ModelSource::BuiltMesh, std::string> MergePrimitives(std::vector<Primitive> primitives)
    {
        if (primitives.empty())
        {
            return Trinity::Unexpected{ std::string("it has no triangles") };
        }

        const auto a_Any = [&primitives](auto member) { return std::ranges::any_of(primitives, [member](const Primitive& primitive) { return !(primitive.*member).empty(); }); };
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

        const bool l_TexCoords0 = a_Any(&Primitive::TexCoords0);
        const bool l_TexCoords1 = a_Any(&Primitive::TexCoords1);
        const bool l_Colors = a_Any(&Primitive::Colors);

        ModelSource::BuiltMesh l_Mesh;
        Trinity::MeshData& l_Data = l_Mesh.Data;
        for (std::size_t it_Slot = 0; it_Slot < primitives.size(); ++it_Slot)
        {
            const Primitive& l_Primitive = primitives[it_Slot];
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
            l_Mesh.SlotMaterials.push_back(l_Primitive.Material);

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

    // Shear, which only a parent with non-uniform scale and a rotated child makes, has no place in a node and is dropped, as a Transform drops it
    void SetTransform(Trinity::ModelNode& node, const glm::mat4& matrix)
    {
        Trinity::TransformComponent l_Transform;
        l_Transform.SetMatrix(matrix);
        node.Translation = l_Transform.Position;
        node.Rotation = l_Transform.Rotation;
        node.Scale = l_Transform.Scale;
    }
}