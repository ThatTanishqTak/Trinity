#include "Trinity/Scripting/ScriptRuntime.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/Scripting/ScriptBackend.hpp"
#include "Trinity/Scripting/ScriptClassRegistry.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        // A script's instance in its backend, or no backend when its class has no loaded code. Created stays when a reload frees the instance, so the one made again knows OnCreate has run
        struct Instance
        {
            ScriptBackend* Backend = nullptr;
            void* Handle = nullptr;
            std::uint32_t Callbacks = 0;
            bool Created = false;
        };

        // An entity's instances, one for each script in its component, in list order. Dead once its instances are freed, and orphaned once its component went without the runtime, so its instances wait to be freed without OnDestroy
        struct Record
        {
            UUID ID;
            entt::entity Handle = entt::null;
            std::vector<Instance, TaggedAllocator<Instance, MemoryTag::Scripting>> Instances;
            bool Dead = false;
            bool Orphaned = false;

            [[nodiscard]] bool IsLive() const { return !Dead && !Orphaned; }
        };
    }

    struct ScriptRuntime::State
    {
        explicit State(Scene& scene) : TargetScene(scene)
        {

        }

        // Signal listeners, which only note what changed, since a script may not run while the registry is in the middle of a change
        void OnScriptAdded(SceneRegistry& registry, entt::entity entity)
        {
            Pending.push_back(registry.get<IDComponent>(entity).ID);
        }

        // By handle, since an entity being destroyed may already have lost its ID
        void OnScriptRemoved([[maybe_unused]] SceneRegistry& registry, entt::entity entity)
        {
            const auto a_Found = std::ranges::find_if(Records, [entity](const Record& record) { return record.Handle == entity && record.IsLive(); });
            if (a_Found != Records.end())
            {
                a_Found->Orphaned = true;
                Changed = true;
            }
        }

        void WarnUnknown(const ScriptEntry& entry)
        {
            const std::string l_Key = std::format("{} {}", ToString(entry.Language), std::string_view(entry.ClassName));
            if (std::ranges::find(Warned, std::string_view(l_Key), [](const TaggedString<MemoryTag::Scripting>& warned) { return std::string_view(warned); }) != Warned.end())
            {
                return;
            }

            Warned.emplace_back(l_Key);
            TR_CORE_WARN("Scripting: no loaded code has {} class {}, so its scripts do not run", ToString(entry.Language), std::string_view(entry.ClassName));
        }

        // The instance when it should be called: its class is loaded and its script enabled. OnCreate runs here the first time, so a script enabled while the scene runs is created before its first call
        Instance* Prepare(std::size_t record, std::size_t index)
        {
            if (!Records[record].IsLive() || index >= Records[record].Instances.size() || Records[record].Instances[index].Backend == nullptr)
            {
                return nullptr;
            }

            const ScriptComponent* l_Component = TargetScene.GetRegistry().try_get<ScriptComponent>(Records[record].Handle);
            if (l_Component == nullptr || index >= l_Component->Scripts.size() || !l_Component->Scripts[index].Enabled)
            {
                return nullptr;
            }

            Instance& l_Instance = Records[record].Instances[index];
            if (!l_Instance.Created)
            {
                l_Instance.Created = true;
                if ((l_Instance.Callbacks & ScriptCallbacks::Create) != 0)
                {
                    l_Instance.Backend->OnCreate(l_Instance.Handle);
                }

                // OnCreate may have taken the component away
                if (!Records[record].IsLive())
                {
                    return nullptr;
                }
            }

            return &Records[record].Instances[index];
        }

        // Calls the callback on every instance that has it, in order. Callbacks never add records, so indices hold across them
        template<typename F>
        bool ForEach(std::uint32_t callback, F call)
        {
            bool l_Any = false;
            for (std::size_t it_Record = 0; it_Record < Records.size(); ++it_Record)
            {
                for (std::size_t it_Index = 0; it_Index < Records[it_Record].Instances.size(); ++it_Index)
                {
                    Instance* l_Instance = Prepare(it_Record, it_Index);
                    if (l_Instance != nullptr && (l_Instance->Callbacks & callback) != 0)
                    {
                        l_Any = true;
                        call(*l_Instance);
                    }
                }
            }

            return l_Any;
        }

        void Deliver(UUID entity, PhysicsEventType type, const ScriptCollision& collision)
        {
            const auto a_Found = Index.find(entity);
            if (a_Found == Index.end())
            {
                return;
            }

            const std::size_t l_Record = a_Found->second;
            for (std::size_t it_Index = 0; it_Index < Records[l_Record].Instances.size(); ++it_Index)
            {
                Instance* l_Instance = Prepare(l_Record, it_Index);
                if (l_Instance != nullptr && (l_Instance->Callbacks & ScriptCallbacks::Physics) != 0)
                {
                    l_Instance->Backend->OnPhysicsEvent(l_Instance->Handle, type, collision);
                }
            }
        }

        // OnDestroy on each script that had OnCreate, when asked, then frees them all
        void End(std::size_t record, bool callOnDestroy)
        {
            for (std::size_t it_Index = 0; it_Index < Records[record].Instances.size(); ++it_Index)
            {
                const Instance l_Instance = Records[record].Instances[it_Index];
                if (l_Instance.Backend == nullptr)
                {
                    continue;
                }

                if (callOnDestroy && l_Instance.Created && (l_Instance.Callbacks & ScriptCallbacks::Destroy) != 0)
                {
                    l_Instance.Backend->OnDestroy(l_Instance.Handle);
                }

                l_Instance.Backend->DestroyInstance(l_Instance.Handle);
                Records[record].Instances[it_Index].Backend = nullptr;
            }

            Records[record].Dead = true;
            Changed = true;
        }

        // Frees orphans without OnDestroy, since their entities are gone, then drops every dead record
        void Compact()
        {
            if (!std::exchange(Changed, false))
            {
                return;
            }

            for (std::size_t it_Record = 0; it_Record < Records.size(); ++it_Record)
            {
                if (Records[it_Record].Orphaned && !Records[it_Record].Dead)
                {
                    End(it_Record, false);
                }
            }

            std::erase_if(Records, [](const Record& record) { return record.Dead; });
            Index.clear();
            for (std::size_t it_Record = 0; it_Record < Records.size(); ++it_Record)
            {
                Index[Records[it_Record].ID] = it_Record;
            }

            Changed = false;
        }

        Scene& TargetScene;
        std::vector<Record, TaggedAllocator<Record, MemoryTag::Scripting>> Records;
        std::unordered_map<UUID, std::size_t, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, std::size_t>, MemoryTag::Scripting>> Index;
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scripting>> Pending;
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scripting>> PendingDestroy;
        std::vector<TaggedString<MemoryTag::Scripting>, TaggedAllocator<TaggedString<MemoryTag::Scripting>, MemoryTag::Scripting>> Warned;
        bool Changed = false;
    };

    // The scene's scripts wait in hierarchy order for the first CreatePending, and any added later follow them as they are added
    ScriptRuntime::ScriptRuntime(Scene& scene) : m_State(Memory::New<State>(MemoryTag::Scripting, scene))
    {
        for (Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            if (it_Entity.Has<ScriptComponent>())
            {
                m_State->Pending.push_back(it_Entity.GetUUID());
            }
        }

        SceneRegistry& l_Registry = scene.GetRegistry();
        l_Registry.on_construct<ScriptComponent>().connect<&State::OnScriptAdded>(*m_State);
        l_Registry.on_destroy<ScriptComponent>().connect<&State::OnScriptRemoved>(*m_State);
    }

    ScriptRuntime::~ScriptRuntime()
    {
        SceneRegistry& l_Registry = m_State->TargetScene.GetRegistry();
        l_Registry.on_construct<ScriptComponent>().disconnect(m_State);
        l_Registry.on_destroy<ScriptComponent>().disconnect(m_State);

        for (std::size_t it_Record = 0; it_Record < m_State->Records.size(); ++it_Record)
        {
            if (!m_State->Records[it_Record].Dead)
            {
                m_State->End(it_Record, !m_State->Records[it_Record].Orphaned);
            }
        }

        Memory::Delete(m_State);
    }

    void ScriptRuntime::CreatePending()
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        for (std::size_t it_Pending = 0; it_Pending < l_State.Pending.size(); ++it_Pending)
        {
            const UUID l_ID = l_State.Pending[it_Pending];
            const Entity l_Entity = l_State.TargetScene.FindEntityByUUID(l_ID);
            if (!l_Entity || !l_Entity.Has<ScriptComponent>())
            {
                continue;
            }

            if (const auto a_Found = l_State.Index.find(l_ID); a_Found != l_State.Index.end() && l_State.Records[a_Found->second].IsLive())
            {
                continue;
            }

            Record l_Record;
            l_Record.ID = l_ID;
            l_Record.Handle = l_Entity.GetHandle();
            const std::size_t l_Count = l_Entity.Get<ScriptComponent>().Scripts.size();
            l_Record.Instances.reserve(l_Count);
            for (std::size_t it_Index = 0; it_Index < l_Count; ++it_Index)
            {
                const ScriptEntry& l_Entry = l_Entity.Get<ScriptComponent>().Scripts[it_Index];
                const ScriptClassDescription* l_Class = ScriptClassRegistry::Find(l_Entry.Language, l_Entry.ClassName);
                ScriptBackend* l_Backend = ScriptBackends::Find(l_Entry.Language);
                Instance l_Instance;
                if (l_Class != nullptr && l_Backend != nullptr)
                {
                    l_Instance.Handle = l_Backend->CreateInstance(*l_Class, l_Entity, *this);
                    l_Instance.Backend = l_Instance.Handle != nullptr ? l_Backend : nullptr;
                    l_Instance.Callbacks = l_Class->Callbacks;
                    if (l_Instance.Handle != nullptr)
                    {
                        l_Backend->ApplyFields(l_Instance.Handle, *l_Class, l_Entry, l_State.TargetScene);
                    }
                }
                else
                {
                    l_State.WarnUnknown(l_Entry);
                }

                l_Record.Instances.push_back(l_Instance);
            }

            l_State.Records.push_back(std::move(l_Record));
            const std::size_t l_RecordIndex = l_State.Records.size() - 1;
            l_State.Index[l_ID] = l_RecordIndex;
            for (std::size_t it_Index = 0; it_Index < l_Count; ++it_Index)
            {
                static_cast<void>(l_State.Prepare(l_RecordIndex, it_Index));
            }
        }

        l_State.Pending.clear();
    }

    void ScriptRuntime::FixedUpdate(Timestep step)
    {
        TR_PROFILE_FUNCTION();

        m_State->ForEach(ScriptCallbacks::FixedUpdate, [step](Instance& instance) { instance.Backend->OnFixedUpdate(instance.Handle, step); });
    }

    // Each to the first entity's scripts, then the second's, with the normal turned to point away from whichever is told
    void ScriptRuntime::DeliverEvents(std::span<const PhysicsEvent3D> events)
    {
        for (const PhysicsEvent3D& it_Event : events)
        {
            m_State->Deliver(it_Event.First, it_Event.Type, { it_Event.Second, it_Event.Point, it_Event.Normal, false });
            if (it_Event.Second)
            {
                m_State->Deliver(it_Event.Second, it_Event.Type, { it_Event.First, it_Event.Point, -it_Event.Normal, false });
            }
        }
    }

    void ScriptRuntime::DeliverEvents(std::span<const PhysicsEvent2D> events)
    {
        for (const PhysicsEvent2D& it_Event : events)
        {
            m_State->Deliver(it_Event.First, it_Event.Type, { it_Event.Second, glm::vec3(it_Event.Point, 0.0f), glm::vec3(it_Event.Normal, 0.0f), true });
            if (it_Event.Second)
            {
                m_State->Deliver(it_Event.Second, it_Event.Type, { it_Event.First, glm::vec3(it_Event.Point, 0.0f), glm::vec3(-it_Event.Normal, 0.0f), true });
            }
        }
    }

    void ScriptRuntime::Update(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();

        m_State->ForEach(ScriptCallbacks::Update, [timestep](Instance& instance) { instance.Backend->OnUpdate(instance.Handle, timestep); });
    }

    bool ScriptRuntime::LateUpdate(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();

        return m_State->ForEach(ScriptCallbacks::LateUpdate, [timestep](Instance& instance) { instance.Backend->OnLateUpdate(instance.Handle, timestep); });
    }

    std::uint32_t ScriptRuntime::ReleaseInstances(ScriptLanguage language)
    {
        TR_PROFILE_FUNCTION();

        std::uint32_t l_Released = 0;
        for (Record& it_Record : m_State->Records)
        {
            ScriptComponent* l_Component = it_Record.IsLive() ? m_State->TargetScene.GetRegistry().try_get<ScriptComponent>(it_Record.Handle) : nullptr;
            for (std::size_t it_Index = 0; it_Index < it_Record.Instances.size(); ++it_Index)
            {
                Instance& l_Instance = it_Record.Instances[it_Index];
                if (l_Instance.Backend == nullptr || l_Instance.Backend->GetLanguage() != language)
                {
                    continue;
                }

                if (l_Component != nullptr && it_Index < l_Component->Scripts.size())
                {
                    ScriptEntry& l_Entry = l_Component->Scripts[it_Index];
                    if (const ScriptClassDescription* l_Class = ScriptClassRegistry::Find(l_Entry.Language, l_Entry.ClassName))
                    {
                        l_Instance.Backend->ReadFields(l_Instance.Handle, *l_Class, l_Entry);
                    }
                }

                l_Instance.Backend->DestroyInstance(l_Instance.Handle);
                l_Instance.Backend = nullptr;
                l_Instance.Handle = nullptr;
                l_Instance.Callbacks = 0;
                ++l_Released;
            }
        }

        return l_Released;
    }

    // Records stay where they are, so the order scripts are called in is the order they had before
    std::uint32_t ScriptRuntime::RecreateInstances(ScriptLanguage language)
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        ScriptBackend* l_Backend = ScriptBackends::Find(language);
        if (l_Backend == nullptr)
        {
            return 0;
        }

        std::uint32_t l_Made = 0;
        for (std::size_t it_Record = 0; it_Record < l_State.Records.size(); ++it_Record)
        {
            if (!l_State.Records[it_Record].IsLive())
            {
                continue;
            }

            const Entity l_Entity(l_State.Records[it_Record].Handle, &l_State.TargetScene);
            for (std::size_t it_Index = 0; it_Index < l_State.Records[it_Record].Instances.size(); ++it_Index)
            {
                const ScriptComponent* l_Component = l_State.TargetScene.GetRegistry().try_get<ScriptComponent>(l_State.Records[it_Record].Handle);
                if (l_Component == nullptr || it_Index >= l_Component->Scripts.size())
                {
                    break;
                }

                const ScriptEntry& l_Entry = l_Component->Scripts[it_Index];
                if (l_State.Records[it_Record].Instances[it_Index].Backend != nullptr || l_Entry.Language != language)
                {
                    continue;
                }

                const ScriptClassDescription* l_Class = ScriptClassRegistry::Find(l_Entry.Language, l_Entry.ClassName);
                if (l_Class == nullptr)
                {
                    l_State.WarnUnknown(l_Entry);

                    continue;
                }

                void* l_Handle = l_Backend->CreateInstance(*l_Class, l_Entity, *this);
                if (l_Handle == nullptr)
                {
                    continue;
                }

                l_Backend->ApplyFields(l_Handle, *l_Class, l_Entry, l_State.TargetScene);
                Instance& l_Instance = l_State.Records[it_Record].Instances[it_Index];
                l_Instance.Handle = l_Handle;
                l_Instance.Backend = l_Backend;
                l_Instance.Callbacks = l_Class->Callbacks;
                ++l_Made;

                // At once, enabled or not, since its OnCreate does not run again
                if (l_Instance.Created && (l_Instance.Callbacks & ScriptCallbacks::Reloaded) != 0)
                {
                    l_Backend->OnReloaded(l_Handle);
                    if (!l_State.Records[it_Record].IsLive())
                    {
                        break;
                    }
                }
            }
        }

        return l_Made;
    }

    void ScriptRuntime::DestroyEntity(Entity entity)
    {
        if (!entity || entity.GetScene() != &m_State->TargetScene)
        {
            return;
        }

        m_State->PendingDestroy.push_back(entity.GetUUID());
    }

    // Every script in a subtree has OnDestroy before any of it goes, in hierarchy order, then the subtree is destroyed
    void ScriptRuntime::DestroyPending()
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scripting>> l_Subtree;
        for (std::size_t it_Pending = 0; it_Pending < l_State.PendingDestroy.size(); ++it_Pending)
        {
            const UUID l_ID = l_State.PendingDestroy[it_Pending];
            const Entity l_Root = l_State.TargetScene.FindEntityByUUID(l_ID);
            if (!l_Root)
            {
                continue;
            }

            l_Subtree.clear();
            for (Entity it_Entity = l_Root; it_Entity; it_Entity = l_State.TargetScene.GetNextInSubtree(it_Entity, l_Root))
            {
                l_Subtree.push_back(it_Entity.GetUUID());
            }

            for (const UUID it_Entity : l_Subtree)
            {
                if (const auto a_Found = l_State.Index.find(it_Entity); a_Found != l_State.Index.end() && l_State.Records[a_Found->second].IsLive())
                {
                    l_State.End(a_Found->second, true);
                }
            }

            if (const Entity l_Again = l_State.TargetScene.FindEntityByUUID(l_ID))
            {
                l_State.TargetScene.DestroyEntity(l_Again);
            }
        }

        l_State.PendingDestroy.clear();
        l_State.Compact();
    }

    ScriptRuntime::Statistics ScriptRuntime::GetStatistics() const
    {
        Statistics l_Statistics;
        for (const Record& it_Record : m_State->Records)
        {
            if (!it_Record.IsLive())
            {
                continue;
            }

            for (const Instance& it_Instance : it_Record.Instances)
            {
                ++(it_Instance.Backend != nullptr ? l_Statistics.Instances : l_Statistics.Unknown);
            }
        }

        return l_Statistics;
    }
}