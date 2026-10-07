#include "Trinity/Renderer/FrameGraph.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"

#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

namespace Trinity
{
    namespace
    {
        constexpr std::uint32_t c_NoIndex = UINT32_MAX;

        // Execute callbacks and names live in blocks reused every frame, so building a graph allocates nothing once the blocks are big enough
        constexpr std::size_t c_FrameMemoryBlockSize = 64 * 1024;
        constexpr std::size_t c_FrameMemoryAlignment = 64;

        // A pooled texture or buffer that no execution has used for this many executions is destroyed, so the sizes a resize leaves behind do not pile up
        constexpr std::uint64_t c_PoolKeepExecutions = 4;

        template<typename T>
        using RendererVector = std::vector<T, TaggedAllocator<T, MemoryTag::Renderer>>;

        // Undefined counts as a write, since leaving it discards the contents
        bool IsWriteState(RHI::ResourceState state)
        {
            switch (state)
            {
                case RHI::ResourceState::Undefined:
                case RHI::ResourceState::RenderTarget:
                case RHI::ResourceState::ResolveDestination:
                case RHI::ResourceState::DepthWrite:
                case RHI::ResourceState::UnorderedAccess:
                case RHI::ResourceState::CopyDestination:
                {
                    return true;
                }
                default:
                {
                    return false;
                }
            }
        }

        RHI::TextureUsage ToTextureUsage(RHI::ResourceState state)
        {
            switch (state)
            {
                case RHI::ResourceState::RenderTarget:
                case RHI::ResourceState::ResolveDestination:
                {
                    return RHI::TextureUsage::RenderTarget;
                }
                case RHI::ResourceState::DepthWrite:
                {
                    return RHI::TextureUsage::DepthStencil;
                }
                case RHI::ResourceState::DepthRead:
                {
                    return RHI::TextureUsage::DepthStencil | RHI::TextureUsage::ShaderResource;
                }
                case RHI::ResourceState::ShaderResource:
                {
                    return RHI::TextureUsage::ShaderResource;
                }
                case RHI::ResourceState::UnorderedAccess:
                {
                    return RHI::TextureUsage::UnorderedAccess;
                }
                case RHI::ResourceState::CopySource:
                {
                    return RHI::TextureUsage::CopySource;
                }
                case RHI::ResourceState::CopyDestination:
                {
                    return RHI::TextureUsage::CopyDestination;
                }
                default:
                {
                    return RHI::TextureUsage::None;
                }
            }
        }

        RHI::BufferUsage ToBufferUsage(RHI::ResourceState state)
        {
            switch (state)
            {
                case RHI::ResourceState::ShaderResource:
                {
                    return RHI::BufferUsage::ShaderResource;
                }
                case RHI::ResourceState::UnorderedAccess:
                {
                    return RHI::BufferUsage::UnorderedAccess;
                }
                case RHI::ResourceState::CopySource:
                {
                    return RHI::BufferUsage::CopySource;
                }
                case RHI::ResourceState::CopyDestination:
                {
                    return RHI::BufferUsage::CopyDestination;
                }
                case RHI::ResourceState::IndexBuffer:
                {
                    return RHI::BufferUsage::Index;
                }
                case RHI::ResourceState::IndirectArgument:
                {
                    return RHI::BufferUsage::Indirect;
                }
                default:
                {
                    return RHI::BufferUsage::None;
                }
            }
        }

        // Everything CreateTexture is given, including the optimized clear, so a pooled texture never clears to a value it was not made for
        bool IsSameTexture(const RHI::TextureDescription& left, const RHI::TextureDescription& right)
        {
            return left.Width == right.Width && left.Height == right.Height && left.MipLevels == right.MipLevels && left.ArrayLayers == right.ArrayLayers && left.Dimension == right.Dimension && left.SampleCount == right.SampleCount && left.TextureFormat == right.TextureFormat && left.Usage == right.Usage && left.ClearColor == right.ClearColor && left.ClearDepth == right.ClearDepth && left.OptimizedClear == right.OptimizedClear;
        }
    }

    struct FrameGraph::State
    {
        struct Access
        {
            std::uint32_t Resource = c_NoIndex;
            bool Buffer = false;
            bool Write = false;
            RHI::ResourceState ResourceState = RHI::ResourceState::Undefined;
        };

        struct Pass
        {
            std::string_view Name;
            FrameGraphPassType Type = FrameGraphPassType::Raster;
            std::uint32_t FirstAccess = 0;
            std::uint32_t AccessCount = 0;
            std::uint32_t FirstColor = 0;
            std::uint32_t ColorCount = 0;
            FrameGraphDepthAttachment Depth{};
            bool SideEffect = false;
            bool Culled = false;
            void* Callable = nullptr;
            InvokeFunction Invoke = nullptr;
            DestroyFunction Destroy = nullptr;
        };

        // LastPass is the last pass left after culling that uses the resource, and decides whether an attachment is stored
        struct Texture
        {
            std::string_view Name;
            RHI::TextureDescription Description;
            RHI::TextureHandle Handle;
            RHI::ResourceState InitialState = RHI::ResourceState::Undefined;
            RHI::ResourceState FinalState = RHI::ResourceState::Undefined;
            RHI::ResourceState CurrentState = RHI::ResourceState::Undefined;
            std::uint32_t LastPass = c_NoIndex;
            bool Imported = false;
            bool Needed = false;
        };

        struct Buffer
        {
            std::string_view Name;
            std::uint64_t Size = 0;
            RHI::BufferUsage Usage = RHI::BufferUsage::None;
            RHI::BufferHandle Handle;
            RHI::ResourceState InitialState = RHI::ResourceState::Undefined;
            RHI::ResourceState FinalState = RHI::ResourceState::Undefined;
            RHI::ResourceState CurrentState = RHI::ResourceState::Undefined;
            std::uint32_t LastPass = c_NoIndex;
            bool Imported = false;
            bool Needed = false;
        };

        struct PooledTexture
        {
            RHI::TextureDescription Description;
            RHI::TextureHandle Handle;
            std::uint64_t LastUsed = 0;
            bool InUse = false;
        };

        struct PooledBuffer
        {
            std::uint64_t Size = 0;
            RHI::BufferUsage Usage = RHI::BufferUsage::None;
            RHI::BufferHandle Handle;
            std::uint64_t LastUsed = 0;
            bool InUse = false;
        };

        struct MemoryBlock
        {
            std::byte* Memory = nullptr;
            std::size_t Size = 0;
        };

        RendererVector<Pass> Passes;
        RendererVector<Access> Accesses;
        RendererVector<FrameGraphColorAttachment> ColorAttachments;
        RendererVector<Texture> Textures;
        RendererVector<Buffer> Buffers;
        RendererVector<PooledTexture> TexturePool;
        RendererVector<PooledBuffer> BufferPool;
        RendererVector<MemoryBlock> Blocks;
        std::size_t BlockIndex = 0;
        std::size_t BlockOffset = 0;
        std::uint64_t Executions = 0;
        Statistics Stats;
        bool Executed = false;
    };

    void FrameGraphPassBuilder::AddColorAttachment(const FrameGraphColorAttachment& attachment)
    {
        FrameGraph::State& l_State = *m_Graph.m_State;
        FrameGraph::State::Pass& l_Pass = l_State.Passes[m_Pass];
        TR_CORE_ASSERT(l_Pass.Type == FrameGraphPassType::Raster && l_Pass.ColorCount < RHI::c_MaxColorAttachments, "Pass '{}' adds a color attachment, but it is not a raster pass or has {} already.", l_Pass.Name, RHI::c_MaxColorAttachments);
        TR_CORE_ASSERT(attachment.Texture.Index < l_State.Textures.size() && (!attachment.Resolve || attachment.Resolve.Index < l_State.Textures.size()), "Pass '{}' adds a color attachment that is not a texture of this graph.", l_Pass.Name);
        if (l_Pass.Type != FrameGraphPassType::Raster || l_Pass.ColorCount >= RHI::c_MaxColorAttachments || attachment.Texture.Index >= l_State.Textures.size())
        {
            return;
        }

        l_State.ColorAttachments.push_back(attachment);
        ++l_Pass.ColorCount;
        m_Graph.AddAccess(m_Pass, attachment.Texture.Index, false, RHI::ResourceState::RenderTarget, true);
        if (attachment.Resolve && attachment.Resolve.Index < l_State.Textures.size())
        {
            m_Graph.AddAccess(m_Pass, attachment.Resolve.Index, false, RHI::ResourceState::ResolveDestination, true);
        }
    }

    void FrameGraphPassBuilder::SetDepthAttachment(const FrameGraphDepthAttachment& attachment)
    {
        FrameGraph::State& l_State = *m_Graph.m_State;
        FrameGraph::State::Pass& l_Pass = l_State.Passes[m_Pass];
        TR_CORE_ASSERT(l_Pass.Type == FrameGraphPassType::Raster && !l_Pass.Depth.Texture, "Pass '{}' sets a depth attachment, but it is not a raster pass or has one already.", l_Pass.Name);
        TR_CORE_ASSERT(attachment.Texture.Index < l_State.Textures.size(), "Pass '{}' sets a depth attachment that is not a texture of this graph.", l_Pass.Name);
        if (l_Pass.Type != FrameGraphPassType::Raster || l_Pass.Depth.Texture || attachment.Texture.Index >= l_State.Textures.size())
        {
            return;
        }

        l_Pass.Depth = attachment;
        m_Graph.AddAccess(m_Pass, attachment.Texture.Index, false, RHI::ResourceState::DepthWrite, true);
    }

    void FrameGraphPassBuilder::Read(FrameGraphTexture texture, RHI::ResourceState state)
    {
        m_Graph.AddAccess(m_Pass, texture.Index, false, state, false);
    }

    void FrameGraphPassBuilder::Write(FrameGraphTexture texture, RHI::ResourceState state)
    {
        m_Graph.AddAccess(m_Pass, texture.Index, false, state, true);
    }

    void FrameGraphPassBuilder::Read(FrameGraphBuffer buffer, RHI::ResourceState state)
    {
        m_Graph.AddAccess(m_Pass, buffer.Index, true, state, false);
    }

    void FrameGraphPassBuilder::Write(FrameGraphBuffer buffer, RHI::ResourceState state)
    {
        m_Graph.AddAccess(m_Pass, buffer.Index, true, state, true);
    }

    void FrameGraphPassBuilder::SetSideEffect()
    {
        m_Graph.m_State->Passes[m_Pass].SideEffect = true;
    }

    RHI::Device& FrameGraphContext::GetDevice() const
    {
        return m_Graph.m_Device;
    }

    RHI::TextureHandle FrameGraphContext::GetTexture(FrameGraphTexture texture) const
    {
        const FrameGraph::State& l_State = *m_Graph.m_State;
        [[maybe_unused]] const FrameGraph::State::Pass& l_Pass = l_State.Passes[m_Pass];
        [[maybe_unused]] const auto a_Accesses = std::span(l_State.Accesses).subspan(l_Pass.FirstAccess, l_Pass.AccessCount);
        TR_CORE_ASSERT(std::ranges::any_of(a_Accesses, [texture](const FrameGraph::State::Access& access) { return !access.Buffer && access.Resource == texture.Index; }), "Pass '{}' uses a texture it did not declare.", l_Pass.Name);

        return texture.Index < l_State.Textures.size() ? l_State.Textures[texture.Index].Handle : RHI::TextureHandle{};
    }

    RHI::BufferHandle FrameGraphContext::GetBuffer(FrameGraphBuffer buffer) const
    {
        const FrameGraph::State& l_State = *m_Graph.m_State;
        [[maybe_unused]] const FrameGraph::State::Pass& l_Pass = l_State.Passes[m_Pass];
        [[maybe_unused]] const auto a_Accesses = std::span(l_State.Accesses).subspan(l_Pass.FirstAccess, l_Pass.AccessCount);
        TR_CORE_ASSERT(std::ranges::any_of(a_Accesses, [buffer](const FrameGraph::State::Access& access) { return access.Buffer && access.Resource == buffer.Index; }), "Pass '{}' uses a buffer it did not declare.", l_Pass.Name);

        return buffer.Index < l_State.Buffers.size() ? l_State.Buffers[buffer.Index].Handle : RHI::BufferHandle{};
    }

    FrameGraph::FrameGraph(RHI::Device& device) : m_Device(device), m_State(Memory::New<State>(MemoryTag::Renderer))
    {

    }

    FrameGraph::~FrameGraph()
    {
        DestroyCallbacks();

        for (const State::PooledTexture& it_Texture : m_State->TexturePool)
        {
            m_Device.DestroyTexture(it_Texture.Handle);
        }

        for (const State::PooledBuffer& it_Buffer : m_State->BufferPool)
        {
            m_Device.DestroyBuffer(it_Buffer.Handle);
        }

        for (const State::MemoryBlock& it_Block : m_State->Blocks)
        {
            Memory::Free(it_Block.Memory);
        }

        Memory::Delete(m_State);
    }

    void FrameGraph::Reset()
    {
        DestroyCallbacks();

        State& l_State = *m_State;
        l_State.Passes.clear();
        l_State.Accesses.clear();
        l_State.ColorAttachments.clear();
        l_State.Textures.clear();
        l_State.Buffers.clear();
        l_State.BlockIndex = 0;
        l_State.BlockOffset = 0;
        l_State.Executed = false;
    }

    FrameGraphTexture FrameGraph::ImportTexture(std::string_view name, RHI::TextureHandle texture, const RHI::TextureDescription& description, RHI::ResourceState initialState, RHI::ResourceState finalState)
    {
        TR_CORE_ASSERT(texture.IsValid() && finalState != RHI::ResourceState::Undefined, "Texture '{}' is imported without a texture, or with Undefined as its final state.", name);

        State::Texture l_Texture;
        l_Texture.Name = CopyName(name);
        l_Texture.Description = description;
        l_Texture.Description.DebugName = {};
        l_Texture.Handle = texture;
        l_Texture.InitialState = initialState;
        l_Texture.FinalState = finalState;
        l_Texture.CurrentState = initialState;
        l_Texture.Imported = true;
        m_State->Textures.push_back(l_Texture);

        return { static_cast<std::uint32_t>(m_State->Textures.size() - 1) };
    }

    FrameGraphBuffer FrameGraph::ImportBuffer(std::string_view name, RHI::BufferHandle buffer, std::uint64_t size, RHI::ResourceState initialState, RHI::ResourceState finalState)
    {
        TR_CORE_ASSERT(buffer.IsValid() && finalState != RHI::ResourceState::Undefined, "Buffer '{}' is imported without a buffer, or with Undefined as its final state.", name);

        State::Buffer l_Buffer;
        l_Buffer.Name = CopyName(name);
        l_Buffer.Size = size;
        l_Buffer.Handle = buffer;
        l_Buffer.InitialState = initialState;
        l_Buffer.FinalState = finalState;
        l_Buffer.CurrentState = initialState;
        l_Buffer.Imported = true;
        m_State->Buffers.push_back(l_Buffer);

        return { static_cast<std::uint32_t>(m_State->Buffers.size() - 1) };
    }

    FrameGraphTexture FrameGraph::CreateTexture(std::string_view name, const RHI::TextureDescription& description)
    {
        State::Texture l_Texture;
        l_Texture.Name = CopyName(name);
        l_Texture.Description = description;
        l_Texture.Description.Usage = RHI::TextureUsage::None;
        l_Texture.Description.DebugName = {};
        m_State->Textures.push_back(l_Texture);

        return { static_cast<std::uint32_t>(m_State->Textures.size() - 1) };
    }

    FrameGraphBuffer FrameGraph::CreateBuffer(std::string_view name, std::uint64_t size)
    {
        State::Buffer l_Buffer;
        l_Buffer.Name = CopyName(name);
        l_Buffer.Size = size;
        m_State->Buffers.push_back(l_Buffer);

        return { static_cast<std::uint32_t>(m_State->Buffers.size() - 1) };
    }

    void FrameGraph::Execute(RHI::CommandList& commands)
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        TR_CORE_ASSERT(!l_State.Executed, "A frame graph executes once between resets.");
        if (l_State.Executed)
        {
            return;
        }

        l_State.Executed = true;
        ++l_State.Executions;
        l_State.Stats = {};
        l_State.Stats.Passes = static_cast<std::uint32_t>(l_State.Passes.size());

        CullPasses();
        AcquireResources();

        for (std::uint32_t it_Pass = 0; it_Pass < l_State.Passes.size(); ++it_Pass)
        {
            if (!l_State.Passes[it_Pass].Culled)
            {
                RecordPass(commands, it_Pass);
            }
        }

        for (std::uint32_t it_Texture = 0; it_Texture < l_State.Textures.size(); ++it_Texture)
        {
            const State::Texture& l_Texture = l_State.Textures[it_Texture];
            if (l_Texture.Imported && l_Texture.CurrentState != l_Texture.FinalState)
            {
                TransitionTexture(commands, it_Texture, l_Texture.FinalState);
            }
        }

        for (std::uint32_t it_Buffer = 0; it_Buffer < l_State.Buffers.size(); ++it_Buffer)
        {
            const State::Buffer& l_Buffer = l_State.Buffers[it_Buffer];
            if (l_Buffer.Imported && l_Buffer.CurrentState != l_Buffer.FinalState)
            {
                TransitionBuffer(commands, it_Buffer, l_Buffer.FinalState);
            }
        }

        ReleaseResources();
        DestroyCallbacks();
    }

    const FrameGraph::Statistics& FrameGraph::GetStatistics() const
    {
        return m_State->Stats;
    }

    std::uint32_t FrameGraph::GetPassCount() const
    {
        return static_cast<std::uint32_t>(m_State->Passes.size());
    }

    std::string_view FrameGraph::GetPassName(std::uint32_t pass) const
    {
        return pass < m_State->Passes.size() ? m_State->Passes[pass].Name : std::string_view{};
    }

    bool FrameGraph::IsPassCulled(std::uint32_t pass) const
    {
        return pass < m_State->Passes.size() && m_State->Passes[pass].Culled;
    }

    std::uint32_t FrameGraph::BeginPass(std::string_view name, FrameGraphPassType type)
    {
        State& l_State = *m_State;
        TR_CORE_ASSERT(!l_State.Executed, "Pass '{}' is added after the graph executed, without a reset.", name);

        State::Pass l_Pass;
        l_Pass.Name = CopyName(name);
        l_Pass.Type = type;
        l_Pass.FirstAccess = static_cast<std::uint32_t>(l_State.Accesses.size());
        l_Pass.FirstColor = static_cast<std::uint32_t>(l_State.ColorAttachments.size());
        l_State.Passes.push_back(l_Pass);

        return static_cast<std::uint32_t>(l_State.Passes.size() - 1);
    }

    void FrameGraph::SetPassCallback(std::uint32_t pass, void* callable, InvokeFunction invoke, DestroyFunction destroy)
    {
        State::Pass& l_Pass = m_State->Passes[pass];
        l_Pass.Callable = callable;
        l_Pass.Invoke = invoke;
        l_Pass.Destroy = destroy;
    }

    // A pass uses each resource once, which also keeps one state per resource per pass
    void FrameGraph::AddAccess(std::uint32_t pass, std::uint32_t resource, bool buffer, RHI::ResourceState state, bool write)
    {
        State& l_State = *m_State;
        State::Pass& l_Pass = l_State.Passes[pass];
        TR_CORE_ASSERT(resource < (buffer ? l_State.Buffers.size() : l_State.Textures.size()), "Pass '{}' uses a {} that is not part of this graph.", l_Pass.Name, buffer ? "buffer" : "texture");
        TR_CORE_ASSERT(state != RHI::ResourceState::Undefined, "Pass '{}' uses a resource in the Undefined state.", l_Pass.Name);
        if (resource >= (buffer ? l_State.Buffers.size() : l_State.Textures.size()))
        {
            return;
        }

        [[maybe_unused]] const auto a_Accesses = std::span(l_State.Accesses).subspan(l_Pass.FirstAccess, l_Pass.AccessCount);
        TR_CORE_ASSERT(std::ranges::none_of(a_Accesses, [resource, buffer](const State::Access& access) { return access.Resource == resource && access.Buffer == buffer; }), "Pass '{}' uses '{}' twice.", l_Pass.Name, buffer ? l_State.Buffers[resource].Name : l_State.Textures[resource].Name);

        l_State.Accesses.push_back({ resource, buffer, write, state });
        ++l_Pass.AccessCount;
    }

    void* FrameGraph::AllocateFrameMemory(std::size_t size, std::size_t alignment)
    {
        TR_CORE_ASSERT(alignment <= c_FrameMemoryAlignment, "Frame graph callbacks are aligned to at most {} bytes.", c_FrameMemoryAlignment);

        State& l_State = *m_State;
        while (true)
        {
            if (l_State.BlockIndex < l_State.Blocks.size())
            {
                const State::MemoryBlock& l_Block = l_State.Blocks[l_State.BlockIndex];
                const std::size_t l_Offset = (l_State.BlockOffset + alignment - 1) / alignment * alignment;
                if (l_Offset + size <= l_Block.Size)
                {
                    l_State.BlockOffset = l_Offset + size;

                    return l_Block.Memory + l_Offset;
                }

                ++l_State.BlockIndex;
                l_State.BlockOffset = 0;

                continue;
            }

            const std::size_t l_Size = std::max(c_FrameMemoryBlockSize, size);
            l_State.Blocks.push_back({ static_cast<std::byte*>(Memory::Allocate(l_Size, MemoryTag::Renderer, c_FrameMemoryAlignment)), l_Size });
        }
    }

    std::string_view FrameGraph::CopyName(std::string_view name)
    {
        if (name.empty())
        {
            return {};
        }

        char* l_Name = static_cast<char*>(AllocateFrameMemory(name.size(), 1));
        std::memcpy(l_Name, name.data(), name.size());

        return { l_Name, name.size() };
    }

    void FrameGraph::DestroyCallbacks()
    {
        for (State::Pass& it_Pass : m_State->Passes)
        {
            if (it_Pass.Callable != nullptr && it_Pass.Destroy != nullptr)
            {
                it_Pass.Destroy(it_Pass.Callable);
            }

            it_Pass.Callable = nullptr;
        }
    }

    // Walking back from the last pass, a pass lives if it has a side effect or writes an imported resource or one a living pass uses. Everything a living pass uses is then needed, its writes included, so earlier writes to part of a texture survive
    void FrameGraph::CullPasses()
    {
        State& l_State = *m_State;
        for (std::size_t it_Pass = l_State.Passes.size(); it_Pass-- > 0;)
        {
            State::Pass& l_Pass = l_State.Passes[it_Pass];
            const auto a_Accesses = std::span(l_State.Accesses).subspan(l_Pass.FirstAccess, l_Pass.AccessCount);

            const auto a_Keeps = [&l_State](const State::Access& access)
            {
                if (!access.Write)
                {
                    return false;
                }

                return access.Buffer ? l_State.Buffers[access.Resource].Imported || l_State.Buffers[access.Resource].Needed : l_State.Textures[access.Resource].Imported || l_State.Textures[access.Resource].Needed;
            };

            l_Pass.Culled = !l_Pass.SideEffect && std::ranges::none_of(a_Accesses, a_Keeps);
            if (l_Pass.Culled)
            {
                ++l_State.Stats.CulledPasses;

                continue;
            }

            // Walking backwards, the first living pass to use a resource is the last one to use it
            for (const State::Access& it_Access : a_Accesses)
            {
                bool& l_Needed = it_Access.Buffer ? l_State.Buffers[it_Access.Resource].Needed : l_State.Textures[it_Access.Resource].Needed;
                std::uint32_t& l_LastPass = it_Access.Buffer ? l_State.Buffers[it_Access.Resource].LastPass : l_State.Textures[it_Access.Resource].LastPass;
                l_Needed = true;
                if (l_LastPass == c_NoIndex)
                {
                    l_LastPass = static_cast<std::uint32_t>(it_Pass);
                }
            }
        }
    }

    // A transient resource's usage is every way a living pass uses it, and it comes from the pool when a matching one is free
    void FrameGraph::AcquireResources()
    {
        State& l_State = *m_State;
        for (const State::Pass& it_Pass : l_State.Passes)
        {
            if (it_Pass.Culled)
            {
                continue;
            }

            for (const State::Access& it_Access : std::span(l_State.Accesses).subspan(it_Pass.FirstAccess, it_Pass.AccessCount))
            {
                if (it_Access.Buffer)
                {
                    State::Buffer& l_Buffer = l_State.Buffers[it_Access.Resource];
                    l_Buffer.Usage = l_Buffer.Usage | ToBufferUsage(it_Access.ResourceState);
                }
                else
                {
                    State::Texture& l_Texture = l_State.Textures[it_Access.Resource];
                    l_Texture.Description.Usage = l_Texture.Imported ? l_Texture.Description.Usage : l_Texture.Description.Usage | ToTextureUsage(it_Access.ResourceState);
                }
            }
        }

        for (State::Texture& it_Texture : l_State.Textures)
        {
            if (it_Texture.Imported || it_Texture.LastPass == c_NoIndex)
            {
                continue;
            }

            const auto a_Pooled = std::ranges::find_if(l_State.TexturePool, [&it_Texture](const State::PooledTexture& pooled) { return !pooled.InUse && IsSameTexture(pooled.Description, it_Texture.Description); });
            if (a_Pooled != l_State.TexturePool.end())
            {
                a_Pooled->InUse = true;
                a_Pooled->LastUsed = l_State.Executions;
                it_Texture.Handle = a_Pooled->Handle;
            }
            else
            {
                RHI::TextureDescription l_Description = it_Texture.Description;
                l_Description.DebugName = it_Texture.Name;
                it_Texture.Handle = m_Device.CreateTexture(l_Description);
                if (it_Texture.Handle)
                {
                    l_State.TexturePool.push_back({ it_Texture.Description, it_Texture.Handle, l_State.Executions, true });
                }
            }

            ++l_State.Stats.TransientTextures;
        }

        for (State::Buffer& it_Buffer : l_State.Buffers)
        {
            if (it_Buffer.Imported || it_Buffer.LastPass == c_NoIndex)
            {
                continue;
            }

            const auto a_Pooled = std::ranges::find_if(l_State.BufferPool, [&it_Buffer](const State::PooledBuffer& pooled) { return !pooled.InUse && pooled.Size == it_Buffer.Size && pooled.Usage == it_Buffer.Usage; });
            if (a_Pooled != l_State.BufferPool.end())
            {
                a_Pooled->InUse = true;
                a_Pooled->LastUsed = l_State.Executions;
                it_Buffer.Handle = a_Pooled->Handle;
            }
            else
            {
                RHI::BufferDescription l_Description;
                l_Description.Size = it_Buffer.Size;
                l_Description.Usage = it_Buffer.Usage;
                l_Description.DebugName = it_Buffer.Name;
                it_Buffer.Handle = m_Device.CreateBuffer(l_Description);
                if (it_Buffer.Handle)
                {
                    l_State.BufferPool.push_back({ it_Buffer.Size, it_Buffer.Usage, it_Buffer.Handle, l_State.Executions, true });
                }
            }

            ++l_State.Stats.TransientBuffers;
        }
    }

    // Barriers first, then for a raster pass the attachments, stored only when the texture is imported or a later pass uses it, and a viewport and scissor over the render area
    void FrameGraph::RecordPass(RHI::CommandList& commands, std::uint32_t pass)
    {
        State& l_State = *m_State;
        const State::Pass& l_Pass = l_State.Passes[pass];
        const auto a_Accesses = std::span(l_State.Accesses).subspan(l_Pass.FirstAccess, l_Pass.AccessCount);

        for (const State::Access& it_Access : a_Accesses)
        {
            const bool l_Missing = it_Access.Buffer ? !l_State.Buffers[it_Access.Resource].Handle : !l_State.Textures[it_Access.Resource].Handle;
            if (l_Missing)
            {
                TR_CORE_ERROR("Frame graph: pass '{}' is skipped, since '{}' could not be created", l_Pass.Name, it_Access.Buffer ? l_State.Buffers[it_Access.Resource].Name : l_State.Textures[it_Access.Resource].Name);

                return;
            }
        }

        for (const State::Access& it_Access : a_Accesses)
        {
            if (it_Access.Buffer)
            {
                TransitionBuffer(commands, it_Access.Resource, it_Access.ResourceState);
            }
            else
            {
                TransitionTexture(commands, it_Access.Resource, it_Access.ResourceState);
            }
        }

        const auto a_Store = [&l_State, pass](std::uint32_t texture)
        {
            const State::Texture& l_Texture = l_State.Textures[texture];

            return l_Texture.Imported || (l_Texture.LastPass != c_NoIndex && l_Texture.LastPass > pass) ? RHI::StoreOp::Store : RHI::StoreOp::DontCare;
        };

        std::uint32_t l_Width = 0;
        std::uint32_t l_Height = 0;
        if (l_Pass.Type == FrameGraphPassType::Raster)
        {
            std::array<RHI::ColorAttachment, RHI::c_MaxColorAttachments> l_Colors{};
            for (std::uint32_t it_Color = 0; it_Color < l_Pass.ColorCount; ++it_Color)
            {
                const FrameGraphColorAttachment& l_Attachment = l_State.ColorAttachments[l_Pass.FirstColor + it_Color];
                const State::Texture& l_Texture = l_State.Textures[l_Attachment.Texture.Index];
                l_Colors[it_Color] = { l_Texture.Handle, l_Attachment.Load, a_Store(l_Attachment.Texture.Index), l_Attachment.ClearColor, l_Attachment.MipLevel, l_Attachment.ArrayLayer, l_Attachment.Resolve ? l_State.Textures[l_Attachment.Resolve.Index].Handle : RHI::TextureHandle{} };
                if (it_Color == 0)
                {
                    l_Width = RHI::GetMipSize(l_Texture.Description.Width, l_Attachment.MipLevel);
                    l_Height = RHI::GetMipSize(l_Texture.Description.Height, l_Attachment.MipLevel);
                }
            }

            RHI::RenderingDescription l_Rendering;
            l_Rendering.ColorAttachments = std::span(l_Colors.data(), l_Pass.ColorCount);
            if (l_Pass.Depth.Texture)
            {
                const State::Texture& l_Texture = l_State.Textures[l_Pass.Depth.Texture.Index];
                l_Rendering.Depth = { l_Texture.Handle, l_Pass.Depth.Load, a_Store(l_Pass.Depth.Texture.Index), l_Pass.Depth.ClearDepth, l_Pass.Depth.MipLevel, l_Pass.Depth.ArrayLayer };
                if (l_Pass.ColorCount == 0)
                {
                    l_Width = RHI::GetMipSize(l_Texture.Description.Width, l_Pass.Depth.MipLevel);
                    l_Height = RHI::GetMipSize(l_Texture.Description.Height, l_Pass.Depth.MipLevel);
                }
            }

            l_Rendering.RenderArea = { 0, 0, l_Width, l_Height };
            commands.BeginRendering(l_Rendering);
            commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(l_Width), static_cast<float>(l_Height), 0.0f, 1.0f });
            commands.SetScissor({ 0, 0, l_Width, l_Height });
        }

        if (l_Pass.Invoke != nullptr)
        {
            const FrameGraphContext l_Context(*this, commands, pass, l_Width, l_Height);
            l_Pass.Invoke(l_Pass.Callable, l_Context);
        }

        if (l_Pass.Type == FrameGraphPassType::Raster)
        {
            commands.EndRendering();
        }
    }

    // Reading in the state a resource is already in needs nothing. A write always gets a barrier, even in the same state, so it waits for whatever came before
    void FrameGraph::TransitionTexture(RHI::CommandList& commands, std::uint32_t texture, RHI::ResourceState state)
    {
        State::Texture& l_Texture = m_State->Textures[texture];
        if (l_Texture.CurrentState == state && !IsWriteState(state))
        {
            return;
        }

        commands.TextureBarrier(l_Texture.Handle, l_Texture.CurrentState, state);
        l_Texture.CurrentState = state;
        ++m_State->Stats.Barriers;
    }

    void FrameGraph::TransitionBuffer(RHI::CommandList& commands, std::uint32_t buffer, RHI::ResourceState state)
    {
        State::Buffer& l_Buffer = m_State->Buffers[buffer];
        if (l_Buffer.CurrentState == state && !IsWriteState(state))
        {
            return;
        }

        commands.BufferBarrier(l_Buffer.Handle, l_Buffer.CurrentState, state);
        l_Buffer.CurrentState = state;
        ++m_State->Stats.Barriers;
    }

    // Everything used this frame goes back to the pool for the next one, and the device keeps a destroyed resource until no frame in flight uses it
    void FrameGraph::ReleaseResources()
    {
        State& l_State = *m_State;
        const std::uint64_t l_Executions = l_State.Executions;

        for (State::PooledTexture& it_Texture : l_State.TexturePool)
        {
            it_Texture.InUse = false;
            if (l_Executions - it_Texture.LastUsed > c_PoolKeepExecutions)
            {
                m_Device.DestroyTexture(it_Texture.Handle);
                it_Texture.Handle = {};
            }
        }

        for (State::PooledBuffer& it_Buffer : l_State.BufferPool)
        {
            it_Buffer.InUse = false;
            if (l_Executions - it_Buffer.LastUsed > c_PoolKeepExecutions)
            {
                m_Device.DestroyBuffer(it_Buffer.Handle);
                it_Buffer.Handle = {};
            }
        }

        std::erase_if(l_State.TexturePool, [](const State::PooledTexture& pooled) { return !pooled.Handle; });
        std::erase_if(l_State.BufferPool, [](const State::PooledBuffer& pooled) { return !pooled.Handle; });

        l_State.Stats.PooledTextures = static_cast<std::uint32_t>(l_State.TexturePool.size());
        l_State.Stats.PooledBuffers = static_cast<std::uint32_t>(l_State.BufferPool.size());
    }
}