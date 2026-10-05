#include "SandboxLayer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <iterator>
#include <limits>
#include <numbers>
#include <random>
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
    constexpr std::uint32_t c_LogTestWorkers = 8;
    constexpr std::int64_t c_LogTestLinesPerWorker = 50;
    constexpr std::uint32_t c_LogTestFloodLines = 100000;
    constexpr std::string_view c_RunCountPath = "/saves/sandbox/runs.txt";
    constexpr std::uint32_t c_AsyncFileCount = 32;
    constexpr std::uint32_t c_ResourceRounds = 2;
    constexpr std::uint32_t c_ResourceFramesPerRound = 100;
    constexpr std::uint32_t c_BuffersPerFrame = 50;
    constexpr std::uint32_t c_SceneEntityCount = 100000;
    constexpr std::uint32_t c_HierarchyEntityCount = 10000;
    constexpr std::uint32_t c_HierarchyReparentCount = 1000;
    constexpr std::uint32_t c_HierarchySeed = 20261005;
    constexpr double c_HierarchyTolerance = 1e-5;
    constexpr std::uint32_t c_SceneFileEntityCount = 5000;
    constexpr std::uint32_t c_SceneFileSeed = 20261006;
    constexpr std::array<std::string_view, 3> c_SceneFilePaths{ "/saves/sandbox/scene_a.trscene", "/saves/sandbox/scene_b.trscene", "/saves/sandbox/scene_c.trscene" };
    constexpr std::string_view c_AssetTestRoot = "/saves/sandbox/assets";
    constexpr std::uint32_t c_AssetFileCount = 64;
    constexpr std::uint32_t c_AssetLoadCount = 1000;
    constexpr std::uint32_t c_AssetLoadsPerFrame = 32;
    constexpr std::uint32_t c_AssetCancelEvery = 3;
    constexpr std::uint64_t c_AssetTimeoutFrames = 600;
    constexpr std::uint32_t c_AssetSeed = 20261007;
    constexpr float c_ClearCycleSeconds = 10.0f;
    constexpr std::array<float, 4> c_TestClearColor{ 0.05f, 0.05f, 0.05f, 1.0f };

    // About a second apart at 60 Hz, so each step of the second window test can be watched
    constexpr std::uint32_t c_SecondWindowStepFrames = 60;
    constexpr std::array<float, 4> c_SecondWindowClearColor{ 0.9f, 0.45f, 0.1f, 1.0f };
    constexpr std::uint32_t c_SecondWindowWidth = 480;
    constexpr std::uint32_t c_SecondWindowHeight = 270;
    constexpr std::uint32_t c_SecondWindowResizedWidth = 640;
    constexpr std::uint32_t c_SecondWindowResizedHeight = 360;

    struct TriangleVertex
    {
        std::array<float, 4> Position;
        std::array<float, 4> Color;
    };

    // Counter-clockwise with Y up, laid out as Triangle.slang reads it
    constexpr std::array<TriangleVertex, 3> c_TriangleVertices
    { {
        { { 0.0f, 0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { 0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
    } };

    static_assert(sizeof(TriangleVertex) == 32);

    constexpr std::uint32_t c_CheckerboardSize = 64;
    constexpr std::uint32_t c_CheckerboardCellSize = 8;
    constexpr Trinity::RHI::Rect c_CheckerboardPatch{ 8, 16, 24, 12 };

    // Not a multiple of 8, so mip 1 ends part way through a block and the last three mips are smaller than one
    constexpr std::uint32_t c_CompressedWidth = 52;
    constexpr std::uint32_t c_CompressedHeight = 36;
    constexpr std::uint32_t c_CompressedMipLevels = 6;
    constexpr std::uint32_t c_CompressedSeed = 20261008;
    constexpr std::array<Trinity::RHI::Format, 8> c_CompressedFormats{ Trinity::RHI::Format::BC1Unorm, Trinity::RHI::Format::BC1Srgb, Trinity::RHI::Format::BC3Unorm, Trinity::RHI::Format::BC3Srgb, Trinity::RHI::Format::BC4Unorm, Trinity::RHI::Format::BC5Unorm, Trinity::RHI::Format::BC7Unorm, Trinity::RHI::Format::BC7Srgb };

    // Left, bottom, right and top edges in clip space, so the quad sits in the top-right corner
    constexpr std::array<float, 4> c_QuadBounds{ 0.55f, 0.55f, 0.95f, 0.95f };

    // Laid out as TexturedQuad.slang reads it: two DescriptorHandles, then the bounds
    struct QuadPushData
    {
        std::array<std::uint32_t, 2> Texture;
        std::array<std::uint32_t, 2> Sampler;
        std::array<float, 4> Bounds;
    };

    static_assert(sizeof(QuadPushData) == 32);

    constexpr std::uint32_t c_FieldColumns = 200;
    constexpr std::uint32_t c_FieldRows = 50;
    constexpr std::uint32_t c_FieldQuads = c_FieldColumns * c_FieldRows;
    constexpr std::uint32_t c_FieldQuadsPerBatch = c_FieldQuads / 2;
    constexpr std::uint64_t c_FieldCheckFrame = 100;

    // After the quad field's first memory reading, so the field's check at the last frame also shows the closed window gave its Renderer memory back
    constexpr std::uint64_t c_SecondWindowOpenFrame = c_FieldCheckFrame + 60;
    constexpr std::array<std::uint32_t, 6> c_FieldQuadIndices{ 0, 1, 2, 2, 1, 3 };

    // Laid out as QuadField.slang reads it
    struct FieldVertex
    {
        std::array<float, 2> Position;
        std::uint32_t Color;
    };

    static_assert(sizeof(FieldVertex) == 12);

    // Red in the low byte, as the shader unpacks it
    std::uint32_t PackColor(const std::array<float, 4>& color)
    {
        std::uint32_t l_Packed = 0;
        for (std::size_t it_Channel = 0; it_Channel < color.size(); ++it_Channel)
        {
            l_Packed |= static_cast<std::uint32_t>(std::lround(std::clamp(color[it_Channel], 0.0f, 1.0f) * 255.0f)) << (it_Channel * 8);
        }

        return l_Packed;
    }

    std::array<std::uint8_t, 4> GetCheckerboardTexel(std::uint32_t x, std::uint32_t y)
    {
        const bool l_Light = ((x / c_CheckerboardCellSize) + (y / c_CheckerboardCellSize)) % 2 == 0;

        return l_Light ? std::array<std::uint8_t, 4>{ 230, 230, 230, 255 } : std::array<std::uint8_t, 4>{ 40, 40, 40, 255 };
    }

    // Different in every texel, so a wrong row pitch or offset in the partial copy shows up in the readback
    std::array<std::uint8_t, 4> GetPatchTexel(std::uint32_t x, std::uint32_t y)
    {
        return { static_cast<std::uint8_t>(128 + x * 5), static_cast<std::uint8_t>(32 + y * 15), static_cast<std::uint8_t>(x * 7 + y * 3), 255 };
    }

    std::string GetAsyncFilePath(std::uint32_t index)
    {
        return std::format("/saves/sandbox/async/file_{:02}.txt", index);
    }

    std::string GetAsyncFileContents(std::uint32_t index)
    {
        return std::format("async file {} {}", index, std::string(index * 1024, static_cast<char>('a' + index % 26)));
    }

    // A hue in [0, 1) at saturation 0.6 and value 0.5, so the window never gets too bright to look at
    std::array<float, 4> HueToColor(float hue)
    {
        const auto a_Channel = [hue](float offset)
        {
            const float l_K = std::fmod(offset + hue * 6.0f, 6.0f);

            return 0.5f - 0.5f * 0.6f * std::clamp(std::min(l_K, 4.0f - l_K), 0.0f, 1.0f);
        };

        return { a_Channel(5.0f), a_Channel(3.0f), a_Channel(1.0f), 1.0f };
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

    // From the Transforms alone in double precision, so it shares no code or rounding with Scene::UpdateWorldTransforms
    glm::dmat4 ComputeReferenceWorld(Trinity::Entity entity)
    {
        glm::dmat4 l_World(1.0);
        for (Trinity::Entity it_Entity = entity; it_Entity; it_Entity = it_Entity.GetParent())
        {
            const Trinity::TransformComponent& l_Transform = it_Entity.Get<Trinity::TransformComponent>();
            const glm::dmat4 l_Local = glm::translate(glm::dmat4(1.0), glm::dvec3(l_Transform.Position)) * glm::mat4_cast(glm::dquat(l_Transform.Rotation)) * glm::scale(glm::dmat4(1.0), glm::dvec3(l_Transform.Scale));
            l_World = l_Local * l_World;
        }

        return l_World;
    }

    // The largest difference of any element, relative to that element's size once it is above 1
    double GetMatrixError(const glm::mat4& matrix, const glm::dmat4& reference)
    {
        double l_Error = 0.0;
        for (glm::length_t it_Column = 0; it_Column < 4; ++it_Column)
        {
            for (glm::length_t it_Row = 0; it_Row < 4; ++it_Row)
            {
                const double l_Expected = reference[it_Column][it_Row];
                l_Error = std::max(l_Error, std::abs(static_cast<double>(matrix[it_Column][it_Row]) - l_Expected) / std::max(1.0, std::abs(l_Expected)));
            }
        }

        return l_Error;
    }

    // Every link agrees with its neighbours, every child count matches its list, and hierarchy order reaches every entity once
    bool CheckHierarchyLinks(Trinity::Scene& scene)
    {
        std::size_t l_Visited = 0;
        for (Trinity::Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            ++l_Visited;

            // A missing neighbour is a null handle in this scene, which only equals another such handle
            std::uint32_t l_Children = 0;
            Trinity::Entity l_Previous(entt::null, &scene);
            for (Trinity::Entity it_Child = it_Entity.GetFirstChild(); it_Child; it_Child = it_Child.GetNextSibling())
            {
                if (it_Child.GetParent() != it_Entity || it_Child.GetPreviousSibling() != l_Previous)
                {
                    return false;
                }

                l_Previous = it_Child;
                ++l_Children;
            }

            if (l_Children != it_Entity.GetChildCount() || it_Entity.GetLastChild() != l_Previous)
            {
                return false;
            }
        }

        return l_Visited == scene.GetEntityCount();
    }

    // Stands in for a game module's component, so the scene file test can unregister it and see it kept as YAML
    struct HealthComponent
    {
        static constexpr std::string_view c_TypeName = "Sandbox.Health";

        float Current = 100.0f;
        float Maximum = 100.0f;

        [[nodiscard]] bool operator==(const HealthComponent&) const = default;
    };

    void SaveHealth(const HealthComponent& component, Trinity::ComponentWriter& writer)
    {
        writer.Write("Current", component.Current);
        writer.Write("Maximum", component.Maximum);
    }

    void LoadHealth(HealthComponent& component, const Trinity::ComponentReader& reader)
    {
        reader.Read("Current", component.Current);
        reader.Read("Maximum", component.Maximum);
    }

    // Plain, huge, tiny, subnormal and negative zero, so the shortest round-trip form is tested at its edges
    float RandomSceneFloat(std::mt19937& random)
    {
        constexpr std::array<float, 6> c_Edges{ -0.0f, 1e-45f, 1.17549435e-38f, 3.40282347e38f, -1e-30f, 0.1f };

        switch (std::uniform_int_distribution<std::uint32_t>(0, 3)(random))
        {
            case 0:
                return c_Edges[std::uniform_int_distribution<std::size_t>(0, c_Edges.size() - 1)(random)];
            case 1:
                return std::uniform_real_distribution<float>(-1e-6f, 1e-6f)(random);
            default:
                return std::uniform_real_distribution<float>(-1000.0f, 1000.0f)(random);
        }
    }

    // Names YAML would misread as other types or as syntax, so the writer's quoting is tested
    void BuildRandomScene(Trinity::Scene& scene, std::mt19937& random)
    {
        constexpr std::array<std::string_view, 16> c_Names{ "Player", "true", "123", "a: b", "#hash", "  padded  ", "", "quote\"s", "line\nbreak", "Ünïcødé", "- dash", "[bracket]", "{brace}", "null", "~", "Entity" };

        std::vector<Trinity::Entity> l_Entities;
        l_Entities.reserve(c_SceneFileEntityCount);
        for (std::uint32_t it_Index = 0; it_Index < c_SceneFileEntityCount; ++it_Index)
        {
            const bool l_Root = l_Entities.empty() || std::uniform_int_distribution<std::uint32_t>(0, 4)(random) == 0;
            const Trinity::Entity l_Parent = l_Root ? Trinity::Entity() : l_Entities[std::uniform_int_distribution<std::size_t>(0, l_Entities.size() - 1)(random)];
            const std::string_view l_Name = c_Names[std::uniform_int_distribution<std::size_t>(0, c_Names.size() - 1)(random)];

            Trinity::Entity l_Entity = scene.CreateEntity(it_Index % 3 == 0 ? std::format("{} {}", l_Name, it_Index) : std::string(l_Name), l_Parent);
            if (it_Index % 11 == 0)
            {
                l_Entity.Remove<Trinity::TagComponent>();
            }

            Trinity::TransformComponent& l_Transform = l_Entity.Get<Trinity::TransformComponent>();
            l_Transform.Position = glm::vec3(RandomSceneFloat(random), RandomSceneFloat(random), RandomSceneFloat(random));
            l_Transform.Rotation = glm::normalize(glm::quat(RandomSceneFloat(random), RandomSceneFloat(random), RandomSceneFloat(random), 1.0f));
            l_Transform.Scale = glm::vec3(RandomSceneFloat(random), RandomSceneFloat(random), RandomSceneFloat(random));

            if (it_Index % 4 == 0)
            {
                Trinity::CameraComponent& l_Camera = l_Entity.Add<Trinity::CameraComponent>();
                l_Camera.OrthographicSize = RandomSceneFloat(random);
                l_Camera.Near = RandomSceneFloat(random);
                l_Camera.Far = RandomSceneFloat(random);
                l_Camera.Primary = it_Index % 8 == 0;
            }

            if (it_Index % 2 == 0)
            {
                Trinity::SpriteRendererComponent& l_Sprite = l_Entity.Add<Trinity::SpriteRendererComponent>();
                l_Sprite.Texture = it_Index % 6 == 0 ? Trinity::UUID() : Trinity::UUID::Generate();
                l_Sprite.Tint = glm::vec4(RandomSceneFloat(random), RandomSceneFloat(random), RandomSceneFloat(random), RandomSceneFloat(random));
                l_Sprite.FlipX = it_Index % 4 == 2;
                l_Sprite.FlipY = it_Index % 8 == 2;
                l_Sprite.UVRect = glm::vec4(RandomSceneFloat(random), RandomSceneFloat(random), RandomSceneFloat(random), RandomSceneFloat(random));
                l_Sprite.SortingLayer = std::uniform_int_distribution<std::int32_t>(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max())(random);
                l_Sprite.OrderInLayer = std::uniform_int_distribution<std::int32_t>(-100, 100)(random);
            }

            if (it_Index % 5 == 0)
            {
                l_Entity.Add<HealthComponent>(RandomSceneFloat(random), RandomSceneFloat(random));
            }

            l_Entities.push_back(l_Entity);
        }
    }

    template<Trinity::Component T>
    bool ComponentMatches(const Trinity::Entity& a, const Trinity::Entity& b)
    {
        return a.Has<T>() == b.Has<T>() && (!a.Has<T>() || a.Get<T>() == b.Get<T>());
    }

    // The same entities in the same hierarchy order, with the same parents and equal components
    bool ScenesMatch(Trinity::Scene& a, Trinity::Scene& b, bool compareHealth)
    {
        Trinity::Entity l_A = a.GetFirstRoot();
        Trinity::Entity l_B = b.GetFirstRoot();
        while (l_A && l_B)
        {
            const Trinity::Entity l_ParentA = l_A.GetParent();
            const Trinity::Entity l_ParentB = l_B.GetParent();
            if (l_A.GetUUID() != l_B.GetUUID() || static_cast<bool>(l_ParentA) != static_cast<bool>(l_ParentB) || (l_ParentA && l_ParentA.GetUUID() != l_ParentB.GetUUID()))
            {
                return false;
            }

            if (!ComponentMatches<Trinity::TagComponent>(l_A, l_B) || !ComponentMatches<Trinity::TransformComponent>(l_A, l_B) || !ComponentMatches<Trinity::CameraComponent>(l_A, l_B) || !ComponentMatches<Trinity::SpriteRendererComponent>(l_A, l_B) || (compareHealth && !ComponentMatches<HealthComponent>(l_A, l_B)))
            {
                return false;
            }

            l_A = a.GetNextInHierarchyOrder(l_A);
            l_B = b.GetNextInHierarchyOrder(l_B);
        }

        return !l_A && !l_B && a.GetEntityCount() == b.GetEntityCount();
    }

    std::string GetAssetFilePath(std::uint32_t index)
    {
        return std::format("{}/{}/file_{:02}.bin", c_AssetTestRoot, index % 2 == 0 ? "a" : "b/c", index);
    }

    // Different lengths, from a few bytes to 4 KiB, so a loaded asset holding another file's bytes shows up
    std::string GetAssetFileContents(std::uint32_t index)
    {
        return std::format("asset {} {}", index, std::string((index * 97) % 4096, static_cast<char>('a' + index % 26)));
    }

    // Files only. Emptied folders stay, which the registry ignores
    void RemoveFiles(std::string_view directory)
    {
        const Trinity::Expected<std::vector<Trinity::DirectoryEntry>, Trinity::FileError> l_Entries = Trinity::FileSystem::List(directory);
        if (!l_Entries)
        {
            return;
        }

        for (const Trinity::DirectoryEntry& it_Entry : *l_Entries)
        {
            const std::string l_Path = std::format("{}/{}", directory, it_Entry.Name);
            if (it_Entry.Type == Trinity::FileType::Directory)
            {
                RemoveFiles(l_Path);
            }
            else
            {
                static_cast<void>(Trinity::FileSystem::RemoveFile(l_Path));
            }
        }
    }

    // A file and its .meta, as a file manager moving both would
    bool MoveWithMeta(const std::string& from, const std::string& to)
    {
        for (const auto& [it_From, it_To] : { std::pair{ from, to }, std::pair{ from + ".meta", to + ".meta" } })
        {
            const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Data = Trinity::FileSystem::ReadFile(it_From);
            if (!l_Data || !Trinity::FileSystem::WriteFile(it_To, *l_Data) || !Trinity::FileSystem::RemoveFile(it_From))
            {
                return false;
            }
        }

        return true;
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

    TR_INFO("Sandbox attached. Escape closes the window, M prints memory use, O overflows the frame allocator, U overflows the upload ring, C lists console variables, V toggles vsync.");
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

    if (Trinity::Application::Get().GetSpecification().CommandLineArgs.HasOption("log-test"))
    {
        TestLogHistory();
    }

    TestFileSystem();
    TestSaves();
    TestScene();
    TestHierarchy();
    TestSceneFiles();
    TestAssetRegistry();
    TestModules();
    TestShaders();
    TestRHI();
    TestResources();
    TestCompressedTextures();
    if (Trinity::Application::Get().GetSpecification().CommandLineArgs.HasOption("stale-handle"))
    {
        TestStaleHandle();
    }

    CreateTriangle();
    CreateCheckerboard();
    CreateQuadField();
    StartAsyncReads();

    TR_INFO("Example UUID: {}", Trinity::UUID::Generate());
    if (Trinity::Application::Get().GetSpecification().CommandLineArgs.HasOption("uuid-test"))
    {
        TestUUIDs();
    }

    m_SecondWindowPending = Trinity::Application::Get().GetSpecification().CommandLineArgs.HasOption("second-window");
    if (m_SecondWindowPending)
    {
        TR_INFO("Second window: opens at frame {} and closes {} frames later", c_SecondWindowOpenFrame, 6 * c_SecondWindowStepFrames);
    }

    Trinity::Memory::LogUsage();
}

void SandboxLayer::OnDetach()
{
    Trinity::Memory::Free(m_ScratchBuffer);
    m_ScratchBuffer = nullptr;

    m_Probe = std::vector<std::uint32_t>();

    m_AsyncReads.clear();

    if (m_AssetLoadsActive)
    {
        TR_WARN("Asset load test: stopped after {} of {} loads, before it finished", m_AssetLoadsStarted, c_AssetLoadCount);
    }

    m_AssetRefs.clear();
    Trinity::AssetManager::SetRegistry(nullptr);
    m_AssetRegistry.reset();

    if (m_SecondWindow)
    {
        TR_WARN("Second window: closed after {} frame(s), before the test finished", m_SecondWindowFrames);
        CloseSecondWindow();
    }

    DestroyTriangle();
    DestroyCheckerboard();
    DestroyQuadField();

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
    UpdateAssetLoads();
    UpdateSecondWindow();

    m_ClearHue = std::fmod(m_ClearHue + timestep.GetSeconds() / c_ClearCycleSeconds, 1.0f);
    m_FieldSeconds = std::fmod(m_FieldSeconds + timestep.GetSeconds(), 2.0f * std::numbers::pi_v<float>);
    Trinity::Application::Get().GetRenderer().SetClearColor(HueToColor(m_ClearHue));

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

void SandboxLayer::OnRender(Trinity::RHI::CommandList& commands)
{
    if (m_TrianglePipeline)
    {
        // The push constants hold a Slang DescriptorHandle, two 32-bit values of which the first is the index
        const std::array<std::uint32_t, 2> l_PushData{ m_TriangleVertexIndex, 0 };

        commands.SetPipeline(m_TrianglePipeline);
        commands.PushConstants(std::as_bytes(std::span(l_PushData)));
        commands.Draw(3, 1, 0, 0);
    }

    if (m_QuadPipeline)
    {
        const QuadPushData l_PushData{ { m_CheckerboardIndex, 0 }, { m_CheckerboardSamplerIndex, 0 }, c_QuadBounds };

        commands.SetPipeline(m_QuadPipeline);
        commands.PushConstants(std::as_bytes(std::span(&l_PushData, 1)));
        commands.Draw(4, 1, 0, 0);
    }

    if (m_FieldPipeline)
    {
        DrawQuadField(commands);
    }
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

    if (event.GetKeyCode() == Trinity::KeyCode::TR_V)
    {
        Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
        l_Renderer.SetVSync(!l_Renderer.IsVSync());

        return true;
    }

    if (event.GetKeyCode() == Trinity::KeyCode::TR_O)
    {
        Trinity::FrameAllocator& l_FrameAllocator = Trinity::Application::Get().GetFrameAllocator();
        [[maybe_unused]] void* l_Overflow = l_FrameAllocator.Allocate(l_FrameAllocator.GetCapacity() + 1);

        return true;
    }

    if (event.GetKeyCode() == Trinity::KeyCode::TR_U)
    {
        m_OverflowUploadNextFrame = true;
        ++m_UploadOverflowRequests;

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

void SandboxLayer::TestScene()
{
    TR_PROFILE_FUNCTION();

    const Trinity::MemoryTagStats l_Before = Trinity::Memory::GetStats(Trinity::MemoryTag::Scene);

    std::uint32_t l_Mismatches = 0;
    std::uint64_t l_PeakBytes = 0;
    {
        Trinity::Scene l_Scene;

        std::vector<Trinity::UUID> l_UUIDs;
        l_UUIDs.reserve(c_SceneEntityCount);
        for (std::uint32_t it_Index = 0; it_Index < c_SceneEntityCount; ++it_Index)
        {
            const Trinity::Entity l_Entity = l_Scene.CreateEntity(std::format("Entity {}", it_Index));
            l_UUIDs.push_back(l_Entity.GetUUID());
        }

        // Longer than any small-string buffer, so every renamed Tag allocates under Scene
        for (std::uint32_t it_Index = 0; it_Index < c_SceneEntityCount; ++it_Index)
        {
            Trinity::Entity l_Entity = l_Scene.FindEntityByUUID(l_UUIDs[it_Index]);
            if (!l_Entity || std::string_view(l_Entity.Get<Trinity::TagComponent>().Tag) != std::format("Entity {}", it_Index))
            {
                ++l_Mismatches;

                continue;
            }

            l_Entity.Get<Trinity::TagComponent>().Tag = std::format("Renamed by the Sandbox scene test as entity {}", it_Index);
        }

        // Every other entity goes first, so the rest are checked with holes between them
        for (std::uint32_t it_Index = 1; it_Index < c_SceneEntityCount; it_Index += 2)
        {
            const Trinity::Entity l_Entity = l_Scene.FindEntityByUUID(l_UUIDs[it_Index]);
            if (l_Entity)
            {
                l_Scene.DestroyEntity(l_Entity);
            }
        }

        l_PeakBytes = Trinity::Memory::GetStats(Trinity::MemoryTag::Scene).PeakBytes;

        if (l_Scene.GetEntityCount() != c_SceneEntityCount / 2)
        {
            ++l_Mismatches;
        }

        for (std::uint32_t it_Index = 0; it_Index < c_SceneEntityCount; ++it_Index)
        {
            const Trinity::Entity l_Entity = l_Scene.FindEntityByUUID(l_UUIDs[it_Index]);
            const bool l_Kept = it_Index % 2 == 0;
            if (l_Kept != static_cast<bool>(l_Entity) || (l_Kept && (l_Entity.GetUUID() != l_UUIDs[it_Index] || std::string_view(l_Entity.Get<Trinity::TagComponent>().Tag) != std::format("Renamed by the Sandbox scene test as entity {}", it_Index))))
            {
                ++l_Mismatches;
            }

            if (l_Entity)
            {
                l_Scene.DestroyEntity(l_Entity);
            }
        }

        if (l_Scene.GetEntityCount() != 0)
        {
            ++l_Mismatches;
        }
    }

    const Trinity::MemoryTagStats l_After = Trinity::Memory::GetStats(Trinity::MemoryTag::Scene);
    if (l_Mismatches != 0 || l_After.CurrentBytes != l_Before.CurrentBytes || l_After.LiveAllocations != l_Before.LiveAllocations)
    {
        TR_ERROR("Scene test: {} mismatch(es) over {} entities, and Scene holds {} in {} live after the scene was destroyed", l_Mismatches, c_SceneEntityCount, Trinity::Memory::FormatBytes(l_After.CurrentBytes), l_After.LiveAllocations);

        return;
    }

    TR_INFO("Scene test: created, renamed and destroyed {} entities with 0 mismatches; Scene peaked at {} and holds {} in {} live after the scene was destroyed", c_SceneEntityCount, Trinity::Memory::FormatBytes(l_PeakBytes), Trinity::Memory::FormatBytes(l_After.CurrentBytes), l_After.LiveAllocations);
}

void SandboxLayer::TestHierarchy()
{
    TR_PROFILE_FUNCTION();

    const Trinity::MemoryTagStats l_Before = Trinity::Memory::GetStats(Trinity::MemoryTag::Scene);

    std::mt19937 l_Random(c_HierarchySeed);
    std::uniform_real_distribution<float> l_Signed(-1.0f, 1.0f);
    std::uniform_real_distribution<float> l_Angle(0.0f, 2.0f * std::numbers::pi_v<float>);
    std::uniform_real_distribution<float> l_Scale(0.8f, 1.25f);
    const auto a_Pick = [&l_Random](std::size_t first, std::size_t count)
    {
        return std::uniform_int_distribution<std::size_t>(first, count - 1)(l_Random);
    };

    std::uint32_t l_Failures = 0;
    std::uint32_t l_Refused = 0;
    double l_WorldError = 0.0;
    double l_KeptError = 0.0;
    std::size_t l_SubtreeSize = 0;
    {
        Trinity::Scene l_Scene;

        std::vector<Trinity::Entity> l_Entities;
        l_Entities.reserve(c_HierarchyEntityCount);
        l_Entities.push_back(l_Scene.CreateEntity("Hierarchy root"));

        // Uniform scales, so keeping a world transform through a reparent never needs the shear a Transform cannot hold
        for (std::uint32_t it_Index = 1; it_Index < c_HierarchyEntityCount; ++it_Index)
        {
            Trinity::Entity l_Entity = l_Scene.CreateEntity(std::format("Node {}", it_Index), l_Entities[a_Pick(0, l_Entities.size())]);

            Trinity::TransformComponent& l_Transform = l_Entity.Get<Trinity::TransformComponent>();
            l_Transform.Position = glm::vec3(l_Signed(l_Random), l_Signed(l_Random), l_Signed(l_Random));
            l_Transform.Rotation = glm::angleAxis(l_Angle(l_Random), glm::normalize(glm::vec3(l_Signed(l_Random), l_Signed(l_Random), l_Signed(l_Random)) + glm::vec3(0.0f, 0.0f, 0.01f)));
            l_Transform.Scale = glm::vec3(l_Scale(l_Random));

            l_Entities.push_back(l_Entity);
        }

        // Half append to a new parent, half move before a new sibling. The root is never moved or made a sibling, so destroying it destroys everything
        for (std::uint32_t it_Move = 0; it_Move < c_HierarchyReparentCount; ++it_Move)
        {
            const Trinity::Entity l_Entity = l_Entities[a_Pick(1, l_Entities.size())];
            const glm::mat4 l_WorldBefore = l_Scene.ComputeWorldMatrix(l_Entity);

            bool l_Moved = false;
            while (!l_Moved)
            {
                const Trinity::Entity l_Target = l_Entities[a_Pick(it_Move % 2 == 0 ? 0 : 1, l_Entities.size())];
                l_Moved = it_Move % 2 == 0 ? l_Scene.SetParent(l_Entity, l_Target) : l_Scene.MoveBefore(l_Entity, l_Target);
                if (!l_Moved)
                {
                    ++l_Refused;
                }
            }

            l_KeptError = std::max(l_KeptError, GetMatrixError(l_Scene.ComputeWorldMatrix(l_Entity), glm::dmat4(l_WorldBefore)));
        }

        if (!CheckHierarchyLinks(l_Scene))
        {
            ++l_Failures;
        }

        l_Scene.UpdateWorldTransforms();
        for (const Trinity::Entity it_Entity : l_Entities)
        {
            l_WorldError = std::max(l_WorldError, GetMatrixError(it_Entity.Get<Trinity::WorldTransformComponent>().Matrix, ComputeReferenceWorld(it_Entity)));
        }

        // The root's first child, compared with its copy entity by entity in hierarchy order
        const Trinity::Entity l_Source = l_Entities.front().GetFirstChild();
        std::unordered_set<Trinity::UUID> l_SourceUUIDs;
        for (Trinity::Entity it_Entity = l_Source; it_Entity; it_Entity = l_Scene.GetNextInSubtree(it_Entity, l_Source))
        {
            l_SourceUUIDs.insert(it_Entity.GetUUID());
        }

        l_SubtreeSize = l_SourceUUIDs.size();

        const Trinity::Entity l_Copy = l_Scene.DuplicateEntity(l_Source);
        if (l_Copy.GetParent() != l_Source.GetParent() || l_Source.GetNextSibling() != l_Copy || l_Scene.GetEntityCount() != c_HierarchyEntityCount + l_SubtreeSize)
        {
            ++l_Failures;
        }

        Trinity::Entity l_Original = l_Source;
        Trinity::Entity l_Duplicate = l_Copy;
        std::size_t l_Compared = 0;
        while (l_Original && l_Duplicate)
        {
            const Trinity::TransformComponent& l_A = l_Original.Get<Trinity::TransformComponent>();
            const Trinity::TransformComponent& l_B = l_Duplicate.Get<Trinity::TransformComponent>();
            const bool l_SameShape = l_Original.GetChildCount() == l_Duplicate.GetChildCount() && std::string_view(l_Original.Get<Trinity::TagComponent>().Tag) == std::string_view(l_Duplicate.Get<Trinity::TagComponent>().Tag);
            const bool l_SameTransform = l_A.Position == l_B.Position && l_A.Rotation == l_B.Rotation && l_A.Scale == l_B.Scale;
            const bool l_NewUUID = !l_SourceUUIDs.contains(l_Duplicate.GetUUID()) && l_Scene.FindEntityByUUID(l_Duplicate.GetUUID()) == l_Duplicate;
            if (!l_SameShape || !l_SameTransform || !l_NewUUID)
            {
                ++l_Failures;
            }

            ++l_Compared;
            l_Original = l_Scene.GetNextInSubtree(l_Original, l_Source);
            l_Duplicate = l_Scene.GetNextInSubtree(l_Duplicate, l_Copy);
        }

        if (l_Original || l_Duplicate || l_Compared != l_SubtreeSize || !CheckHierarchyLinks(l_Scene))
        {
            ++l_Failures;
        }

        l_Scene.DestroyEntity(l_Entities.front());
        if (l_Scene.GetEntityCount() != 0 || l_Scene.GetRootCount() != 0 || l_Scene.GetFirstRoot())
        {
            ++l_Failures;
        }

        for (const auto [it_Id, it_Storage] : l_Scene.GetRegistry().storage())
        {
            if (!it_Storage.empty())
            {
                ++l_Failures;
            }
        }
    }

    const Trinity::MemoryTagStats l_After = Trinity::Memory::GetStats(Trinity::MemoryTag::Scene);
    if (l_Failures != 0 || l_WorldError > c_HierarchyTolerance || l_KeptError > c_HierarchyTolerance || l_After.CurrentBytes != l_Before.CurrentBytes || l_After.LiveAllocations != l_Before.LiveAllocations)
    {
        TR_ERROR("Hierarchy test: {} failure(s), world matrices off by {:.2e} and kept world transforms by {:.2e} (limit {:.0e}), and Scene holds {} in {} live after the scene was destroyed", l_Failures, l_WorldError, l_KeptError, c_HierarchyTolerance, Trinity::Memory::FormatBytes(l_After.CurrentBytes), l_After.LiveAllocations);

        return;
    }

    TR_INFO("Hierarchy test: {} entities, {} reparented ({} refused as cycles); world matrices within {:.2e} of a double-precision reference and kept world transforms within {:.2e} (limit {:.0e}); a {}-entity subtree duplicated with new UUIDs and the same shape; destroying the root emptied every pool, and Scene holds {} in {} live after the scene was destroyed", c_HierarchyEntityCount, c_HierarchyReparentCount, l_Refused, l_WorldError, l_KeptError, c_HierarchyTolerance, l_SubtreeSize, Trinity::Memory::FormatBytes(l_After.CurrentBytes), l_After.LiveAllocations);
}

void SandboxLayer::TestSceneFiles()
{
    TR_PROFILE_FUNCTION();

    const Trinity::MemoryTagStats l_Before = Trinity::Memory::GetStats(Trinity::MemoryTag::Scene);
    const Trinity::ComponentSerializer l_HealthSerializer = Trinity::MakeComponentSerializer<HealthComponent, SaveHealth, LoadHealth>();

    std::vector<std::string> l_Failures;
    std::size_t l_FileSize = 0;
    std::size_t l_KeptUnknown = 0;
    std::size_t l_HealthCount = 0;
    std::string l_Refusal;
    {
        Trinity::Scene l_A;
        Trinity::Scene l_B;
        Trinity::Scene l_C;
        for (Trinity::Scene* it_Scene : { &l_A, &l_B, &l_C })
        {
            static_cast<void>(it_Scene->GetRegistry().storage<HealthComponent>());
        }

        std::mt19937 l_Random(c_SceneFileSeed);
        BuildRandomScene(l_A, l_Random);
        l_HealthCount = l_A.GetRegistry().storage<HealthComponent>().size();

        // A is saved knowing Health, B loads it without, and C loads B's file knowing Health again
        Trinity::SceneSerializer::RegisterComponent(l_HealthSerializer);
        const Trinity::Expected<void, std::string> l_SavedA = Trinity::SceneSerializer::Save(l_A, c_SceneFilePaths[0]);

        Trinity::SceneSerializer::UnregisterComponent(HealthComponent::c_TypeName);
        const Trinity::Expected<void, std::string> l_LoadedB = Trinity::SceneSerializer::Load(l_B, c_SceneFilePaths[0]);
        const Trinity::Expected<void, std::string> l_SavedB = Trinity::SceneSerializer::Save(l_B, c_SceneFilePaths[1]);
        l_KeptUnknown = l_B.GetRegistry().storage<Trinity::UnknownComponentsComponent>().size();

        Trinity::SceneSerializer::RegisterComponent(l_HealthSerializer);
        const Trinity::Expected<void, std::string> l_LoadedC = Trinity::SceneSerializer::Load(l_C, c_SceneFilePaths[1]);
        const Trinity::Expected<void, std::string> l_SavedC = Trinity::SceneSerializer::Save(l_C, c_SceneFilePaths[2]);

        for (const Trinity::Expected<void, std::string>* it_Result : { &l_SavedA, &l_LoadedB, &l_SavedB, &l_LoadedC, &l_SavedC })
        {
            if (!*it_Result)
            {
                l_Failures.push_back(it_Result->GetError());
            }
        }

        const Trinity::Expected<std::string, Trinity::FileError> l_TextA = Trinity::FileSystem::ReadText(c_SceneFilePaths[0]);
        const Trinity::Expected<std::string, Trinity::FileError> l_TextB = Trinity::FileSystem::ReadText(c_SceneFilePaths[1]);
        const Trinity::Expected<std::string, Trinity::FileError> l_TextC = Trinity::FileSystem::ReadText(c_SceneFilePaths[2]);
        if (!l_TextA || !l_TextB || !l_TextC || *l_TextA != *l_TextB || *l_TextA != *l_TextC)
        {
            l_Failures.push_back("the three saved files differ");
        }

        l_FileSize = l_TextA ? l_TextA->size() : 0;

        if (l_KeptUnknown != l_HealthCount || !ScenesMatch(l_A, l_B, false) || !ScenesMatch(l_A, l_C, true))
        {
            l_Failures.push_back(std::format("components differ after loading, with {} of {} Health components kept as YAML", l_KeptUnknown, l_HealthCount));
        }

        const Trinity::Expected<void, std::string> l_Newer = Trinity::SceneSerializer::LoadFromText(l_C, std::format("Format: {}\nEntities: []\n", Trinity::SceneSerializer::c_FormatVersion + 1));
        if (l_Newer || l_C.GetEntityCount() != c_SceneFileEntityCount)
        {
            l_Failures.push_back("a file from a newer format was loaded, or emptied the scene");
        }
        else
        {
            l_Refusal = l_Newer.GetError();
        }

        Trinity::SceneSerializer::UnregisterComponent(HealthComponent::c_TypeName);
    }

    const Trinity::MemoryTagStats l_After = Trinity::Memory::GetStats(Trinity::MemoryTag::Scene);
    if (!l_Failures.empty() || l_After.CurrentBytes != l_Before.CurrentBytes || l_After.LiveAllocations != l_Before.LiveAllocations)
    {
        for (const std::string& it_Failure : l_Failures)
        {
            TR_ERROR("Scene file test: {}", it_Failure);
        }

        TR_ERROR("Scene file test: {} failure(s), and Scene holds {} in {} live after the scenes were destroyed", l_Failures.size(), Trinity::Memory::FormatBytes(l_After.CurrentBytes), l_After.LiveAllocations);

        return;
    }

    TR_INFO("Scene file test: {} entities saved, loaded and saved again through {} into {} byte-identical files of {}; every component equal after loading, {} Health components kept as YAML while unregistered; a newer format refused with \"{}\"; Scene holds {} in {} live after the scenes were destroyed", c_SceneFileEntityCount, c_SceneFilePaths[0], c_SceneFilePaths.size(), Trinity::Memory::FormatBytes(l_FileSize), l_KeptUnknown, l_Refusal, Trinity::Memory::FormatBytes(l_After.CurrentBytes), l_After.LiveAllocations);
}

void SandboxLayer::TestAssetRegistry()
{
    TR_PROFILE_FUNCTION();

    RemoveFiles(c_AssetTestRoot);

    std::vector<std::string> l_Paths;
    for (std::uint32_t it_Index = 0; it_Index < c_AssetFileCount; ++it_Index)
    {
        l_Paths.push_back(GetAssetFilePath(it_Index));
        if (!Trinity::FileSystem::WriteText(l_Paths.back(), GetAssetFileContents(it_Index)))
        {
            TR_ERROR("Asset registry test: {} could not be written", l_Paths.back());

            return;
        }
    }

    m_AssetRegistry = Trinity::CreateScope<Trinity::AssetRegistry>(c_AssetTestRoot);
    const Trinity::AssetScanReport l_First = m_AssetRegistry->Scan();

    // One file renamed in its folder, one moved to another, each with its .meta
    const Trinity::AssetRecord* l_RenamedRecord = m_AssetRegistry->FindByPath(l_Paths[0]);
    const Trinity::AssetRecord* l_MovedRecord = m_AssetRegistry->FindByPath(l_Paths[1]);
    const Trinity::UUID l_RenamedID = l_RenamedRecord != nullptr ? l_RenamedRecord->ID : Trinity::UUID();
    const Trinity::UUID l_MovedID = l_MovedRecord != nullptr ? l_MovedRecord->ID : Trinity::UUID();
    const std::string l_RenamedPath = std::format("{}/a/renamed_00.bin", c_AssetTestRoot);
    const std::string l_MovedPath = std::format("{}/moved/file_01.bin", c_AssetTestRoot);
    const bool l_FilesMoved = MoveWithMeta(l_Paths[0], l_RenamedPath) && MoveWithMeta(l_Paths[1], l_MovedPath);

    const Trinity::AssetScanReport l_Second = m_AssetRegistry->Scan();
    const Trinity::AssetRecord* l_Renamed = m_AssetRegistry->FindByPath(l_RenamedPath);
    const Trinity::AssetRecord* l_Moved = m_AssetRegistry->FindByPath(l_MovedPath);
    const bool l_Kept = l_FilesMoved && l_RenamedID && l_MovedID && l_Renamed != nullptr && l_Moved != nullptr && l_Renamed->ID == l_RenamedID && l_Moved->ID == l_MovedID && m_AssetRegistry->FindByPath(l_Paths[0]) == nullptr;
    if (l_First.Created != c_AssetFileCount || l_Second.Assets != c_AssetFileCount || l_Second.Moved != 2 || l_Second.Created != 0 || l_Second.Regenerated != 0 || l_Second.Removed != 0 || !l_Kept)
    {
        TR_ERROR("Asset registry test: the first scan gave {} of {} files a .meta, and after renaming and moving two the second found {} moved, {} new and {} given a new UUID, {}", l_First.Created, c_AssetFileCount, l_Second.Moved, l_Second.Created, l_Second.Regenerated, l_Kept ? "with their UUIDs kept" : "without their UUIDs kept");

        return;
    }

    TR_INFO("Asset registry test: {} files under {} given a .meta each, and a renamed and a moved one kept their UUIDs {} and {}", c_AssetFileCount, c_AssetTestRoot, l_RenamedID, l_MovedID);

    l_Paths[0] = l_RenamedPath;
    l_Paths[1] = l_MovedPath;

    // A warning by design, so it only runs when asked for
    if (Trinity::Application::Get().GetSpecification().CommandLineArgs.HasOption("asset-meta-test"))
    {
        const Trinity::UUID l_OldID = m_AssetRegistry->FindByPath(l_Paths[2])->ID;
        static_cast<void>(Trinity::FileSystem::RemoveFile(l_Paths[2] + ".meta"));

        const Trinity::AssetScanReport l_Third = m_AssetRegistry->Scan();
        const Trinity::AssetRecord* l_Record = m_AssetRegistry->FindByPath(l_Paths[2]);
        if (l_Third.Regenerated != 1 || l_Third.Created != 0 || l_Record == nullptr || l_Record->ID == l_OldID || !Trinity::FileSystem::Exists(l_Paths[2] + ".meta"))
        {
            TR_ERROR("Asset meta test: deleting the .meta of {} gave {} new UUID(s) and {} new asset(s), not one new UUID", l_Paths[2], l_Third.Regenerated, l_Third.Created);
        }
        else
        {
            TR_INFO("Asset meta test: deleting the .meta of {} gave it the new UUID {} in place of {}, with one warning", l_Paths[2], l_Record->ID, l_OldID);
        }
    }

    for (std::uint32_t it_Index = 0; it_Index < c_AssetFileCount; ++it_Index)
    {
        const Trinity::UUID l_ID = m_AssetRegistry->FindByPath(l_Paths[it_Index])->ID;
        m_AssetIDs.push_back(l_ID);
        m_AssetFileIndices.emplace(l_ID, it_Index);
    }

    Trinity::AssetManager::SetRegistry(m_AssetRegistry.get());
    m_AssetsBefore = Trinity::Memory::GetStats(Trinity::MemoryTag::Assets);
    m_AssetRandom.seed(c_AssetSeed);
    m_AssetPhaseFrame = Trinity::Application::Get().GetFrameCount();
    m_AssetLoadsActive = true;
}

// Each frame takes up to 32 assets nobody holds, so every one starts a read. A finished one is checked against its file and let go, and every third is let go a frame later whether or not it has finished. After 1000 loads the rest finish, and once released Assets must be as it was
void SandboxLayer::UpdateAssetLoads()
{
    if (!m_AssetLoadsActive)
    {
        return;
    }

    const std::uint64_t l_Frame = Trinity::Application::Get().GetFrameCount();
    if (l_Frame - m_AssetPhaseFrame > c_AssetTimeoutFrames)
    {
        TR_ERROR("Asset load test: not finished after {} frames, with {} of {} loads started and {} reference(s) held", c_AssetTimeoutFrames, m_AssetLoadsStarted, c_AssetLoadCount, m_AssetRefs.size());
        m_AssetRefs.clear();
        Trinity::AssetManager::SetRegistry(nullptr);
        m_AssetLoadsActive = false;

        return;
    }

    const bool l_AllStarted = m_AssetLoadsStarted == c_AssetLoadCount;
    if (l_AllStarted && std::ranges::any_of(m_AssetRefs, [](const HeldAsset& held) { return held.Reference.GetState() == Trinity::AssetState::Loading; }))
    {
        return;
    }

    std::erase_if(m_AssetRefs, [this, l_Frame](const HeldAsset& held)
    {
        const Trinity::AssetRef<Trinity::BinaryAsset>& l_Reference = held.Reference;
        if (l_Reference.GetState() == Trinity::AssetState::Loading && (!held.Cancel || held.Frame == l_Frame))
        {
            return false;
        }

        if (l_Reference.GetState() == Trinity::AssetState::Loading)
        {
            ++m_AssetReleasedWhileLoading;
        }
        else if (const Trinity::BinaryAsset* l_Asset = l_Reference.Get(); l_Reference.IsReady() && l_Asset != nullptr)
        {
            const std::string l_Expected = GetAssetFileContents(m_AssetFileIndices.at(l_Reference.GetID()));
            const bool l_Matches = l_Asset->Data.size() == l_Expected.size() && std::memcmp(l_Asset->Data.data(), l_Expected.data(), l_Expected.size()) == 0;
            ++(l_Matches ? m_AssetVerified : m_AssetMismatches);
        }
        else
        {
            ++m_AssetFailures;
        }

        return true;
    });

    if (!l_AllStarted)
    {
        std::vector<Trinity::UUID> l_Free;
        std::ranges::copy_if(m_AssetIDs, std::back_inserter(l_Free), [](Trinity::UUID id) { return Trinity::AssetManager::GetState(id) == Trinity::AssetState::None; });
        std::ranges::shuffle(l_Free, m_AssetRandom);

        for (std::size_t it_Index = 0; it_Index < l_Free.size() && it_Index < c_AssetLoadsPerFrame && m_AssetLoadsStarted < c_AssetLoadCount; ++it_Index)
        {
            m_AssetRefs.push_back({ l_Frame, m_AssetLoadsStarted % c_AssetCancelEvery == 0, Trinity::AssetRef<Trinity::BinaryAsset>(l_Free[it_Index]) });
            ++m_AssetLoadsStarted;
        }

        return;
    }

    // Loads released while their bytes were being decoded finish on a worker and are thrown away a frame or two later
    const Trinity::MemoryTagStats l_Assets = Trinity::Memory::GetStats(Trinity::MemoryTag::Assets);
    if (l_Assets.CurrentBytes != m_AssetsBefore.CurrentBytes || l_Assets.LiveAllocations != m_AssetsBefore.LiveAllocations || Trinity::AssetManager::GetEntryCount() != 0)
    {
        return;
    }

    Trinity::AssetManager::SetRegistry(nullptr);
    m_AssetLoadsActive = false;

    if (m_AssetFailures != 0 || m_AssetMismatches != 0)
    {
        TR_ERROR("Asset load test: {} asset(s) failed and {} did not match their file", m_AssetFailures, m_AssetMismatches);

        return;
    }

    TR_INFO("Asset load test: {} asynchronous loads and releases of {} assets over {} frames; {} finished and matched their file, {} were released while still loading, and Assets holds {} in {} live after the last release", c_AssetLoadCount, c_AssetFileCount, l_Frame - m_AssetPhaseFrame, m_AssetVerified, m_AssetReleasedWhileLoading, Trinity::Memory::FormatBytes(l_Assets.CurrentBytes), l_Assets.LiveAllocations);
}

void SandboxLayer::TestModules()
{
    // Modules exist only where the engine is shared: a module linking the static engine would carry a second copy of it
#if defined(TR_ENGINE_SHARED)
    TR_PROFILE_FUNCTION();

    using AttachFunction = void (*)();
    using DescribeFunction = void (*)(std::string&);
    using TagEntityFunction = void (*)(Trinity::Scene&, Trinity::UUID);
    using DetachFunction = void (*)();

    const std::size_t l_VariablesBefore = CountConsoleVariables();
    const Trinity::MemoryTagStats l_GameBefore = Trinity::Memory::GetStats(Trinity::MemoryTag::Game);
    const Trinity::MemoryTagStats l_SceneBefore = Trinity::Memory::GetStats(Trinity::MemoryTag::Scene);

    // Outlives every load, so a pool the module created would be destroyed through code that has already unloaded
    Trinity::Scope<Trinity::Scene> l_Scene = Trinity::CreateScope<Trinity::Scene>();

    std::string l_Description;
    std::uint32_t l_Failures = 0;
    std::uint32_t l_SceneFailures = 0;
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
        const TagEntityFunction l_TagEntity = l_Module->GetFunction<TagEntityFunction>("SandboxModuleTagEntity");
        const DetachFunction l_Detach = l_Module->GetFunction<DetachFunction>("SandboxModuleDetach");
        if (l_Attach == nullptr || l_Describe == nullptr || l_TagEntity == nullptr || l_Detach == nullptr)
        {
            TR_ERROR("Module test: {} is missing an entry point", l_Module->GetPath().string());

            return;
        }

        l_Attach();
        l_Describe(l_Description);

        const Trinity::ConsoleVariableBase* l_Variable = Trinity::ConsoleVariables::Find("sandbox.module_value");
        const bool l_Loaded = l_Variable != nullptr && l_Variable->ToString() == "42" && CountConsoleVariables() == l_VariablesBefore + 1;

        // FindEntityByName runs in the engine, so it reads the engine's Tag pool, not one the module made
        const Trinity::UUID l_UUID = Trinity::UUID::Generate();
        l_TagEntity(*l_Scene, l_UUID);
        const Trinity::Entity l_Tagged = l_Scene->FindEntityByName(std::format("Tagged by the Sandbox module as {}", l_UUID));

        l_Detach();
        l_Module->Unload();

        if (!l_Tagged || l_Tagged.GetUUID() != l_UUID || l_Scene->GetEntityCount() != 1)
        {
            ++l_SceneFailures;
        }

        if (l_Tagged)
        {
            l_Scene->DestroyEntity(l_Tagged);
        }

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

    l_Scene.reset();

    const Trinity::MemoryTagStats l_SceneAfter = Trinity::Memory::GetStats(Trinity::MemoryTag::Scene);
    if (l_SceneFailures != 0 || l_SceneAfter.CurrentBytes != l_SceneBefore.CurrentBytes || l_SceneAfter.LiveAllocations != l_SceneBefore.LiveAllocations)
    {
        TR_ERROR("Module scene test: the engine missed {} of {} Tag(s) added by the module, and Scene went from {} in {} live to {} in {} live", l_SceneFailures, c_ModuleLoadCount, Trinity::Memory::FormatBytes(l_SceneBefore.CurrentBytes), l_SceneBefore.LiveAllocations, Trinity::Memory::FormatBytes(l_SceneAfter.CurrentBytes), l_SceneAfter.LiveAllocations);

        return;
    }

    TR_INFO("Module scene test: the module tagged an entity of Sandbox's scene on each of {} loads, and the engine found all {} in its own Tag pool; Scene holds {} in {} live before and after", c_ModuleLoadCount, c_ModuleLoadCount, Trinity::Memory::FormatBytes(l_SceneAfter.CurrentBytes), l_SceneAfter.LiveAllocations);
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

    Trinity::RHI::TextureDescription l_TargetDescription;
    l_TargetDescription.Width = c_TargetSize;
    l_TargetDescription.Height = c_TargetSize;
    l_TargetDescription.TextureFormat = Trinity::RHI::Format::RGBA8Unorm;
    l_TargetDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget | Trinity::RHI::TextureUsage::CopySource;
    l_TargetDescription.ClearColor = c_TestClearColor;
    l_TargetDescription.DebugName = "Sandbox target";

    const std::uint64_t l_RowPitch = Trinity::RHI::GetTextureCopyRowPitch(l_TargetDescription.TextureFormat, c_TargetSize);

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

    const std::array<Trinity::RHI::ColorAttachment, 1> l_Attachments{ Trinity::RHI::ColorAttachment{ l_Target, Trinity::RHI::LoadOp::Clear, Trinity::RHI::StoreOp::Store, c_TestClearColor } };
    Trinity::RHI::RenderingDescription l_Rendering;
    l_Rendering.ColorAttachments = l_Attachments;
    l_Rendering.RenderArea = { 0, 0, c_TargetSize, c_TargetSize };
    l_Commands.BeginRendering(l_Rendering);
    l_Commands.EndRendering();

    l_Commands.TextureBarrier(l_Target, Trinity::RHI::ResourceState::RenderTarget, Trinity::RHI::ResourceState::CopySource);
    l_Commands.CopyTextureToBuffer(l_Target, 0, l_Readback, 0);
    l_Device.EndFrame();
    l_Device.WaitIdle();

    const std::span<const std::byte> l_Data = l_Device.GetMappedData(l_Readback);
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();

    // The null device copies nothing, so only a GPU's readback has pixels to check
    const bool l_CheckPixels = l_Info.API != Trinity::GraphicsAPI::None && l_Data.size() == l_ReadbackDescription.Size;
    std::uint32_t l_WrongPixels = 0;
    for (std::uint32_t it_Y = 0; l_CheckPixels && it_Y < c_TargetSize; ++it_Y)
    {
        for (std::uint32_t it_X = 0; it_X < c_TargetSize; ++it_X)
        {
            const std::byte* l_Pixel = l_Data.data() + static_cast<std::size_t>(it_Y * l_RowPitch + it_X * 4);

            bool l_Matches = true;
            for (std::size_t it_Channel = 0; it_Channel < c_TestClearColor.size(); ++it_Channel)
            {
                const long l_Expected = std::lround(c_TestClearColor[it_Channel] * 255.0f);
                l_Matches = l_Matches && std::abs(std::to_integer<long>(l_Pixel[it_Channel]) - l_Expected) <= 1;
            }

            l_WrongPixels += l_Matches ? 0 : 1;
        }
    }

    const std::size_t l_Mapped = l_Data.size();

    l_Device.DestroyBuffer(l_Readback);
    l_Device.DestroyTexture(l_Target);

    if (l_Mapped != l_ReadbackDescription.Size)
    {
        TR_ERROR("Trinity::RHI: the readback buffer maps {} of {}", Trinity::Memory::FormatBytes(l_Mapped), Trinity::Memory::FormatBytes(l_ReadbackDescription.Size));

        return;
    }

    if (l_WrongPixels != 0)
    {
        TR_ERROR("Trinity::RHI: {} of {} pixels read back from {} are not the clear colour", l_WrongPixels, c_TargetSize * c_TargetSize, Trinity::ToString(l_Info.API));

        return;
    }

    TR_INFO("Trinity::RHI: {} device on {} cleared a {}x{} target and copied it into a {} readback buffer{}", Trinity::ToString(l_Info.API), l_Info.AdapterName, c_TargetSize, c_TargetSize, Trinity::Memory::FormatBytes(l_Mapped), l_CheckPixels ? ", and every pixel holds the clear colour" : "");
}

// Each mip of every BC format the device supports is uploaded from random blocks and read back, so both copies' block pitch, row count and footprint are checked byte for byte, mips smaller than a block included
void SandboxLayer::TestCompressedTextures()
{
    TR_PROFILE_FUNCTION();

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();
    const Trinity::RHI::TextureUsage l_Usage = Trinity::RHI::TextureUsage::ShaderResource | Trinity::RHI::TextureUsage::CopyDestination | Trinity::RHI::TextureUsage::CopySource;

    // The null device copies nothing, so only a GPU's readback has blocks to compare
    const bool l_Compare = l_Info.API != Trinity::GraphicsAPI::None;

    std::mt19937 l_Random(c_CompressedSeed);
    std::vector<std::string_view> l_Tested;
    std::vector<std::string_view> l_Unsupported;
    std::uint64_t l_ComparedBytes = 0;
    std::uint64_t l_WrongBytes = 0;
    std::uint32_t l_Failures = 0;

    for (const Trinity::RHI::Format it_Format : c_CompressedFormats)
    {
        if (!l_Device.IsFormatSupported(it_Format, l_Usage))
        {
            l_Unsupported.push_back(Trinity::RHI::ToString(it_Format));

            continue;
        }

        // Each mip starts on a copy offset boundary, at the same offset in the staging and readback buffers
        std::array<std::uint64_t, c_CompressedMipLevels> l_Offsets{};
        std::uint64_t l_Size = 0;
        for (std::uint32_t it_Mip = 0; it_Mip < c_CompressedMipLevels; ++it_Mip)
        {
            l_Offsets[it_Mip] = (l_Size + Trinity::RHI::c_TextureCopyOffsetAlignment - 1) / Trinity::RHI::c_TextureCopyOffsetAlignment * Trinity::RHI::c_TextureCopyOffsetAlignment;
            l_Size = l_Offsets[it_Mip] + Trinity::RHI::GetTextureCopySize(it_Format, Trinity::RHI::GetMipSize(c_CompressedWidth, it_Mip), Trinity::RHI::GetMipSize(c_CompressedHeight, it_Mip));
        }

        Trinity::RHI::TextureDescription l_TextureDescription;
        l_TextureDescription.Width = c_CompressedWidth;
        l_TextureDescription.Height = c_CompressedHeight;
        l_TextureDescription.MipLevels = c_CompressedMipLevels;
        l_TextureDescription.TextureFormat = it_Format;
        l_TextureDescription.Usage = l_Usage;
        l_TextureDescription.DebugName = "Sandbox compressed texture";

        Trinity::RHI::BufferDescription l_StagingDescription;
        l_StagingDescription.Size = l_Size;
        l_StagingDescription.Memory = Trinity::RHI::MemoryType::Upload;
        l_StagingDescription.DebugName = "Sandbox compressed staging";

        Trinity::RHI::BufferDescription l_ReadbackDescription;
        l_ReadbackDescription.Size = l_Size;
        l_ReadbackDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
        l_ReadbackDescription.Memory = Trinity::RHI::MemoryType::Readback;
        l_ReadbackDescription.DebugName = "Sandbox compressed readback";

        const Trinity::RHI::TextureHandle l_Texture = l_Device.CreateTexture(l_TextureDescription);
        const Trinity::RHI::BufferHandle l_Staging = l_Device.CreateBuffer(l_StagingDescription);
        const Trinity::RHI::BufferHandle l_Readback = l_Device.CreateBuffer(l_ReadbackDescription);
        const std::span<std::byte> l_StagingData = l_Device.GetMappedData(l_Staging);
        if (!l_Texture || !l_Staging || !l_Readback || l_StagingData.size() != l_Size)
        {
            TR_ERROR("Compressed textures: could not create a {} texture or its staging and readback buffers", Trinity::RHI::ToString(it_Format));

            l_Device.DestroyTexture(l_Texture);
            l_Device.DestroyBuffer(l_Staging);
            l_Device.DestroyBuffer(l_Readback);
            ++l_Failures;

            continue;
        }

        std::ranges::generate(l_StagingData, [&l_Random]() { return static_cast<std::byte>(l_Random() & 0xFF); });

        Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();
        l_Commands.TextureBarrier(l_Texture, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::CopyDestination);
        for (std::uint32_t it_Mip = 0; it_Mip < c_CompressedMipLevels; ++it_Mip)
        {
            l_Commands.CopyBufferToTexture(l_Staging, l_Offsets[it_Mip], l_Texture, it_Mip, { 0, 0, Trinity::RHI::GetMipSize(c_CompressedWidth, it_Mip), Trinity::RHI::GetMipSize(c_CompressedHeight, it_Mip) });
        }

        l_Commands.TextureBarrier(l_Texture, Trinity::RHI::ResourceState::CopyDestination, Trinity::RHI::ResourceState::CopySource);
        for (std::uint32_t it_Mip = 0; it_Mip < c_CompressedMipLevels; ++it_Mip)
        {
            l_Commands.CopyTextureToBuffer(l_Texture, it_Mip, l_Readback, l_Offsets[it_Mip]);
        }

        l_Device.EndFrame();
        l_Device.WaitIdle();

        // Only the blocks of each row are compared; the padding up to the row pitch is not copied
        const std::span<const std::byte> l_ReadbackData = l_Device.GetMappedData(l_Readback);
        const std::uint32_t l_Block = Trinity::RHI::GetFormatBlockDimension(it_Format);
        for (std::uint32_t it_Mip = 0; l_Compare && l_ReadbackData.size() == l_Size && it_Mip < c_CompressedMipLevels; ++it_Mip)
        {
            const std::uint32_t l_Width = Trinity::RHI::GetMipSize(c_CompressedWidth, it_Mip);
            const std::uint32_t l_Height = Trinity::RHI::GetMipSize(c_CompressedHeight, it_Mip);
            const std::uint64_t l_RowPitch = Trinity::RHI::GetTextureCopyRowPitch(it_Format, l_Width);
            const std::uint64_t l_RowBytes = std::uint64_t{ (l_Width + l_Block - 1) / l_Block } * Trinity::RHI::GetFormatSize(it_Format);
            for (std::uint32_t it_Row = 0; it_Row < Trinity::RHI::GetTextureCopyRowCount(it_Format, l_Height); ++it_Row)
            {
                const std::size_t l_Start = static_cast<std::size_t>(l_Offsets[it_Mip] + it_Row * l_RowPitch);
                const std::span<const std::byte> l_Read = l_ReadbackData.subspan(l_Start, static_cast<std::size_t>(l_RowBytes));
                const std::span<const std::byte> l_Written = l_StagingData.subspan(l_Start, static_cast<std::size_t>(l_RowBytes));
                for (std::size_t it_Byte = 0; it_Byte < l_Read.size(); ++it_Byte)
                {
                    l_WrongBytes += l_Read[it_Byte] == l_Written[it_Byte] ? 0 : 1;
                }

                l_ComparedBytes += l_RowBytes;
            }
        }

        if (l_Compare && l_ReadbackData.size() != l_Size)
        {
            TR_ERROR("Compressed textures: the {} readback buffer maps {} of {}", Trinity::RHI::ToString(it_Format), Trinity::Memory::FormatBytes(l_ReadbackData.size()), Trinity::Memory::FormatBytes(l_Size));
            ++l_Failures;
        }

        l_Device.DestroyTexture(l_Texture);
        l_Device.DestroyBuffer(l_Staging);
        l_Device.DestroyBuffer(l_Readback);

        l_Tested.push_back(Trinity::RHI::ToString(it_Format));
    }

    if (l_WrongBytes != 0)
    {
        TR_ERROR("Compressed textures: {} of {} bytes read back on {} differ from the upload", l_WrongBytes, l_ComparedBytes, Trinity::ToString(l_Info.API));
    }
    else if (l_Failures == 0)
    {
        TR_INFO("Compressed textures: {} uploaded and read back {} mips of a {}x{} texture in {} format(s){}", Trinity::ToString(l_Info.API), c_CompressedMipLevels, c_CompressedWidth, c_CompressedHeight, l_Tested.size(), l_Compare ? std::format(", and all {} bytes match", l_ComparedBytes) : "");
    }

    if (!l_Unsupported.empty())
    {
        std::string l_Names;
        for (const std::string_view it_Name : l_Unsupported)
        {
            l_Names += l_Names.empty() ? std::string(it_Name) : std::format(", {}", it_Name);
        }

        TR_INFO("Compressed textures: {} does not support {} for sampling and copies, so the test skipped them", Trinity::ToString(l_Info.API), l_Names);
    }

    // No GPU renders into a block-compressed texture, so a device that claims to has a broken query
    constexpr Trinity::RHI::Format c_TargetFormat = Trinity::RHI::Format::BC7Unorm;
    if (l_Device.IsFormatSupported(c_TargetFormat, Trinity::RHI::TextureUsage::RenderTarget))
    {
        TR_ERROR("Compressed textures: {} reports {} as usable for a render target", Trinity::ToString(l_Info.API), Trinity::RHI::ToString(c_TargetFormat));
    }
    else
    {
        TR_INFO("Compressed textures: {} reports {} as unusable for a render target", Trinity::ToString(l_Info.API), Trinity::RHI::ToString(c_TargetFormat));
    }

    if (!Trinity::Application::Get().GetSpecification().CommandLineArgs.HasOption("unsupported-format-test"))
    {
        return;
    }

    // Deliberately logs the refusal as an error, which is why it runs only on request
    Trinity::RHI::TextureDescription l_TargetDescription;
    l_TargetDescription.Width = c_CompressedWidth;
    l_TargetDescription.Height = c_CompressedHeight;
    l_TargetDescription.TextureFormat = c_TargetFormat;
    l_TargetDescription.Usage = Trinity::RHI::TextureUsage::RenderTarget;
    l_TargetDescription.DebugName = "Sandbox compressed render target";

    const Trinity::RHI::TextureHandle l_Target = l_Device.CreateTexture(l_TargetDescription);
    if (l_Target)
    {
        TR_ERROR("Unsupported format test: {} created a {} render target instead of refusing it", Trinity::ToString(l_Info.API), Trinity::RHI::ToString(c_TargetFormat));
        l_Device.DestroyTexture(l_Target);

        return;
    }

    TR_INFO("Unsupported format test: {} refused a {} render target with an invalid handle", Trinity::ToString(l_Info.API), Trinity::RHI::ToString(c_TargetFormat));
}

// Every round creates and destroys the same buffers and textures across frames, so a second round that ends above the first means a leak
void SandboxLayer::TestResources()
{
    TR_PROFILE_FUNCTION();

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();

    std::uint32_t l_Buffers = 0;
    std::uint32_t l_Textures = 0;
    std::uint32_t l_Samplers = 0;
    std::uint32_t l_Failures = 0;
    std::uint32_t l_BadMappings = 0;
    std::array<std::uint64_t, c_ResourceRounds> l_RendererBytes{};

    for (std::uint32_t it_Round = 0; it_Round < c_ResourceRounds; ++it_Round)
    {
        for (std::uint32_t it_Frame = 0; it_Frame < c_ResourceFramesPerRound; ++it_Frame)
        {
            [[maybe_unused]] Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();

            std::array<Trinity::RHI::BufferHandle, c_BuffersPerFrame> l_FrameBuffers{};
            for (std::uint32_t it_Index = 0; it_Index < c_BuffersPerFrame; ++it_Index)
            {
                Trinity::RHI::BufferDescription l_Description;
                l_Description.Size = std::uint64_t{ 256 } << (it_Index % 8);
                l_Description.Memory = static_cast<Trinity::RHI::MemoryType>(it_Index % 3);
                l_Description.Usage = l_Description.Memory == Trinity::RHI::MemoryType::GPU ? Trinity::RHI::BufferUsage::ShaderResource | Trinity::RHI::BufferUsage::CopyDestination : Trinity::RHI::BufferUsage::None;
                l_Description.DebugName = "Sandbox churn buffer";

                l_FrameBuffers[it_Index] = l_Device.CreateBuffer(l_Description);
                if (!l_FrameBuffers[it_Index])
                {
                    ++l_Failures;

                    continue;
                }

                ++l_Buffers;

                const std::span<std::byte> l_Mapped = l_Device.GetMappedData(l_FrameBuffers[it_Index]);
                const std::uint64_t l_Expected = l_Description.Memory == Trinity::RHI::MemoryType::GPU ? 0 : l_Description.Size;
                if (l_Mapped.size() != l_Expected)
                {
                    ++l_BadMappings;
                }

                std::ranges::fill(l_Mapped, static_cast<std::byte>(it_Index));
            }

            Trinity::RHI::TextureDescription l_Color;
            l_Color.Width = 256;
            l_Color.Height = 256;
            l_Color.Usage = Trinity::RHI::TextureUsage::RenderTarget | Trinity::RHI::TextureUsage::CopySource;
            l_Color.DebugName = "Sandbox churn color";

            Trinity::RHI::TextureDescription l_Depth = l_Color;
            l_Depth.TextureFormat = Trinity::RHI::Format::D32Float;
            l_Depth.Usage = Trinity::RHI::TextureUsage::DepthStencil;
            l_Depth.DebugName = "Sandbox churn depth";

            Trinity::RHI::TextureDescription l_Mipmapped = l_Color;
            l_Mipmapped.MipLevels = 9;
            l_Mipmapped.Usage = Trinity::RHI::TextureUsage::ShaderResource | Trinity::RHI::TextureUsage::CopyDestination;
            l_Mipmapped.DebugName = "Sandbox churn mipmapped";

            const std::array<Trinity::RHI::TextureHandle, 3> l_FrameTextures{ l_Device.CreateTexture(l_Color), l_Device.CreateTexture(l_Depth), l_Device.CreateTexture(l_Mipmapped) };
            for (Trinity::RHI::TextureHandle it_Texture : l_FrameTextures)
            {
                l_Textures += it_Texture ? 1 : 0;
                l_Failures += it_Texture ? 0 : 1;
                l_Device.DestroyTexture(it_Texture);
            }

            for (Trinity::RHI::BufferHandle it_Buffer : l_FrameBuffers)
            {
                l_Device.DestroyBuffer(it_Buffer);
            }

            Trinity::RHI::SamplerDescription l_SamplerDescription;
            l_SamplerDescription.MinFilter = static_cast<Trinity::RHI::Filter>(it_Frame % 2);
            l_SamplerDescription.AddressU = static_cast<Trinity::RHI::AddressMode>(it_Frame % 3);
            l_SamplerDescription.DebugName = "Sandbox churn sampler";

            const Trinity::RHI::SamplerHandle l_Sampler = l_Device.CreateSampler(l_SamplerDescription);
            l_Samplers += l_Sampler ? 1 : 0;
            l_Failures += l_Sampler ? 0 : 1;
            l_Device.DestroySampler(l_Sampler);

            l_Device.EndFrame();
        }

        l_Device.WaitIdle();
        l_RendererBytes[it_Round] = Trinity::Memory::GetStats(Trinity::MemoryTag::Renderer).CurrentBytes;
    }

    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();
    if (l_Failures != 0 || l_BadMappings != 0)
    {
        TR_ERROR("Trinity::RHI: {} resource(s) could not be created and {} buffer(s) mapped the wrong size on {}", l_Failures, l_BadMappings, Trinity::ToString(l_Info.API));
    }

    if (l_RendererBytes.back() != l_RendererBytes.front())
    {
        TR_ERROR("Trinity::RHI: Renderer went from {} after the first round to {} after the last, so resources leak on {}", Trinity::Memory::FormatBytes(l_RendererBytes.front()), Trinity::Memory::FormatBytes(l_RendererBytes.back()), Trinity::ToString(l_Info.API));

        return;
    }

    TR_INFO("Trinity::RHI: {} created and destroyed {} buffers and {} textures over {} frames ({} samplers alongside), and Renderer held {} after every round", Trinity::ToString(l_Info.API), l_Buffers, l_Textures, c_ResourceRounds * c_ResourceFramesPerRound, l_Samplers, Trinity::Memory::FormatBytes(l_RendererBytes.back()));
}

// Asserts on purpose, so it only runs with --stale-handle
void SandboxLayer::TestStaleHandle()
{
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();

    Trinity::RHI::BufferDescription l_Description;
    l_Description.Size = 256;
    l_Description.Memory = Trinity::RHI::MemoryType::Upload;
    l_Description.DebugName = "Sandbox stale buffer";

    const Trinity::RHI::BufferHandle l_Buffer = l_Device.CreateBuffer(l_Description);
    l_Device.DestroyBuffer(l_Buffer);

    TR_WARN("Trinity::RHI: mapping a destroyed buffer on purpose (--stale-handle), so an assertion should fail next");

    const std::size_t l_Size = l_Device.GetMappedData(l_Buffer).size();

    TR_ERROR("Trinity::RHI: the stale handle did not assert, since asserts are off in this configuration, and mapped {}", Trinity::Memory::FormatBytes(l_Size));
}

// Builds the pipeline from the shader blobs and uploads the vertices into a GPU buffer that the vertex shader reads through its bindless index
void SandboxLayer::CreateTriangle()
{
    TR_PROFILE_FUNCTION();

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const std::string_view l_Extension = l_Device.GetInfo().API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";

    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_VertexShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/Triangle.VertexMain.{}", l_Extension));
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_PixelShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/Triangle.PixelMain.{}", l_Extension));
    if (!l_VertexShader || !l_PixelShader)
    {
        TR_INFO("Triangle: no {} shaders under /engine/shaders, so nothing is drawn", l_Extension);

        return;
    }

    const std::array<Trinity::RHI::Format, 1> l_ColorFormats{ Trinity::Application::Get().GetRenderer().GetSceneFormat() };

    Trinity::RHI::GraphicsPipelineDescription l_PipelineDescription;
    l_PipelineDescription.VertexShader = { *l_VertexShader, "VertexMain" };
    l_PipelineDescription.PixelShader = { *l_PixelShader, "PixelMain" };
    l_PipelineDescription.ColorFormats = l_ColorFormats;
    l_PipelineDescription.DebugName = "Sandbox triangle";

    Trinity::RHI::BufferDescription l_VertexDescription;
    l_VertexDescription.Size = sizeof(c_TriangleVertices);
    l_VertexDescription.Usage = Trinity::RHI::BufferUsage::ShaderResource | Trinity::RHI::BufferUsage::CopyDestination;
    l_VertexDescription.DebugName = "Sandbox triangle vertices";

    Trinity::RHI::BufferDescription l_StagingDescription;
    l_StagingDescription.Size = sizeof(c_TriangleVertices);
    l_StagingDescription.Memory = Trinity::RHI::MemoryType::Upload;
    l_StagingDescription.DebugName = "Sandbox triangle staging";

    m_TrianglePipeline = l_Device.CreateGraphicsPipeline(l_PipelineDescription);
    m_TriangleVertices = l_Device.CreateBuffer(l_VertexDescription);
    const Trinity::RHI::BufferHandle l_Staging = l_Device.CreateBuffer(l_StagingDescription);
    m_TriangleVertexIndex = m_TriangleVertices ? l_Device.GetShaderResourceIndex(m_TriangleVertices) : Trinity::RHI::c_NoBindlessIndex;
    if (!m_TrianglePipeline || !l_Staging || m_TriangleVertexIndex == Trinity::RHI::c_NoBindlessIndex)
    {
        TR_ERROR("Triangle: could not create the pipeline, the vertex buffer or its bindless index");

        l_Device.DestroyBuffer(l_Staging);
        DestroyTriangle();

        return;
    }

    const std::span<std::byte> l_Mapped = l_Device.GetMappedData(l_Staging);
    std::memcpy(l_Mapped.data(), c_TriangleVertices.data(), sizeof(c_TriangleVertices));

    Trinity::RHI::CommandList& l_Commands = l_Device.BeginFrame();
    l_Commands.BufferBarrier(m_TriangleVertices, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::CopyDestination);
    l_Commands.CopyBuffer(l_Staging, 0, m_TriangleVertices, 0, sizeof(c_TriangleVertices));
    l_Commands.BufferBarrier(m_TriangleVertices, Trinity::RHI::ResourceState::CopyDestination, Trinity::RHI::ResourceState::ShaderResource);
    l_Device.EndFrame();

    l_Device.DestroyBuffer(l_Staging);

    TR_INFO("Triangle: {} pipeline built from {} and {} of {}, vertices at bindless index {}", Trinity::ToString(l_Device.GetInfo().API), Trinity::Memory::FormatBytes(l_VertexShader->size()), Trinity::Memory::FormatBytes(l_PixelShader->size()), l_Extension, m_TriangleVertexIndex);
}

void SandboxLayer::DestroyTriangle()
{
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();

    l_Device.DestroyPipeline(m_TrianglePipeline);
    l_Device.DestroyBuffer(m_TriangleVertices);
    m_TrianglePipeline = {};
    m_TriangleVertices = {};
    m_TriangleVertexIndex = Trinity::RHI::c_NoBindlessIndex;
}

// Uploads the checkerboard whole, patches part of it in a second frame the way the font atlas will be updated, reads it back to compare, then samples it on a quad every frame
void SandboxLayer::CreateCheckerboard()
{
    TR_PROFILE_FUNCTION();

    constexpr Trinity::RHI::Format c_Format = Trinity::RHI::Format::RGBA8Unorm;
    constexpr std::uint32_t c_TexelSize = 4;

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();

    const std::uint64_t l_ImagePitch = Trinity::RHI::GetTextureCopyRowPitch(c_Format, c_CheckerboardSize);
    const std::uint64_t l_PatchPitch = Trinity::RHI::GetTextureCopyRowPitch(c_Format, c_CheckerboardPatch.Width);
    const std::uint64_t l_PatchOffset = (l_ImagePitch * c_CheckerboardSize + Trinity::RHI::c_TextureCopyOffsetAlignment - 1) / Trinity::RHI::c_TextureCopyOffsetAlignment * Trinity::RHI::c_TextureCopyOffsetAlignment;

    Trinity::RHI::TextureDescription l_TextureDescription;
    l_TextureDescription.Width = c_CheckerboardSize;
    l_TextureDescription.Height = c_CheckerboardSize;
    l_TextureDescription.TextureFormat = c_Format;
    l_TextureDescription.Usage = Trinity::RHI::TextureUsage::ShaderResource | Trinity::RHI::TextureUsage::CopyDestination | Trinity::RHI::TextureUsage::CopySource;
    l_TextureDescription.DebugName = "Sandbox checkerboard";

    Trinity::RHI::SamplerDescription l_SamplerDescription;
    l_SamplerDescription.MinFilter = Trinity::RHI::Filter::Nearest;
    l_SamplerDescription.MagFilter = Trinity::RHI::Filter::Nearest;
    l_SamplerDescription.MipFilter = Trinity::RHI::Filter::Nearest;
    l_SamplerDescription.DebugName = "Sandbox nearest sampler";

    Trinity::RHI::BufferDescription l_StagingDescription;
    l_StagingDescription.Size = l_PatchOffset + l_PatchPitch * c_CheckerboardPatch.Height;
    l_StagingDescription.Memory = Trinity::RHI::MemoryType::Upload;
    l_StagingDescription.DebugName = "Sandbox checkerboard staging";

    Trinity::RHI::BufferDescription l_ReadbackDescription;
    l_ReadbackDescription.Size = l_ImagePitch * c_CheckerboardSize;
    l_ReadbackDescription.Usage = Trinity::RHI::BufferUsage::CopyDestination;
    l_ReadbackDescription.Memory = Trinity::RHI::MemoryType::Readback;
    l_ReadbackDescription.DebugName = "Sandbox checkerboard readback";

    m_Checkerboard = l_Device.CreateTexture(l_TextureDescription);
    m_CheckerboardSampler = l_Device.CreateSampler(l_SamplerDescription);
    const Trinity::RHI::BufferHandle l_Staging = l_Device.CreateBuffer(l_StagingDescription);
    const Trinity::RHI::BufferHandle l_Readback = l_Device.CreateBuffer(l_ReadbackDescription);
    m_CheckerboardIndex = m_Checkerboard ? l_Device.GetShaderResourceIndex(m_Checkerboard) : Trinity::RHI::c_NoBindlessIndex;
    m_CheckerboardSamplerIndex = m_CheckerboardSampler ? l_Device.GetSamplerIndex(m_CheckerboardSampler) : Trinity::RHI::c_NoBindlessIndex;
    if (!l_Staging || !l_Readback || m_CheckerboardIndex == Trinity::RHI::c_NoBindlessIndex || m_CheckerboardSamplerIndex == Trinity::RHI::c_NoBindlessIndex)
    {
        TR_ERROR("Checkerboard: could not create the texture, the sampler, their bindless indices or the staging and readback buffers");

        l_Device.DestroyBuffer(l_Staging);
        l_Device.DestroyBuffer(l_Readback);
        DestroyCheckerboard();

        return;
    }

    // The expected texture, tightly packed, and the staging buffer with the whole image followed by the patch
    std::vector<std::uint8_t> l_Expected(std::size_t{ c_CheckerboardSize } * c_CheckerboardSize * c_TexelSize);
    const std::span<std::byte> l_StagingData = l_Device.GetMappedData(l_Staging);
    for (std::uint32_t it_Y = 0; it_Y < c_CheckerboardSize; ++it_Y)
    {
        for (std::uint32_t it_X = 0; it_X < c_CheckerboardSize; ++it_X)
        {
            const std::array<std::uint8_t, 4> l_Texel = GetCheckerboardTexel(it_X, it_Y);
            std::memcpy(l_StagingData.data() + it_Y * l_ImagePitch + it_X * c_TexelSize, l_Texel.data(), c_TexelSize);
            std::memcpy(l_Expected.data() + (std::size_t{ it_Y } * c_CheckerboardSize + it_X) * c_TexelSize, l_Texel.data(), c_TexelSize);
        }
    }

    for (std::uint32_t it_Y = 0; it_Y < c_CheckerboardPatch.Height; ++it_Y)
    {
        for (std::uint32_t it_X = 0; it_X < c_CheckerboardPatch.Width; ++it_X)
        {
            const std::array<std::uint8_t, 4> l_Texel = GetPatchTexel(it_X, it_Y);
            const std::size_t l_ExpectedTexel = (std::size_t{ it_Y } + static_cast<std::size_t>(c_CheckerboardPatch.Y)) * c_CheckerboardSize + it_X + static_cast<std::size_t>(c_CheckerboardPatch.X);
            std::memcpy(l_StagingData.data() + l_PatchOffset + it_Y * l_PatchPitch + it_X * c_TexelSize, l_Texel.data(), c_TexelSize);
            std::memcpy(l_Expected.data() + l_ExpectedTexel * c_TexelSize, l_Texel.data(), c_TexelSize);
        }
    }

    Trinity::RHI::CommandList& l_Upload = l_Device.BeginFrame();
    l_Upload.TextureBarrier(m_Checkerboard, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::CopyDestination);
    l_Upload.CopyBufferToTexture(l_Staging, 0, m_Checkerboard, 0, { 0, 0, c_CheckerboardSize, c_CheckerboardSize });
    l_Upload.TextureBarrier(m_Checkerboard, Trinity::RHI::ResourceState::CopyDestination, Trinity::RHI::ResourceState::ShaderResource);
    l_Device.EndFrame();

    Trinity::RHI::CommandList& l_Patch = l_Device.BeginFrame();
    l_Patch.TextureBarrier(m_Checkerboard, Trinity::RHI::ResourceState::ShaderResource, Trinity::RHI::ResourceState::CopyDestination);
    l_Patch.CopyBufferToTexture(l_Staging, l_PatchOffset, m_Checkerboard, 0, c_CheckerboardPatch);
    l_Patch.TextureBarrier(m_Checkerboard, Trinity::RHI::ResourceState::CopyDestination, Trinity::RHI::ResourceState::CopySource);
    l_Patch.CopyTextureToBuffer(m_Checkerboard, 0, l_Readback, 0);
    l_Patch.TextureBarrier(m_Checkerboard, Trinity::RHI::ResourceState::CopySource, Trinity::RHI::ResourceState::ShaderResource);
    l_Device.EndFrame();
    l_Device.WaitIdle();

    // The null device copies nothing, so only a GPU's readback has texels to compare
    const Trinity::RHI::DeviceInfo& l_Info = l_Device.GetInfo();
    const std::span<const std::byte> l_ReadbackData = l_Device.GetMappedData(l_Readback);
    const bool l_Compare = l_Info.API != Trinity::GraphicsAPI::None && l_ReadbackData.size() == l_ReadbackDescription.Size;
    std::uint32_t l_WrongBytes = 0;
    for (std::uint32_t it_Y = 0; l_Compare && it_Y < c_CheckerboardSize; ++it_Y)
    {
        for (std::uint32_t it_Byte = 0; it_Byte < c_CheckerboardSize * c_TexelSize; ++it_Byte)
        {
            const std::uint8_t l_Read = std::to_integer<std::uint8_t>(l_ReadbackData[it_Y * l_ImagePitch + it_Byte]);
            l_WrongBytes += l_Read == l_Expected[std::size_t{ it_Y } * c_CheckerboardSize * c_TexelSize + it_Byte] ? 0 : 1;
        }
    }

    l_Device.DestroyBuffer(l_Staging);
    l_Device.DestroyBuffer(l_Readback);

    if (l_WrongBytes != 0)
    {
        TR_ERROR("Checkerboard: {} of {} bytes read back on {} differ from the upload", l_WrongBytes, l_Expected.size(), Trinity::ToString(l_Info.API));
    }
    else
    {
        TR_INFO("Checkerboard: {} uploaded a {}x{} texture and a {}x{} patch at ({}, {}){}", Trinity::ToString(l_Info.API), c_CheckerboardSize, c_CheckerboardSize, c_CheckerboardPatch.Width, c_CheckerboardPatch.Height, c_CheckerboardPatch.X, c_CheckerboardPatch.Y, l_Compare ? std::format(", and all {} bytes read back match the upload", l_Expected.size()) : "");
    }

    const std::string_view l_Extension = l_Info.API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_VertexShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/TexturedQuad.VertexMain.{}", l_Extension));
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_PixelShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/TexturedQuad.PixelMain.{}", l_Extension));
    if (!l_VertexShader || !l_PixelShader)
    {
        TR_INFO("Checkerboard: no {} shaders under /engine/shaders, so the quad is not drawn", l_Extension);

        return;
    }

    const std::array<Trinity::RHI::Format, 1> l_ColorFormats{ Trinity::Application::Get().GetRenderer().GetSceneFormat() };

    Trinity::RHI::GraphicsPipelineDescription l_PipelineDescription;
    l_PipelineDescription.VertexShader = { *l_VertexShader, "VertexMain" };
    l_PipelineDescription.PixelShader = { *l_PixelShader, "PixelMain" };
    l_PipelineDescription.ColorFormats = l_ColorFormats;
    l_PipelineDescription.Topology = Trinity::RHI::PrimitiveTopology::TriangleStrip;
    l_PipelineDescription.Cull = Trinity::RHI::CullMode::None;
    l_PipelineDescription.DebugName = "Sandbox textured quad";

    m_QuadPipeline = l_Device.CreateGraphicsPipeline(l_PipelineDescription);
    if (!m_QuadPipeline)
    {
        TR_ERROR("Checkerboard: could not create the textured quad pipeline");

        return;
    }

    TR_INFO("Checkerboard: sampled on a quad at bindless texture index {} and sampler index {}", m_CheckerboardIndex, m_CheckerboardSamplerIndex);
}

void SandboxLayer::DestroyCheckerboard()
{
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();

    l_Device.DestroyPipeline(m_QuadPipeline);
    l_Device.DestroyTexture(m_Checkerboard);
    l_Device.DestroySampler(m_CheckerboardSampler);
    m_QuadPipeline = {};
    m_Checkerboard = {};
    m_CheckerboardSampler = {};
    m_CheckerboardIndex = Trinity::RHI::c_NoBindlessIndex;
    m_CheckerboardSamplerIndex = Trinity::RHI::c_NoBindlessIndex;
}

void SandboxLayer::CreateQuadField()
{
    TR_PROFILE_FUNCTION();

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const std::string_view l_Extension = l_Device.GetInfo().API == Trinity::GraphicsAPI::D3D12 ? "dxil" : "spv";

    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_VertexShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/QuadField.VertexMain.{}", l_Extension));
    const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_PixelShader = Trinity::FileSystem::ReadFile(std::format("/engine/shaders/QuadField.PixelMain.{}", l_Extension));
    if (!l_VertexShader || !l_PixelShader)
    {
        TR_INFO("Quad field: no {} shaders under /engine/shaders, so nothing is drawn", l_Extension);

        return;
    }

    const std::array<Trinity::RHI::Format, 1> l_ColorFormats{ Trinity::Application::Get().GetRenderer().GetSceneFormat() };

    Trinity::RHI::GraphicsPipelineDescription l_PipelineDescription;
    l_PipelineDescription.VertexShader = { *l_VertexShader, "VertexMain" };
    l_PipelineDescription.PixelShader = { *l_PixelShader, "PixelMain" };
    l_PipelineDescription.ColorFormats = l_ColorFormats;
    l_PipelineDescription.Cull = Trinity::RHI::CullMode::None;
    l_PipelineDescription.DebugName = "Sandbox quad field";

    m_FieldPipeline = l_Device.CreateGraphicsPipeline(l_PipelineDescription);
    if (!m_FieldPipeline)
    {
        TR_ERROR("Quad field: could not create the pipeline");

        return;
    }

    TR_INFO("Quad field: {} pipeline built, drawing {} quads a frame from a {} upload ring slot", Trinity::ToString(l_Device.GetInfo().API), c_FieldQuads, Trinity::Memory::FormatBytes(l_Device.GetUploadCapacity()));
}

// Reports whether Renderer memory moved between frame c_FieldCheckFrame and the last frame, unless U overflowed the ring on purpose
void SandboxLayer::DestroyQuadField()
{
    Trinity::Application::Get().GetDevice().DestroyPipeline(m_FieldPipeline);
    m_FieldPipeline = {};

    if (m_FieldFailedFrames != 0)
    {
        TR_ERROR("Quad field: {} frame(s) got no upload memory for their quads", m_FieldFailedFrames);
    }

    if (m_FieldFrames == 0)
    {
        return;
    }

    if (m_FieldFrames >= c_FieldCheckFrame && m_UploadOverflowRequests == 0 && m_RendererBytesLast != m_RendererBytesAtCheck)
    {
        TR_ERROR("Quad field: Renderer went from {} at frame {} to {} at frame {}, so per-frame uploads leak", Trinity::Memory::FormatBytes(m_RendererBytesAtCheck), c_FieldCheckFrame, Trinity::Memory::FormatBytes(m_RendererBytesLast), m_FieldFrames);

        return;
    }

    TR_INFO("Quad field: drew {} quads in each of {} frame(s) from the upload ring{}", c_FieldQuads, m_FieldFrames, m_FieldFrames >= c_FieldCheckFrame ? std::format(", and Renderer held {} at frame {} and {} at the last", Trinity::Memory::FormatBytes(m_RendererBytesAtCheck), c_FieldCheckFrame, Trinity::Memory::FormatBytes(m_RendererBytesLast)) : "");
}

// Every quad goes into the upload ring each frame: the first half with 16-bit indices and the second with 32-bit, each half in two DrawIndexed calls
void SandboxLayer::DrawQuadField(Trinity::RHI::CommandList& commands)
{
    TR_PROFILE_FUNCTION();

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    if (m_OverflowUploadNextFrame)
    {
        [[maybe_unused]] const Trinity::RHI::UploadAllocation l_Overflow = l_Device.AllocateUpload(l_Device.GetUploadCapacity() + 1, 16);
        m_OverflowUploadNextFrame = false;
    }

    std::array<float, c_FieldColumns> l_Waves{};
    std::array<std::uint32_t, c_FieldColumns> l_Colors{};
    for (std::uint32_t it_Column = 0; it_Column < c_FieldColumns; ++it_Column)
    {
        const std::array<float, 4> l_Hue = HueToColor(static_cast<float>(it_Column) / static_cast<float>(c_FieldColumns));
        l_Waves[it_Column] = 0.06f * std::sin(m_FieldSeconds * 2.0f + static_cast<float>(it_Column) * 0.06f);
        l_Colors[it_Column] = PackColor({ 1.0f - l_Hue[0], 1.0f - l_Hue[1], 1.0f - l_Hue[2], 1.0f });
    }

    commands.SetPipeline(m_FieldPipeline);

    bool l_Drawn = true;
    for (std::uint32_t it_Batch = 0; it_Batch < 2; ++it_Batch)
    {
        const Trinity::RHI::IndexFormat l_Format = it_Batch == 0 ? Trinity::RHI::IndexFormat::UInt16 : Trinity::RHI::IndexFormat::UInt32;
        const std::uint32_t l_IndexSize = Trinity::RHI::GetIndexSize(l_Format);
        const std::uint32_t l_IndexCount = c_FieldQuadsPerBatch * static_cast<std::uint32_t>(c_FieldQuadIndices.size());

        const Trinity::RHI::UploadAllocation l_Vertices = l_Device.AllocateUpload(std::uint64_t{ c_FieldQuadsPerBatch } * 4 * sizeof(FieldVertex), 4);
        const Trinity::RHI::UploadAllocation l_Indices = l_Device.AllocateUpload(std::uint64_t{ l_IndexCount } * l_IndexSize, l_IndexSize);
        if (l_Vertices.Data.empty() || l_Indices.Data.empty())
        {
            l_Drawn = false;

            continue;
        }

        for (std::uint32_t it_Quad = 0; it_Quad < c_FieldQuadsPerBatch; ++it_Quad)
        {
            const std::uint32_t l_Column = (it_Batch * c_FieldQuadsPerBatch + it_Quad) % c_FieldColumns;
            const std::uint32_t l_Row = (it_Batch * c_FieldQuadsPerBatch + it_Quad) / c_FieldColumns;
            const float l_Left = -0.95f + static_cast<float>(l_Column) * 0.0095f;
            const float l_Bottom = -0.92f + static_cast<float>(l_Row) * 0.008f + l_Waves[l_Column];

            const std::array<FieldVertex, 4> l_Corners
            { {
                { { l_Left, l_Bottom }, l_Colors[l_Column] },
                { { l_Left + 0.0065f, l_Bottom }, l_Colors[l_Column] },
                { { l_Left, l_Bottom + 0.005f }, l_Colors[l_Column] },
                { { l_Left + 0.0065f, l_Bottom + 0.005f }, l_Colors[l_Column] }
            } };
            std::memcpy(l_Vertices.Data.data() + std::size_t{ it_Quad } * sizeof(l_Corners), l_Corners.data(), sizeof(l_Corners));

            for (std::size_t it_Index = 0; it_Index < c_FieldQuadIndices.size(); ++it_Index)
            {
                const std::uint32_t l_Index = it_Quad * 4 + c_FieldQuadIndices[it_Index];
                const std::uint16_t l_ShortIndex = static_cast<std::uint16_t>(l_Index);
                std::byte* l_Destination = l_Indices.Data.data() + (std::size_t{ it_Quad } * c_FieldQuadIndices.size() + it_Index) * l_IndexSize;
                std::memcpy(l_Destination, l_Format == Trinity::RHI::IndexFormat::UInt16 ? static_cast<const void*>(&l_ShortIndex) : static_cast<const void*>(&l_Index), l_IndexSize);
            }
        }

        // The vertex shader reads from wherever the ring put this half's vertices
        const std::array<std::uint32_t, 3> l_PushData{ l_Vertices.ShaderResourceIndex, 0, static_cast<std::uint32_t>(l_Vertices.Offset) };
        commands.PushConstants(std::as_bytes(std::span(l_PushData)));
        commands.SetIndexBuffer(l_Indices.Buffer, l_Indices.Offset, l_Format);
        commands.DrawIndexed(l_IndexCount / 2, 1, 0, 0);
        commands.DrawIndexed(l_IndexCount / 2, 1, l_IndexCount / 2, 0);
    }

    // Read at the same point of every frame, so the two numbers DestroyQuadField compares are alike
    m_RendererBytesLast = Trinity::Memory::GetStats(Trinity::MemoryTag::Renderer).CurrentBytes;
    m_FieldFrames += l_Drawn ? 1 : 0;
    m_FieldFailedFrames += l_Drawn ? 0 : 1;
    if (m_FieldFrames == c_FieldCheckFrame && l_Drawn)
    {
        m_RendererBytesAtCheck = m_RendererBytesLast;
    }
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


// An undecorated window without a taskbar entry, owned by the main window, as ImGui's viewports will be. It stays headless when the main window is
void SandboxLayer::OpenSecondWindow()
{
    Trinity::Window& l_MainWindow = Trinity::Application::Get().GetWindow();
    const Trinity::WindowPosition l_MainPosition = l_MainWindow.GetPosition();

    Trinity::WindowSpecification l_Specification;
    l_Specification.Title = "Sandbox second window";
    l_Specification.Width = c_SecondWindowWidth;
    l_Specification.Height = c_SecondWindowHeight;
    l_Specification.Position = Trinity::WindowPosition{ l_MainPosition.X + 60, l_MainPosition.Y + 60 };
    l_Specification.Owner = &l_MainWindow;
    l_Specification.Decorated = false;
    l_Specification.TaskbarIcon = false;
    l_Specification.Headless = l_MainWindow.GetNativeHandle() == nullptr;

    m_SecondWindow = Trinity::Window::Create(l_Specification);
    m_SecondWindow->SetEventCallback(TR_BIND_EVENT_FN(OnSecondWindowEvent));

    m_SecondOutput = Trinity::Application::Get().GetRenderer().AddOutput(*m_SecondWindow, c_SecondWindowClearColor, [this](Trinity::RHI::CommandList&, std::uint32_t width, std::uint32_t height)
    {
        ++m_SecondWindowDraws;
        m_SecondDrawWidth = width;
        m_SecondDrawHeight = height;
    });

    if (m_SecondOutput == 0)
    {
        TR_ERROR("Second window: the renderer cannot draw to it");
        ++m_SecondWindowFailures;
    }

    const Trinity::WindowPosition l_Position = m_SecondWindow->GetPosition();
    TR_INFO("Second window: opened {}x{} at ({}, {}), {}", m_SecondWindow->GetWidth(), m_SecondWindow->GetHeight(), l_Position.X, l_Position.Y, l_Specification.Headless ? "headless" : "owned by the main window, without decorations or a taskbar entry");
}

// One step every c_SecondWindowStepFrames: move, resize, focus, fade, check the drawing, close
void SandboxLayer::UpdateSecondWindow()
{
    if (m_SecondWindowPending && Trinity::Application::Get().GetRenderer().GetFrameCount() >= c_SecondWindowOpenFrame)
    {
        m_SecondWindowPending = false;
        OpenSecondWindow();
    }

    if (!m_SecondWindow)
    {
        return;
    }

    if (m_SecondWindowClosing)
    {
        TR_INFO("Second window: closed by the user");
        CloseSecondWindow();

        return;
    }

    ++m_SecondWindowFrames;
    if (m_SecondWindowFrames % c_SecondWindowStepFrames != 0)
    {
        return;
    }

    switch (m_SecondWindowFrames / c_SecondWindowStepFrames)
    {
        case 1:
        {
            const Trinity::WindowPosition l_Start = m_SecondWindow->GetPosition();
            m_SecondTargetPosition = { l_Start.X + 200, l_Start.Y + 100 };
            m_SecondWindow->SetPosition(m_SecondTargetPosition);

            const Trinity::WindowPosition l_Position = m_SecondWindow->GetPosition();
            const bool l_Moved = l_Position.X == m_SecondTargetPosition.X && l_Position.Y == m_SecondTargetPosition.Y && m_SecondMovedTo.X == l_Position.X && m_SecondMovedTo.Y == l_Position.Y;
            m_SecondWindowFailures += l_Moved ? 0 : 1;
            TR_INFO("Second window: moved to ({}, {}), asked for ({}, {}), and the last move event said ({}, {})", l_Position.X, l_Position.Y, m_SecondTargetPosition.X, m_SecondTargetPosition.Y, m_SecondMovedTo.X, m_SecondMovedTo.Y);

            break;
        }
        case 2:
        {
            m_SecondWindow->SetSize(c_SecondWindowResizedWidth, c_SecondWindowResizedHeight);

            const bool l_Resized = m_SecondWindow->GetWidth() == c_SecondWindowResizedWidth && m_SecondWindow->GetHeight() == c_SecondWindowResizedHeight && m_SecondResizedWidth == c_SecondWindowResizedWidth && m_SecondResizedHeight == c_SecondWindowResizedHeight;
            m_SecondWindowFailures += l_Resized ? 0 : 1;
            TR_INFO("Second window: resized to {}x{}, and the last resize event said {}x{}", m_SecondWindow->GetWidth(), m_SecondWindow->GetHeight(), m_SecondResizedWidth, m_SecondResizedHeight);

            break;
        }
        case 3:
        {
            // Windows may keep the foreground with another program the user is in, so this is reported but not counted
            m_SecondWindow->Focus();
            TR_INFO("Second window: asked for focus, and it {}", m_SecondWindow->IsFocused() ? "has it" : "does not have it");

            break;
        }
        case 4:
        {
            m_SecondWindow->SetOpacity(0.5f);
            TR_INFO("Second window: half transparent");

            break;
        }
        case 5:
        {
            m_SecondWindow->SetOpacity(1.0f);
            Trinity::Application::Get().GetWindow().Focus();
            TR_INFO("Second window: opaque again, and the main window asked for focus back");

            break;
        }
        default:
        {
            const bool l_Drawn = m_SecondWindowDraws > 0 && m_SecondDrawWidth == c_SecondWindowResizedWidth && m_SecondDrawHeight == c_SecondWindowResizedHeight;
            m_SecondWindowFailures += l_Drawn ? 0 : 1;
            TR_INFO("Second window: drawn {} time(s) in {} frame(s), last at {}x{}", m_SecondWindowDraws, m_SecondWindowFrames, m_SecondDrawWidth, m_SecondDrawHeight);

            if (m_SecondWindowFailures == 0)
            {
                TR_INFO("Second window: every check passed");
            }
            else
            {
                TR_ERROR("Second window: {} check(s) failed", m_SecondWindowFailures);
            }

            CloseSecondWindow();

            break;
        }
    }
}

void SandboxLayer::CloseSecondWindow()
{
    Trinity::Application::Get().GetRenderer().RemoveOutput(m_SecondOutput);
    m_SecondOutput = 0;
    m_SecondWindow.reset();
}

// The second window's events go here, not through the layer stack, so closing it never closes the Sandbox
void SandboxLayer::OnSecondWindowEvent(Trinity::Event& event)
{
    TR_TRACE("Second window: {}", event);

    Trinity::EventDispatcher l_Dispatcher(event);
    l_Dispatcher.Dispatch<Trinity::WindowMovedEvent>([this](Trinity::WindowMovedEvent& moved) { m_SecondMovedTo = { moved.GetX(), moved.GetY() }; return true; });
    l_Dispatcher.Dispatch<Trinity::WindowResizeEvent>([this](Trinity::WindowResizeEvent& resized) { m_SecondResizedWidth = resized.GetWidth(); m_SecondResizedHeight = resized.GetHeight(); return true; });
    l_Dispatcher.Dispatch<Trinity::WindowCloseEvent>([this](Trinity::WindowCloseEvent&) { m_SecondWindowClosing = true; return true; });
}


// Each worker's lines must reach the history in the order it wrote them. 100,000 more lines, empty, short and over the length limit, must leave the Log tag where it was, and every kept line whole and apart from the others
void SandboxLayer::TestLogHistory()
{
    Trinity::JobCounter l_Counter;
    for (std::uint32_t it_Worker = 0; it_Worker < c_LogTestWorkers; ++it_Worker)
    {
        Trinity::JobSystem::Submit([it_Worker]
        {
            for (std::int64_t it_Line = 0; it_Line < c_LogTestLinesPerWorker; ++it_Line)
            {
                Trinity::Log::Print(Trinity::LogChannel::Client, Trinity::LogLevel::Trace, "Log test: worker {} line {}", it_Worker, it_Line);
            }
        }, &l_Counter);
    }

    Trinity::JobSystem::Wait(l_Counter);

    std::array<std::int64_t, c_LogTestWorkers> l_LastLines{};
    l_LastLines.fill(-1);
    std::uint32_t l_Found = 0;
    std::uint32_t l_OutOfOrder = 0;

    // Nothing logs while the history is read, since its lock is held
    {
        constexpr std::string_view c_Prefix = "Log test: worker ";

        const Trinity::LogHistory::Reader l_History = Trinity::LogHistory::Read();
        for (std::size_t it_Index = 0; it_Index < l_History.GetCount(); ++it_Index)
        {
            const Trinity::LogEntry& l_Entry = l_History[it_Index];
            if (it_Index > 0 && l_Entry.Sequence <= l_History[it_Index - 1].Sequence)
            {
                ++l_OutOfOrder;
            }

            if (!l_Entry.Text.starts_with(c_Prefix))
            {
                continue;
            }

            std::uint32_t l_Worker = 0;
            std::int64_t l_Line = 0;
            const char* l_End = l_Entry.Text.data() + l_Entry.Text.size();
            const std::from_chars_result l_WorkerResult = std::from_chars(l_Entry.Text.data() + c_Prefix.size(), l_End, l_Worker);
            if (l_WorkerResult.ec != std::errc{} || l_WorkerResult.ptr + 6 > l_End || l_Worker >= c_LogTestWorkers)
            {
                continue;
            }

            std::from_chars(l_WorkerResult.ptr + 6, l_End, l_Line);
            l_OutOfOrder += l_Line == l_LastLines[l_Worker] + 1 ? 0 : 1;
            l_LastLines[l_Worker] = l_Line;
            ++l_Found;
        }
    }

    const Trinity::MemoryTagStats l_Before = Trinity::Memory::GetStats(Trinity::MemoryTag::Log);

    static const std::string s_Filler(Trinity::LogHistory::c_MaximumLineSize + 1000, 'x');
    for (std::uint32_t it_Worker = 0; it_Worker < c_LogTestWorkers; ++it_Worker)
    {
        Trinity::JobSystem::Submit([it_Worker]
        {
            for (std::uint32_t it_Line = it_Worker; it_Line < c_LogTestFloodLines; it_Line += c_LogTestWorkers)
            {
                const std::size_t l_Length = it_Line % 997 == 0 ? s_Filler.size() : (it_Line % 3 == 0 ? 0 : (it_Line * 37) % 600);
                Trinity::LogHistory::Add(Trinity::LogChannel::Client, Trinity::LogLevel::Trace, std::chrono::system_clock::now(), std::string_view(s_Filler).substr(0, l_Length));
            }
        }, &l_Counter);
    }

    Trinity::JobSystem::Wait(l_Counter);

    const Trinity::MemoryTagStats l_After = Trinity::Memory::GetStats(Trinity::MemoryTag::Log);
    std::size_t l_Kept = 0;
    std::size_t l_Damaged = 0;
    {
        // Each line's text and its closing null, ordered by where they sit in memory, so neighbours can be compared
        std::vector<std::pair<const char*, std::size_t>> l_Spans;

        const Trinity::LogHistory::Reader l_History = Trinity::LogHistory::Read();
        l_Kept = l_History.GetCount();
        for (std::size_t it_Index = 0; it_Index < l_Kept; ++it_Index)
        {
            const std::string_view l_Text = l_History[it_Index].Text;
            const bool l_Whole = l_Text.size() <= Trinity::LogHistory::c_MaximumLineSize && std::ranges::all_of(l_Text, [](char character) { return character == 'x'; }) && l_Text.data()[l_Text.size()] == '\0';
            l_Damaged += l_Whole ? 0 : 1;
            l_Spans.emplace_back(l_Text.data(), l_Text.size() + 1);
        }

        std::ranges::sort(l_Spans);
        for (std::size_t it_Index = 1; it_Index < l_Spans.size(); ++it_Index)
        {
            l_Damaged += l_Spans[it_Index - 1].first + l_Spans[it_Index - 1].second > l_Spans[it_Index].first ? 1 : 0;
        }
    }

    const bool l_Flat = l_After.CurrentBytes == l_Before.CurrentBytes && l_After.LiveAllocations == l_Before.LiveAllocations;
    const bool l_Ordered = l_Found == c_LogTestWorkers * c_LogTestLinesPerWorker && l_OutOfOrder == 0;
    if (l_Ordered && l_Flat && l_Damaged == 0)
    {
        TR_INFO("Log test: {} worker lines in order, and after {} more the Log tag still holds {} in {} allocation(s), with {} whole lines kept", l_Found, c_LogTestFloodLines, Trinity::Memory::FormatBytes(l_After.CurrentBytes), l_After.LiveAllocations, l_Kept);
    }
    else
    {
        TR_ERROR("Log test: {} of {} worker lines found with {} out of order, the Log tag went from {} to {}, and the {} kept lines had {} damaged or overlapping", l_Found, c_LogTestWorkers * c_LogTestLinesPerWorker, l_OutOfOrder, Trinity::Memory::FormatBytes(l_Before.CurrentBytes), Trinity::Memory::FormatBytes(l_After.CurrentBytes), l_Kept, l_Damaged);
    }
}