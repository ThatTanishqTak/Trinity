#include "Trinity/Scripting/NativeComponents.hpp"

#include "Trinity/Scripting/NativeComponentRegistry.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <entt/core/hashed_string.hpp>

#include <algorithm>
#include <utility>

namespace Trinity
{
    namespace
    {
        struct Registered
        {
            NativeComponentDescription Description;
            std::vector<NativeComponents::FieldAccess, TaggedAllocator<NativeComponents::FieldAccess, MemoryTag::Scripting>> Fields;
        };

        struct State
        {
            std::vector<Registered, TaggedAllocator<Registered, MemoryTag::Scripting>> Components;
            std::uint64_t Generation = 0;
        };

        State* s_State = nullptr;

        auto FindComponent(std::string_view name)
        {
            return std::ranges::find_if(s_State->Components, [name](const Registered& component) { return std::string_view(component.Description.Name) == name; });
        }

        const Registered* FindRegistered(std::string_view name)
        {
            if (s_State == nullptr)
            {
                return nullptr;
            }

            const auto a_Found = FindComponent(name);

            return a_Found != s_State->Components.end() ? &*a_Found : nullptr;
        }

        // Why the component cannot be registered, or nothing when it can
        std::string_view FindProblem(const NativeComponentDescription& description, std::span<const NativeComponents::FieldAccess> fields, const ComponentSerializer& serializer)
        {
            if (description.Name.empty() || serializer.Name != std::string_view(description.Name))
            {
                return "it has no name, or its serializer has another";
            }

            if (fields.size() != description.Fields.size() || std::ranges::any_of(fields, [](const NativeComponents::FieldAccess& field) { return field.Get == nullptr || field.Set == nullptr; }))
            {
                return "a field lacks its getter or its setter";
            }

            for (auto it_Field = description.Fields.begin(); it_Field != description.Fields.end(); ++it_Field)
            {
                if (it_Field->Name.empty())
                {
                    return "a field has no name";
                }

                if (std::any_of(description.Fields.begin(), it_Field, [&it_Field](const ScriptFieldDescription& field) { return field.Name == it_Field->Name; }))
                {
                    return "a field name repeats";
                }

                if ((it_Field->Default.GetElementType() == ScriptFieldType::Enum) == it_Field->EnumValues.empty())
                {
                    return "an enum field, or a list of enums, has no values, or another field has enum values";
                }
            }

            return {};
        }

        // The entity's component as its pool holds it, or null when the entity is without it
        void* FindValue(Entity entity, std::string_view name)
        {
            SceneRegistry::common_type* l_Storage = entity.GetScene()->GetRegistry().storage(entt::hashed_string::value(name.data(), name.size()));

            return l_Storage != nullptr && l_Storage->contains(entity.GetHandle()) ? l_Storage->value(entity.GetHandle()) : nullptr;
        }

        std::optional<std::size_t> FindFieldIndex(const Registered& component, std::string_view field)
        {
            const auto a_Found = std::ranges::find_if(component.Description.Fields, [field](const ScriptFieldDescription& description) { return std::string_view(description.Name) == field; });
            if (a_Found == component.Description.Fields.end())
            {
                return std::nullopt;
            }

            return static_cast<std::size_t>(a_Found - component.Description.Fields.begin());
        }
    }

    const ScriptFieldDescription* NativeComponentDescription::FindField(std::string_view name) const
    {
        const auto a_Found = std::ranges::find_if(Fields, [name](const ScriptFieldDescription& field) { return std::string_view(field.Name) == name; });

        return a_Found != Fields.end() ? &*a_Found : nullptr;
    }

    namespace NativeComponents
    {
        void Initialize()
        {
            TR_CORE_ASSERT(s_State == nullptr, "The native components are already initialized.");

            s_State = Memory::New<State>(MemoryTag::Scripting);
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The native components are not initialized.");

            if (!s_State->Components.empty())
            {
                TR_CORE_WARN("Scripting: {} C++ component(s) were still registered at shutdown", s_State->Components.size());
            }

            Memory::Delete(s_State);
            s_State = nullptr;
        }

        // The description goes first, so the serializer can save and load through it as soon as it is registered
        bool Register(const NativeComponentDescription& description, std::span<const FieldAccess> fields, const ComponentSerializer& serializer)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The native components are not initialized.");

            if (const std::string_view l_Problem = FindProblem(description, fields, serializer); !l_Problem.empty())
            {
                TR_CORE_ERROR("Scripting: C++ component {} cannot be registered, because {}", std::string_view(description.Name), l_Problem);

                return false;
            }

            if (FindRegistered(description.Name) != nullptr)
            {
                TR_CORE_ERROR("Scripting: C++ component {} is already registered", std::string_view(description.Name));

                return false;
            }

            Registered l_Component{ description, {} };
            l_Component.Fields.assign(fields.begin(), fields.end());
            s_State->Components.push_back(std::move(l_Component));
            ++s_State->Generation;

            if (!SceneSerializer::RegisterComponent(serializer))
            {
                s_State->Components.pop_back();

                return false;
            }

            return true;
        }

        void Unregister(std::string_view name)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The native components are not initialized.");

            if (FindRegistered(name) == nullptr)
            {
                return;
            }

            SceneSerializer::UnregisterComponent(name);
            s_State->Components.erase(FindComponent(name));
            ++s_State->Generation;
        }

        const NativeComponentDescription* Find(std::string_view name)
        {
            const Registered* l_Component = FindRegistered(name);

            return l_Component != nullptr ? &l_Component->Description : nullptr;
        }

        std::uint64_t GetGeneration()
        {
            return s_State != nullptr ? s_State->Generation : 0;
        }

        void WriteFields(std::string_view name, const void* component, ComponentWriter& writer)
        {
            const Registered* l_Component = FindRegistered(name);
            if (l_Component == nullptr)
            {
                return;
            }

            for (std::size_t it_Index = 0; it_Index < l_Component->Fields.size(); ++it_Index)
            {
                writer.Write(l_Component->Description.Fields[it_Index].Name, l_Component->Fields[it_Index].Get(component));
            }
        }

        void ReadFields(std::string_view name, void* component, const ComponentReader& reader)
        {
            const Registered* l_Component = FindRegistered(name);
            if (l_Component == nullptr)
            {
                return;
            }

            for (std::size_t it_Index = 0; it_Index < l_Component->Fields.size(); ++it_Index)
            {
                const ScriptFieldDescription& l_Field = l_Component->Description.Fields[it_Index];
                ScriptFieldValue l_Value;
                if (!reader.Read(l_Field.Name, l_Value))
                {
                    continue;
                }

                if (!l_Value.HasSameType(l_Field.Default))
                {
                    TR_CORE_WARN("Scene: field {} of {} was saved as {} and is now {}, so it keeps its default", std::string_view(l_Field.Name), name, DescribeType(l_Value), DescribeType(l_Field.Default));

                    continue;
                }

                l_Component->Fields[it_Index].Set(component, l_Value);
            }
        }

        std::optional<ScriptFieldValue> GetField(Entity entity, std::string_view component, std::string_view field)
        {
            const Registered* l_Component = FindRegistered(component);
            const std::optional<std::size_t> l_Index = l_Component != nullptr ? FindFieldIndex(*l_Component, field) : std::nullopt;
            const void* l_Value = l_Index ? FindValue(entity, component) : nullptr;
            if (l_Value == nullptr)
            {
                return std::nullopt;
            }

            return l_Component->Fields[*l_Index].Get(l_Value);
        }

        bool SetField(Entity entity, std::string_view component, std::string_view field, const ScriptFieldValue& value)
        {
            const Registered* l_Component = FindRegistered(component);
            const std::optional<std::size_t> l_Index = l_Component != nullptr ? FindFieldIndex(*l_Component, field) : std::nullopt;
            void* l_Value = l_Index ? FindValue(entity, component) : nullptr;
            if (l_Value == nullptr || !l_Component->Description.Fields[*l_Index].Default.HasSameType(value))
            {
                return false;
            }

            l_Component->Fields[*l_Index].Set(l_Value, value);

            return true;
        }

        void RemapEntityFields(Entity entity, const std::function<void(UUID& entity)>& remap)
        {
            if (s_State == nullptr)
            {
                return;
            }

            for (const Registered& it_Component : s_State->Components)
            {
                void* l_Value = nullptr;
                for (std::size_t it_Index = 0; it_Index < it_Component.Fields.size(); ++it_Index)
                {
                    if (it_Component.Description.Fields[it_Index].Default.GetElementType() != ScriptFieldType::Entity)
                    {
                        continue;
                    }

                    l_Value = l_Value != nullptr ? l_Value : FindValue(entity, it_Component.Description.Name);
                    if (l_Value == nullptr)
                    {
                        break;
                    }

                    // An entity field, or each entity of a list of them
                    ScriptFieldValue l_Field = it_Component.Fields[it_Index].Get(l_Value);
                    bool l_Changed = false;
                    l_Field.ForEachValue<UUID>(ScriptFieldType::Entity, [&remap, &l_Changed](UUID& id)
                    {
                        const UUID l_Before = id;
                        remap(id);
                        l_Changed = l_Changed || id != l_Before;
                    });

                    if (l_Changed)
                    {
                        it_Component.Fields[it_Index].Set(l_Value, l_Field);
                    }
                }
            }
        }
    }
}