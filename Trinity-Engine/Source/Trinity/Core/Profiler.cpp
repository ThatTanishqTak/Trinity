#include "Trinity/Core/Profiler.hpp"

#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        constexpr std::size_t c_SummaryScopeCount = 5;
        constexpr std::uint64_t c_SummaryIntervalNanoseconds = 1'000'000'000;

        struct ScopeStats
        {
            std::uint64_t Calls = 0;
            std::uint64_t SelfNanoseconds = 0;
            std::uint64_t InclusiveNanoseconds = 0;
            std::uint64_t MaxInclusiveNanoseconds = 0;
        };

        struct TraceEvent
        {
            const char* Name = nullptr;
            std::uint64_t StartNanoseconds = 0;
            std::uint64_t DurationNanoseconds = 0;
        };

        struct ThreadState
        {
            std::uint32_t Id = 0;
            std::string Name;
            std::mutex Mutex;
            std::unordered_map<const char*, ScopeStats> Stats;
            std::vector<TraceEvent> Events;
        };

        std::mutex s_RegistryMutex;
        std::vector<std::shared_ptr<ThreadState>> s_Threads;
        std::atomic<std::uint32_t> s_NextThreadId{ 0 };

        std::atomic<bool> s_Active{ false };
        std::atomic<bool> s_LogSummary{ false };
        std::atomic<bool> s_Capturing{ false };

        std::filesystem::path s_CapturePath;
        std::uint64_t s_CaptureFrames = 0;
        std::uint64_t s_CapturedFrames = 0;
        std::uint64_t s_EpochNanoseconds = 0;
        std::uint64_t s_FrameStartNanoseconds = 0;
        std::uint64_t s_IntervalStartNanoseconds = 0;
        std::uint64_t s_IntervalFrames = 0;

        thread_local ProfileScope* t_CurrentScope = nullptr;

        std::uint64_t Now()
        {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        ThreadState& GetThreadState()
        {
            thread_local const std::shared_ptr<ThreadState> t_State = []
            {
                auto a_State = std::make_shared<ThreadState>();
                a_State->Id = s_NextThreadId.fetch_add(1, std::memory_order_relaxed);
                a_State->Name = std::format("Thread {}", a_State->Id);

                std::scoped_lock l_Lock(s_RegistryMutex);
                s_Threads.push_back(a_State);

                return a_State;
            }();

            return *t_State;
        }

        void RecordEvent(const char* name, std::uint64_t start, std::uint64_t duration)
        {
            ThreadState& l_State = GetThreadState();
            std::scoped_lock l_Lock(l_State.Mutex);
            l_State.Events.push_back({ name, start, duration });
        }

        void RecordScope(const char* name, std::uint64_t start, std::uint64_t duration, std::uint64_t self)
        {
            ThreadState& l_State = GetThreadState();
            std::scoped_lock l_Lock(l_State.Mutex);

            if (s_LogSummary.load(std::memory_order_relaxed))
            {
                ScopeStats& l_Stats = l_State.Stats[name];
                ++l_Stats.Calls;
                l_Stats.SelfNanoseconds += self;
                l_Stats.InclusiveNanoseconds += duration;
                l_Stats.MaxInclusiveNanoseconds = std::max(l_Stats.MaxInclusiveNanoseconds, duration);
            }

            if (s_Capturing.load(std::memory_order_relaxed))
            {
                l_State.Events.push_back({ name, start, duration });
            }
        }

        void UpdateActive()
        {
            s_Active.store(s_LogSummary.load(std::memory_order_relaxed) || s_Capturing.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }

        double ToMilliseconds(std::uint64_t nanoseconds, std::uint64_t divisor)
        {
            return static_cast<double>(nanoseconds) / 1'000'000.0 / static_cast<double>(std::max<std::uint64_t>(divisor, 1));
        }

        void LogSummary(std::uint64_t intervalNanoseconds)
        {
            std::unordered_map<std::string_view, ScopeStats> l_Merged;
            {
                std::scoped_lock l_RegistryLock(s_RegistryMutex);
                for (const std::shared_ptr<ThreadState>& it_State : s_Threads)
                {
                    std::scoped_lock l_Lock(it_State->Mutex);
                    for (const auto& [a_Name, a_Stats] : it_State->Stats)
                    {
                        ScopeStats& l_Total = l_Merged[a_Name];
                        l_Total.Calls += a_Stats.Calls;
                        l_Total.SelfNanoseconds += a_Stats.SelfNanoseconds;
                        l_Total.InclusiveNanoseconds += a_Stats.InclusiveNanoseconds;
                        l_Total.MaxInclusiveNanoseconds = std::max(l_Total.MaxInclusiveNanoseconds, a_Stats.MaxInclusiveNanoseconds);
                    }
                    it_State->Stats.clear();
                }
            }

            std::vector<std::pair<std::string_view, ScopeStats>> l_Sorted(l_Merged.begin(), l_Merged.end());
            std::ranges::sort(l_Sorted, std::ranges::greater{}, [](const auto& entry) { return entry.second.SelfNanoseconds; });

            TR_CORE_INFO("Profile: {} frames in {:.2f} s, top {} scopes by self time", s_IntervalFrames, static_cast<double>(intervalNanoseconds) / 1'000'000'000.0, std::min(c_SummaryScopeCount, l_Sorted.size()));
            TR_CORE_INFO("  {:<32} {:>12} {:>12} {:>12} {:>10}", "Scope", "self/frame", "incl/frame", "calls/frame", "max");

            for (std::size_t it_Index = 0; it_Index < l_Sorted.size() && it_Index < c_SummaryScopeCount; ++it_Index)
            {
                const auto& [a_Name, a_Stats] = l_Sorted[it_Index];
                TR_CORE_INFO("  {:<32} {:>9.3f} ms {:>9.3f} ms {:>12.1f} {:>7.3f} ms", a_Name, ToMilliseconds(a_Stats.SelfNanoseconds, s_IntervalFrames), ToMilliseconds(a_Stats.InclusiveNanoseconds, s_IntervalFrames),
                    static_cast<double>(a_Stats.Calls) / static_cast<double>(std::max<std::uint64_t>(s_IntervalFrames, 1)), ToMilliseconds(a_Stats.MaxInclusiveNanoseconds, 1));
            }
        }

        std::string EscapeJson(std::string_view text)
        {
            std::string l_Result;
            l_Result.reserve(text.size());
            for (const char it_Character : text)
            {
                if (it_Character == '"' || it_Character == '\\')
                {
                    l_Result.push_back('\\');
                    l_Result.push_back(it_Character);
                }
                else if (static_cast<unsigned char>(it_Character) < 0x20)
                {
                    l_Result += std::format("\\u{:04x}", static_cast<unsigned int>(static_cast<unsigned char>(it_Character)));
                }
                else
                {
                    l_Result.push_back(it_Character);
                }
            }

            return l_Result;
        }

        void WriteCapture()
        {
            s_Capturing.store(false, std::memory_order_relaxed);
            UpdateActive();

            if (s_CapturePath.has_parent_path())
            {
                std::error_code l_Ignored;
                std::filesystem::create_directories(s_CapturePath.parent_path(), l_Ignored);
            }

            std::ofstream l_File(s_CapturePath, std::ios::binary | std::ios::trunc);
            if (!l_File)
            {
                TR_CORE_ERROR("Could not write the profile capture to '{}'", s_CapturePath.string());

                return;
            }

            std::size_t l_EventCount = 0;
            bool l_First = true;
            l_File << "{\"traceEvents\":[\n";

            std::scoped_lock l_RegistryLock(s_RegistryMutex);
            for (const std::shared_ptr<ThreadState>& it_State : s_Threads)
            {
                std::scoped_lock l_Lock(it_State->Mutex);

                l_File << (l_First ? "" : ",\n") << std::format(R"({{"name":"thread_name","ph":"M","pid":1,"tid":{},"args":{{"name":"{}"}}}})", it_State->Id, EscapeJson(it_State->Name));
                l_First = false;

                for (const TraceEvent& it_Event : it_State->Events)
                {
                    const double l_StartMicroseconds = static_cast<double>(it_Event.StartNanoseconds - s_EpochNanoseconds) / 1000.0;
                    const double l_DurationMicroseconds = static_cast<double>(it_Event.DurationNanoseconds) / 1000.0;
                    l_File << std::format(",\n{{\"name\":\"{}\",\"ph\":\"X\",\"pid\":1,\"tid\":{},\"ts\":{:.3f},\"dur\":{:.3f}}}", EscapeJson(it_Event.Name), it_State->Id, l_StartMicroseconds, l_DurationMicroseconds);
                }

                l_EventCount += it_State->Events.size();
                it_State->Events = std::vector<TraceEvent>();
            }

            l_File << "\n],\"displayTimeUnit\":\"ms\"}\n";

            TR_CORE_INFO("Wrote {} profile events from {} frames to '{}'", l_EventCount, s_CapturedFrames, s_CapturePath.string());
        }
    }

    namespace Profiler
    {
        void Initialize(const ProfilerSpecification& specification)
        {
#if defined(TR_ENABLE_PROFILING)
            s_EpochNanoseconds = Now();
            s_IntervalStartNanoseconds = s_EpochNanoseconds;
            s_IntervalFrames = 0;
            s_FrameStartNanoseconds = 0;
            s_CapturePath = specification.CapturePath;
            s_CaptureFrames = specification.CaptureFrames;
            s_CapturedFrames = 0;

            s_LogSummary.store(specification.LogSummary, std::memory_order_relaxed);
            s_Capturing.store(!s_CapturePath.empty() && s_CaptureFrames != 0, std::memory_order_relaxed);
            UpdateActive();

            SetThreadName("Main");

            if (s_Capturing.load(std::memory_order_relaxed))
            {
                TR_CORE_INFO("Capturing {} frames of profile events to '{}'", s_CaptureFrames, s_CapturePath.string());
            }
#else
            if (specification.LogSummary || !specification.CapturePath.empty())
            {
                TR_CORE_WARN("Profiling is not compiled into this build; ignoring --profile and --profile-capture");
            }
#endif
        }

        void Shutdown()
        {
            if (s_Capturing.load(std::memory_order_relaxed))
            {
                WriteCapture();
            }

            s_LogSummary.store(false, std::memory_order_relaxed);
            UpdateActive();

            std::scoped_lock l_RegistryLock(s_RegistryMutex);
            for (const std::shared_ptr<ThreadState>& it_State : s_Threads)
            {
                std::scoped_lock l_Lock(it_State->Mutex);
                it_State->Stats = std::unordered_map<const char*, ScopeStats>();
                it_State->Events = std::vector<TraceEvent>();
            }
        }

        void BeginFrame()
        {
            if (!s_Active.load(std::memory_order_relaxed))
            {
                return;
            }

            const std::uint64_t l_Now = Now();

            if (s_Capturing.load(std::memory_order_relaxed))
            {
                if (s_FrameStartNanoseconds != 0)
                {
                    RecordEvent("Frame", s_FrameStartNanoseconds, l_Now - s_FrameStartNanoseconds);
                    ++s_CapturedFrames;
                }

                if (s_CapturedFrames >= s_CaptureFrames)
                {
                    WriteCapture();
                }
            }

            s_FrameStartNanoseconds = l_Now;
            ++s_IntervalFrames;

            if (s_LogSummary.load(std::memory_order_relaxed) && l_Now - s_IntervalStartNanoseconds >= c_SummaryIntervalNanoseconds)
            {
                LogSummary(l_Now - s_IntervalStartNanoseconds);
                s_IntervalStartNanoseconds = l_Now;
                s_IntervalFrames = 0;
            }
        }

        void SetThreadName(std::string name)
        {
            ThreadState& l_State = GetThreadState();
            std::scoped_lock l_Lock(l_State.Mutex);
            l_State.Name = std::move(name);
        }

        bool IsActive()
        {
            return s_Active.load(std::memory_order_relaxed);
        }
    }

    ProfileScope::ProfileScope(const char* name)
    {
        if (!s_Active.load(std::memory_order_relaxed))
        {
            return;
        }

        m_Name = name;
        m_Parent = t_CurrentScope;
        t_CurrentScope = this;
        m_StartNanoseconds = Now();
    }

    ProfileScope::~ProfileScope()
    {
        if (m_Name == nullptr)
        {
            return;
        }

        const std::uint64_t l_Duration = Now() - m_StartNanoseconds;

        t_CurrentScope = m_Parent;
        if (m_Parent != nullptr)
        {
            m_Parent->m_ChildNanoseconds += l_Duration;
        }

        RecordScope(m_Name, m_StartNanoseconds, l_Duration, l_Duration - m_ChildNanoseconds);
    }
}