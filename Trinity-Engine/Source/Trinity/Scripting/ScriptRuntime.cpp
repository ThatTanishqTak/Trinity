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
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        // A script's instance in its backend, or no backend when its class has no loaded code. Its serial names it while scripts before it are removed. Created stays when a reload frees the instance, so the one made again knows OnCreate has run. Faulted once a call into it failed, such as a C# script throwing, after which it is not called again, OnDestroy included, until a reload makes it again
        struct Instance
        {
            ScriptBackend* Backend = nullptr;
            void* Handle = nullptr;
            std::uint64_t Serial = 0;
            std::uint32_t Callbacks = 0;
            bool Created = false;
            bool Faulted = false;
        };

        // A script RemoveScript took away, by its entity and its instance's serial
        struct Removal
        {
            UUID Entity;
            std::uint64_t Serial = 0;
        };

        ScriptRuntime* s_Active = nullptr;

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
        State(Scene& scene, ScriptRuntime& runtime, ScriptRuntime* previous) : TargetScene(scene), Runtime(runtime), Previous(previous)
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

        // The live record of the entity, or none
        [[nodiscard]] std::optional<std::size_t> FindRecord(UUID entity) const
        {
            const auto a_Found = Index.find(entity);
            if (a_Found == Index.end() || !Records[a_Found->second].IsLive())
            {
                return std::nullopt;
            }

            return a_Found->second;
        }

        // The entity's record, made when it has none, with an instance for each script its component has and the record lacks, given its fields. Each is in place before its instance is made, so a constructor that adds a script, as C# may, adds it after this one. The record's index, or none when the entity has no scripts
        std::optional<std::size_t> MakeRecord(Entity entity)
        {
            if (!entity || !entity.Has<ScriptComponent>())
            {
                return std::nullopt;
            }

            const UUID l_ID = entity.GetUUID();
            std::optional<std::size_t> l_Record = FindRecord(l_ID);
            if (!l_Record)
            {
                Record l_New;
                l_New.ID = l_ID;
                l_New.Handle = entity.GetHandle();
                Records.push_back(std::move(l_New));
                l_Record = Records.size() - 1;
                Index[l_ID] = *l_Record;
            }

            for (std::size_t it_Index = Records[*l_Record].Instances.size(); Records[*l_Record].IsLive() && entity.Has<ScriptComponent>() && it_Index < entity.Get<ScriptComponent>().Scripts.size(); ++it_Index)
            {
                Records[*l_Record].Instances.push_back({ .Serial = ++NextSerial });

                // Copied, since a constructor that adds a script moves the component's list
                const ScriptEntry l_Entry = entity.Get<ScriptComponent>().Scripts[it_Index];
                const ScriptClassDescription* l_Class = ScriptClassRegistry::Find(l_Entry.Language, l_Entry.ClassName);
                ScriptBackend* l_Backend = ScriptBackends::Find(l_Entry.Language);
                if (l_Class == nullptr || l_Backend == nullptr)
                {
                    WarnUnknown(l_Entry);

                    continue;
                }

                const std::uint32_t l_Callbacks = l_Class->Callbacks;
                void* l_Handle = l_Backend->CreateInstance(*l_Class, entity, Runtime);
                if (l_Handle == nullptr)
                {
                    continue;
                }

                Instance& l_Instance = Records[*l_Record].Instances[it_Index];
                l_Instance.Handle = l_Handle;
                l_Instance.Backend = l_Backend;
                l_Instance.Callbacks = l_Callbacks;
                if (const ScriptClassDescription* l_Again = ScriptClassRegistry::Find(l_Entry.Language, l_Entry.ClassName))
                {
                    const bool l_Applied = l_Backend->ApplyFields(l_Handle, *l_Again, l_Entry, TargetScene);
                    Records[*l_Record].Instances[it_Index].Faulted = !l_Applied;
                }
            }

            return l_Record;
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
            if (l_Instance.Faulted)
            {
                return nullptr;
            }

            if (!l_Instance.Created)
            {
                l_Instance.Created = true;
                if ((l_Instance.Callbacks & ScriptCallbacks::Create) != 0 && !l_Instance.Backend->OnCreate(l_Instance.Handle))
                {
                    Records[record].Instances[index].Faulted = true;

                    return nullptr;
                }

                // OnCreate may have taken the component away
                if (!Records[record].IsLive())
                {
                    return nullptr;
                }
            }

            return &Records[record].Instances[index];
        }

        // Calls the callback on every instance that has it, in order, and stops calling one whose call failed. Callbacks never add records, so indices hold across them
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
                        if (!call(*l_Instance))
                        {
                            Records[it_Record].Instances[it_Index].Faulted = true;
                        }
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
                if (l_Instance != nullptr && (l_Instance->Callbacks & ScriptCallbacks::Physics) != 0 && !l_Instance->Backend->OnPhysicsEvent(l_Instance->Handle, type, collision))
                {
                    Records[l_Record].Instances[it_Index].Faulted = true;
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

                if (callOnDestroy && l_Instance.Created && !l_Instance.Faulted && (l_Instance.Callbacks & ScriptCallbacks::Destroy) != 0)
                {
                    static_cast<void>(l_Instance.Backend->OnDestroy(l_Instance.Handle));
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
        ScriptRuntime& Runtime;
        ScriptRuntime* Previous = nullptr;
        std::vector<Record, TaggedAllocator<Record, MemoryTag::Scripting>> Records;
        std::unordered_map<UUID, std::size_t, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, std::size_t>, MemoryTag::Scripting>> Index;
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scripting>> Pending;
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scripting>> PendingDestroy;
        std::vector<Removal, TaggedAllocator<Removal, MemoryTag::Scripting>> PendingRemove;
        std::vector<TaggedString<MemoryTag::Scripting>, TaggedAllocator<TaggedString<MemoryTag::Scripting>, MemoryTag::Scripting>> Warned;
        std::uint64_t NextSerial = 0;
        bool Changed = false;
    };

    // The scene's scripts wait in hierarchy order for the first CreatePending, and any added later follow them as they are added
    ScriptRuntime::ScriptRuntime(Scene& scene) : m_State(Memory::New<State>(MemoryTag::Scripting, scene, *this, s_Active))
    {
        s_Active = this;
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

        if (s_Active == this)
        {
            s_Active = m_State->Previous;
        }

        Memory::Delete(m_State);
    }

    ScriptRuntime* ScriptRuntime::GetActive()
    {
        return s_Active;
    }

    Scene& ScriptRuntime::GetScene()
    {
        return m_State->TargetScene;
    }

    void ScriptRuntime::BeginFrame(Timestep timestep, Timestep step)
    {
        m_Time.DeltaTime = timestep.GetSeconds();
        m_Time.FixedDeltaTime = step.GetSeconds();
        m_Time.Elapsed += static_cast<double>(timestep.GetDuration().count()) * 1e-9;
        ++m_Time.FrameCount;
    }

    void ScriptRuntime::SetPhysics(PhysicsScene3D* physics, PhysicsScene2D* physics2D)
    {
        m_Physics = physics;
        m_Physics2D = physics2D;
    }

    void ScriptRuntime::CreatePending()
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        for (std::size_t it_Pending = 0; it_Pending < l_State.Pending.size(); ++it_Pending)
        {
            const std::optional<std::size_t> l_Record = l_State.MakeRecord(l_State.TargetScene.FindEntityByUUID(l_State.Pending[it_Pending]));
            for (std::size_t it_Index = 0; l_Record && it_Index < l_State.Records[*l_Record].Instances.size(); ++it_Index)
            {
                static_cast<void>(l_State.Prepare(*l_Record, it_Index));
            }
        }

        l_State.Pending.clear();
    }

    void ScriptRuntime::FixedUpdate(Timestep step)
    {
        TR_PROFILE_FUNCTION();

        ++m_Time.StepCount;
        m_State->ForEach(ScriptCallbacks::FixedUpdate, [step](Instance& instance) { return instance.Backend->OnFixedUpdate(instance.Handle, step); });
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

        m_State->ForEach(ScriptCallbacks::Update, [timestep](Instance& instance) { return instance.Backend->OnUpdate(instance.Handle, timestep); });
    }

    bool ScriptRuntime::LateUpdate(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();

        return m_State->ForEach(ScriptCallbacks::LateUpdate, [timestep](Instance& instance) { return instance.Backend->OnLateUpdate(instance.Handle, timestep); });
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

                // The new code gets its chance, so an instance that faulted before is called again
                Instance& l_Instance = l_State.Records[it_Record].Instances[it_Index];
                l_Instance.Handle = l_Handle;
                l_Instance.Backend = l_Backend;
                l_Instance.Callbacks = l_Class->Callbacks;
                l_Instance.Faulted = !l_Backend->ApplyFields(l_Handle, *l_Class, l_Entry, l_State.TargetScene);
                ++l_Made;

                // At once, enabled or not, since its OnCreate does not run again
                if (l_Instance.Created && !l_Instance.Faulted && (l_Instance.Callbacks & ScriptCallbacks::Reloaded) != 0)
                {
                    if (!l_Backend->OnReloaded(l_Handle))
                    {
                        l_State.Records[it_Record].Instances[it_Index].Faulted = true;
                    }

                    if (!l_State.Records[it_Record].IsLive())
                    {
                        break;
                    }
                }
            }
        }

        return l_Made;
    }

    Entity ScriptRuntime::DuplicateEntity(Entity entity)
    {
        if (!entity || entity.GetScene() != &m_State->TargetScene)
        {
            return {};
        }

        const Entity l_Copy = m_State->TargetScene.DuplicateEntity(entity);
        for (Entity it_Entity = l_Copy; it_Entity; it_Entity = m_State->TargetScene.GetNextInSubtree(it_Entity, l_Copy))
        {
            static_cast<void>(m_State->MakeRecord(it_Entity));
        }

        return l_Copy;
    }

    void ScriptRuntime::DestroyEntity(Entity entity)
    {
        if (!entity || entity.GetScene() != &m_State->TargetScene)
        {
            return;
        }

        m_State->PendingDestroy.push_back(entity.GetUUID());
    }

    void* ScriptRuntime::GetInstance(Entity entity, std::size_t index, ScriptLanguage language) const
    {
        if (!entity || entity.GetScene() != &m_State->TargetScene)
        {
            return nullptr;
        }

        const std::optional<std::size_t> l_Record = m_State->FindRecord(entity.GetUUID());
        if (!l_Record || index >= m_State->Records[*l_Record].Instances.size())
        {
            return nullptr;
        }

        const Instance& l_Instance = m_State->Records[*l_Record].Instances[index];

        return l_Instance.Backend != nullptr && l_Instance.Backend->GetLanguage() == language ? l_Instance.Handle : nullptr;
    }

    std::int32_t ScriptRuntime::FindScript(Entity entity, ScriptLanguage language, std::string_view className) const
    {
        if (!entity || entity.GetScene() != &m_State->TargetScene || !entity.Has<ScriptComponent>())
        {
            return -1;
        }

        const auto& l_Scripts = entity.Get<ScriptComponent>().Scripts;
        const auto a_Found = std::ranges::find_if(l_Scripts, [language, className](const ScriptEntry& entry) { return entry.Language == language && std::string_view(entry.ClassName) == className; });

        return a_Found != l_Scripts.end() ? static_cast<std::int32_t>(a_Found - l_Scripts.begin()) : -1;
    }

    // In Pending too, so the next CreatePending gives it OnCreate if no call has yet
    std::int32_t ScriptRuntime::AddScript(Entity entity, ScriptLanguage language, std::string_view className)
    {
        if (!entity || entity.GetScene() != &m_State->TargetScene)
        {
            return -1;
        }

        const ScriptClassDescription* l_Class = ScriptClassRegistry::Find(language, className);
        if (l_Class == nullptr)
        {
            TR_CORE_ERROR("Scripting: no loaded code has {} class {}, so it was not added to {}", ToString(language), className, std::string_view(entity.Get<TagComponent>().Tag));

            return -1;
        }

        ScriptEntry l_Entry = ScriptClassRegistry::CreateEntry(*l_Class);
        if (!entity.Has<ScriptComponent>())
        {
            entity.Add<ScriptComponent>();
        }

        auto& l_Scripts = entity.Get<ScriptComponent>().Scripts;
        l_Scripts.push_back(std::move(l_Entry));
        const std::int32_t l_Index = static_cast<std::int32_t>(l_Scripts.size() - 1);
        m_State->Pending.push_back(entity.GetUUID());
        static_cast<void>(m_State->MakeRecord(entity));

        return l_Index;
    }

    bool ScriptRuntime::RemoveScript(Entity entity, std::size_t index)
    {
        if (!entity || entity.GetScene() != &m_State->TargetScene || !entity.Has<ScriptComponent>() || index >= entity.Get<ScriptComponent>().Scripts.size())
        {
            return false;
        }

        const std::optional<std::size_t> l_Record = m_State->FindRecord(entity.GetUUID());
        if (!l_Record || index >= m_State->Records[*l_Record].Instances.size())
        {
            auto& l_Scripts = entity.Get<ScriptComponent>().Scripts;
            l_Scripts.erase(l_Scripts.begin() + static_cast<std::ptrdiff_t>(index));

            return true;
        }

        m_State->PendingRemove.push_back({ entity.GetUUID(), m_State->Records[*l_Record].Instances[index].Serial });

        return true;
    }

    // Each removed script has OnDestroy, if it had OnCreate, before it and its instance go. Then every script in a destroyed subtree has OnDestroy before any of it goes, in hierarchy order, and the subtree is destroyed
    void ScriptRuntime::DestroyPending()
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        for (std::size_t it_Pending = 0; it_Pending < l_State.PendingRemove.size(); ++it_Pending)
        {
            const Removal l_Removal = l_State.PendingRemove[it_Pending];
            const auto a_FindIndex = [&l_State, &l_Removal]() -> std::optional<std::pair<std::size_t, std::size_t>>
            {
                const std::optional<std::size_t> l_Record = l_State.FindRecord(l_Removal.Entity);
                if (!l_Record)
                {
                    return std::nullopt;
                }

                const auto& l_Instances = l_State.Records[*l_Record].Instances;
                const auto a_Found = std::ranges::find(l_Instances, l_Removal.Serial, &Instance::Serial);

                return a_Found != l_Instances.end() ? std::optional(std::pair(*l_Record, static_cast<std::size_t>(a_Found - l_Instances.begin()))) : std::nullopt;
            };

            std::optional<std::pair<std::size_t, std::size_t>> l_Found = a_FindIndex();
            if (!l_Found)
            {
                continue;
            }

            const Instance l_Instance = l_State.Records[l_Found->first].Instances[l_Found->second];
            if (l_Instance.Backend != nullptr && l_Instance.Created && !l_Instance.Faulted && (l_Instance.Callbacks & ScriptCallbacks::Destroy) != 0)
            {
                static_cast<void>(l_Instance.Backend->OnDestroy(l_Instance.Handle));

                // OnDestroy may have added or removed scripts
                l_Found = a_FindIndex();
                if (!l_Found)
                {
                    continue;
                }
            }

            Record& l_Record = l_State.Records[l_Found->first];
            const Instance l_Current = l_Record.Instances[l_Found->second];
            if (l_Current.Backend != nullptr)
            {
                l_Current.Backend->DestroyInstance(l_Current.Handle);
            }

            l_Record.Instances.erase(l_Record.Instances.begin() + static_cast<std::ptrdiff_t>(l_Found->second));
            if (ScriptComponent* l_Component = l_State.TargetScene.GetRegistry().try_get<ScriptComponent>(l_Record.Handle); l_Component != nullptr && l_Found->second < l_Component->Scripts.size())
            {
                l_Component->Scripts.erase(l_Component->Scripts.begin() + static_cast<std::ptrdiff_t>(l_Found->second));
            }
        }

        l_State.PendingRemove.clear();

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
                if (const std::optional<std::size_t> l_Record = l_State.FindRecord(it_Entity))
                {
                    l_State.End(*l_Record, true);
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
                l_Statistics.Faulted += it_Instance.Backend != nullptr && it_Instance.Faulted ? 1u : 0u;
            }
        }

        return l_Statistics;
    }
}