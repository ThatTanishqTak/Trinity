#include "SandboxLayer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <format>
#include <iterator>
#include <span>
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
    constexpr std::size_t c_ParallelValueCount = 10000000;
    constexpr std::string_view c_RunCountPath = "/saves/sandbox/runs.txt";
    constexpr std::uint32_t c_AsyncFileCount = 32;

    std::string GetAsyncFilePath(std::uint32_t index)
    {
        return std::format("/saves/sandbox/async/file_{:02}.txt", index);
    }

    std::string GetAsyncFileContents(std::uint32_t index)
    {
        return std::format("async file {} {}", index, std::string(index * 1024, static_cast<char>('a' + index % 26)));
    }

    constexpr std::uint64_t Mix(std::uint64_t value)
    {
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdull;
        value ^= value >> 33;
        value *= 0xc4ceb9fe1a85ec53ull;
        value ^= value >> 33;

        return value & 0xFFFF;
    }

#if defined(TR_ENGINE_SHARED)
    constexpr std::uint32_t c_ModuleLoadCount = 100;

    std::size_t CountConsoleVariables()
    {
        std::size_t l_Count = 0;
        for (const Trinity::ConsoleVariableBase* it_Variable = Trinity::ConsoleVariableBase::GetFirst(); it_Variable != nullptr; it_Variable = it_Variable->GetNext())
        {
            ++l_Count;
        }

        return l_Count;
    }
#endif

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
    if (Trinity::Application::Get().GetSpecification().CommandLineArgs.HasOption("parallel-test"))
    {
        TestParallelFor();
    }

    TestFileSystem();
    TestSaves();
    TestModules();
    TestShaders();
    TestRHI();
    StartAsyncReads();

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

    m_AsyncReads.clear();

    TR_INFO("Ran {} frame job(s) on frame memory; {} saw it change underneath them", m_FrameJobsRun.load(), m_FrameJobMismatches.load());
}

void SandboxLayer::OnUpdate(Trinity::Timestep timestep)
{
    TR_PROFILE_FUNCTION();

    Trinity::FrameAllocator& l_FrameAllocator = Trinity::Application::Get().GetFrameAllocator();

    {
        TR_PROFILE_SCOPE("SandboxLayer::FillSamples");
        const std::span<float> l_Samples = l_FrameAllocator.AllocateArray<float>(c_FrameSampleCount);
        std::ranges::fill(l_Samples, timestep.GetSeconds());

        Trinity::Application::Get().SubmitFrameJob([this, l_Samples, l_Expected = timestep.GetSeconds()]
        {
            TR_PROFILE_SCOPE("SandboxLayer::CheckSamples");

            const bool l_Intact = std::ranges::all_of(l_Samples, [l_Expected](float sample) { return sample == l_Expected; });
            m_FrameJobMismatches.fetch_add(l_Intact ? 0 : 1, std::memory_order_relaxed);
            m_FrameJobsRun.fetch_add(1, std::memory_order_relaxed);
        });
    }

    CheckAsyncReads();

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

void SandboxLayer::TestParallelFor()
{
    TR_PROFILE_FUNCTION();

    using Clock = std::chrono::steady_clock;

    std::vector<std::uint32_t, Trinity::TaggedAllocator<std::uint32_t, Trinity::MemoryTag::Game>> l_Values(c_ParallelValueCount);
    for (std::size_t it_Index = 0; it_Index < l_Values.size(); ++it_Index)
    {
        l_Values[it_Index] = static_cast<std::uint32_t>(it_Index) * 2654435761u;
    }

    const Clock::time_point l_SerialStart = Clock::now();
    std::uint64_t l_Serial = 0;
    for (const std::uint32_t it_Value : l_Values)
    {
        l_Serial += Mix(it_Value);
    }
    const double l_SerialMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - l_SerialStart).count();

    const Clock::time_point l_ParallelStart = Clock::now();
    std::atomic<std::uint64_t> l_Parallel{ 0 };
    Trinity::JobSystem::ParallelFor(l_Values.size(), [&l_Values, &l_Parallel](std::size_t begin, std::size_t end)
    {
        std::uint64_t l_Sum = 0;
        for (std::size_t it_Index = begin; it_Index < end; ++it_Index)
        {
            l_Sum += Mix(l_Values[it_Index]);
        }

        l_Parallel.fetch_add(l_Sum, std::memory_order_relaxed);
    });
    const double l_ParallelMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - l_ParallelStart).count();

    TR_INFO("ParallelFor over {} values on {} threads: {} ({} serial result), serial {:.2f} ms, parallel {:.2f} ms, speedup {:.2f}x", l_Values.size(), Trinity::JobSystem::GetWorkerCount() + 1, l_Parallel.load(), l_Parallel.load() == l_Serial ? "matches the" : "DOES NOT MATCH the", l_SerialMilliseconds, l_ParallelMilliseconds, l_SerialMilliseconds / l_ParallelMilliseconds);

    Trinity::Memory::LogUsage();
}

void SandboxLayer::TestFileSystem()
{
    TR_PROFILE_FUNCTION();

    const Trinity::ApplicationCommandLineArgs& l_Args = Trinity::Application::Get().GetSpecification().CommandLineArgs;
    const std::string l_LogName = std::filesystem::path(l_Args[0]).stem().string() + ".log";
    const std::string l_LogPath = "/logs/" + l_LogName;

    Trinity::Log::Flush();

    const Trinity::Expected<std::string, Trinity::FileError> l_Log = Trinity::FileSystem::ReadText(l_LogPath);
    if (l_Log)
    {
        TR_INFO("Read {} through the file system: {} bytes, {} lines", l_LogPath, l_Log->size(), std::ranges::count(*l_Log, '\n'));
    }
    else
    {
        TR_ERROR("Could not read {}: {}", l_LogPath, Trinity::ToString(l_Log.GetError()));
    }

    std::string l_WrongCase = l_LogPath;
    std::ranges::transform(l_WrongCase, l_WrongCase.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });

    for (const std::string_view it_Path : { std::string_view(l_WrongCase), std::string_view("/logs/../secret.txt"), std::string_view("/logs/C:/Windows"), std::string_view("logs/relative.txt"), std::string_view("/missing/file.txt"), std::string_view("/logs") })
    {
        const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Result = Trinity::FileSystem::ReadFile(it_Path);
        TR_INFO("  ReadFile({}) -> {}", it_Path, l_Result ? "ok" : Trinity::ToString(l_Result.GetError()));
    }

    if (const Trinity::Expected<std::vector<Trinity::DirectoryEntry>, Trinity::FileError> l_Root = Trinity::FileSystem::List("/"))
    {
        std::string l_Names;
        for (const Trinity::DirectoryEntry& it_Entry : *l_Root)
        {
            std::format_to(std::back_inserter(l_Names), "{}{}{}", l_Names.empty() ? "" : ", ", it_Entry.Name, it_Entry.Type == Trinity::FileType::Directory ? "/" : "");
        }

        TR_INFO("  List(/) -> {}", l_Names);
    }
}

void SandboxLayer::TestSaves()
{
    TR_PROFILE_FUNCTION();

    std::uint32_t l_Runs = 0;
    if (const Trinity::Expected<std::string, Trinity::FileError> l_Previous = Trinity::FileSystem::ReadText(c_RunCountPath))
    {
        std::from_chars(l_Previous->data(), l_Previous->data() + l_Previous->size(), l_Runs);
    }
    ++l_Runs;

    const Trinity::Expected<void, Trinity::FileError> l_Saved = Trinity::FileSystem::WriteText(c_RunCountPath, std::to_string(l_Runs));
    TR_INFO("Sandbox has started {} time(s) with these saves ({}: {})", l_Runs, c_RunCountPath, l_Saved ? "saved" : Trinity::ToString(l_Saved.GetError()));

    for (const std::string_view it_Path : { "/logs/overwrite.txt", "/saves/../escape.txt", "/nowhere/file.txt" })
    {
        const Trinity::Expected<void, Trinity::FileError> l_Result = Trinity::FileSystem::WriteText(it_Path, "x");
        TR_INFO("  WriteText({}) -> {}", it_Path, l_Result ? "ok" : Trinity::ToString(l_Result.GetError()));
    }

    Trinity::Scope<Trinity::MemorySource> l_Builtin = Trinity::CreateScope<Trinity::MemorySource>("sandbox built-ins");
    l_Builtin->AddText("motd.txt", "Hello from a memory-backed mount");
    Trinity::FileSystem::Mount("/builtin", std::move(l_Builtin));

    const Trinity::Expected<std::string, Trinity::FileError> l_Message = Trinity::FileSystem::ReadText("/builtin/motd.txt");
    TR_INFO("  ReadText(/builtin/motd.txt) -> {}", l_Message ? *l_Message : std::string(Trinity::ToString(l_Message.GetError())));

    const Trinity::Expected<void, Trinity::FileError> l_Refused = Trinity::FileSystem::WriteText("/builtin/motd.txt", "changed");
    TR_INFO("  WriteText(/builtin/motd.txt) -> {}", l_Refused ? "ok" : Trinity::ToString(l_Refused.GetError()));
}

void SandboxLayer::TestModules()
{
    // Modules exist only where the engine is shared: a module linking the static engine would carry a second copy of it
#if defined(TR_ENGINE_SHARED)
    TR_PROFILE_FUNCTION();

    using AttachFunction = void (*)();
    using DescribeFunction = void (*)(std::string&);
    using DetachFunction = void (*)();

    const std::size_t l_VariablesBefore = CountConsoleVariables();
    const Trinity::MemoryTagStats l_GameBefore = Trinity::Memory::GetStats(Trinity::MemoryTag::Game);

    std::string l_Description;
    std::uint32_t l_Failures = 0;
    for (std::uint32_t it_Load = 0; it_Load < c_ModuleLoadCount; ++it_Load)
    {
        Trinity::Expected<Trinity::SharedLibrary, std::string> l_Module = Trinity::SharedLibrary::Load(TR_SANDBOX_MODULE);
        if (!l_Module)
        {
            TR_ERROR("Module test: {}", l_Module.GetError());

            return;
        }

        const AttachFunction l_Attach = l_Module->GetFunction<AttachFunction>("SandboxModuleAttach");
        const DescribeFunction l_Describe = l_Module->GetFunction<DescribeFunction>("SandboxModuleDescribe");
        const DetachFunction l_Detach = l_Module->GetFunction<DetachFunction>("SandboxModuleDetach");
        if (l_Attach == nullptr || l_Describe == nullptr || l_Detach == nullptr)
        {
            TR_ERROR("Module test: {} is missing an entry point", l_Module->GetPath().string());

            return;
        }

        l_Attach();
        l_Describe(l_Description);

        const Trinity::ConsoleVariableBase* l_Variable = Trinity::ConsoleVariables::Find("sandbox.module_value");
        const bool l_Loaded = l_Variable != nullptr && l_Variable->ToString() == "42" && CountConsoleVariables() == l_VariablesBefore + 1;

        l_Detach();
        l_Module->Unload();

        const Trinity::MemoryTagStats l_GameAfter = Trinity::Memory::GetStats(Trinity::MemoryTag::Game);
        const bool l_Unloaded = Trinity::ConsoleVariables::Find("sandbox.module_value") == nullptr && CountConsoleVariables() == l_VariablesBefore && l_GameAfter.CurrentBytes == l_GameBefore.CurrentBytes && l_GameAfter.LiveAllocations == l_GameBefore.LiveAllocations;

        if (!l_Loaded || !l_Unloaded)
        {
            ++l_Failures;
        }
    }

    TR_INFO("{}", l_Description);

    if (l_Failures != 0)
    {
        TR_ERROR("Module test: {} of {} load(s) of {} went wrong or left a console variable or Game memory behind", l_Failures, c_ModuleLoadCount, TR_SANDBOX_MODULE);

        return;
    }

    TR_INFO("Module test: loaded and unloaded {} {} times; {} console variable(s) and {} under Game before and after", TR_SANDBOX_MODULE, c_ModuleLoadCount, l_VariablesBefore, Trinity::Memory::FormatBytes(l_GameBefore.CurrentBytes));
#endif
}

void SandboxLayer::TestShaders()
{
    TR_PROFILE_FUNCTION();

    if (!Trinity::FileSystem::Exists("/engine/shaders"))
    {
        TR_INFO("Shaders: none under /engine/shaders, because slangc was not found when the build was configured");

        return;
    }

    std::vector<std::string_view> l_Extensions;
#if defined(TR_RHI_VULKAN)
    l_Extensions.push_back("spv");
#endif
#if defined(TR_RHI_D3D12)
    l_Extensions.push_back("dxil");
#endif

    std::string l_Report;
    for (const std::string_view it_Extension : l_Extensions)
    {
        for (const std::string_view it_Entry : { "VertexMain", "PixelMain" })
        {
            const std::string l_Path = std::format("/engine/shaders/Triangle.{}.{}", it_Entry, it_Extension);
            const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Blob = Trinity::FileSystem::ReadFile(l_Path);
            if (!l_Blob)
            {
                TR_ERROR("Shaders: cannot read {}: {}", l_Path, Trinity::ToString(l_Blob.GetError()));

                return;
            }

            // SPIR-V starts with the magic number 0x07230203, and DXIL sits in a container that starts with "DXBC"
            const std::array<std::uint8_t, 4> l_Magic = it_Extension == "spv" ? std::array<std::uint8_t, 4>{ 0x03, 0x02, 0x23, 0x07 } : std::array<std::uint8_t, 4>{ 'D', 'X', 'B', 'C' };
            const bool l_Valid = l_Blob->size() >= l_Magic.size() && std::ranges::equal(std::span(l_Blob->data(), l_Magic.size()), l_Magic, [](std::byte byte, std::uint8_t expected) { return std::to_integer<std::uint8_t>(byte) == expected; });
            if (!l_Valid)
            {
                TR_ERROR("Shaders: {} does not start with the {} header", l_Path, it_Extension == "spv" ? "SPIR-V" : "DXIL container");

                return;
            }

            l_Report += std::format("{}{} ({})", l_Report.empty() ? "" : ", ", l_Path.substr(l_Path.rfind('/') + 1), Trinity::Memory::FormatBytes(l_Blob->size()));
        }
    }

    TR_INFO("Shaders: read {} through /engine/shaders", l_Report);
}

// Clears a small render target and copies it into a readback buffer, the shape of the offscreen render test to come
void SandboxLayer::TestRHI()
{
    TR_PROFILE_FUNCTION();

    constexpr std::uint32_t c_TargetSize = 64;

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();

    if (l_Device.GetInfo().API != Trinity::GraphicsAPI::None)
    {
        TR_INFO("RHI: skipping the clear and copy test on {} until its backend records frames", Trinity::ToString(l_Device.GetInfo().API));

        return;
    }

    Trinity::RHI::TextureDescription l_TargetDescription;
    l_TargetDescription.Width = c_TargetSize;
    l_TargetDescription.Height = c_TargetSize;
    l_TargetDescription.TextureFormat = Trinity::RHI::Format::RGBA8Unorm;
    l_TargetDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget | Trinity::RHI::TextureUsage::CopySource;
    l_TargetDescription.DebugName = "Sandbox target";

    const std::uint64_t l_RowPitch = (std::uint64_t{ c_TargetSize } * Trinity::RHI::GetFormatSize(l_TargetDescription.TextureFormat) + Trinity::RHI::c_TextureCopyRowAlignment - 1) / Trinity::RHI::c_TextureCopyRowAlignment * Trinity::RHI::c_TextureCopyRowAlignment;

    Trinity::RHI::BufferDescription l_ReadbackDescription;
    l_ReadbackDescription.Size = l_RowPitch * c_TargetSize;
    l_ReadbackDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_ReadbackDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_ReadbackDescription.DebugName = "Sandbox readback";

    const Trinity::RHI::TextureHandle l_Target = l_Device.CreateTexture(l_TargetDescription);
    const Trinity::RHI::BufferHandle l_Readback = l_Device.CreateBuffer(l_ReadbackDescription);
    if (!l_Target || !l_Readback)
    {
        TR_ERROR("Trinity::RHI: could not create the test target or readback buffer");

        l_Device.DestroyTexture(l_Target);
        l_Device.DestroyBuffer(l_Readback);

        return;
    }

    Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();
    l_Commands.TextureBarrier(l_Target, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::RenderTarget);

    const std::array<Trinity::RHI::ColorAttachment, 1> l_Attachments{ Trinity::RHI::ColorAttachment{ l_Target, Trinity::RHI::LoadOp::Clear, Trinity::RHI::StoreOp::Store, { 0.1f, 0.2f, 0.3f, 1.0f } } };
    Trinity::RHI::RenderingDescription l_Rendering;
    l_Rendering.ColorAttachments = l_Attachments;
    l_Rendering.RenderArea = { 0, 0, c_TargetSize, c_TargetSize };
    l_Commands.BeginRendering(l_Rendering);
    l_Commands.EndRendering();

    l_Commands.TextureBarrier(l_Target, Trinity::RHI::ResourceState::RenderTarget, Trinity::RHI::ResourceState::CopySource);
    l_Commands.CopyTextureToBuffer(l_Target, l_Readback);
    l_Device.EndFrame();
    l_Device.WaitIdle();

    const std::size_t l_Mapped = l_Device.GetMappedData(l_Readback).size();

    l_Device.DestroyBuffer(l_Readback);
    l_Device.DestroyTexture(l_Target);

    if (l_Mapped != l_ReadbackDescription.Size)
    {
        TR_ERROR("Trinity::RHI: the readback buffer maps {} of {}", Trinity::Memory::FormatBytes(l_Mapped), Trinity::Memory::FormatBytes(l_ReadbackDescription.Size));

        return;
    }

    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();
    TR_INFO("Trinity::RHI: {} device on {} cleared a {}x{} target and copied it into a {} readback buffer", Trinity::ToString(l_Info.API), l_Info.AdapterName, c_TargetSize, c_TargetSize, Trinity::Memory::FormatBytes(l_Mapped));
}

void SandboxLayer::StartAsyncReads()
{
    TR_PROFILE_FUNCTION();

    for (std::uint32_t it_Index = 0; it_Index < c_AsyncFileCount; ++it_Index)
    {
        if (!Trinity::FileSystem::WriteText(GetAsyncFilePath(it_Index), GetAsyncFileContents(it_Index)))
        {
            TR_ERROR("Could not prepare {} for the asynchronous read test", GetAsyncFilePath(it_Index));

            return;
        }
    }

    m_AsyncStartFrame = Trinity::Application::Get().GetFrameCount();
    for (std::uint32_t it_Index = 0; it_Index < c_AsyncFileCount; ++it_Index)
    {
        m_AsyncReads.push_back(Trinity::FileSystem::ReadFileAsync(GetAsyncFilePath(it_Index), [this, it_Index](Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> result)
        {
            const std::string l_Expected = GetAsyncFileContents(it_Index);
            const bool l_Matches = result && std::string_view(reinterpret_cast<const char*>(result->data()), result->size()) == l_Expected;

            m_AsyncMismatches += l_Matches ? 0 : 1;
            m_AsyncOffMainThread += Trinity::MainThread::IsMainThread() ? 0 : 1;
            ++m_AsyncCompleted;
        }));
    }

    m_CancelledRead = Trinity::FileSystem::ReadFileAsync(GetAsyncFilePath(0), [this](Trinity::Expected<Trinity::FileBuffer, Trinity::FileError>) { ++m_CancelledCallbacks; });
    m_CancelledRead.Cancel();
}

void SandboxLayer::CheckAsyncReads()
{
    if (m_AsyncReported || m_AsyncReads.empty() || m_AsyncCompleted < m_AsyncReads.size())
    {
        return;
    }

    m_AsyncReported = true;
    TR_INFO("Loaded {} of {} files asynchronously within {} frame(s): {} mismatched, {} callback(s) off the main thread, cancelled callback ran {} time(s)", m_AsyncCompleted, m_AsyncReads.size(), Trinity::Application::Get().GetFrameCount() - m_AsyncStartFrame, m_AsyncMismatches, m_AsyncOffMainThread, m_CancelledCallbacks);
}