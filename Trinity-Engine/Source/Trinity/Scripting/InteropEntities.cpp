#include "Trinity/Scripting/Interop.hpp"

#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/Scripting/ScriptApi.hpp"
#include "Trinity/Scripting/ScriptRuntime.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace Trinity
{
    namespace Interop
    {

        namespace
        {
            [[nodiscard]] EntityId ToId(Entity entity)
            {
                return entity ? entity.GetUUID().GetValue() : 0;
            }

            // A language C# names that the engine has, or none
            [[nodiscard]] bool ToLanguage(Int32 language, ScriptLanguage& result)
            {
                if (language != std::to_underlying(ScriptLanguage::Cpp) && language != std::to_underlying(ScriptLanguage::CSharp))
                {
                    return false;
                }

                result = static_cast<ScriptLanguage>(language);

                return true;
            }
        }

        Entity FindEntity(EntityId id)
        {
            ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();

            return l_Runtime != nullptr && id != 0 ? l_Runtime->GetScene().FindEntityByUUID(UUID(id)) : Entity();
        }

        Bool Entity_Exists(EntityId entity)
        {
            return static_cast<bool>(FindEntity(entity));
        }

        EntityId Entity_FindByName(Utf8 name)
        {
            ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();

            return l_Runtime != nullptr ? ToId(l_Runtime->GetScene().FindEntityByName(ToStringView(name))) : 0;
        }

        // 0 for a parent named that the scene does not have
        EntityId Entity_Create(Utf8 name, EntityId parent)
        {
            ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();
            const Entity l_Parent = FindEntity(parent);
            if (l_Runtime == nullptr || (parent != 0 && !l_Parent))
            {
                return 0;
            }

            return ToId(l_Runtime->GetScene().CreateEntity(ToStringView(name), l_Parent));
        }

        Bool Entity_Destroy(EntityId entity)
        {
            const Entity l_Entity = FindEntity(entity);
            if (!l_Entity)
            {
                return false;
            }

            ScriptRuntime::GetActive()->DestroyEntity(l_Entity);

            return true;
        }

        EntityId Entity_Duplicate(EntityId entity)
        {
            const Entity l_Entity = FindEntity(entity);

            return l_Entity ? ToId(ScriptRuntime::GetActive()->DuplicateEntity(l_Entity)) : 0;
        }

        // Valid until the tag changes, which C# copies at once
        Utf8 Entity_GetName(EntityId entity)
        {
            const Entity l_Entity = FindEntity(entity);

            return l_Entity ? ToUtf8(std::string_view(l_Entity.Get<TagComponent>().Tag)) : Utf8{ nullptr, 0 };
        }

        Bool Entity_SetName(EntityId entity, Utf8 name)
        {
            Entity l_Entity = FindEntity(entity);
            if (!l_Entity)
            {
                return false;
            }

            l_Entity.Patch<TagComponent>([name](TagComponent& tag) { tag.Tag = ToStringView(name); });

            return true;
        }

        EntityId Entity_GetParent(EntityId entity)
        {
            const Entity l_Entity = FindEntity(entity);

            return l_Entity ? ToId(l_Entity.GetParent()) : 0;
        }

        // False for an entity or a parent the scene does not have, or a parent inside the entity's own subtree
        Bool Entity_SetParent(EntityId entity, EntityId parent, Bool keepWorldTransform)
        {
            const Entity l_Entity = FindEntity(entity);
            const Entity l_Parent = FindEntity(parent);
            if (!l_Entity || (parent != 0 && !l_Parent))
            {
                return false;
            }

            return l_Entity.GetScene()->SetParent(l_Entity, l_Parent, keepWorldTransform);
        }

        EntityId Entity_GetFirstChild(EntityId entity)
        {
            const Entity l_Entity = FindEntity(entity);

            return l_Entity ? ToId(l_Entity.GetFirstChild()) : 0;
        }

        EntityId Entity_GetNextSibling(EntityId entity)
        {
            const Entity l_Entity = FindEntity(entity);

            return l_Entity ? ToId(l_Entity.GetNextSibling()) : 0;
        }

        UInt32 Entity_GetChildCount(EntityId entity)
        {
            const Entity l_Entity = FindEntity(entity);

            return l_Entity ? l_Entity.GetChildCount() : 0;
        }

        Bool Transform_GetWorld(EntityId entity, Vector3* position, Quaternion* rotation, Vector3* scale)
        {
            const Entity l_Entity = FindEntity(entity);
            if (!l_Entity)
            {
                return false;
            }

            const TransformComponent l_World = l_Entity.GetScene()->ComputeWorldTransform(l_Entity);
            *position = l_World.Position;
            *rotation = l_World.Rotation;
            *scale = l_World.Scale;

            return true;
        }

        // Each part not given stays as the world has it
        Bool Transform_SetWorld(EntityId entity, const Vector3* position, const Quaternion* rotation, const Vector3* scale)
        {
            const Entity l_Entity = FindEntity(entity);
            if (!l_Entity)
            {
                return false;
            }

            Scene& l_Scene = *l_Entity.GetScene();
            TransformComponent l_World = l_Scene.ComputeWorldTransform(l_Entity);
            l_World.Position = position != nullptr ? *position : l_World.Position;
            l_World.Rotation = rotation != nullptr ? *rotation : l_World.Rotation;
            l_World.Scale = scale != nullptr ? *scale : l_World.Scale;
            l_Scene.SetWorldTransform(l_Entity, l_World);

            return true;
        }

        Int32 Scripting_GetScriptCount(EntityId entity)
        {
            const Entity l_Entity = FindEntity(entity);

            return l_Entity && l_Entity.Has<ScriptComponent>() ? static_cast<Int32>(l_Entity.Get<ScriptComponent>().Scripts.size()) : 0;
        }

        // A C# instance is its GC handle, and a C++ one its address, which C# only compares with 0
        Int64 Scripting_GetInstance(EntityId entity, Int32 index, Int32 language)
        {
            const Entity l_Entity = FindEntity(entity);
            ScriptLanguage l_Language = ScriptLanguage::CSharp;
            if (!l_Entity || index < 0 || !ToLanguage(language, l_Language))
            {
                return 0;
            }

            return static_cast<Int64>(reinterpret_cast<std::intptr_t>(ScriptRuntime::GetActive()->GetInstance(l_Entity, static_cast<std::size_t>(index), l_Language)));
        }

        Int32 Scripting_FindScript(EntityId entity, Int32 language, Utf8 className)
        {
            const Entity l_Entity = FindEntity(entity);
            ScriptLanguage l_Language = ScriptLanguage::CSharp;

            return l_Entity && ToLanguage(language, l_Language) ? ScriptRuntime::GetActive()->FindScript(l_Entity, l_Language, ToStringView(className)) : -1;
        }

        Int32 Scripting_AddScript(EntityId entity, Int32 language, Utf8 className)
        {
            const Entity l_Entity = FindEntity(entity);
            ScriptLanguage l_Language = ScriptLanguage::CSharp;

            return l_Entity && ToLanguage(language, l_Language) ? ScriptRuntime::GetActive()->AddScript(l_Entity, l_Language, ToStringView(className)) : -1;
        }

        Bool Scripting_RemoveScript(EntityId entity, Int32 index)
        {
            const Entity l_Entity = FindEntity(entity);

            return l_Entity && index >= 0 && ScriptRuntime::GetActive()->RemoveScript(l_Entity, static_cast<std::size_t>(index));
        }

        // All 0 while no scene runs
        Float Time_GetDeltaTime()
        {
            const ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();

            return l_Runtime != nullptr ? l_Runtime->GetTime().DeltaTime : 0.0f;
        }

        Float Time_GetFixedDeltaTime()
        {
            const ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();

            return l_Runtime != nullptr ? l_Runtime->GetTime().FixedDeltaTime : 0.0f;
        }

        Double Time_GetElapsed()
        {
            const ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();

            return l_Runtime != nullptr ? l_Runtime->GetTime().Elapsed : 0.0;
        }

        UInt64 Time_GetFrameCount()
        {
            const ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();

            return l_Runtime != nullptr ? l_Runtime->GetTime().FrameCount : 0;
        }

        UInt64 Time_GetStepCount()
        {
            const ScriptRuntime* l_Runtime = ScriptRuntime::GetActive();

            return l_Runtime != nullptr ? l_Runtime->GetTime().StepCount : 0;
        }
    }
}