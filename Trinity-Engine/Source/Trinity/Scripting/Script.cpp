#include "Trinity/Scripting/Script.hpp"

#include "Trinity/Scripting/ScriptRuntime.hpp"

namespace Trinity
{
    void Script::Destroy(Entity entity)
    {
        if (m_Runtime != nullptr)
        {
            m_Runtime->DestroyEntity(entity);
        }
    }
}