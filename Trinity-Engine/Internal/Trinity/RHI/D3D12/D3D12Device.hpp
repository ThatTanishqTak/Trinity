#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/Device.hpp"
#include "Trinity/RHI/HandlePool.hpp"
#include "Trinity/RHI/ReleaseQueue.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        class D3D12CommandList final : public CommandList
        {
        public:
            D3D12CommandList() = default;

            void TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after) override;
            void BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after) override;

            void BeginRendering(const RenderingDescription& description) override;
            void EndRendering() override;

            void SetPipeline(PipelineHandle pipeline) override;
            void SetViewport(const Viewport& viewport) override;
            void SetScissor(const Rect& scissor) override;
            void PushConstants(std::span<const std::byte> data) override;
            void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) override;

            void CopyBuffer(BufferHandle source, std::uint64_t sourceOffset, BufferHandle destination, std::uint64_t destinationOffset, std::uint64_t size) override;
            void CopyTextureToBuffer(TextureHandle source, BufferHandle destination) override;
        };

        class D3D12Device final : public Device
        {
        public:
            // Null, with the reason in error, when no adapter can run the engine
            [[nodiscard]] static Scope<D3D12Device> Create(const DeviceSpecification& specification, std::string& error);

            D3D12Device() = default;
            ~D3D12Device() override;

            [[nodiscard]] const DeviceInfo& GetInfo() const override { return m_Info; }

            [[nodiscard]] BufferHandle CreateBuffer(const BufferDescription& description) override;
            void DestroyBuffer(BufferHandle buffer) override;
            [[nodiscard]] std::span<std::byte> GetMappedData(BufferHandle buffer) override;

            [[nodiscard]] TextureHandle CreateTexture(const TextureDescription& description) override;
            void DestroyTexture(TextureHandle texture) override;

            [[nodiscard]] PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescription& description) override;
            void DestroyPipeline(PipelineHandle pipeline) override;

            [[nodiscard]] Scope<SwapChain> CreateSwapChain(const SwapChainSpecification& specification) override;

            [[nodiscard]] CommandList& BeginFrame() override;
            void EndFrame() override;
            void WaitIdle() override;

        private:
            struct D3D12Buffer
            {
                Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
                std::byte* Mapped = nullptr;
                std::uint64_t Size = 0;
            };

            struct D3D12Texture
            {
                Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
                DXGI_FORMAT ResourceFormat = DXGI_FORMAT_UNKNOWN;
                std::uint32_t Width = 0;
                std::uint32_t Height = 0;
                std::uint32_t MipLevels = 0;
            };

            static void CALLBACK OnDebugMessage(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context);

            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            [[nodiscard]] bool CreateAllocator(std::string& error);
            void EnableDebugMessages();
            void LogRuntime() const;
            void ReportLiveObjects() const;

            DeviceInfo m_Info;
            bool m_Validation = false;

            Microsoft::WRL::ComPtr<IDXGIFactory6> m_Factory;
            Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
            Microsoft::WRL::ComPtr<ID3D12Device10> m_Device;
            Microsoft::WRL::ComPtr<ID3D12InfoQueue1> m_InfoQueue;
            DWORD m_MessageCallbackCookie = 0;
            std::atomic<std::uint32_t> m_MessageCount{ 0 };
            Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_Allocator;

            HandlePool<D3D12Buffer, BufferHandle> m_Buffers;
            HandlePool<D3D12Texture, TextureHandle> m_Textures;
            ReleaseQueue<Microsoft::WRL::ComPtr<D3D12MA::Allocation>> m_Releases;

            D3D12CommandList m_CommandList;
            bool m_InFrame = false;
        };
    }
}