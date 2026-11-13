#include "Trinity/RHI/D3D12/D3D12CommandQueue.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"

#include <cstdint>

namespace Trinity
{
    namespace RHI
    {
        D3D12CommandQueue::D3D12CommandQueue(const D3D12Device& device) : m_Device(device)
        {

        }

        HRESULT D3D12CommandQueue::Initialize()
        {
            D3D12_COMMAND_QUEUE_DESC l_QueueDescription{};
            l_QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

            HRESULT l_Result = m_Device.GetHandle()->CreateCommandQueue(&l_QueueDescription, IID_PPV_ARGS(&m_Queue));
            if (SUCCEEDED(l_Result))
            {
                l_Result = m_Device.GetHandle()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
            }

            if (SUCCEEDED(l_Result))
            {
                m_Queue->SetName(L"Trinity direct queue");
            }

            return l_Result;
        }

        bool D3D12CommandQueue::CreateFenceEvent()
        {
            m_FenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);

            return m_FenceEvent != nullptr;
        }

        void D3D12CommandQueue::Reset()
        {
            m_Fence.Reset();
            m_Queue.Reset();
            if (m_FenceEvent != nullptr)
            {
                ::CloseHandle(m_FenceEvent);
            }
        }

        void D3D12CommandQueue::WaitForFence(std::uint64_t value)
        {
            if (m_Fence->GetCompletedValue() < value && SUCCEEDED(m_Fence->SetEventOnCompletion(value, m_FenceEvent)))
            {
                ::WaitForSingleObject(m_FenceEvent, INFINITE);
            }
        }

        // Presents go on the queue after the frame's signal, so waiting for that signal alone can leave a swap chain buffer in use. A new signal comes after them
        void D3D12CommandQueue::WaitIdle()
        {
            m_Queue->Signal(m_Fence.Get(), ++m_FenceValue);
            WaitForFence(m_FenceValue);
        }

        std::uint64_t D3D12CommandQueue::Submit(ID3D12CommandList* commandList)
        {
            ID3D12CommandList* const l_Lists[] = { commandList };
            m_Queue->ExecuteCommandLists(1, l_Lists);

            const std::uint64_t l_Value = ++m_FenceValue;
            m_Queue->Signal(m_Fence.Get(), l_Value);

            return l_Value;
        }
    }
}