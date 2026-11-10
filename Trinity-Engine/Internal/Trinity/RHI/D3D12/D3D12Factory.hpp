#pragma once

#include "Trinity/RHI/D3D12/D3D12Headers.hpp"
#include "Trinity/RHI/Device.hpp"

#include <string>

namespace Trinity
{
    namespace RHI
    {
        // The DXGI factory, with the debug layer before it when validation is asked for, the adapters it offers in order of preference for performance, the adapter chosen, and whether presents can tear
        class D3D12Factory
        {
        public:
            // False, with the reason in error, when the DXGI factory cannot be created
            [[nodiscard]] bool Initialize(const DeviceSpecification& specification, std::string& error);
            // The adapter, then the factory, once the device is gone, so the live-object report that follows finds nothing of theirs
            void Reset();

            // The adapter at a place in the order of preference for performance, or false past the last
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