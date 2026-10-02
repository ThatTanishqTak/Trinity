#include "Trinity/Core/MainThread.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"

#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        using TaskList = std::vector<MainThread::Task, TaggedAllocator<MainThread::Task, MemoryTag::Engine>>;

        struct State
        {
            std::mutex Mutex;
            TaskList Pending;
            TaskList Running;
            std::thread::id MainThreadId;
        };

        State* s_State = nullptr;
    }

    namespace MainThread
    {
        void Initialize()
        {
            TR_CORE_ASSERT(s_State == nullptr, "The main thread queue is already initialized.");

            s_State = Memory::New<State>(MemoryTag::Engine);
            s_State->MainThreadId = std::this_thread::get_id();
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The main thread queue is not initialized.");
            TR_CORE_ASSERT(IsMainThread(), "The main thread queue must be shut down on the main thread.");

            std::size_t l_Discarded = 0;
            {
                std::scoped_lock l_Lock(s_State->Mutex);
                l_Discarded = s_State->Pending.size();
            }

            if (l_Discarded != 0)
            {
                TR_CORE_INFO("Discarded {} main thread task(s) that never ran", l_Discarded);
            }

            Memory::Delete(s_State);
            s_State = nullptr;
        }

        void Post(Task task)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The main thread queue is not initialized.");
            TR_CORE_ASSERT(static_cast<bool>(task), "Posted an empty task.");

            std::scoped_lock l_Lock(s_State->Mutex);
            s_State->Pending.push_back(std::move(task));
        }

        std::size_t ExecutePending()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The main thread queue is not initialized.");
            TR_CORE_ASSERT(IsMainThread(), "ExecutePending must run on the main thread.");

            {
                std::scoped_lock l_Lock(s_State->Mutex);
                std::swap(s_State->Pending, s_State->Running);
            }

            const std::size_t l_Count = s_State->Running.size();
            for (Task& it_Task : s_State->Running)
            {
                it_Task();
            }

            s_State->Running.clear();

            return l_Count;
        }

        bool IsMainThread()
        {
            return s_State != nullptr && std::this_thread::get_id() == s_State->MainThreadId;
        }
    }
}