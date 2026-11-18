#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/Device.hpp"

#include <string>

namespace Trinity
{
    namespace RHI
    {
        // Adapters come in order of preference for performance
        class D3D12Factory
        {
        public:
            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            // Releases the adapter, then the factory, after the device, so the live-object report is clean
            void Reset();

            // False past the last adapter
            [[nodiscard]] bool EnumerateAdapter(UINT index, Microsoft::WRL::ComPtr<IDXGIAdapter1>& adapter) const;
            void SetAdapter(const Microsoft::WRL::ComPtr<IDXGIAdapter1>& adapter) { m_Adapter = adapter; }
            void LogRuntime() const;

            [[nodiscard]] IDXGIFactory6* GetHandle() const { return m_Factory.Get(); }
            [[nodiscard]] IDXGIAdapter1* GetAdapter() const { return m_Adapter.Get(); }
            [[nodiscard]] bool IsValidationEnabled() const { return m_Validation; }
            [[nodiscard]] bool IsTearingSupported() const { return m_TearingSupported; }

        private:
            bool m_Validation = false;
            bool m_TearingSupported = false;

            Microsoft::WRL::ComPtr<IDXGIFactory6> m_Factory;
            Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
        };
    }
}