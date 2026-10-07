#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/RHI/CommandList.hpp"
#include "Trinity/RHI/Device.hpp"
#include "Trinity/RHI/Resources.hpp"
#include "Trinity/RHI/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Trinity
{
    // A texture or buffer for one frame of a graph, which becomes an RHI handle when the graph executes
    struct FrameGraphTexture
    {
        std::uint32_t Index = UINT32_MAX;

        [[nodiscard]] constexpr bool IsValid() const { return Index != UINT32_MAX; }
        constexpr explicit operator bool() const { return IsValid(); }
    };

    struct FrameGraphBuffer
    {
        std::uint32_t Index = UINT32_MAX;

        [[nodiscard]] constexpr bool IsValid() const { return Index != UINT32_MAX; }
        constexpr explicit operator bool() const { return IsValid(); }
    };

    // A raster pass runs inside rendering that the graph begins and ends, a compute or copy pass outside it
    enum class FrameGraphPassType : std::uint8_t
    {
        Raster,
        Compute,
        Copy
    };

    // Whether the contents are stored is up to the graph: they are when the texture is imported or a later pass uses it
    struct FrameGraphColorAttachment
    {
        FrameGraphTexture Texture{};
        RHI::LoadOp Load = RHI::LoadOp::Clear;
        std::array<float, 4> ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        std::uint32_t MipLevel = 0;
        std::uint32_t ArrayLayer = 0;
        FrameGraphTexture Resolve{};
    };

    struct FrameGraphDepthAttachment
    {
        FrameGraphTexture Texture{};
        RHI::LoadOp Load = RHI::LoadOp::Clear;
        float ClearDepth = 0.0f;
        std::uint32_t MipLevel = 0;
        std::uint32_t ArrayLayer = 0;
    };

    class FrameGraph;

    // Declares what one pass reads and writes, while the graph is being built
    class TRINITY_API FrameGraphPassBuilder
    {
    public:
        void AddColorAttachment(const FrameGraphColorAttachment& attachment);
        void SetDepthAttachment(const FrameGraphDepthAttachment& attachment);

        // In the state the pass needs, such as ShaderResource, UnorderedAccess, CopySource or CopyDestination
        void Read(FrameGraphTexture texture, RHI::ResourceState state);
        void Write(FrameGraphTexture texture, RHI::ResourceState state);
        void Read(FrameGraphBuffer buffer, RHI::ResourceState state);
        void Write(FrameGraphBuffer buffer, RHI::ResourceState state);

        // Never culled, for a pass whose effect lies outside the graph
        void SetSideEffect();

    private:
        friend class FrameGraph;

        FrameGraphPassBuilder(FrameGraph& graph, std::uint32_t pass) : m_Graph(graph), m_Pass(pass)
        {

        }

        FrameGraph& m_Graph;
        std::uint32_t m_Pass = 0;
    };

    // What a pass's execute callback records with, and the RHI handles of the resources it declared
    class TRINITY_API FrameGraphContext
    {
    public:
        [[nodiscard]] RHI::CommandList& GetCommands() const { return m_Commands; }
        [[nodiscard]] RHI::Device& GetDevice() const;
        [[nodiscard]] RHI::TextureHandle GetTexture(FrameGraphTexture texture) const;
        [[nodiscard]] RHI::BufferHandle GetBuffer(FrameGraphBuffer buffer) const;

        // A raster pass's render area, the size of its first attachment's mip
        [[nodiscard]] std::uint32_t GetWidth() const { return m_Width; }
        [[nodiscard]] std::uint32_t GetHeight() const { return m_Height; }

    private:
        friend class FrameGraph;

        FrameGraphContext(const FrameGraph& graph, RHI::CommandList& commands, std::uint32_t pass, std::uint32_t width, std::uint32_t height) : m_Graph(graph), m_Commands(commands), m_Pass(pass), m_Width(width), m_Height(height)
        {

        }

        const FrameGraph& m_Graph;
        RHI::CommandList& m_Commands;
        std::uint32_t m_Pass = 0;
        std::uint32_t m_Width = 0;
        std::uint32_t m_Height = 0;
    };

    // Passes declare the textures and buffers they read and write, and run in the order they are added, which is always a valid order since a pass can only use what earlier passes wrote. Executing culls passes whose results nobody uses, takes transient textures and buffers from a pool, and inserts every barrier
    class TRINITY_API FrameGraph
    {
    public:
        struct Statistics
        {
            std::uint32_t Passes = 0;
            std::uint32_t CulledPasses = 0;
            std::uint32_t Barriers = 0;
            std::uint32_t TransientTextures = 0;
            std::uint32_t TransientBuffers = 0;
            std::uint32_t PooledTextures = 0;
            std::uint32_t PooledBuffers = 0;
        };

        struct PassTime
        {
            std::string_view Name;
            float Milliseconds = 0.0f;
        };

        explicit FrameGraph(RHI::Device& device);
        ~FrameGraph();

        FrameGraph(const FrameGraph&) = delete;
        FrameGraph& operator=(const FrameGraph&) = delete;

        // Clears the last frame's passes and resources and keeps the pool
        void Reset();

        // The graph moves an imported resource from its initial state through what passes need, and leaves it in its final state
        [[nodiscard]] FrameGraphTexture ImportTexture(std::string_view name, RHI::TextureHandle texture, const RHI::TextureDescription& description, RHI::ResourceState initialState, RHI::ResourceState finalState);
        [[nodiscard]] FrameGraphBuffer ImportBuffer(std::string_view name, RHI::BufferHandle buffer, std::uint64_t size, RHI::ResourceState initialState, RHI::ResourceState finalState);

        // The usage comes from the passes that use it, and the contents do not last from one frame to the next
        [[nodiscard]] FrameGraphTexture CreateTexture(std::string_view name, const RHI::TextureDescription& description);
        [[nodiscard]] FrameGraphBuffer CreateBuffer(std::string_view name, std::uint64_t size);

        // Setup runs at once with a FrameGraphPassBuilder. Execute runs from Execute with a const FrameGraphContext&, and is destroyed once the frame's graph has executed or been reset
        template<typename Setup, typename Callback>
        void AddPass(std::string_view name, FrameGraphPassType type, Setup&& setup, Callback&& execute)
        {
            using Callable = std::decay_t<Callback>;

            const std::uint32_t l_Pass = BeginPass(name, type);
            FrameGraphPassBuilder l_Builder(*this, l_Pass);
            setup(l_Builder);

            void* l_Memory = AllocateFrameMemory(sizeof(Callable), alignof(Callable));
            new (l_Memory) Callable(std::forward<Callback>(execute));
            SetPassCallback(l_Pass, l_Memory, [](void* callable, const FrameGraphContext& context) { (*static_cast<Callable*>(callable))(context); }, [](void* callable) { static_cast<Callable*>(callable)->~Callable(); });
        }

        // Once per Reset, between the device's BeginFrame and EndFrame
        void Execute(RHI::CommandList& commands);

        [[nodiscard]] const Statistics& GetStatistics() const;
        [[nodiscard]] std::uint32_t GetPassCount() const;
        [[nodiscard]] std::string_view GetPassName(std::uint32_t pass) const;
        [[nodiscard]] bool IsPassCulled(std::uint32_t pass) const;

        // The GPU time of each pass that ran, in the order of the latest timed execution, and of all of them together, averaged over 30 executions and published once every 30. A pass's time includes the barriers before it. An execution's timestamps are read back c_FramesInFlight executions later, so a graph executes at most once per device frame, and the times stay empty until the first 30 are in
        [[nodiscard]] std::span<const PassTime> GetPassTimes() const;
        [[nodiscard]] float GetGpuMilliseconds() const;

    private:
        friend class FrameGraphPassBuilder;
        friend class FrameGraphContext;

        using InvokeFunction = void (*)(void* callable, const FrameGraphContext& context);
        using DestroyFunction = void (*)(void* callable);

        struct State;

        [[nodiscard]] std::uint32_t BeginPass(std::string_view name, FrameGraphPassType type);
        void SetPassCallback(std::uint32_t pass, void* callable, InvokeFunction invoke, DestroyFunction destroy);
        void AddAccess(std::uint32_t pass, std::uint32_t resource, bool buffer, RHI::ResourceState state, bool write);
        [[nodiscard]] void* AllocateFrameMemory(std::size_t size, std::size_t alignment);
        [[nodiscard]] std::string_view CopyName(std::string_view name);
        void DestroyCallbacks();

        void CullPasses();
        void AcquireResources();
        void RecordPass(RHI::CommandList& commands, std::uint32_t pass);
        void TransitionTexture(RHI::CommandList& commands, std::uint32_t texture, RHI::ResourceState state);
        void TransitionBuffer(RHI::CommandList& commands, std::uint32_t buffer, RHI::ResourceState state);
        void ReleaseResources();

        void BeginTimestamps(RHI::CommandList& commands);
        void WritePassTimestamp(RHI::CommandList& commands, std::uint32_t pass);
        void EndTimestamps(RHI::CommandList& commands);
        void ReadTimestamps(std::uint32_t slot);
        void PublishTimes(std::uint32_t slot);

        RHI::Device& m_Device;
        State* m_State = nullptr;
    };
}