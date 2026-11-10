#include "Trinity/RHI/D3D12/D3D12Factory.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Platform.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <system_error>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            using Microsoft::WRL::ComPtr;

            std::filesystem::path GetModulePath(HMODULE module)
            {
                std::wstring l_Path(MAX_PATH, L'\0');
                while (true)
                {
                    const DWORD l_Length = ::GetModuleFileNameW(module, l_Path.data(), static_cast<DWORD>(l_Path.size()));
                    if (l_Length == 0)
                    {
                        return {};
                    }

                    if (l_Length < l_Path.size())
                    {
                        l_Path.resize(l_Length);

                        return l_Path;
                    }

                    l_Path.resize(l_Path.size() * 2);
                }
            }
        }

        bool D3D12Factory::Initialize(const DeviceSpecification& specification, std::string& error)
        {
            m_Validation = specification.EnableValidation;

            // The debug layer must be on before the device exists
            UINT l_FactoryFlags = 0;
            if (m_Validation)
            {
                ComPtr<ID3D12Debug1> l_Debug;
                if (SUCCEEDED(::D3D12GetDebugInterface(IID_PPV_ARGS(&l_Debug))))
                {
                    l_Debug->EnableDebugLayer();
                    l_Debug->SetEnableGPUBasedValidation(specification.EnableGPUValidation ? TRUE : FALSE);
                    l_FactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

                    TR_CORE_INFO("D3D12: debug layer on{}", specification.EnableGPUValidation ? ", with GPU-based validation" : "");
                }
                else
                {
                    TR_CORE_WARN("D3D12: the debug layer is missing, so validation is off. Add the Graphics Tools optional feature in Windows settings");
                    m_Validation = false;
                }
            }

            const HRESULT l_Result = ::CreateDXGIFactory2(l_FactoryFlags, IID_PPV_ARGS(&m_Factory));
            if (FAILED(l_Result))
            {
                error = std::format("CreateDXGIFactory2 failed with {}", FormatResult(l_Result));

                return false;
            }

            BOOL l_Tearing = FALSE;
            m_TearingSupported = SUCCEEDED(m_Factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &l_Tearing, sizeof(l_Tearing))) && l_Tearing == TRUE;

            return true;
        }

        void D3D12Factory::Reset()
        {
            m_Adapter.Reset();
            m_Factory.Reset();
        }

        bool D3D12Factory::EnumerateAdapter(UINT index, ComPtr<IDXGIAdapter1>& adapter) const
        {
            return m_Factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
        }

        // Says which D3D12Core.dll the process loaded: the Agility SDK's beside the executable, or the one built into Windows
        void D3D12Factory::LogRuntime() const
        {
            const std::filesystem::path l_Path = GetModulePath(::GetModuleHandleW(L"D3D12Core.dll"));

            std::error_code l_Error;
            [[maybe_unused]] const bool l_Agility = !l_Path.empty() && std::filesystem::equivalent(l_Path.parent_path(), Platform::GetExecutableDirectory() / "D3D12", l_Error);

#if defined(TR_AGILITY_SDK_VERSION)
            if (l_Agility)
            {
                TR_CORE_INFO("D3D12: Agility SDK {} runtime loaded from '{}'", TR_AGILITY_SDK_VERSION, l_Path.string());
            }
            else
            {
                TR_CORE_WARN("D3D12: the Agility SDK {} runtime beside the executable was not loaded, so the Windows runtime at '{}' is in use", TR_AGILITY_SDK_VERSION, l_Path.string());
            }
#else
            TR_CORE_INFO("D3D12: this build has no Agility SDK, so the Windows runtime at '{}' is in use", l_Path.string());
#endif
        }
    }
}