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
    // Becomes an RHI handle when the graph executes
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

    // Raster runs inside rendering. AsyncCompute runs on the compute queue where the device has one
    enum class FrameGraphPassType : std::uint8_t
    {
        Raster,
        Compute,
        Copy,
        AsyncCompute
    };

    // The graph stores the contents when the texture is imported or a later pass uses it
    struct FrameGraphColorAttachment
    {
        FrameGraphTexture Texture{};
        RHI::LoadOp Load = RHI::LoadOp::Clear;
        std::array<float, 4> ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        std::uint32_t MipLevel = 0;
        std::uint32_t ArrayLayer = 0;
        FrameGraphTexture Resolve{};
    };

    // A format with stencil loads and clears its stencil as it does its depth
    struct FrameGraphDepthAttachment
    {
        FrameGraphTexture Texture{};
        RHI::LoadOp Load = RHI::LoadOp::Clear;
        float ClearDepth = 0.0f;
        std::uint8_t ClearStencil = 0;
        std::uint32_t MipLevel = 0;
        std::uint32_t ArrayLayer = 0;
    };

    class FrameGraph;

    class TRINITY_API FrameGraphPassBuilder
    {
    public:
        void AddColorAttachment(const FrameGraphColorAttachment& attachment);
        void SetDepthAttachment(const FrameGraphDepthAttachment& attachment);

        // In the state the pass needs, such as ShaderResource or CopyDestination
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

    // Passes run in the order added. Executing culls unused passes, pools transients, adds barriers
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

        // An imported resource is moved from its initial state, and left in its final state
        [[nodiscard]] FrameGraphTexture ImportTexture(std::string_view name, RHI::TextureHandle texture, const RHI::TextureDescription& description, RHI::ResourceState initialState, RHI::ResourceState finalState);
        [[nodiscard]] FrameGraphBuffer ImportBuffer(std::string_view name, RHI::BufferHandle buffer, std::uint64_t size, RHI::ResourceState initialState, RHI::ResourceState finalState);

        // The usage comes from the passes that use it. The contents do not last between frames
        [[nodiscard]] FrameGraphTexture CreateTexture(std::string_view name, const RHI::TextureDescription& description);
        [[nodiscard]] FrameGraphBuffer CreateBuffer(std::string_view name, std::uint64_t size);

        // Setup runs at once. Execute runs later, and is destroyed once the graph has executed or reset
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

        // Averaged over 30 executions, published every 30. A graph executes at most once a device frame
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
        [[nodiscard]] bool MoveToCompute(std::uint32_t pass);
        [[nodiscard]] bool UsesPendingCompute(std::uint32_t pass) const;
        void MarkGraphics(std::uint32_t pass);
        void SendCompute(bool afterGraphics);
        void RecordPass(RHI::CommandList& commands, std::uint32_t pass);
        void TransitionTexture(RHI::CommandList& commands, std::uint32_t texture, RHI::ResourceState state);
        void TransitionBuffer(RHI::CommandList& commands, std::uint32_t buffer, RHI::ResourceState state);
        void ReleaseResources();

        [[nodiscard]] std::uint32_t BeginTimestamps(RHI::CommandList& commands);
        [[nodiscard]] std::uint32_t WriteTimestamp(RHI::CommandList& commands, RHI::QueueType queue);
        [[nodiscard]] std::uint32_t WritePassTimestamp(RHI::CommandList& commands, std::uint32_t pass, RHI::QueueType queue, std::uint32_t begin);
        void EndTimestamps(RHI::CommandList& commands, std::uint32_t lastGraphics);
        void ReadTimestamps(std::uint32_t slot);
        void PublishTimes(std::uint32_t slot);

        RHI::Device& m_Device;
        State* m_State = nullptr;
    };
}