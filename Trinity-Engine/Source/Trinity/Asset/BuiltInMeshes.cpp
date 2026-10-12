#include "Trinity/Asset/BuiltInMeshes.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <span>
#include <utility>

namespace Trinity
{
    namespace
    {
        // "trinity" in ASCII and then the shape's number, which a random UUID is all but certain never to be
        constexpr std::uint64_t c_IDBase = 0x7472696E69747900;

        // Around Y, and from pole to pole on the sphere, half of that on each end of the capsule
        constexpr std::uint32_t c_Segments = 32;
        constexpr std::uint32_t c_Rings = 16;
        constexpr std::uint32_t c_PlaneSegments = 10;

        constexpr float c_Radius = 0.5f;
        constexpr float c_CapsuleHalfSegment = 0.5f;
        constexpr float c_CylinderHalfHeight = 0.5f;
        constexpr float c_PlaneSize = 10.0f;

        // One ring of a surface turned about Y, from the top down: its radius and height, the outward and upward parts of its normal, and V there. A ring of radius 0 is a pole
        struct Ring
        {
            float Radius = 0.0f;
            float Height = 0.0f;
            float NormalOut = 0.0f;
            float NormalUp = 0.0f;
            float V = 0.0f;
        };

        void AddVertex(MeshData& mesh, glm::vec3 position, glm::vec3 normal, glm::vec3 tangent, glm::vec2 uv)
        {
            mesh.Positions.push_back(position);
            mesh.Normals.push_back(normal);
            mesh.Tangents.emplace_back(tangent, 1.0f);
            mesh.TexCoords0.push_back(uv);
        }

        void AddTriangle(MeshData& mesh, std::uint32_t first, std::uint32_t second, std::uint32_t third)
        {
            mesh.Indices.insert(mesh.Indices.end(), { first, second, third });
        }

        // A flat grid facing the normal, with U along across and the top of a texture along cross(normal, across), which is the bitangent glTF makes from a tangent of handedness +1. Wound anticlockwise seen from the front, as glTF's are
        void AddFace(MeshData& mesh, glm::vec3 center, glm::vec3 normal, glm::vec3 across, glm::vec2 size, std::uint32_t segments)
        {
            const glm::vec3 l_Up = glm::cross(normal, across);
            const std::uint32_t l_First = static_cast<std::uint32_t>(mesh.Positions.size());
            const std::uint32_t l_Row = segments + 1;
            for (std::uint32_t it_Row = 0; it_Row <= segments; ++it_Row)
            {
                const float l_V = static_cast<float>(it_Row) / static_cast<float>(segments);
                for (std::uint32_t it_Column = 0; it_Column <= segments; ++it_Column)
                {
                    const float l_U = static_cast<float>(it_Column) / static_cast<float>(segments);
                    AddVertex(mesh, center + across * ((l_U - 0.5f) * size.x) + l_Up * ((0.5f - l_V) * size.y), normal, across, glm::vec2(l_U, l_V));
                }
            }

            for (std::uint32_t it_Row = 0; it_Row < segments; ++it_Row)
            {
                for (std::uint32_t it_Column = 0; it_Column < segments; ++it_Column)
                {
                    const std::uint32_t l_TopLeft = l_First + it_Row * l_Row + it_Column;
                    const std::uint32_t l_BottomLeft = l_TopLeft + l_Row;
                    AddTriangle(mesh, l_BottomLeft, l_BottomLeft + 1, l_TopLeft + 1);
                    AddTriangle(mesh, l_BottomLeft, l_TopLeft + 1, l_TopLeft);
                }
            }
        }

        // A disc of c_Radius facing the normal, its texture laid flat across it as AddFace lays one
        void AddDisc(MeshData& mesh, glm::vec3 center, glm::vec3 normal, glm::vec3 across)
        {
            const glm::vec3 l_Up = glm::cross(normal, across);
            const std::uint32_t l_Center = static_cast<std::uint32_t>(mesh.Positions.size());
            AddVertex(mesh, center, normal, across, glm::vec2(0.5f));
            for (std::uint32_t it_Segment = 0; it_Segment < c_Segments; ++it_Segment)
            {
                const float l_Angle = glm::two_pi<float>() * static_cast<float>(it_Segment) / static_cast<float>(c_Segments);
                const float l_Across = std::cos(l_Angle);
                const float l_Along = std::sin(l_Angle);
                AddVertex(mesh, center + (across * l_Across + l_Up * l_Along) * c_Radius, normal, across, glm::vec2(0.5f + l_Across * 0.5f, 0.5f - l_Along * 0.5f));
            }

            for (std::uint32_t it_Segment = 0; it_Segment < c_Segments; ++it_Segment)
            {
                AddTriangle(mesh, l_Center, l_Center + 1 + it_Segment, l_Center + 1 + (it_Segment + 1) % c_Segments);
            }
        }

        // Each ring gets c_Segments + 1 vertices, with the seam at -Z doubled so U runs from 0 to 1 around, facing +Z at 0.5. A pole's vertices sit in the middle of their segments' U, the last of them unused, and only the triangles that are not flat at a pole are kept
        void AddLathe(MeshData& mesh, std::span<const Ring> rings)
        {
            const std::uint32_t l_First = static_cast<std::uint32_t>(mesh.Positions.size());
            const std::uint32_t l_Row = c_Segments + 1;
            for (const Ring& it_Ring : rings)
            {
                const bool l_Pole = it_Ring.Radius == 0.0f;
                for (std::uint32_t it_Segment = 0; it_Segment <= c_Segments; ++it_Segment)
                {
                    const float l_U = std::min((static_cast<float>(it_Segment) + (l_Pole ? 0.5f : 0.0f)) / static_cast<float>(c_Segments), 1.0f);
                    const float l_Angle = glm::two_pi<float>() * l_U;
                    const glm::vec3 l_Out(-std::sin(l_Angle), 0.0f, -std::cos(l_Angle));
                    const glm::vec3 l_Tangent(-std::cos(l_Angle), 0.0f, std::sin(l_Angle));
                    AddVertex(mesh, l_Out * it_Ring.Radius + glm::vec3(0.0f, it_Ring.Height, 0.0f), glm::normalize(l_Out * it_Ring.NormalOut + glm::vec3(0.0f, it_Ring.NormalUp, 0.0f)), l_Tangent, glm::vec2(l_U, it_Ring.V));
                }
            }

            for (std::uint32_t it_Ring = 0; it_Ring + 1 < rings.size(); ++it_Ring)
            {
                const bool l_TopPole = rings[it_Ring].Radius == 0.0f;
                const bool l_BottomPole = rings[it_Ring + 1].Radius == 0.0f;
                for (std::uint32_t it_Segment = 0; it_Segment < c_Segments; ++it_Segment)
                {
                    const std::uint32_t l_TopLeft = l_First + it_Ring * l_Row + it_Segment;
                    const std::uint32_t l_BottomLeft = l_TopLeft + l_Row;
                    if (!l_BottomPole)
                    {
                        AddTriangle(mesh, l_BottomLeft, l_BottomLeft + 1, l_TopPole ? l_TopLeft : l_TopLeft + 1);
                    }

                    if (!l_TopPole)
                    {
                        AddTriangle(mesh, l_BottomLeft, l_TopLeft + 1, l_TopLeft);
                    }
                }
            }
        }

        // A ring on a sphere of c_Radius, with theta from the top pole, lifted by the height given. Exactly 0 at the poles
        Ring MakeSphereRing(float theta, float lift, float v, bool pole)
        {
            const float l_Out = pole ? 0.0f : std::sin(theta);
            const float l_Up = std::cos(theta);

            return { c_Radius * l_Out, lift + c_Radius * l_Up, l_Out, pole ? std::round(l_Up) : l_Up, v };
        }

        void MakeSphere(MeshData& mesh)
        {
            std::vector<Ring> l_Rings;
            for (std::uint32_t it_Ring = 0; it_Ring <= c_Rings; ++it_Ring)
            {
                const float l_V = static_cast<float>(it_Ring) / static_cast<float>(c_Rings);
                l_Rings.push_back(MakeSphereRing(glm::pi<float>() * l_V, 0.0f, l_V, it_Ring == 0 || it_Ring == c_Rings));
            }

            AddLathe(mesh, l_Rings);
        }

        // Two half spheres apart by the straight part, the ring at the middle of each also the end of the straight part. V follows the distance along the surface from top to bottom
        void MakeCapsule(MeshData& mesh)
        {
            const std::uint32_t l_CapRings = c_Rings / 2;
            const float l_Quarter = glm::half_pi<float>() * c_Radius;
            const float l_Length = 2.0f * l_Quarter + 2.0f * c_CapsuleHalfSegment;
            std::vector<Ring> l_Rings;
            for (std::uint32_t it_Ring = 0; it_Ring <= l_CapRings; ++it_Ring)
            {
                const float l_Fraction = static_cast<float>(it_Ring) / static_cast<float>(l_CapRings);
                l_Rings.push_back(MakeSphereRing(glm::half_pi<float>() * l_Fraction, c_CapsuleHalfSegment, l_Quarter * l_Fraction / l_Length, it_Ring == 0));
            }

            for (std::uint32_t it_Ring = 0; it_Ring <= l_CapRings; ++it_Ring)
            {
                const float l_Fraction = static_cast<float>(it_Ring) / static_cast<float>(l_CapRings);
                l_Rings.push_back(MakeSphereRing(glm::half_pi<float>() * (1.0f + l_Fraction), -c_CapsuleHalfSegment, (l_Quarter * (1.0f + l_Fraction) + 2.0f * c_CapsuleHalfSegment) / l_Length, it_Ring == l_CapRings));
            }

            AddLathe(mesh, l_Rings);
        }

        // The side as a lathe of two rings, then the caps with their own vertices, so the edges stay sharp
        void MakeCylinder(MeshData& mesh)
        {
            const std::array<Ring, 2> l_Side{ { { c_Radius, c_CylinderHalfHeight, 1.0f, 0.0f, 0.0f }, { c_Radius, -c_CylinderHalfHeight, 1.0f, 0.0f, 1.0f } } };
            AddLathe(mesh, l_Side);
            AddDisc(mesh, glm::vec3(0.0f, c_CylinderHalfHeight, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            AddDisc(mesh, glm::vec3(0.0f, -c_CylinderHalfHeight, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        }

        // Each face's texture upright on the sides, with +Y's top towards -Z and -Y's towards +Z
        void MakeCube(MeshData& mesh)
        {
            const std::array<std::pair<glm::vec3, glm::vec3>, 6> l_Faces{ {
                { glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f) },
                { glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
                { glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f) },
                { glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f) },
                { glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f) },
                { glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(-1.0f, 0.0f, 0.0f) }
            } };

            for (const auto& [it_Normal, it_Across] : l_Faces)
            {
                AddFace(mesh, it_Normal * 0.5f, it_Normal, it_Across, glm::vec2(1.0f), 1);
            }
        }
    }

    UUID GetBuiltInMeshID(BuiltInMesh mesh)
    {
        return UUID(c_IDBase + static_cast<std::uint64_t>(mesh) + 1);
    }

    std::optional<BuiltInMesh> FindBuiltInMesh(UUID id)
    {
        const std::uint64_t l_Value = id.GetValue();
        if (l_Value <= c_IDBase || l_Value > c_IDBase + c_BuiltInMeshes.size())
        {
            return std::nullopt;
        }

        return c_BuiltInMeshes[l_Value - c_IDBase - 1];
    }

    std::string_view ToString(BuiltInMesh mesh)
    {
        switch (mesh)
        {
            case BuiltInMesh::Cube:
            {
                return "Cube";
            }
            case BuiltInMesh::Sphere:
            {
                return "Sphere";
            }
            case BuiltInMesh::Capsule:
            {
                return "Capsule";
            }
            case BuiltInMesh::Cylinder:
            {
                return "Cylinder";
            }
            case BuiltInMesh::Plane:
            {
                return "Plane";
            }
            case BuiltInMesh::Quad:
            {
                return "Quad";
            }
        }

        return "Unknown";
    }

    MeshData MakeBuiltInMesh(BuiltInMesh mesh)
    {
        MeshData l_Mesh;
        switch (mesh)
        {
            case BuiltInMesh::Cube:
            {
                MakeCube(l_Mesh);

                break;
            }
            case BuiltInMesh::Sphere:
            {
                MakeSphere(l_Mesh);

                break;
            }
            case BuiltInMesh::Capsule:
            {
                MakeCapsule(l_Mesh);

                break;
            }
            case BuiltInMesh::Cylinder:
            {
                MakeCylinder(l_Mesh);

                break;
            }
            case BuiltInMesh::Plane:
            {
                AddFace(l_Mesh, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(c_PlaneSize), c_PlaneSegments);

                break;
            }
            case BuiltInMesh::Quad:
            {
                AddFace(l_Mesh, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(1.0f), 1);

                break;
            }
        }

        return l_Mesh;
    }

    Expected<std::vector<std::byte>, std::string> CookBuiltInMesh(UUID id)
    {
        const std::optional<BuiltInMesh> l_Mesh = FindBuiltInMesh(id);
        if (!l_Mesh)
        {
            return Unexpected{ std::format("{} is not a built-in mesh", id) };
        }

        return CookMesh(MakeBuiltInMesh(*l_Mesh));
    }
}