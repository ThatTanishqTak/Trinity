#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Renderer/GraphicsAPI.hpp"
#include "Trinity/RHI/CommandList.hpp"
#include "Trinity/RHI/Pipeline.hpp"
#include "Trinity/RHI/Resources.hpp"
#include "Trinity/RHI/SwapChain.hpp"
#include "Trinity/RHI/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Trinity
{
    namespace RHI
    {
        struct DeviceSpecification
        {
            GraphicsAPI API = GraphicsAPI::None;

            bool EnableValidation = false;
        };

        struct DeviceInfo
        {
            GraphicsAPI API = GraphicsAPI::None;
            std::string AdapterName;
            std::uint64_t VideoMemoryBytes = 0;
        };

        class TRINITY_API Device
        {
        public:
            virtual ~Device() = default;

            Device(const Device&) = delete;
            Device& operator=(const Device&) = delete;

            [[nodiscard]] virtual const DeviceInfo& GetInfo() const = 0;

            [[nodiscard]] virtual BufferHandle CreateBuffer(const BufferDescription& description) = 0;
            virtual void DestroyBuffer(BufferHandle buffer) = 0;

            [[nodiscard]] virtual std::span<std::byte> GetMappedData(BufferHandle buffer) = 0;

            [[nodiscard]] virtual TextureHandle CreateTexture(const TextureDescription& description) = 0;
            virtual void DestroyTexture(TextureHandle texture) = 0;

            [[nodiscard]] virtual PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescription& description) = 0;
            virtual void DestroyPipeline(PipelineHandle pipeline) = 0;

            [[nodiscard]] virtual Scope<SwapChain> CreateSwapChain(const SwapChainSpecification& specification) = 0;

            [[nodiscard]] virtual CommandList& BeginFrame() = 0;

            virtual void EndFrame() = 0;
            virtual void WaitIdle() = 0;

        protected:
            Device() = default;
        };

        [[nodiscard]] TRINITY_API Expected<Scope<Device>, std::string> CreateDevice(const DeviceSpecification& specification);
    }
}