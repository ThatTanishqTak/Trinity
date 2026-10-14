#pragma once

#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scripting/Interop.hpp"

namespace Trinity::Interop
{
    // What a component's or an entity's function tells C#, which turns each failure into an exception naming what failed
    enum class AccessResult : Int32
    {
        Done,
        NoEntity,
        NoComponent,
        BadValue
    };

    // The running scene's entity of that UUID, or an invalid one when no scene runs or it has none
    [[nodiscard]] Entity FindEntity(EntityId id);
}