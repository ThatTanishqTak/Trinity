#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"

#include <cstdint>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Device;

        // The direct queue, with the fence its submissions signal and the event a wait for the fence blocks on
        class D3D12CommandQueue
        {
        public:
            explicit D3D12CommandQueue(const D3D12Device& device);

            // S_OK, or the failure creating the queue or its fence
            [[nodiscard]] HRESULT Initialize();
            // False when the event cannot be created
            [[nodiscard]] bool CreateFenceEvent();
            void Reset();

            // Blocks until the fence reaches the value
            void WaitForFence(std::uint64_t value);
            // Blocks until everything submitted so far has finished, presents included
            void WaitIdle();
            // The list after everything submitted before it, returning the value the fence reaches once it has finished
            [[nodiscard]] std::uint64_t Submit(ID3D12CommandList* commandList);

            [[nodiscard]] ID3D12CommandQueue* GetHandle() const { return m_Queue.Get(); }
            // Whether the queue and its fence were both created, so the queue can be waited on
            [[nodiscard]] bool IsReady() const { return m_Fence && m_Queue; }

        private:
            const D3D12Device& m_Device;

            Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_Queue;
            Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
            HANDLE m_FenceEvent = nullptr;
            std::uint64_t m_FenceValue = 0;
        };
    }
}