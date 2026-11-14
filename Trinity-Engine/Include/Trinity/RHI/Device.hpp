#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"
#include "Trinity/RHI/CommandList.hpp"
#include "Trinity/RHI/Pipeline.hpp"
#include "Trinity/RHI/Resources.hpp"
#include "Trinity/RHI/SwapChain.hpp"
#include "Trinity/RHI/Types.hpp"
#include "Trinity/RHI/UploadRing.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        struct DeviceSpecification
        {
            GraphicsAPI API = GraphicsAPI::None;

            bool EnableValidation = false;
            bool EnableGPUValidation = false;
            // Uploads go to a copy queue of their own where the GPU has one, or else to the graphics queue in a submission of their own. Off records them into the frame's commands
            bool EnableCopyQueue = true;

            // The file in the virtual file system the device keeps its pipeline cache in between runs, or none
            std::string PipelineCachePath;
        };

        struct DeviceInfo
        {
            GraphicsAPI API = GraphicsAPI::None;
            std::string AdapterName;
            std::uint64_t VideoMemoryBytes = 0;
        };

        // A heap of GPU memory, with how much of it the process may use before the driver starts moving memory out and how much it uses. Without FromDriver the driver reports neither, and only the size is known
        struct MemoryHeapBudget
        {
            std::uint64_t Size = 0;
            std::uint64_t Budget = 0;
            std::uint64_t Usage = 0;
            bool DeviceLocal = false;
            bool FromDriver = false;
        };

        class TRINITY_API Device
        {
        public:
            virtual ~Device() = default;

            Device(const Device&) = delete;
            Device& operator=(const Device&) = delete;

            [[nodiscard]] virtual const DeviceInfo& GetInfo() const = 0;
            // Each heap's budget and use, which the device fetches from the driver once a frame. Null reports none
            [[nodiscard]] virtual std::vector<MemoryHeapBudget> GetMemoryBudget() const = 0;

            [[nodiscard]] virtual BufferHandle CreateBuffer(const BufferDescription& description) = 0;
            virtual void DestroyBuffer(BufferHandle buffer) = 0;

            [[nodiscard]] virtual std::span<std::byte> GetMappedData(BufferHandle buffer) = 0;

            // Whether a texture of this format can be created with every usage asked for and this many samples. CreateTexture refuses one that cannot, returning an invalid handle
            [[nodiscard]] virtual bool IsFormatSupported(Format format, TextureUsage usage, std::uint32_t sampleCount = 1) const = 0;

            [[nodiscard]] virtual TextureHandle CreateTexture(const TextureDescription& description) = 0;
            virtual void DestroyTexture(TextureHandle texture) = 0;

            [[nodiscard]] virtual std::uint32_t GetShaderResourceIndex(BufferHandle buffer) = 0;
            [[nodiscard]] virtual std::uint32_t GetUnorderedAccessIndex(BufferHandle buffer) = 0;
            [[nodiscard]] virtual std::uint32_t GetShaderResourceIndex(TextureHandle texture) = 0;
            [[nodiscard]] virtual std::uint32_t GetUnorderedAccessIndex(TextureHandle texture) = 0;

            // Every sampler gets an index into the bindless sampler heap, separate from the resource indices
            [[nodiscard]] virtual SamplerHandle CreateSampler(const SamplerDescription& description) = 0;
            virtual void DestroySampler(SamplerHandle sampler) = 0;
            [[nodiscard]] virtual std::uint32_t GetSamplerIndex(SamplerHandle sampler) = 0;

            [[nodiscard]] virtual QueryPoolHandle CreateQueryPool(const QueryPoolDescription& description) = 0;
            virtual void DestroyQueryPool(QueryPoolHandle pool) = 0;

            // Timestamp ticks per second
            [[nodiscard]] virtual std::uint64_t GetTimestampFrequency() const = 0;

            [[nodiscard]] virtual PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescription& description) = 0;
            [[nodiscard]] virtual PipelineHandle CreateComputePipeline(const ComputePipelineDescription& description) = 0;
            virtual void DestroyPipeline(PipelineHandle pipeline) = 0;

            [[nodiscard]] virtual Scope<SwapChain> CreateSwapChain(const SwapChainSpecification& specification) = 0;

            // Only between BeginFrame and EndFrame, and valid until that frame has finished on the GPU
            [[nodiscard]] UploadAllocation AllocateUpload(std::uint64_t size, std::uint64_t alignment);
            [[nodiscard]] std::uint64_t GetUploadCapacity() const;

            [[nodiscard]] virtual CommandList& BeginFrame() = 0;

            // The list the loaders record their uploads into, from BeginFrame until SubmitUploads: the copy queue's, or the frame's own when renderer.copy_queue is off. Only copies and barriers, and what follows a copy, as a texture moving on to ShaderResource, the frame's list records once the uploads are submitted
            [[nodiscard]] virtual CommandList& GetUploadCommands() = 0;
            // Sends the uploads to their queue now, so they run while the rest of the frame is recorded, and has the frame's commands wait for them. EndFrame does it when this was not called, and a frame that uploaded nothing submits nothing
            virtual void SubmitUploads() = 0;

            virtual void EndFrame() = 0;
            virtual void WaitIdle() = 0;

        protected:
            Device() = default;

            // The format support and block size every backend checks before creating a texture. Logs why when it refuses
            [[nodiscard]] bool CanCreateTexture(const TextureDescription& description) const;

            UploadRing m_UploadRing;
        };

        [[nodiscard]] TRINITY_API Expected<Scope<Device>, std::string> CreateDevice(const DeviceSpecification& specification);
        // One line for a statistics overlay, such as "Video memory: 1.25 GiB of 6.50 GiB budget, 8.00 GiB heap", or the heap's size alone without the driver's numbers
        [[nodiscard]] TRINITY_API std::string DescribeMemoryHeap(const MemoryHeapBudget& heap);
    }
}