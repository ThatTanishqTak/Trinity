#include "Trinity/Scripting/Script.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/Scripting/NativeScripts.hpp"
#include "Trinity/Scripting/ScriptRuntime.hpp"

namespace Trinity
{
    namespace
    {
        const ScriptTime c_NoTime;
    }

    Scene& Script::GetScene() const
    {
        TR_CORE_ASSERT(m_Runtime != nullptr, "Scripting: a script reached for its scene before the runtime made it");

        return m_Runtime->GetScene();
    }

    const ScriptTime& Script::GetTime() const
    {
        return m_Runtime != nullptr ? m_Runtime->GetTime() : c_NoTime;
    }

    PhysicsScene3D* Script::GetPhysics() const
    {
        return m_Runtime != nullptr ? m_Runtime->GetPhysics() : nullptr;
    }

    PhysicsScene2D* Script::GetPhysics2D() const
    {
        return m_Runtime != nullptr ? m_Runtime->GetPhysics2D() : nullptr;
    }

    Entity Script::FindEntity(UUID id) const
    {
        return m_Runtime != nullptr && id ? m_Runtime->GetScene().FindEntityByUUID(id) : Entity();
    }

    Entity Script::FindEntityByName(std::string_view name) const
    {
        return m_Runtime != nullptr ? m_Runtime->GetScene().FindEntityByName(name) : Entity();
    }

    Entity Script::CreateEntity(std::string_view name, Entity parent)
    {
        if (m_Runtime == nullptr || (parent && parent.GetScene() != &m_Runtime->GetScene()))
        {
            return {};
        }

        return m_Runtime->GetScene().CreateEntity(name, parent);
    }

    Entity Script::Duplicate(Entity entity)
    {
        return m_Runtime != nullptr ? m_Runtime->DuplicateEntity(entity) : Entity();
    }

    void Script::Destroy(Entity entity)
    {
        if (m_Runtime != nullptr)
        {
            m_Runtime->DestroyEntity(entity);
        }
    }

    bool Script::HasScript(Entity entity, ScriptLanguage language, std::string_view className) const
    {
        return m_Runtime != nullptr && m_Runtime->FindScript(entity, language, className) >= 0;
    }

    bool Script::AddScript(Entity entity, ScriptLanguage language, std::string_view className)
    {
        return m_Runtime != nullptr && m_Runtime->AddScript(entity, language, className) >= 0;
    }

    bool Script::RemoveScript(Entity entity, ScriptLanguage language, std::string_view className)
    {
        if (m_Runtime == nullptr)
        {
            return false;
        }

        const std::int32_t l_Index = m_Runtime->FindScript(entity, language, className);

        return l_Index >= 0 && m_Runtime->RemoveScript(entity, static_cast<std::size_t>(l_Index));
    }

    Script* Script::FindNativeScript(Entity entity, const void* type) const
    {
        if (m_Runtime == nullptr || !entity || !entity.Has<ScriptComponent>())
        {
            return nullptr;
        }

        const std::size_t l_Count = entity.Get<ScriptComponent>().Scripts.size();
        for (std::size_t it_Index = 0; it_Index < l_Count; ++it_Index)
        {
            Script* l_Script = static_cast<Script*>(m_Runtime->GetInstance(entity, it_Index, ScriptLanguage::Cpp));
            if (l_Script != nullptr && l_Script->m_Type == type)
            {
                return l_Script;
            }
        }

        return nullptr;
    }

    // By the name the class was registered under, which its type finds
    Script* Script::AddNativeScript(Entity entity, const void* type)
    {
        const std::string_view l_Name = NativeScripts::FindClassName(type);
        if (m_Runtime == nullptr || l_Name.empty())
        {
            TR_CORE_ERROR("Scripting: a C++ script class that is not registered was not added to an entity");

            return nullptr;
        }

        const std::int32_t l_Index = m_Runtime->AddScript(entity, ScriptLanguage::Cpp, l_Name);

        return l_Index >= 0 ? static_cast<Script*>(m_Runtime->GetInstance(entity, static_cast<std::size_t>(l_Index), ScriptLanguage::Cpp)) : nullptr;
    }

    bool Script::RemoveNativeScript(Entity entity, const void* type)
    {
        if (m_Runtime == nullptr || !entity || !entity.Has<ScriptComponent>())
        {
            return false;
        }

        const std::size_t l_Count = entity.Get<ScriptComponent>().Scripts.size();
        for (std::size_t it_Index = 0; it_Index < l_Count; ++it_Index)
        {
            const Script* l_Script = static_cast<const Script*>(m_Runtime->GetInstance(entity, it_Index, ScriptLanguage::Cpp));
            if (l_Script != nullptr && l_Script->m_Type == type)
            {
                return m_Runtime->RemoveScript(entity, it_Index);
            }
        }

        return false;
    }
}