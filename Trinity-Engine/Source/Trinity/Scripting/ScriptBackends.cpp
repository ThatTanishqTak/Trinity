#include "Trinity/Scripting/ScriptBackend.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Scripting/NativeScripts.hpp"

#include <algorithm>
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
        };

        [[nodiscard]] ScriptLanguage GetLanguage() const override { return ScriptLanguage::Cpp; }

        [[nodiscard]] void* CreateInstance(const ScriptClassDescription& description, Entity entity, ScriptRuntime& runtime) override
        {
            const auto a_Found = std::ranges::find_if(m_Factories, [&description](const Factory& factory) { return factory.Name == description.Name; });
            if (a_Found == m_Factories.end())
            {
                TR_CORE_ERROR("Scripting: C++ class {} is registered without a way to make it", std::string_view(description.Name));

                return nullptr;
            }

            Script* l_Script = a_Found->Create();
            if (l_Script == nullptr)
            {
                TR_CORE_ERROR("Scripting: C++ class {} made no instance", std::string_view(description.Name));

                return nullptr;
            }

            l_Script->m_Entity = entity;
            l_Script->m_Runtime = &runtime;
            l_Script->m_Release = a_Found->Destroy;

            return l_Script;
        }

        void DestroyInstance(void* instance) override
        {
            Script* l_Script = static_cast<Script*>(instance);
            l_Script->m_Release(l_Script);
        }

        void OnCreate(void* instance) override { static_cast<Script*>(instance)->OnCreate(); }
        void OnFixedUpdate(void* instance, Timestep step) override { static_cast<Script*>(instance)->OnFixedUpdate(step); }
        void OnUpdate(void* instance, Timestep timestep) override { static_cast<Script*>(instance)->OnUpdate(timestep); }
        void OnLateUpdate(void* instance, Timestep timestep) override { static_cast<Script*>(instance)->OnLateUpdate(timestep); }
        void OnDestroy(void* instance) override { static_cast<Script*>(instance)->OnDestroy(); }

        void OnPhysicsEvent(void* instance, PhysicsEventType type, const ScriptCollision& collision) override
        {
            Script* l_Script = static_cast<Script*>(instance);
            switch (type)
            {
                case PhysicsEventType::ContactBegin: l_Script->OnContactBegin(collision); break;
                case PhysicsEventType::ContactStay: l_Script->OnContactStay(collision); break;
                case PhysicsEventType::ContactEnd: l_Script->OnContactEnd(collision); break;
                case PhysicsEventType::TriggerEnter: l_Script->OnTriggerEnter(collision); break;
                case PhysicsEventType::TriggerExit: l_Script->OnTriggerExit(collision); break;
                case PhysicsEventType::JointBreak: l_Script->OnJointBreak(collision); break;
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
        bool Register(ScriptClassDescription description, CreateFunction create, DestroyFunction destroy)
        {
            TR_CORE_ASSERT(s_Native != nullptr, "The script backends are not initialized.");

            if (create == nullptr || destroy == nullptr)
            {
                TR_CORE_ERROR("Scripting: C++ class {} needs a function to make its instances and one to free them", std::string_view(description.Name));

                return false;
            }

            description.Language = ScriptLanguage::Cpp;
            if (!ScriptClassRegistry::Register(description))
            {
                return false;
            }

            return s_Native->Add({ TaggedString<MemoryTag::Scripting>(std::string_view(description.Name)), create, destroy });
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