#include "Trinity/Scripting/ScriptRuntime.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/Scene/SceneManager.hpp"
#include "Trinity/Scene/SceneRuntime.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"
#include "Trinity/Scripting/ScriptBackend.hpp"
#include "Trinity/Scripting/ScriptClassRegistry.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <iterator>
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

        // A timer or a coroutine, by its owner's instance handle. Due counts the fixed steps a wait in seconds or a timer ends at, and the frame a wait in frames does, and Armed the step and frame it began waiting in, so none runs again in the step or frame it waited in. Interval is a repeating timer's, in steps, 0 for one that runs once. Running while its resume is under way, and Dead once it has finished or been stopped, freed then unless still running
        struct Task
        {
            std::uint64_t ID = 0;
            const void* Owner = nullptr;
            void* Object = nullptr;
            ScriptTaskCalls Calls;
            ScriptWait Wait;
            std::uint64_t Due = 0;
            std::uint64_t ArmedStep = 0;
            std::uint64_t ArmedFrame = 0;
            std::uint64_t Interval = 0;
            ScriptTaskKind Kind = ScriptTaskKind::Coroutine;
            bool Running = false;
            bool Dead = false;
        };

        // The time each language's scripts take, charged to the language called into last until a call into the other, or until the part of the frame being timed ends. Only a clock read a change of language, so a scene of one language reads it twice for each part
        class LanguageClock
        {
        public:
            using Clock = std::chrono::steady_clock;

            void Enter(ScriptLanguage language)
            {
                if (m_Depth == 0 || m_Current == language)
                {
                    return;
                }

                const Clock::time_point l_Now = Clock::now();
                Charge(l_Now);
                m_Current = language;
                m_Since = l_Now;
            }

            void Begin()
            {
                ++m_Depth;
            }

            void End()
            {
                if (m_Depth > 0 && --m_Depth == 0 && m_Current)
                {
                    Charge(Clock::now());
                    m_Current.reset();
                }
            }

            // The frame's time becomes what is reported, and the next frame starts from nothing
            void EndFrame()
            {
                for (std::size_t it_Language = 0; it_Language < m_Frame.size(); ++it_Language)
                {
                    m_Last[it_Language] = std::chrono::duration<float, std::milli>(m_Frame[it_Language]).count();
                    m_Frame[it_Language] = {};
                }
            }

            [[nodiscard]] float GetMilliseconds(ScriptLanguage language) const
            {
                return m_Last[static_cast<std::size_t>(language)];
            }

        private:
            void Charge(Clock::time_point now)
            {
                if (m_Current)
                {
                    m_Frame[static_cast<std::size_t>(*m_Current)] += now - m_Since;
                }
            }

            std::array<Clock::duration, 2> m_Frame{};
            std::array<float, 2> m_Last{};
            std::optional<ScriptLanguage> m_Current;
            Clock::time_point m_Since;
            std::uint32_t m_Depth = 0;
        };

        // Times each part of the frame the runtime runs scripts in, nested parts counting as one
        class TimedPart
        {
        public:
            explicit TimedPart(LanguageClock& clock) : m_Clock(clock)
            {
                m_Clock.Begin();
            }

            ~TimedPart()
            {
                m_Clock.End();
            }

            TimedPart(const TimedPart&) = delete;
            TimedPart& operator=(const TimedPart&) = delete;

        private:
            LanguageClock& m_Clock;
        };

        ScriptRuntime* s_Active = nullptr;
        // Across every runtime, so a task carried into another keeps the ID its script holds
        std::uint64_t s_NextTask = 0;

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
                Time.Enter(l_Entry.Language);
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
                Time.Enter(l_Instance.Backend->GetLanguage());
                if ((l_Instance.Callbacks & ScriptCallbacks::Create) != 0 && !l_Instance.Backend->OnCreate(l_Instance.Handle))
                {
                    Fault(Records[record].Instances[index]);

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
                        Time.Enter(l_Instance->Backend->GetLanguage());
                        if (!call(*l_Instance))
                        {
                            Fault(Records[it_Record].Instances[it_Index]);
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
                if (l_Instance == nullptr || (l_Instance->Callbacks & ScriptCallbacks::Physics) == 0)
                {
                    continue;
                }

                Time.Enter(l_Instance->Backend->GetLanguage());
                if (!l_Instance->Backend->OnPhysicsEvent(l_Instance->Handle, type, collision))
                {
                    Fault(Records[l_Record].Instances[it_Index]);
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
                    Time.Enter(l_Instance.Backend->GetLanguage());
                    static_cast<void>(l_Instance.Backend->OnDestroy(l_Instance.Handle));
                }

                static_cast<void>(StopOwner(l_Instance.Handle));
                l_Instance.Backend->DestroyInstance(l_Instance.Handle);
                Records[record].Instances[it_Index].Backend = nullptr;
            }

            Records[record].Dead = true;
            Changed = true;
        }

        // A failed instance is called no more, and its timers and coroutines stop
        void Fault(Instance& instance)
        {
            instance.Faulted = true;
            static_cast<void>(StopOwner(instance.Handle));
        }

        // The steps a wait of that many seconds takes, rounded up, so it ends on the first step at or past its time, with a little slack for a time a whole number of steps long
        [[nodiscard]] std::uint64_t StepsFor(float seconds) const
        {
            const float l_Step = Runtime.GetTime().FixedDeltaTime;
            if (!(seconds > 0.0f) || !(l_Step > 0.0f))
            {
                return 0;
            }

            return static_cast<std::uint64_t>(std::max(std::ceil(static_cast<double>(seconds) / static_cast<double>(l_Step) - 1e-3), 0.0));
        }

        void Arm(Task& task, const ScriptWait& wait) const
        {
            const ScriptTime& l_Time = Runtime.GetTime();
            task.Wait = wait;
            task.ArmedStep = l_Time.StepCount;
            task.ArmedFrame = l_Time.FrameCount;
            if (wait.Type == ScriptWaitType::Seconds)
            {
                task.Due = l_Time.StepCount + StepsFor(wait.Seconds);
            }
            else if (wait.Type == ScriptWaitType::Frames)
            {
                task.Due = l_Time.FrameCount + std::max<std::uint64_t>(wait.Frames, 1);
            }
        }

        // Freed now, or once its resume returns when it is running
        void Finish(Task& task)
        {
            if (task.Dead)
            {
                return;
            }

            task.Dead = true;
            TasksChanged = true;
            if (!task.Running)
            {
                task.Calls.Free(task.Object);
                task.Object = nullptr;
            }
        }

        // Every task of the instance, of either kind or of the one asked for, the number stopped
        std::uint32_t StopOwner(const void* owner, std::optional<ScriptTaskKind> kind = std::nullopt)
        {
            std::uint32_t l_Stopped = 0;
            for (Task& it_Task : Tasks)
            {
                if (!it_Task.Dead && it_Task.Owner == owner && (!kind || it_Task.Kind == *kind))
                {
                    Finish(it_Task);
                    ++l_Stopped;
                }
            }

            CompactTasks();

            return l_Stopped;
        }

        // The instance a failed task belonged to, which then fails as a script whose callback failed does
        void FaultOwner(const void* owner)
        {
            for (Record& it_Record : Records)
            {
                for (Instance& it_Instance : it_Record.Instances)
                {
                    if (it_Instance.Backend != nullptr && it_Instance.Handle == owner)
                    {
                        Fault(it_Instance);

                        return;
                    }
                }
            }

            static_cast<void>(StopOwner(owner));
        }

        // The task at that index runs until it next waits, finishes, or fails. What it starts meanwhile goes on the end of the list, which may move, so it is found again by its index
        void Run(std::size_t index)
        {
            const ScriptTaskCalls l_Calls = Tasks[index].Calls;
            void* l_Object = Tasks[index].Object;
            Tasks[index].Running = true;
            ++TaskDepth;
            ScriptWait l_Next;
            Time.Enter(l_Calls.Language);
            const bool l_Resumed = l_Calls.Resume(l_Object, l_Next);
            --TaskDepth;

            Task& l_Task = Tasks[index];
            l_Task.Running = false;
            if (l_Task.Dead)
            {
                l_Calls.Free(l_Object);
                l_Task.Object = nullptr;
            }
            else if (!l_Resumed)
            {
                const void* l_Owner = l_Task.Owner;
                Finish(l_Task);
                FaultOwner(l_Owner);
            }
            else if (l_Task.Kind == ScriptTaskKind::Timer)
            {
                if (l_Task.Interval == 0)
                {
                    Finish(l_Task);
                }
                else
                {
                    l_Task.Due += l_Task.Interval;
                }
            }
            else if (l_Next.Type == ScriptWaitType::Done)
            {
                Finish(l_Task);
            }
            else
            {
                Arm(l_Task, l_Next);
            }
        }

        // Asked as the task would run, since a condition of C#'s may start or stop tasks, or throw
        [[nodiscard]] bool IsMet(std::size_t index)
        {
            const Task l_Task = Tasks[index];
            Time.Enter(l_Task.Calls.Language);
            if (l_Task.Wait.Condition != nullptr)
            {
                return l_Task.Wait.Condition(l_Task.Wait.Context);
            }

            if (l_Task.Calls.Check == nullptr)
            {
                return true;
            }

            Tasks[index].Running = true;
            ++TaskDepth;
            bool l_Met = false;
            const bool l_Asked = l_Task.Calls.Check(l_Task.Object, l_Met);
            --TaskDepth;
            Tasks[index].Running = false;
            if (Tasks[index].Dead)
            {
                l_Task.Calls.Free(l_Task.Object);
                Tasks[index].Object = nullptr;

                return false;
            }

            if (!l_Asked)
            {
                Finish(Tasks[index]);
                FaultOwner(l_Task.Owner);

                return false;
            }

            return l_Met;
        }

        // In each fixed step, after the scripts' OnFixedUpdate: due timers, and coroutines waiting for the step. Those started meanwhile wait for the next
        void RunStepTasks()
        {
            const std::uint64_t l_Step = Runtime.GetTime().StepCount;
            const std::size_t l_Count = Tasks.size();
            for (std::size_t it_Task = 0; it_Task < l_Count; ++it_Task)
            {
                const Task& l_Task = Tasks[it_Task];
                if (l_Task.Dead || l_Task.Running)
                {
                    continue;
                }

                const bool l_Due = l_Task.Kind == ScriptTaskKind::Timer ? l_Task.Due <= l_Step : l_Task.Wait.Type == ScriptWaitType::FixedStep && l_Task.ArmedStep < l_Step;
                if (l_Due)
                {
                    Run(it_Task);
                }
            }

            CompactTasks();
        }

        // After the scripts' OnUpdate: coroutines whose seconds the frame's steps reached, whose frames have passed, or whose condition holds, none in the frame it waited in
        void RunFrameTasks()
        {
            const ScriptTime& l_Time = Runtime.GetTime();
            const std::size_t l_Count = Tasks.size();
            for (std::size_t it_Task = 0; it_Task < l_Count; ++it_Task)
            {
                const Task& l_Task = Tasks[it_Task];
                if (l_Task.Dead || l_Task.Running || l_Task.Kind != ScriptTaskKind::Coroutine || l_Task.ArmedFrame >= l_Time.FrameCount)
                {
                    continue;
                }

                bool l_Due = false;
                switch (l_Task.Wait.Type)
                {
                    case ScriptWaitType::Seconds:
                    {
                        l_Due = l_Task.Due <= l_Time.StepCount;

                        break;
                    }
                    case ScriptWaitType::Frames:
                    {
                        l_Due = l_Task.Due <= l_Time.FrameCount;

                        break;
                    }
                    case ScriptWaitType::Condition:
                    {
                        l_Due = IsMet(it_Task);

                        break;
                    }
                    case ScriptWaitType::Done:
                    case ScriptWaitType::FixedStep:
                    {
                        break;
                    }
                }

                if (l_Due && !Tasks[it_Task].Dead)
                {
                    Run(it_Task);
                }
            }

            CompactTasks();
        }

        // Drops finished tasks once none runs, which frees nothing, since each was freed as it finished
        void CompactTasks()
        {
            if (TaskDepth == 0 && std::exchange(TasksChanged, false))
            {
                std::erase_if(Tasks, [](const Task& task) { return task.Dead; });
            }
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
        std::vector<InstancingTime, TaggedAllocator<InstancingTime, MemoryTag::Scripting>> Instancing;
        // Prefabs an instance of was refused, each logged once
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scripting>> Refused;
        std::vector<Task, TaggedAllocator<Task, MemoryTag::Scripting>> Tasks;
        std::uint64_t NextSerial = 0;
        std::uint32_t TaskDepth = 0;
        LanguageClock Time;
        bool Changed = false;
        bool TasksChanged = false;
    };

    // The step and frame the carry left its runtime at, so each wait goes on with what was left of it
    struct ScriptCarry::Data
    {
        std::vector<Record, TaggedAllocator<Record, MemoryTag::Scripting>> Records;
        std::vector<Task, TaggedAllocator<Task, MemoryTag::Scripting>> Tasks;
        std::uint64_t StepCount = 0;
        std::uint64_t FrameCount = 0;
    };

    ScriptCarry::~ScriptCarry()
    {
        Free();
    }

    ScriptCarry::ScriptCarry(ScriptCarry&& other) noexcept : m_Data(std::exchange(other.m_Data, nullptr))
    {

    }

    ScriptCarry& ScriptCarry::operator=(ScriptCarry&& other) noexcept
    {
        if (this != &other)
        {
            Free();
            m_Data = std::exchange(other.m_Data, nullptr);
        }

        return *this;
    }

    std::size_t ScriptCarry::GetInstanceCount() const
    {
        std::size_t l_Count = 0;
        if (m_Data == nullptr)
        {
            return 0;
        }

        for (const Record& it_Record : m_Data->Records)
        {
            l_Count += static_cast<std::size_t>(std::ranges::count_if(it_Record.Instances, [](const Instance& instance) { return instance.Backend != nullptr; }));
        }

        return l_Count;
    }

    std::size_t ScriptCarry::GetTaskCount() const
    {
        return m_Data != nullptr ? m_Data->Tasks.size() : 0;
    }

    // Tasks first, since a C# task holds its script
    void ScriptCarry::Free()
    {
        if (m_Data == nullptr)
        {
            return;
        }

        for (Task& it_Task : m_Data->Tasks)
        {
            it_Task.Calls.Free(it_Task.Object);
        }

        for (Record& it_Record : m_Data->Records)
        {
            for (const Instance& it_Instance : it_Record.Instances)
            {
                if (it_Instance.Backend != nullptr)
                {
                    it_Instance.Backend->DestroyInstance(it_Instance.Handle);
                }
            }
        }

        Memory::Delete(m_Data);
        m_Data = nullptr;
    }

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

        // Each went with its script, so none should be left
        for (Task& it_Task : m_State->Tasks)
        {
            m_State->Finish(it_Task);
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

    bool ScriptRuntime::CreatePending()
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        if (l_State.Pending.empty())
        {
            return false;
        }

        const TimedPart l_Timed(l_State.Time);
        for (std::size_t it_Pending = 0; it_Pending < l_State.Pending.size(); ++it_Pending)
        {
            const std::optional<std::size_t> l_Record = l_State.MakeRecord(l_State.TargetScene.FindEntityByUUID(l_State.Pending[it_Pending]));
            for (std::size_t it_Index = 0; l_Record && it_Index < l_State.Records[*l_Record].Instances.size(); ++it_Index)
            {
                static_cast<void>(l_State.Prepare(*l_Record, it_Index));
            }
        }

        l_State.Pending.clear();

        return true;
    }

    // Only C# has handlers, since a C++ script reads its actions as it runs
    void ScriptRuntime::RaiseInputEvents()
    {
        TR_PROFILE_FUNCTION();

        ScriptBackend* l_Backend = ScriptBackends::Find(ScriptLanguage::CSharp);
        if (l_Backend == nullptr)
        {
            return;
        }

        const TimedPart l_Timed(m_State->Time);
        m_State->Time.Enter(ScriptLanguage::CSharp);
        l_Backend->RaiseInputEvents();
    }

    void ScriptRuntime::FixedUpdate(Timestep step)
    {
        TR_PROFILE_FUNCTION();

        ++m_Time.StepCount;
        const TimedPart l_Timed(m_State->Time);
        m_State->ForEach(ScriptCallbacks::FixedUpdate, [step](Instance& instance) { return instance.Backend->OnFixedUpdate(instance.Handle, step); });
        m_State->RunStepTasks();
    }

    // Each to the first entity's scripts, then the second's, with the normal turned to point away from whichever is told. A character's hit goes to the character's alone, with the normal of the surface it hit
    void ScriptRuntime::DeliverEvents(std::span<const PhysicsEvent3D> events)
    {
        const TimedPart l_Timed(m_State->Time);
        for (const PhysicsEvent3D& it_Event : events)
        {
            m_State->Deliver(it_Event.First, it_Event.Type, { it_Event.Second, it_Event.Point, it_Event.Normal, false });
            if (it_Event.Second && it_Event.Type != PhysicsEventType::CharacterHit)
            {
                m_State->Deliver(it_Event.Second, it_Event.Type, { it_Event.First, it_Event.Point, -it_Event.Normal, false });
            }
        }
    }

    void ScriptRuntime::DeliverEvents(std::span<const PhysicsEvent2D> events)
    {
        const TimedPart l_Timed(m_State->Time);
        for (const PhysicsEvent2D& it_Event : events)
        {
            m_State->Deliver(it_Event.First, it_Event.Type, { it_Event.Second, glm::vec3(it_Event.Point, 0.0f), glm::vec3(it_Event.Normal, 0.0f), true });
            if (it_Event.Second && it_Event.Type != PhysicsEventType::CharacterHit)
            {
                m_State->Deliver(it_Event.Second, it_Event.Type, { it_Event.First, glm::vec3(it_Event.Point, 0.0f), glm::vec3(-it_Event.Normal, 0.0f), true });
            }
        }
    }

    void ScriptRuntime::Update(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();

        const TimedPart l_Timed(m_State->Time);
        m_State->ForEach(ScriptCallbacks::Update, [timestep](Instance& instance) { return instance.Backend->OnUpdate(instance.Handle, timestep); });
        m_State->RunFrameTasks();
    }

    bool ScriptRuntime::LateUpdate(Timestep timestep)
    {
        TR_PROFILE_FUNCTION();

        const TimedPart l_Timed(m_State->Time);

        return m_State->ForEach(ScriptCallbacks::LateUpdate, [timestep](Instance& instance) { return instance.Backend->OnLateUpdate(instance.Handle, timestep); });
    }

    std::uint32_t ScriptRuntime::ReleaseInstances(ScriptLanguage language)
    {
        TR_PROFILE_FUNCTION();

        std::uint32_t l_Released = 0;
        std::uint32_t l_Cancelled = 0;
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

                l_Cancelled += m_State->StopOwner(l_Instance.Handle);
                l_Instance.Backend->DestroyInstance(l_Instance.Handle);
                l_Instance.Backend = nullptr;
                l_Instance.Handle = nullptr;
                l_Instance.Callbacks = 0;
                ++l_Released;
            }
        }

        if (l_Cancelled != 0)
        {
            TR_CORE_INFO("Scripting: the reload cancelled {} waiting {} timer(s) and coroutine(s)", l_Cancelled, ToString(language));
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
                        l_State.Fault(l_State.Records[it_Record].Instances[it_Index]);
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

    std::uint64_t ScriptRuntime::StartTask(const void* owner, ScriptTaskKind kind, const ScriptTaskCalls& calls, void* task, const ScriptWait& wait, float interval)
    {
        TR_CORE_ASSERT(calls.Resume != nullptr && calls.Free != nullptr, "Scripting: a task needs a way to resume it and one to free it");

        State& l_State = *m_State;
        Task l_Task;
        l_Task.ID = ++s_NextTask;
        l_Task.Owner = owner;
        l_Task.Object = task;
        l_Task.Calls = calls;
        l_Task.Kind = kind;
        if (kind == ScriptTaskKind::Timer)
        {
            l_State.Arm(l_Task, { .Type = ScriptWaitType::Seconds, .Seconds = wait.Seconds });
            l_Task.Interval = interval > 0.0f ? std::max<std::uint64_t>(l_State.StepsFor(interval), 1) : 0;
        }
        else
        {
            l_State.Arm(l_Task, wait.Type == ScriptWaitType::Done ? ScriptWait{ .Type = ScriptWaitType::Frames, .Frames = 1 } : wait);
        }

        l_State.Tasks.push_back(l_Task);

        return l_Task.ID;
    }

    bool ScriptRuntime::StopTask(std::uint64_t id)
    {
        const auto a_Found = std::ranges::find(m_State->Tasks, id, &Task::ID);
        if (a_Found == m_State->Tasks.end() || a_Found->Dead)
        {
            return false;
        }

        m_State->Finish(*a_Found);
        m_State->CompactTasks();

        return true;
    }

    std::uint32_t ScriptRuntime::StopTasks(const void* owner, ScriptTaskKind kind)
    {
        return m_State->StopOwner(owner, kind);
    }

    bool ScriptRuntime::IsTaskRunning(std::uint64_t id) const
    {
        const auto a_Found = std::ranges::find(m_State->Tasks, id, &Task::ID);

        return a_Found != m_State->Tasks.end() && !a_Found->Dead;
    }

    // Each record leaves its place dead and empty, which the next Compact drops, and each of its instances' tasks goes with it
    ScriptCarry ScriptRuntime::Extract(std::span<const UUID> entities)
    {
        State& l_State = *m_State;
        ScriptCarry l_Carry;
        l_Carry.m_Data = Memory::New<ScriptCarry::Data>(MemoryTag::Scripting);
        l_Carry.m_Data->StepCount = m_Time.StepCount;
        l_Carry.m_Data->FrameCount = m_Time.FrameCount;
        for (const UUID it_Entity : entities)
        {
            const std::optional<std::size_t> l_Found = l_State.FindRecord(it_Entity);
            if (!l_Found)
            {
                continue;
            }

            Record& l_Record = l_State.Records[*l_Found];
            Record l_Moved;
            l_Moved.ID = l_Record.ID;
            l_Moved.Instances = std::move(l_Record.Instances);
            l_Record.Instances.clear();
            l_Record.Dead = true;
            l_State.Changed = true;
            for (const Instance& it_Instance : l_Moved.Instances)
            {
                for (Task& it_Task : l_State.Tasks)
                {
                    if (!it_Task.Dead && it_Instance.Backend != nullptr && it_Task.Owner == it_Instance.Handle)
                    {
                        l_Carry.m_Data->Tasks.push_back(it_Task);
                        it_Task.Dead = true;
                        it_Task.Object = nullptr;
                        l_State.TasksChanged = true;
                    }
                }
            }

            l_Carry.m_Data->Records.push_back(std::move(l_Moved));
        }

        l_State.CompactTasks();
        l_State.Compact();

        return l_Carry;
    }

    void ScriptRuntime::Adopt(ScriptCarry& carry)
    {
        if (carry.m_Data == nullptr)
        {
            return;
        }

        State& l_State = *m_State;
        ScriptCarry::Data& l_Data = *carry.m_Data;
        std::vector<const void*, TaggedAllocator<const void*, MemoryTag::Scripting>> l_Owners;
        for (Record& it_Record : l_Data.Records)
        {
            const Entity l_Entity = l_State.TargetScene.FindEntityByUUID(it_Record.ID);
            if (!l_Entity || !l_Entity.Has<ScriptComponent>() || l_State.FindRecord(it_Record.ID))
            {
                continue;
            }

            Record l_Record;
            l_Record.ID = it_Record.ID;
            l_Record.Handle = l_Entity.GetHandle();
            l_Record.Instances = std::move(it_Record.Instances);
            it_Record.Instances.clear();
            for (Instance& it_Instance : l_Record.Instances)
            {
                if (it_Instance.Backend != nullptr)
                {
                    it_Instance.Backend->Rebind(it_Instance.Handle, l_Entity, *this);
                    l_Owners.push_back(it_Instance.Handle);
                }

                it_Instance.Serial = ++l_State.NextSerial;
            }

            l_State.Records.push_back(std::move(l_Record));
            l_State.Index[it_Record.ID] = l_State.Records.size() - 1;
        }

        // What was left of each wait, from this runtime's step and frame
        const auto a_Rebase = [](std::uint64_t due, std::uint64_t then, std::uint64_t now) { return now + (due > then ? due - then : 0); };
        for (auto it_Task = l_Data.Tasks.begin(); it_Task != l_Data.Tasks.end();)
        {
            if (std::ranges::find(l_Owners, it_Task->Owner) == l_Owners.end())
            {
                ++it_Task;

                continue;
            }

            Task l_Task = *it_Task;
            if (l_Task.Kind == ScriptTaskKind::Timer || l_Task.Wait.Type == ScriptWaitType::Seconds)
            {
                l_Task.Due = a_Rebase(l_Task.Due, l_Data.StepCount, m_Time.StepCount);
            }
            else if (l_Task.Wait.Type == ScriptWaitType::Frames)
            {
                l_Task.Due = a_Rebase(l_Task.Due, l_Data.FrameCount, m_Time.FrameCount);
            }

            l_Task.ArmedStep = m_Time.StepCount;
            l_Task.ArmedFrame = m_Time.FrameCount;
            l_State.Tasks.push_back(l_Task);
            it_Task = l_Data.Tasks.erase(it_Task);
        }
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

    // Each instance has a new UUID, so its entities' are new too. A runtime that a SceneManager runs instances only what the manager holds, in Forge's Play as in a game, so a prefab the scene's fields do not name is refused in both
    Entity ScriptRuntime::InstantiatePrefab(UUID prefab, Entity parent, const std::optional<InstancePose>& pose)
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        if (parent && parent.GetScene() != &l_State.TargetScene)
        {
            TR_CORE_ERROR("Scripting: no instance of prefab {} is made, since its parent {} is not in the running scene", prefab, parent.GetUUID());

            return {};
        }

        const auto a_Refuse = [&l_State, prefab](const std::string& why)
        {
            if (std::ranges::find(l_State.Refused, prefab) == l_State.Refused.end())
            {
                l_State.Refused.push_back(prefab);
                TR_CORE_ERROR("Scripting: no instance of prefab {} is made, since {}", prefab, why);
            }

            return Entity();
        };

        const SceneManager* l_Manager = SceneManager::GetActive();
        if (l_Manager != nullptr && l_Manager->GetRuntime() != nullptr && &l_Manager->GetRuntime()->GetScripts() == this && !l_Manager->Holds(prefab))
        {
            return a_Refuse("the running scene does not hold it, as it holds each prefab its fields name");
        }

        const auto l_Start = std::chrono::steady_clock::now();
        const Expected<Entity, std::string> l_Root = SceneSerializer::InstantiatePrefab(l_State.TargetScene, prefab, UUID::Generate(), parent, {});
        if (!l_Root)
        {
            return a_Refuse(l_Root.GetError());
        }

        if (pose)
        {
            TransformComponent l_World = l_State.TargetScene.ComputeWorldTransform(*l_Root);
            l_World.Position = pose->Position;
            l_World.Rotation = pose->Rotation;
            l_State.TargetScene.SetWorldTransform(*l_Root, l_World);
        }

        for (Entity it_Entity = *l_Root; it_Entity; it_Entity = l_State.TargetScene.GetNextInSubtree(it_Entity, *l_Root))
        {
            static_cast<void>(l_State.MakeRecord(it_Entity));
        }

        const double l_Microseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - l_Start).count();
        auto a_Time = std::ranges::find(l_State.Instancing, prefab, &InstancingTime::Prefab);
        if (a_Time == l_State.Instancing.end())
        {
            l_State.Instancing.push_back({ .Prefab = prefab, .Count = 0, .TotalMicroseconds = 0.0, .MostMicroseconds = 0.0 });
            a_Time = std::prev(l_State.Instancing.end());
        }

        ++a_Time->Count;
        a_Time->TotalMicroseconds += l_Microseconds;
        a_Time->MostMicroseconds = std::max(a_Time->MostMicroseconds, l_Microseconds);

        return *l_Root;
    }

    std::span<const ScriptRuntime::InstancingTime> ScriptRuntime::GetInstancingTimes() const
    {
        return m_State->Instancing;
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

    bool ScriptRuntime::IsFaulted(Entity entity, std::size_t index) const
    {
        if (!entity || entity.GetScene() != &m_State->TargetScene)
        {
            return false;
        }

        const std::optional<std::size_t> l_Record = m_State->FindRecord(entity.GetUUID());
        if (!l_Record || index >= m_State->Records[*l_Record].Instances.size())
        {
            return false;
        }

        const Instance& l_Instance = m_State->Records[*l_Record].Instances[index];

        return l_Instance.Backend != nullptr && l_Instance.Faulted;
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
        std::optional<TimedPart> l_Timed(std::in_place, l_State.Time);
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
                l_State.Time.Enter(l_Instance.Backend->GetLanguage());
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
                static_cast<void>(l_State.StopOwner(l_Current.Handle));
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
        l_Timed.reset();
        l_State.Time.EndFrame();
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
                if (it_Instance.Backend != nullptr)
                {
                    ++(it_Instance.Backend->GetLanguage() == ScriptLanguage::Cpp ? l_Statistics.Cpp : l_Statistics.CSharp).Instances;
                }
            }
        }

        for (const Task& it_Task : m_State->Tasks)
        {
            if (it_Task.Dead)
            {
                continue;
            }

            ++l_Statistics.Tasks;
            LanguageStatistics& l_Language = it_Task.Calls.Language == ScriptLanguage::Cpp ? l_Statistics.Cpp : l_Statistics.CSharp;
            ++(it_Task.Kind == ScriptTaskKind::Timer ? l_Language.Timers : l_Language.Coroutines);
        }

        l_Statistics.Cpp.Milliseconds = m_State->Time.GetMilliseconds(ScriptLanguage::Cpp);
        l_Statistics.CSharp.Milliseconds = m_State->Time.GetMilliseconds(ScriptLanguage::CSharp);

        return l_Statistics;
    }
}