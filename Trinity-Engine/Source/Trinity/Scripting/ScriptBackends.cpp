#include "Trinity/Scripting/ScriptBackend.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Scripting/NativeScripts.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Trinity
{
    // C++ classes, each with how its registrant makes and frees its instances
    class NativeScriptBackend final : public ScriptBackend
    {
    public:
        struct Factory
        {
            TaggedString<MemoryTag::Scripting> Name;
            NativeScripts::CreateFunction Create = nullptr;
            NativeScripts::DestroyFunction Destroy = nullptr;
            std::vector<NativeScripts::FieldAccess, TaggedAllocator<NativeScripts::FieldAccess, MemoryTag::Scripting>> Fields;
        };

        [[nodiscard]] ScriptLanguage GetLanguage() const override { return ScriptLanguage::Cpp; }

        [[nodiscard]] void* CreateInstance(const ScriptClassDescription& description, Entity entity, ScriptRuntime& runtime) override
        {
            const Factory* l_Factory = Find(description.Name);
            if (l_Factory == nullptr)
            {
                TR_CORE_ERROR("Scripting: C++ class {} is registered without a way to make it", std::string_view(description.Name));

                return nullptr;
            }

            Script* l_Script = l_Factory->Create();
            if (l_Script == nullptr)
            {
                TR_CORE_ERROR("Scripting: C++ class {} made no instance", std::string_view(description.Name));

                return nullptr;
            }

            l_Script->m_Entity = entity;
            l_Script->m_Runtime = &runtime;
            l_Script->m_Release = l_Factory->Destroy;

            return l_Script;
        }

        void DestroyInstance(void* instance) override
        {
            Script* l_Script = static_cast<Script*>(instance);
            l_Script->m_Release(l_Script);
        }

        // The class was registered with a setter for each of its fields, in the same order
        void ApplyFields(void* instance, const ScriptClassDescription& description, const ScriptEntry& entry, Scene& scene) override
        {
            const Factory* l_Factory = Find(description.Name);
            if (l_Factory == nullptr || l_Factory->Fields.size() != description.Fields.size())
            {
                return;
            }

            Script* l_Script = static_cast<Script*>(instance);
            for (std::size_t it_Index = 0; it_Index < description.Fields.size(); ++it_Index)
            {
                l_Factory->Fields[it_Index].Set(l_Script, ScriptClassRegistry::GetFieldValue(entry, description.Fields[it_Index]), scene);
            }
        }

        void ReadFields(const void* instance, const ScriptClassDescription& description, ScriptEntry& entry) override
        {
            const Factory* l_Factory = Find(description.Name);
            if (l_Factory == nullptr || l_Factory->Fields.size() != description.Fields.size())
            {
                return;
            }

            const Script* l_Script = static_cast<const Script*>(instance);
            for (std::size_t it_Index = 0; it_Index < description.Fields.size(); ++it_Index)
            {
                entry.SetField(description.Fields[it_Index].Name, l_Factory->Fields[it_Index].Get(l_Script));
            }
        }

        void OnCreate(void* instance) override { static_cast<Script*>(instance)->OnCreate(); }
        void OnFixedUpdate(void* instance, Timestep step) override { static_cast<Script*>(instance)->OnFixedUpdate(step); }
        void OnUpdate(void* instance, Timestep timestep) override { static_cast<Script*>(instance)->OnUpdate(timestep); }
        void OnLateUpdate(void* instance, Timestep timestep) override { static_cast<Script*>(instance)->OnLateUpdate(timestep); }
        void OnDestroy(void* instance) override { static_cast<Script*>(instance)->OnDestroy(); }
        void OnReloaded(void* instance) override { static_cast<Script*>(instance)->OnReloaded(); }

        void OnPhysicsEvent(void* instance, PhysicsEventType type, const ScriptCollision& collision) override
        {
            Script* l_Script = static_cast<Script*>(instance);
            switch (type)
            {
                case PhysicsEventType::ContactBegin:
                {
                    l_Script->OnContactBegin(collision);
                    
                    break;
                }
                case PhysicsEventType::ContactStay:
                {
                    l_Script->OnContactStay(collision);
                    
                    break;
                }
                case PhysicsEventType::ContactEnd:
                {
                    l_Script->OnContactEnd(collision);
                    
                    break;
                }
                case PhysicsEventType::TriggerEnter:
                {
                    l_Script->OnTriggerEnter(collision);

                    break;
                }
                case PhysicsEventType::TriggerExit:
                {
                    l_Script->OnTriggerExit(collision);

                    break;
                }
                case PhysicsEventType::JointBreak:
                {
                    l_Script->OnJointBreak(collision);

                    break;
                }
            }
        }

        bool Add(Factory factory)
        {
            m_Factories.push_back(std::move(factory));

            return true;
        }

        void Remove(std::string_view name)
        {
            std::erase_if(m_Factories, [name](const Factory& factory) { return std::string_view(factory.Name) == name; });
        }

        [[nodiscard]] bool IsEmpty() const { return m_Factories.empty(); }

    private:
        [[nodiscard]] const Factory* Find(std::string_view name) const
        {
            const auto a_Found = std::ranges::find_if(m_Factories, [name](const Factory& factory) { return std::string_view(factory.Name) == name; });

            return a_Found != m_Factories.end() ? &*a_Found : nullptr;
        }

        std::vector<Factory, TaggedAllocator<Factory, MemoryTag::Scripting>> m_Factories;
    };

    namespace
    {
        NativeScriptBackend* s_Native = nullptr;
    }

    namespace ScriptBackends
    {
        void Initialize()
        {
            TR_CORE_ASSERT(s_Native == nullptr, "The script backends are already initialized.");

            s_Native = Memory::New<NativeScriptBackend>(MemoryTag::Scripting);
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_Native != nullptr, "The script backends are not initialized.");

            if (!s_Native->IsEmpty())
            {
                TR_CORE_WARN("Scripting: C++ script classes were still registered at shutdown");
            }

            Memory::Delete(s_Native);
            s_Native = nullptr;
        }

        ScriptBackend* Find(ScriptLanguage language)
        {
            return language == ScriptLanguage::Cpp ? s_Native : nullptr;
        }
    }

    namespace NativeScripts
    {
        bool Register(ScriptClassDescription description, CreateFunction create, DestroyFunction destroy, std::span<const FieldAccess> fields)
        {
            TR_CORE_ASSERT(s_Native != nullptr, "The script backends are not initialized.");

            if (create == nullptr || destroy == nullptr)
            {
                TR_CORE_ERROR("Scripting: C++ class {} needs a function to make its instances and one to free them", std::string_view(description.Name));

                return false;
            }

            if (fields.size() != description.Fields.size() || std::ranges::any_of(fields, [](const FieldAccess& field) { return field.Get == nullptr || field.Set == nullptr; }))
            {
                TR_CORE_ERROR("Scripting: C++ class {} has {} field(s), and needs a getter and a setter for each", std::string_view(description.Name), description.Fields.size());

                return false;
            }

            description.Language = ScriptLanguage::Cpp;
            if (!ScriptClassRegistry::Register(description))
            {
                return false;
            }

            NativeScriptBackend::Factory l_Factory{ TaggedString<MemoryTag::Scripting>(std::string_view(description.Name)), create, destroy, {} };
            l_Factory.Fields.assign(fields.begin(), fields.end());

            return s_Native->Add(std::move(l_Factory));
        }

        // Instances of the class still alive keep their own way to be freed, but no more are made
        void Unregister(std::string_view name)
        {
            TR_CORE_ASSERT(s_Native != nullptr, "The script backends are not initialized.");

            ScriptClassRegistry::Unregister(ScriptLanguage::Cpp, name);
            s_Native->Remove(name);
        }
    }
}