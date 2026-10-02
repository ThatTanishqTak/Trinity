#include "Trinity/Core/JobSystem.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Log.hpp"
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

        struct QueuedJob
        {
            Job Function;
            JobCounter* Counter = nullptr;
        };

        ConsoleVariable<std::int32_t> s_WorkerCountVariable("jobs.worker_count", 0, "Job system worker threads; 0 or less uses one per core minus one", ConsoleVariableFlags::ReadOnly);

        std::mutex s_Mutex;
        std::condition_variable s_Signal;
        std::deque<QueuedJob> s_Queue;
        std::vector<std::thread> s_Workers;
        std::uint32_t s_WorkerCount = 0;
        bool s_Stopping = false;
        bool s_Initialized = false;

        thread_local std::uint32_t t_ThreadIndex = 0;

        QueuedJob PopFront()
        {
            QueuedJob l_Job = std::move(s_Queue.front());
            s_Queue.pop_front();

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
                std::scoped_lock l_Lock(s_Mutex);
                s_Signal.notify_all();
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
                    std::unique_lock l_Lock(s_Mutex);
                    s_Signal.wait(l_Lock, [] { return !s_Queue.empty() || s_Stopping; });
                    if (s_Queue.empty())
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
            TR_CORE_ASSERT(!s_Initialized, "The job system is already initialized.");

            const std::int32_t l_Requested = s_WorkerCountVariable.Get();
            const std::uint32_t l_Cores = std::thread::hardware_concurrency();
            const std::uint32_t l_Default = l_Cores > 1 ? l_Cores - 1 : 1;

            s_WorkerCount = std::clamp<std::uint32_t>(l_Requested > 0 ? static_cast<std::uint32_t>(l_Requested) : l_Default, 1, c_MaxWorkers);
            s_Stopping = false;
            s_Initialized = true;

            s_Workers.reserve(s_WorkerCount);
            for (std::uint32_t it_Index = 1; it_Index <= s_WorkerCount; ++it_Index)
            {
                s_Workers.emplace_back(WorkerMain, it_Index);
            }

            TR_CORE_INFO("Job system started {} worker thread(s) on {} hardware thread(s)", s_WorkerCount, l_Cores);
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_Initialized, "The job system is not initialized.");

            {
                std::scoped_lock l_Lock(s_Mutex);
                s_Stopping = true;
            }
            s_Signal.notify_all();

            for (std::thread& it_Worker : s_Workers)
            {
                it_Worker.join();
            }

            s_Workers = std::vector<std::thread>();
            s_Queue = std::deque<QueuedJob>();
            s_WorkerCount = 0;
            s_Initialized = false;
        }

        void Submit(Job job, JobCounter* counter)
        {
            TR_CORE_ASSERT(s_Initialized, "The job system is not initialized.");
            TR_CORE_ASSERT(static_cast<bool>(job), "Submitted an empty job.");

            if (counter != nullptr)
            {
                JobCounterAccess::Add(*counter);
            }

            {
                std::scoped_lock l_Lock(s_Mutex);
                s_Queue.push_back({ std::move(job), counter });
            }
            s_Signal.notify_one();
        }

        void Wait(const JobCounter& counter)
        {
            std::unique_lock l_Lock(s_Mutex);
            while (!counter.IsDone())
            {
                if (s_Queue.empty())
                {
                    s_Signal.wait(l_Lock);

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

        std::uint32_t GetWorkerCount()
        {
            return s_WorkerCount;
        }

        std::uint32_t GetThreadIndex()
        {
            return t_ThreadIndex;
        }
    }
}