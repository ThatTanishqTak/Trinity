#pragma once

#include "Trinity/Core/Export.hpp"

#include <glm/glm.hpp>

#include <cstdint>

// On everywhere but Distribution, where every DebugDraw call is an empty inline function and the Renderer draws no debug lines
#if defined(TR_DISTRIBUTION)
#define TR_DEBUG_DRAW 0
#else
#define TR_DEBUG_DRAW 1
#endif

namespace Trinity
{
    // Whether a debug line hides behind the scene's meshes, or shows over everything
    enum class DebugDepth : std::uint8_t
    {
        Test,
        OnTop
    };

    // Lines in world space for seeing what code is doing, such as colliders, contacts and joints. Called on the main thread before the frame graph is built, as in OnUpdate, OnImGuiRender or OnPrepareRender, they are drawn that frame into every view whose SceneOptions::DebugLines is on, then cleared. They go over the view's tonemapped image, after its sprites and selection outline, so colours are sRGB with alpha, as they show. Curves are drawn as straight segments
    namespace DebugDraw
    {
        // Lines past this many in a frame, for each depth mode, are dropped with one warning
        constexpr std::uint32_t c_MaxLines = 1u << 18;

#if TR_DEBUG_DRAW
        TRINITY_API void Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, DebugDepth depth = DebugDepth::Test);
        // The box between -1 and 1 on each axis under a transform, such as a box collider's offset under its entity's world matrix
        TRINITY_API void Box(const glm::mat4& transform, const glm::vec4& color, DebugDepth depth = DebugDepth::Test);
        // An axis-aligned box, by its centre and half extents
        TRINITY_API void Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color, DebugDepth depth = DebugDepth::Test);
        TRINITY_API void Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, DebugDepth depth = DebugDepth::Test);
        // A circle about each axis
        TRINITY_API void Sphere(const glm::vec3& center, float radius, const glm::vec4& color, DebugDepth depth = DebugDepth::Test);
        // The points within the radius of the segment between the two centres: a circle and two half circles at each end, and four lines along its side
        TRINITY_API void Capsule(const glm::vec3& start, const glm::vec3& end, float radius, const glm::vec4& color, DebugDepth depth = DebugDepth::Test);
        // A line with four barbs at its head, a fifth of its length
        TRINITY_API void Arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, DebugDepth depth = DebugDepth::Test);
        // Three lines along the axes through a point, each the size long
        TRINITY_API void Cross(const glm::vec3& center, float size, const glm::vec4& color, DebugDepth depth = DebugDepth::Test);
#else
        inline void Line(const glm::vec3&, const glm::vec3&, const glm::vec4&, DebugDepth = DebugDepth::Test) {}
        inline void Box(const glm::mat4&, const glm::vec4&, DebugDepth = DebugDepth::Test) {}
        inline void Box(const glm::vec3&, const glm::vec3&, const glm::vec4&, DebugDepth = DebugDepth::Test) {}
        inline void Circle(const glm::vec3&, const glm::vec3&, float, const glm::vec4&, DebugDepth = DebugDepth::Test) {}
        inline void Sphere(const glm::vec3&, float, const glm::vec4&, DebugDepth = DebugDepth::Test) {}
        inline void Capsule(const glm::vec3&, const glm::vec3&, float, const glm::vec4&, DebugDepth = DebugDepth::Test) {}
        inline void Arrow(const glm::vec3&, const glm::vec3&, const glm::vec4&, DebugDepth = DebugDepth::Test) {}
        inline void Cross(const glm::vec3&, float, const glm::vec4&, DebugDepth = DebugDepth::Test) {}
#endif
    }
}