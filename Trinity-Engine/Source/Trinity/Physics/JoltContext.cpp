#include "Trinity/Physics/JoltContext.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/JobSystem.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Memory.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace Trinity
{
    namespace
    {
        // Jobs one step can have waiting at once, and barriers, of which a world's step uses one
        constexpr JPH::uint c_MaxJobs = 2048;
        constexpr JPH::uint c_MaxBarriers = 8;
        // Jolt expects 16-byte alignment from its plain allocations, for its vectors
        constexpr std::size_t c_Alignment = 16;

        void* AllocateJolt(std::size_t size)
        {
            return Memory::Allocate(size, MemoryTag::Physics, c_Alignment);
        }

        void* ReallocateJolt(void* block, [[maybe_unused]] std::size_t oldSize, std::size_t newSize)
        {
            return Memory::Reallocate(block, newSize, MemoryTag::Physics, c_Alignment);
        }

        void FreeJolt(void* block)
        {
            if (block != nullptr)
            {
                Memory::Free(block);
            }
        }

        void* AlignedAllocateJolt(std::size_t size, std::size_t alignment)
        {
            return Memory::Allocate(size, MemoryTag::Physics, std::max(alignment, c_Alignment));
        }

        void TraceJolt(const char* format, ...)
        {
            char l_Buffer[1024];
            va_list l_Arguments;
            va_start(l_Arguments, format);
            std::vsnprintf(l_Buffer, sizeof(l_Buffer), format, l_Arguments);
            va_end(l_Arguments);

            TR_CORE_TRACE("Jolt: {}", l_Buffer);
        }

#if defined(JPH_ENABLE_ASSERTS)
        // True breaks into the debugger, as TR_CORE_ASSERT does
        bool AssertJolt(const char* expression, const char* message, const char* file, JPH::uint line)
        {
            TR_CORE_ERROR("Jolt: assertion '{}' failed{}{} at {}:{}", expression, message != nullptr ? ": " : "", message != nullptr ? message : "", file, line);

            return true;
        }
#endif

        // Each job Jolt queues becomes a Trinity job, which runs it and lets it go. Inside the class JobSystem names Jolt's base, so Trinity's is named in full. A job a barrier has already run on the waiting thread runs no second time, since Execute only runs a job whose dependencies are met and which has not started. The destructor waits for every Trinity job still holding one, so none outlives the free list
        class JobSystemAdapter final : public JPH::JobSystemWithBarrier
        {
        public:
            JobSystemAdapter()
            {
                JobSystemWithBarrier::Init(c_MaxBarriers);
                m_Jobs.Init(c_MaxJobs, c_MaxJobs);
            }

            ~JobSystemAdapter() override
            {
                Trinity::JobSystem::Wait(m_Pending);
            }

            [[nodiscard]] int GetMaxConcurrency() const override
            {
                return static_cast<int>(Trinity::JobSystem::GetWorkerCount()) + 1;
            }

            // Every job slot taken means a step asked for more than c_MaxJobs at once, which waits for one to free up
            JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function, JPH::uint32 dependencies) override
            {
                JPH::uint32 l_Index = AvailableJobs::cInvalidObjectIndex;
                while ((l_Index = m_Jobs.ConstructObject(name, color, this, function, dependencies)) == AvailableJobs::cInvalidObjectIndex)
                {
                    TR_CORE_ASSERT(false, "Jolt asked for more than {} jobs at once.", c_MaxJobs);
                    Trinity::JobSystem::Wait(m_Pending);
                }

                Job* l_Job = &m_Jobs.Get(l_Index);
                JobHandle l_Handle(l_Job);
                if (dependencies == 0)
                {
                    QueueJob(l_Job);
                }

                return l_Handle;
            }

        protected:
            void QueueJob(Job* job) override
            {
                job->AddRef();
                Trinity::JobSystem::Submit([job]
                {
                    job->Execute();
                    job->Release();
                }, &m_Pending);
            }

            void QueueJobs(Job** jobs, JPH::uint count) override
            {
                for (JPH::uint it_Job = 0; it_Job < count; ++it_Job)
                {
                    QueueJob(jobs[it_Job]);
                }
            }

            void FreeJob(Job* job) override
            {
                m_Jobs.DestructObject(job);
            }

        private:
            using AvailableJobs = JPH::FixedSizeFreeList<Job>;

            AvailableJobs m_Jobs;
            JobCounter m_Pending;
        };

        std::uint32_t s_Users = 0;
        JobSystemAdapter* s_JobSystem = nullptr;
        // Said the first time a world is made, rather than with every first world, which Play and Stop make many of
        bool s_Reported = false;
    }

    namespace JoltContext
    {
        // The hooks go in before anything of Jolt's allocates, and stay, since they hold no state
        void Acquire()
        {
            TR_CORE_ASSERT(MainThread::IsMainThread(), "Physics worlds are made on the main thread.");
            if (s_Users++ > 0)
            {
                return;
            }

            JPH::Allocate = &AllocateJolt;
            JPH::Reallocate = &ReallocateJolt;
            JPH::Free = &FreeJolt;
            JPH::AlignedAllocate = &AlignedAllocateJolt;
            JPH::AlignedFree = &FreeJolt;
            JPH::Trace = &TraceJolt;
            JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &AssertJolt;)

                JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
            s_JobSystem = Memory::New<JobSystemAdapter>(MemoryTag::Physics);
            if (!std::exchange(s_Reported, true))
            {
                TR_CORE_INFO("Physics: Jolt {}.{}.{} built for {}, running up to {} jobs at once on Trinity's {} worker(s) and the main thread", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH, TR_PHYSICS_AVX2 ? "AVX2" : "SSE4.2", c_MaxJobs, JobSystem::GetWorkerCount());
            }
        }

        void Release()
        {
            TR_CORE_ASSERT(MainThread::IsMainThread() && s_Users > 0, "Physics worlds are destroyed on the main thread, once each.");
            if (s_Users == 0 || --s_Users > 0)
            {
                return;
            }

            Memory::Delete(s_JobSystem);
            s_JobSystem = nullptr;
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }

        JPH::JobSystem& GetJobSystem()
        {
            TR_CORE_ASSERT(s_JobSystem != nullptr, "Jolt's job system is used with no physics world.");

            return *s_JobSystem;
        }
    }
}