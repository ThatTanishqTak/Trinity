#include "SandboxLayer.hpp"

#include <algorithm>
#include <atomic>
#include <format>
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr std::size_t c_ScratchBufferSize = 1024 * 1024;
    constexpr std::size_t c_ProbeElementCount = 256 * 1024;
    constexpr std::size_t c_FrameSampleCount = 16 * 1024;
    constexpr std::size_t c_UUIDTestCount = 1000000;
    constexpr std::uint64_t c_JobTestCount = 1000;
    constexpr std::uint64_t c_ValuesPerJob = 10000;

    Trinity::ConsoleVariable<float> s_ReportInterval("sandbox.report_interval", 1.0f, "Seconds between Sandbox fps reports");
    Trinity::ConsoleVariable<bool> s_ListConsoleVariables("sandbox.list_cvars", false, "Log every console variable when the Sandbox starts", Trinity::ConsoleVariableFlags::ReadOnly);
}

SandboxLayer::SandboxLayer() : Layer("Sandbox")
{

}

void SandboxLayer::OnAttach()
{
    m_ScratchBuffer = Trinity::Memory::Allocate(c_ScratchBufferSize, Trinity::MemoryTag::Game);

    TR_INFO("Sandbox attached. Escape closes the window, M prints memory use, O overflows the frame allocator, C lists console variables.");
    TR_INFO("Reporting fps every {} s (sandbox.report_interval)", s_ReportInterval.Get());

    if (s_ListConsoleVariables.Get())
    {
        Trinity::ConsoleVariables::LogAll();
    }

    if (Trinity::Memory::IsTrackingGlobalAllocations())
    {
        const std::uint64_t l_Before = Trinity::Memory::GetStats(Trinity::MemoryTag::Untagged).CurrentBytes;
        m_Probe.resize(c_ProbeElementCount);
        const std::uint64_t l_After = Trinity::Memory::GetStats(Trinity::MemoryTag::Untagged).CurrentBytes;

        TR_INFO("A std::vector of {} uint32 added {} to Untagged", m_Probe.size(), Trinity::Memory::FormatBytes(l_After - l_Before));
    }

    TestJobs();

    TR_INFO("Example UUID: {}", Trinity::UUID::Generate());
    if (Trinity::Application::Get().GetSpecification().CommandLineArgs.HasOption("uuid-test"))
    {
        TestUUIDs();
    }

    Trinity::Memory::LogUsage();
}

void SandboxLayer::OnDetach()
{
    Trinity::Memory::Free(m_ScratchBuffer);
    m_ScratchBuffer = nullptr;

    m_Probe = std::vector<std::uint32_t>();
}

void SandboxLayer::OnUpdate(Trinity::Timestep timestep)
{
    TR_PROFILE_FUNCTION();

    Trinity::FrameAllocator& l_FrameAllocator = Trinity::Application::Get().GetFrameAllocator();

    {
        TR_PROFILE_SCOPE("SandboxLayer::FillSamples");
        const std::span<float> l_Samples = l_FrameAllocator.AllocateArray<float>(c_FrameSampleCount);
        std::ranges::fill(l_Samples, timestep.GetSeconds());
    }

    m_SecondsSinceReport += timestep;
    ++m_FramesSinceReport;

    if (m_SecondsSinceReport >= s_ReportInterval.Get())
    {
        TR_TRACE("{:.1f} fps, frame memory {} of {} (peak {})", static_cast<float>(m_FramesSinceReport) / m_SecondsSinceReport, Trinity::Memory::FormatBytes(l_FrameAllocator.GetUsed()), Trinity::Memory::FormatBytes(l_FrameAllocator.GetCapacity()), Trinity::Memory::FormatBytes(l_FrameAllocator.GetPeakUsed()));
        m_SecondsSinceReport = 0.0f;
        m_FramesSinceReport = 0;
    }
}

void SandboxLayer::OnEvent(Trinity::Event& event)
{
    if (event.GetEventType() != Trinity::EventType::MouseMoved)
    {
        TR_TRACE("{}", event);
    }

    Trinity::EventDispatcher l_Dispatcher(event);
    l_Dispatcher.Dispatch<Trinity::KeyPressedEvent>(TR_BIND_EVENT_FN(OnKeyPressed));
}

bool SandboxLayer::OnKeyPressed(Trinity::KeyPressedEvent& event)
{
    if (event.GetKeyCode() == Trinity::KeyCode::TR_ESCAPE)
    {
        Trinity::Application::Get().Close();

        return true;
    }

    if (event.GetKeyCode() == Trinity::KeyCode::TR_M)
    {
        Trinity::Memory::LogUsage();

        return true;
    }

    if (event.GetKeyCode() == Trinity::KeyCode::TR_C)
    {
        Trinity::ConsoleVariables::LogAll();

        return true;
    }

    if (event.GetKeyCode() == Trinity::KeyCode::TR_O)
    {
        Trinity::FrameAllocator& l_FrameAllocator = Trinity::Application::Get().GetFrameAllocator();
        [[maybe_unused]] void* l_Overflow = l_FrameAllocator.Allocate(l_FrameAllocator.GetCapacity() + 1);

        return true;
    }

    return false;
}


void SandboxLayer::TestUUIDs()
{
    TR_PROFILE_FUNCTION();

    std::unordered_set<Trinity::UUID> l_Seen;
    l_Seen.reserve(c_UUIDTestCount);

    std::size_t l_Collisions = 0;
    std::size_t l_Invalid = 0;
    std::size_t l_RoundTripFailures = 0;
    for (std::size_t it_Index = 0; it_Index < c_UUIDTestCount; ++it_Index)
    {
        const Trinity::UUID l_UUID = Trinity::UUID::Generate();
        if (!l_UUID)
        {
            ++l_Invalid;
        }

        if (!l_Seen.insert(l_UUID).second)
        {
            ++l_Collisions;
        }

        if (Trinity::UUID::Parse(l_UUID.ToString()) != l_UUID)
        {
            ++l_RoundTripFailures;
        }
    }

    TR_INFO("Generated {} UUIDs: {} collision(s), {} invalid, {} failed to round-trip through text", c_UUIDTestCount, l_Collisions, l_Invalid, l_RoundTripFailures);
}


void SandboxLayer::TestJobs()
{
    TR_PROFILE_FUNCTION();

    std::vector<std::atomic<std::uint32_t>> l_JobsPerThread(Trinity::JobSystem::GetWorkerCount() + 1);
    std::atomic<std::uint64_t> l_Total{ 0 };

    Trinity::JobCounter l_Counter;
    for (std::uint64_t it_Job = 0; it_Job < c_JobTestCount; ++it_Job)
    {
        Trinity::JobSystem::Submit([it_Job, &l_JobsPerThread, &l_Total]
        {
            TR_PROFILE_SCOPE("SandboxLayer::TestJob");

            std::uint64_t l_Sum = 0;
            for (std::uint64_t it_Value = it_Job * c_ValuesPerJob; it_Value < (it_Job + 1) * c_ValuesPerJob; ++it_Value)
            {
                l_Sum += it_Value;
            }

            l_Total.fetch_add(l_Sum, std::memory_order_relaxed);
            l_JobsPerThread[Trinity::JobSystem::GetThreadIndex()].fetch_add(1, std::memory_order_relaxed);
        }, &l_Counter);
    }

    Trinity::JobSystem::Wait(l_Counter);

    const std::uint64_t l_ValueCount = c_JobTestCount * c_ValuesPerJob;
    const std::uint64_t l_Expected = l_ValueCount * (l_ValueCount - 1) / 2;

    std::string l_Distribution;
    for (const std::atomic<std::uint32_t>& it_Count : l_JobsPerThread)
    {
        std::format_to(std::back_inserter(l_Distribution), "{}{}", l_Distribution.empty() ? "" : " ", it_Count.load());
    }

    TR_INFO("Ran {} jobs, {} pending: sum {} is {}; jobs per thread (main first): {}", c_JobTestCount, l_Counter.GetPending(), l_Total.load(), l_Total.load() == l_Expected ? "correct" : "WRONG", l_Distribution);
}