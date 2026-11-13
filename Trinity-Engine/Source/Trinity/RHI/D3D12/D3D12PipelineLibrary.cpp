#include "Trinity/RHI/D3D12/D3D12PipelineLibrary.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12Factory.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Log.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        using Microsoft::WRL::ComPtr;

        D3D12PipelineLibrary::D3D12PipelineLibrary(const D3D12Factory& factory, const D3D12Device& device) : m_Factory(factory), m_Device(device)
        {

        }

        D3D12PipelineLibrary::~D3D12PipelineLibrary()
        {
            Destroy();
        }

        // The runtime checks the adapter and driver that made the data too, refusing it with D3D12_ERROR_ADAPTER_NOT_FOUND or D3D12_ERROR_DRIVER_VERSION_MISMATCH
        bool D3D12PipelineLibrary::Initialize(std::string_view path, std::string& error)
        {
            DXGI_ADAPTER_DESC1 l_Adapter{};
            m_Factory.GetAdapter()->GetDesc1(&l_Adapter);

            // The user-mode driver's version, which DXGI gives for IDXGIDevice
            LARGE_INTEGER l_Driver{};
            static_cast<void>(m_Factory.GetAdapter()->CheckInterfaceSupport(__uuidof(IDXGIDevice), &l_Driver));

            PipelineCacheKey l_Key;
            l_Key.API = GraphicsAPI::D3D12;
            l_Key.VendorID = l_Adapter.VendorId;
            l_Key.DeviceID = l_Adapter.DeviceId;
            l_Key.DriverVersion = static_cast<std::uint64_t>(l_Driver.QuadPart);
            m_File.Open(path, l_Key);

            HRESULT l_Result = E_FAIL;
            const std::span<const std::byte> l_Data = m_File.GetData();
            if (!l_Data.empty())
            {
                l_Result = m_Device.GetHandle()->CreatePipelineLibrary(l_Data.data(), l_Data.size(), IID_PPV_ARGS(&m_Library));
                if (FAILED(l_Result))
                {
                    TR_CORE_INFO("D3D12: the runtime refused the pipeline cache '{}' ({}), so a new one is started", m_File.GetPath(), FormatResult(l_Result));
                }
            }

            if (FAILED(l_Result))
            {
                l_Result = m_Device.GetHandle()->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&m_Library));
            }

            if (l_Result == DXGI_ERROR_UNSUPPORTED)
            {
                TR_CORE_INFO("D3D12: this runtime has no pipeline libraries, so pipeline states are made without one");

                return true;
            }

            if (FAILED(l_Result))
            {
                error = std::format("the pipeline library could not be created on {} ({})", m_Device.GetInfo().AdapterName, FormatResult(l_Result));

                return false;
            }

            m_Library->SetName(L"Pipeline library");

            return true;
        }

        void D3D12PipelineLibrary::Destroy()
        {
            std::vector<std::byte> l_Data;
            if (m_Library && !m_File.GetPath().empty() && m_File.HasNewPipelines())
            {
                l_Data.resize(m_Library->GetSerializedSize());

                const HRESULT l_Result = m_Library->Serialize(l_Data.data(), l_Data.size());
                if (FAILED(l_Result))
                {
                    TR_CORE_WARN("D3D12: the pipeline library could not be serialized ({})", FormatResult(l_Result));
                    l_Data.clear();
                }
            }

            m_File.Close(l_Data);
            m_Library.Reset();
        }

        // A name the library has not got, or holds for another description, fails to load with E_INVALIDARG, and the pipeline state is made instead
        template<typename Load, typename Make>
        HRESULT D3D12PipelineLibrary::Create(const std::wstring& name, ComPtr<ID3D12PipelineState>& state, Load load, Make make)
        {
            const std::chrono::steady_clock::time_point l_Start = std::chrono::steady_clock::now();
            if (m_Library)
            {
                const std::scoped_lock l_Lock(m_Mutex);
                if (SUCCEEDED(load()))
                {
                    m_File.Record(std::chrono::steady_clock::now() - l_Start, true);

                    return S_OK;
                }
            }

            const HRESULT l_Result = make();
            if (FAILED(l_Result))
            {
                return l_Result;
            }

            m_File.Record(std::chrono::steady_clock::now() - l_Start, false);

            // Another thread may have stored the same pipeline state under the name since, which leaves the library as it would be
            if (m_Library)
            {
                const std::scoped_lock l_Lock(m_Mutex);
                static_cast<void>(m_Library->StorePipeline(name.c_str(), state.Get()));
            }

            return S_OK;
        }

        HRESULT D3D12PipelineLibrary::CreateGraphics(const std::wstring& name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& description, ComPtr<ID3D12PipelineState>& state)
        {
            return Create(name, state, [&]() { return m_Library->LoadGraphicsPipeline(name.c_str(), &description, IID_PPV_ARGS(&state)); }, [&]() { return m_Device.GetHandle()->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&state)); });
        }

        HRESULT D3D12PipelineLibrary::CreateCompute(const std::wstring& name, const D3D12_COMPUTE_PIPELINE_STATE_DESC& description, ComPtr<ID3D12PipelineState>& state)
        {
            return Create(name, state, [&]() { return m_Library->LoadComputePipeline(name.c_str(), &description, IID_PPV_ARGS(&state)); }, [&]() { return m_Device.GetHandle()->CreateComputePipelineState(&description, IID_PPV_ARGS(&state)); });
        }
    }
}