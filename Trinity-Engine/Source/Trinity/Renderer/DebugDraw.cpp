#include "Trinity/Renderer/DebugDraw.hpp"

#if TR_DEBUG_DRAW

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Renderer/DebugDrawBuffer.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        using VertexList = std::vector<DebugVertex, TaggedAllocator<DebugVertex, MemoryTag::Renderer>>;

        // A full circle is this many segments, and an arc its share of them
        constexpr std::uint32_t c_CircleSegments = 32;

        // Depth-tested lines, then lines on top. Each keeps its capacity from frame to frame, so drawing allocates nothing once the most lines a frame has drawn have been drawn once
        std::array<VertexList, 2> s_Vertices;
        std::array<bool, 2> s_ReportedFull{};

        std::uint32_t PackColor(const glm::vec4& color)
        {
            const auto a_Pack = [](float value) { return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };

            return a_Pack(color.r) | (a_Pack(color.g) << 8) | (a_Pack(color.b) << 16) | (a_Pack(color.a) << 24);
        }

        // Two unit vectors at right angles to the normal and to each other, with the normal itself made unit length. A zero normal is taken as +Y
        void GetBasis(const glm::vec3& normal, glm::vec3& axis, glm::vec3& first, glm::vec3& second)
        {
            const float l_Length = glm::length(normal);
            axis = l_Length > 1e-12f ? normal / l_Length : glm::vec3(0.0f, 1.0f, 0.0f);
            first = glm::normalize(glm::cross(axis, std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f)));
            second = glm::cross(axis, first);
        }

        void AddLine(const glm::vec3& from, const glm::vec3& to, std::uint32_t color, DebugDepth depth)
        {
            TR_CORE_ASSERT(MainThread::IsMainThread(), "DebugDraw is called on the main thread only.");

            const std::size_t l_List = static_cast<std::size_t>(depth);
            VertexList& l_Vertices = s_Vertices[l_List];
            if (l_Vertices.size() >= std::size_t{ DebugDraw::c_MaxLines } * 2)
            {
                if (!std::exchange(s_ReportedFull[l_List], true))
                {
                    TR_CORE_WARN("DebugDraw: more than {} {} lines this frame, so the rest are not drawn", DebugDraw::c_MaxLines, depth == DebugDepth::Test ? "depth-tested" : "on-top");
                }

                return;
            }

            l_Vertices.push_back({ from, color });
            l_Vertices.push_back({ to, color });
        }

        // Part of a circle about the centre, in the plane of two unit vectors at right angles, from one angle to another in radians, starting along the first
        void AddArc(const glm::vec3& center, const glm::vec3& first, const glm::vec3& second, float radius, float begin, float end, std::uint32_t color, DebugDepth depth)
        {
            const std::uint32_t l_Segments = std::max(1u, static_cast<std::uint32_t>(std::ceil(static_cast<float>(c_CircleSegments) * std::abs(end - begin) / glm::two_pi<float>())));
            const auto a_Point = [&](float angle) { return center + (first * std::cos(angle) + second * std::sin(angle)) * radius; };

            glm::vec3 l_Previous = a_Point(begin);
            for (std::uint32_t it_Segment = 1; it_Segment <= l_Segments; ++it_Segment)
            {
                const glm::vec3 l_Next = a_Point(begin + (end - begin) * static_cast<float>(it_Segment) / static_cast<float>(l_Segments));
                AddLine(l_Previous, l_Next, color, depth);
                l_Previous = l_Next;
            }
        }
    }

    namespace DebugDraw
    {
        void Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, DebugDepth depth)
        {
            AddLine(from, to, PackColor(color), depth);
        }

        // Corners are numbered by their bits, X in the first, so each edge joins two corners one bit apart
        void Box(const glm::mat4& transform, const glm::vec4& color, DebugDepth depth)
        {
            std::array<glm::vec3, 8> l_Corners{};
            for (std::size_t it_Corner = 0; it_Corner < l_Corners.size(); ++it_Corner)
            {
                const glm::vec4 l_Local((it_Corner & 1) != 0 ? 1.0f : -1.0f, (it_Corner & 2) != 0 ? 1.0f : -1.0f, (it_Corner & 4) != 0 ? 1.0f : -1.0f, 1.0f);
                l_Corners[it_Corner] = glm::vec3(transform * l_Local);
            }

            const std::uint32_t l_Color = PackColor(color);
            for (std::size_t it_Corner = 0; it_Corner < l_Corners.size(); ++it_Corner)
            {
                for (std::size_t it_Bit = 1; it_Bit < l_Corners.size(); it_Bit <<= 1)
                {
                    if ((it_Corner & it_Bit) == 0)
                    {
                        AddLine(l_Corners[it_Corner], l_Corners[it_Corner | it_Bit], l_Color, depth);
                    }
                }
            }
        }

        void Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color, DebugDepth depth)
        {
            glm::mat4 l_Transform(1.0f);
            l_Transform[0][0] = halfExtents.x;
            l_Transform[1][1] = halfExtents.y;
            l_Transform[2][2] = halfExtents.z;
            l_Transform[3] = glm::vec4(center, 1.0f);
            Box(l_Transform, color, depth);
        }

        void Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, DebugDepth depth)
        {
            glm::vec3 l_Axis{ 0.0f };
            glm::vec3 l_First{ 0.0f };
            glm::vec3 l_Second{ 0.0f };
            GetBasis(normal, l_Axis, l_First, l_Second);
            AddArc(center, l_First, l_Second, radius, 0.0f, glm::two_pi<float>(), PackColor(color), depth);
        }

        void Sphere(const glm::vec3& center, float radius, const glm::vec4& color, DebugDepth depth)
        {
            const std::uint32_t l_Color = PackColor(color);
            const glm::vec3 l_X(1.0f, 0.0f, 0.0f);
            const glm::vec3 l_Y(0.0f, 1.0f, 0.0f);
            const glm::vec3 l_Z(0.0f, 0.0f, 1.0f);
            AddArc(center, l_Y, l_Z, radius, 0.0f, glm::two_pi<float>(), l_Color, depth);
            AddArc(center, l_Z, l_X, radius, 0.0f, glm::two_pi<float>(), l_Color, depth);
            AddArc(center, l_X, l_Y, radius, 0.0f, glm::two_pi<float>(), l_Color, depth);
        }

        // The end caps are half circles that bulge away from the segment, in two planes through its axis. Ends that meet make a sphere
        void Capsule(const glm::vec3& start, const glm::vec3& end, float radius, const glm::vec4& color, DebugDepth depth)
        {
            const glm::vec3 l_Segment = end - start;
            if (glm::length(l_Segment) <= 1e-6f)
            {
                Sphere(start, radius, color, depth);

                return;
            }

            glm::vec3 l_Axis{ 0.0f };
            glm::vec3 l_First{ 0.0f };
            glm::vec3 l_Second{ 0.0f };
            GetBasis(l_Segment, l_Axis, l_First, l_Second);

            const std::uint32_t l_Color = PackColor(color);
            AddArc(start, l_First, l_Second, radius, 0.0f, glm::two_pi<float>(), l_Color, depth);
            AddArc(end, l_First, l_Second, radius, 0.0f, glm::two_pi<float>(), l_Color, depth);
            AddArc(end, l_First, l_Axis, radius, 0.0f, glm::pi<float>(), l_Color, depth);
            AddArc(end, l_Second, l_Axis, radius, 0.0f, glm::pi<float>(), l_Color, depth);
            AddArc(start, l_First, -l_Axis, radius, 0.0f, glm::pi<float>(), l_Color, depth);
            AddArc(start, l_Second, -l_Axis, radius, 0.0f, glm::pi<float>(), l_Color, depth);
            for (const glm::vec3& it_Side : { l_First, -l_First, l_Second, -l_Second })
            {
                AddLine(start + it_Side * radius, end + it_Side * radius, l_Color, depth);
            }
        }

        void Arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, DebugDepth depth)
        {
            const std::uint32_t l_Color = PackColor(color);
            AddLine(from, to, l_Color, depth);

            const float l_Length = glm::length(to - from);
            if (l_Length <= 1e-6f)
            {
                return;
            }

            glm::vec3 l_Axis{ 0.0f };
            glm::vec3 l_First{ 0.0f };
            glm::vec3 l_Second{ 0.0f };
            GetBasis(to - from, l_Axis, l_First, l_Second);

            const float l_Head = l_Length * 0.2f;
            const glm::vec3 l_Base = to - l_Axis * l_Head;
            for (const glm::vec3& it_Side : { l_First, -l_First, l_Second, -l_Second })
            {
                AddLine(to, l_Base + it_Side * (l_Head * 0.5f), l_Color, depth);
            }
        }

        void Cross(const glm::vec3& center, float size, const glm::vec4& color, DebugDepth depth)
        {
            const std::uint32_t l_Color = PackColor(color);
            const float l_Half = size * 0.5f;
            AddLine(center - glm::vec3(l_Half, 0.0f, 0.0f), center + glm::vec3(l_Half, 0.0f, 0.0f), l_Color, depth);
            AddLine(center - glm::vec3(0.0f, l_Half, 0.0f), center + glm::vec3(0.0f, l_Half, 0.0f), l_Color, depth);
            AddLine(center - glm::vec3(0.0f, 0.0f, l_Half), center + glm::vec3(0.0f, 0.0f, l_Half), l_Color, depth);
        }
    }

    namespace DebugDrawBuffer
    {
        std::span<const DebugVertex> GetVertices(DebugDepth depth)
        {
            return s_Vertices[static_cast<std::size_t>(depth)];
        }

        void Clear()
        {
            for (VertexList& it_Vertices : s_Vertices)
            {
                it_Vertices.clear();
            }

            s_ReportedFull = {};
        }

        void Release()
        {
            for (VertexList& it_Vertices : s_Vertices)
            {
                VertexList().swap(it_Vertices);
            }

            s_ReportedFull = {};
        }
    }
}

#endif