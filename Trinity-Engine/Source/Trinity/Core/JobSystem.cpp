#include "Trinity/Core/JobSystem.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

namespace Trinity
{
    struct JobCounterAccess
    {
        static void Add(JobCounter& counter)
        {
            counter.m_Pending.fetch_add(1, std::memory_order_relaxed);
        }

        static bool Complete(JobCounter& counter)
        {
            return counter.m_Pending.fetch_sub(1, std::memory_order_acq_rel) == 1;
        }
    };

    namespace
    {
        constexpr std::uint32_t c_MaxWorkers = 64;
        constexpr std::size_t c_BatchesPerThread = 4;

        struct QueuedJob
        {
            Job Function;
            JobCounter* Counter = nullptr;
        };

        struct State
        {
            std::mutex Mutex;
            std::condition_variable Signal;
            std::deque<QueuedJob, TaggedAllocator<QueuedJob, MemoryTag::Jobs>> Queue;
            std::vector<std::thread, TaggedAllocator<std::thread, MemoryTag::Jobs>> Workers;
            std::uint32_t WorkerCount = 0;
            bool Stopping = false;
        };

        ConsoleVariable<std::int32_t> s_WorkerCountVariable("jobs.worker_count", 0, "Job system worker threads; 0 or less uses one per core minus one", ConsoleVariableFlags::ReadOnly);

        State* s_State = nullptr;

        thread_local std::uint32_t t_ThreadIndex = 0;

        QueuedJob PopFront()
        {
            QueuedJob l_Job = std::move(s_State->Queue.front());
            s_State->Queue.pop_front();

            return l_Job;
        }

        void Execute(QueuedJob& job) noexcept
        {
            {
                Job l_Function = std::move(job.Function);
                l_Function();
            }

            if (job.Counter != nullptr && JobCounterAccess::Complete(*job.Counter))
            {
                std::scoped_lock l_Lock(s_State->Mutex);
                s_State->Signal.notify_all();
            }
        }

        void WorkerMain(std::uint32_t index)
        {
            t_ThreadIndex = index;
            Profiler::SetThreadName(std::format("Worker {}", index));

            while (true)
            {
                QueuedJob l_Job;
                {
                    std::unique_lock l_Lock(s_State->Mutex);
                    s_State->Signal.wait(l_Lock, [] { return !s_State->Queue.empty() || s_State->Stopping; });
                    if (s_State->Queue.empty())
                    {
                        return;
                    }

                    l_Job = PopFront();
                }

                Execute(l_Job);
            }
        }
    }

    JobCounter::~JobCounter()
    {
        TR_CORE_ASSERT(IsDone(), "A JobCounter was destroyed while jobs submitted with it were still pending.");
    }

    namespace JobSystem
    {
        void Initialize()
        {
            TR_CORE_ASSERT(s_State == nullptr, "The job system is already initialized.");

            const std::int32_t l_Requested = s_WorkerCountVariable.Get();
            const std::uint32_t l_Cores = std::thread::hardware_concurrency();
            const std::uint32_t l_Default = l_Cores > 1 ? l_Cores - 1 : 1;

            s_State = Memory::New<State>(MemoryTag::Jobs);
            s_State->WorkerCount = std::clamp<std::uint32_t>(l_Requested > 0 ? static_cast<std::uint32_t>(l_Requested) : l_Default, 1, c_MaxWorkers);

            s_State->Workers.reserve(s_State->WorkerCount);
            for (std::uint32_t it_Index = 1; it_Index <= s_State->WorkerCount; ++it_Index)
            {
                s_State->Workers.emplace_back(WorkerMain, it_Index);
            }

            TR_CORE_INFO("Job system started {} worker thread(s) on {} hardware thread(s)", s_State->WorkerCount, l_Cores);
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The job system is not initialized.");

            {
                std::scoped_lock l_Lock(s_State->Mutex);
                s_State->Stopping = true;
            }
            s_State->Signal.notify_all();

            for (std::thread& it_Worker : s_State->Workers)
            {
                it_Worker.join();
            }

            Memory::Delete(s_State);
            s_State = nullptr;
        }

        void Submit(Job job, JobCounter* counter)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The job system is not initialized.");
            TR_CORE_ASSERT(static_cast<bool>(job), "Submitted an empty job.");

            if (counter != nullptr)
            {
                JobCounterAccess::Add(*counter);
            }

            {
                std::scoped_lock l_Lock(s_State->Mutex);
                s_State->Queue.push_back({ std::move(job), counter });
            }
            s_State->Signal.notify_one();
        }

        void Wait(const JobCounter& counter)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The job system is not initialized.");

            std::unique_lock l_Lock(s_State->Mutex);
            while (!counter.IsDone())
            {
                if (s_State->Queue.empty())
                {
                    s_State->Signal.wait(l_Lock);

                    continue;
                }

                {
                    QueuedJob l_Job = PopFront();
                    l_Lock.unlock();
                    Execute(l_Job);
                }
                l_Lock.lock();
            }
        }

        void ParallelForRanges(std::size_t count, RangeFunction function, const void* context, std::size_t batchSize)
        {
            if (count == 0)
            {
                return;
            }

            if (batchSize == 0)
            {
                const std::size_t l_TargetBatches = (static_cast<std::size_t>(GetWorkerCount()) + 1) * c_BatchesPerThread;
                batchSize = std::max<std::size_t>(1, count / l_TargetBatches + (count % l_TargetBatches != 0 ? 1 : 0));
            }

            const std::size_t l_BatchCount = count / batchSize + (count % batchSize != 0 ? 1 : 0);
            if (l_BatchCount == 1)
            {
                TR_PROFILE_SCOPE("JobSystem::ParallelFor");
                function(context, 0, count);

                return;
            }

            JobCounter l_Counter;
            for (std::size_t it_Batch = 0; it_Batch < l_BatchCount; ++it_Batch)
            {
                const std::size_t l_Begin = it_Batch * batchSize;
                const std::size_t l_End = std::min(count, l_Begin + batchSize);

                Submit([function, context, l_Begin, l_End]
                {
                    TR_PROFILE_SCOPE("JobSystem::ParallelFor");
                    function(context, l_Begin, l_End);
                }, &l_Counter);
            }

            Wait(l_Counter);
        }

        std::uint32_t GetWorkerCount()
        {
            return s_State != nullptr ? s_State->WorkerCount : 0;
        }

        std::uint32_t GetThreadIndex()
        {
            return t_ThreadIndex;
        }
    }
}