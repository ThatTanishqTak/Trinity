#pragma once

#include "Trinity/Core/UUID.hpp"

#include <functional>

namespace Trinity
{
    class Entity;

    namespace NativeComponents
    {
        void Initialize();
        void Shutdown();

        // Each entity field of every module component the entity has, which remap may change, as a duplicated hierarchy's references are pointed at its own copies
        void RemapEntityFields(Entity entity, const std::function<void(UUID& entity)>& remap);
    }
}