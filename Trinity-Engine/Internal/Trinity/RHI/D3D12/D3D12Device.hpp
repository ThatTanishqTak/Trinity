#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/Device.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace Trinity
{
    namespace RHI
    {
        class D3D12Factory;

        // The first adapter in the factory's order whose device has feature level 12_0, Shader Model 6.6, resource binding tier 3 and enhanced barriers, that device, and with validation the info queue's callback, whose warnings and errors reach the log and are counted, and the report of what outlived it
        class D3D12Device
        {
        public:
            explicit D3D12Device(D3D12Factory& factory);
            ~D3D12Device();

            // False, with the reason in error, when no adapter can run the engine
            [[nodiscard]] bool Initialize(std::string& error);
            // The message callback, the info queue, then the device. Does nothing a second time
            void Reset();
            void ReportLiveObjects() const;

            [[nodiscard]] const DeviceInfo& GetInfo() const { return m_Info; }
            [[nodiscard]] ID3D12Device10* GetHandle() const { return m_Device.Get(); }
            [[nodiscard]] std::uint32_t GetMessageCount() const { return m_MessageCount.load(); }

        private:
            static void CALLBACK OnDebugMessage(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context);

            void EnableDebugMessages();

            D3D12Factory& m_Factory;

            DeviceInfo m_Info;
            Microsoft::WRL::ComPtr<ID3D12Device10> m_Device;
            Microsoft::WRL::ComPtr<ID3D12InfoQueue1> m_InfoQueue;
            DWORD m_MessageCallbackCookie = 0;
            std::atomic<std::uint32_t> m_MessageCount{ 0 };
        };
    }
}