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

        // Each entity field of the entity's module components, as a duplicate's references are remapped
        void RemapEntityFields(Entity entity, const std::function<void(UUID& entity)>& remap);
    }
}