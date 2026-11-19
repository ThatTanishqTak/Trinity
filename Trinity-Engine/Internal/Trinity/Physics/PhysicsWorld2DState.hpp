#pragma once

// Included only by sources in Source/Trinity/Physics
#include "Trinity/Core/JobSystem.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Physics/PhysicsWorld2D.hpp"

#include <box2d/box2d.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Trinity
{
    namespace Box2DLayers
    {
        // In every shape's mask and no shape's category, so a query reaches every shape on its layers
        constexpr std::uint64_t c_QueryBit = 1ull << 63;
    }

    namespace Box2DConvert
    {
        // Box2D's b2MakeRot and b2Rot_GetAngle approximate, which is millimetres off a few metres out
        [[nodiscard]] inline b2Rot ToRotation(float angle)
        {
            return { std::cos(angle), std::sin(angle) };
        }

        [[nodiscard]] inline float ToAngle(b2Rot rotation)
        {
            return std::atan2(rotation.s, rotation.c);
        }
    }

    // A Box2D task is split into ranges, each a Trinity job. Records are reused from the first a step
    struct Box2DTaskQueue
    {
        // A few for each stage of the step, and one for each worker while the solver runs
        static constexpr std::size_t c_MaxTasks = 128;

        std::array<JobCounter, c_MaxTasks> Tasks;
        std::atomic<std::size_t> Count{ 0 };
    };

    struct PhysicsWorld2D::State
    {
        b2WorldId World = b2_nullWorldId;
        std::uint32_t SubSteps = 4;
        CollisionLayers Layers;
        Box2DTaskQueue Tasks;
    };
}