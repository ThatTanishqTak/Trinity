#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12Factory.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            using Microsoft::WRL::ComPtr;

            struct AdapterSupport
            {
                D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_12_0;
                D3D_SHADER_MODEL ShaderModel = D3D_SHADER_MODEL_5_1;
                D3D12_RESOURCE_BINDING_TIER BindingTier = D3D12_RESOURCE_BINDING_TIER_1;
                bool EnhancedBarriers = false;
            };

            std::string ToUtf8(std::wstring_view text)
            {
                if (text.empty())
                {
                    return {};
                }

                const int l_Size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
                std::string l_Result(static_cast<std::size_t>(l_Size), '\0');
                ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), l_Result.data(), l_Size, nullptr, nullptr);

                return l_Result;
            }

            std::string FormatFeatureLevel(D3D_FEATURE_LEVEL level)
            {
                const std::uint32_t l_Level = static_cast<std::uint32_t>(level);

                return std::format("{}_{}", l_Level >> 12, (l_Level >> 8) & 0xF);
            }

            std::string FormatShaderModel(D3D_SHADER_MODEL model)
            {
                const std::uint32_t l_Model = static_cast<std::uint32_t>(model);

                return std::format("{}.{}", l_Model >> 4, l_Model & 0xF);
            }

            AdapterSupport QuerySupport(ID3D12Device* device)
            {
                AdapterSupport l_Support;

                const std::array<D3D_FEATURE_LEVEL, 3> l_RequestedLevels{ D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2 };
                D3D12_FEATURE_DATA_FEATURE_LEVELS l_Levels{};
                l_Levels.NumFeatureLevels = static_cast<UINT>(l_RequestedLevels.size());
                l_Levels.pFeatureLevelsRequested = l_RequestedLevels.data();
                if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &l_Levels, sizeof(l_Levels))))
                {
                    l_Support.FeatureLevel = l_Levels.MaxSupportedFeatureLevel;
                }

                // A runtime rejects shader models newer than it knows, so start at the newest these headers know and step down
                for (std::uint32_t it_Model = static_cast<std::uint32_t>(D3D_HIGHEST_SHADER_MODEL); it_Model >= static_cast<std::uint32_t>(D3D_SHADER_MODEL_6_0); --it_Model)
                {
                    D3D12_FEATURE_DATA_SHADER_MODEL l_ShaderModel{ static_cast<D3D_SHADER_MODEL>(it_Model) };
                    const HRESULT l_Result = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &l_ShaderModel, sizeof(l_ShaderModel));
                    if (SUCCEEDED(l_Result))
                    {
                        l_Support.ShaderModel = l_ShaderModel.HighestShaderModel;

                        break;
                    }

                    if (l_Result != E_INVALIDARG)
                    {
                        break;
                    }
                }

                D3D12_FEATURE_DATA_D3D12_OPTIONS l_Options{};
                if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &l_Options, sizeof(l_Options))))
                {
                    l_Support.BindingTier = l_Options.ResourceBindingTier;
                }

                D3D12_FEATURE_DATA_D3D12_OPTIONS12 l_Options12{};
                if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &l_Options12, sizeof(l_Options12))))
                {
                    l_Support.EnhancedBarriers = l_Options12.EnhancedBarriersSupported != FALSE;
                }

                return l_Support;
            }

            // Empty when the adapter has everything the engine is built on
            std::string GetMissingFeatures(const AdapterSupport& support)
            {
                std::string l_Missing;
                const auto a_Add = [&l_Missing](std::string_view feature)
                {
                    l_Missing += l_Missing.empty() ? "" : ", ";
                    l_Missing += feature;
                };

                if (support.ShaderModel < D3D_SHADER_MODEL_6_6)
                {
                    a_Add("Shader Model 6.6");
                }

                if (support.BindingTier < D3D12_RESOURCE_BINDING_TIER_3)
                {
                    a_Add("resource binding tier 3");
                }

                if (!support.EnhancedBarriers)
                {
                    a_Add("enhanced barriers");
                }

                return l_Missing;
            }
        }

        D3D12Device::D3D12Device(D3D12Factory& factory) : m_Factory(factory)
        {

        }

        D3D12Device::~D3D12Device()
        {
            Reset();
        }

        bool D3D12Device::Initialize(std::string& error)
        {
            std::string l_Skipped;
            ComPtr<IDXGIAdapter1> l_Adapter;
            for (UINT it_Index = 0; m_Factory.EnumerateAdapter(it_Index, l_Adapter); ++it_Index)
            {
                DXGI_ADAPTER_DESC1 l_Description{};
                l_Adapter->GetDesc1(&l_Description);
                if ((l_Description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                {
                    continue;
                }

                const std::string l_Name = ToUtf8(l_Description.Description);

                ComPtr<ID3D12Device10> l_Device;
                const HRESULT l_Result = ::D3D12CreateDevice(l_Adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&l_Device));
                if (FAILED(l_Result))
                {
                    l_Skipped += std::format("{}{}: no feature level 12_0 device ({})", l_Skipped.empty() ? "" : "; ", l_Name, FormatResult(l_Result));

                    continue;
                }

                const AdapterSupport l_Support = QuerySupport(l_Device.Get());
                const std::string l_Missing = GetMissingFeatures(l_Support);
                if (!l_Missing.empty())
                {
                    l_Skipped += std::format("{}{}: no {}", l_Skipped.empty() ? "" : "; ", l_Name, l_Missing);

                    continue;
                }

                m_Factory.SetAdapter(l_Adapter);
                m_Device = l_Device;

                m_Info.API = GraphicsAPI::D3D12;
                m_Info.AdapterName = l_Name;
                m_Info.VideoMemoryBytes = l_Description.DedicatedVideoMemory;

                TR_CORE_INFO("D3D12: {} with {} of video memory: feature level {}, Shader Model {}, resource binding tier {}, enhanced barriers", l_Name, Memory::FormatBytes(m_Info.VideoMemoryBytes), FormatFeatureLevel(l_Support.FeatureLevel), FormatShaderModel(l_Support.ShaderModel), static_cast<int>(l_Support.BindingTier));

                break;
            }

            if (!m_Device)
            {
                error = l_Skipped.empty() ? std::string("no hardware adapter was found") : std::format("no adapter has what the engine needs ({})", l_Skipped);

                return false;
            }

            EnableDebugMessages();

            return true;
        }

        void D3D12Device::Reset()
        {
            if (m_InfoQueue && m_MessageCallbackCookie != 0)
            {
                m_InfoQueue->UnregisterMessageCallback(m_MessageCallbackCookie);
            }

            m_InfoQueue.Reset();
            m_Device.Reset();
        }

        void D3D12Device::EnableDebugMessages()
        {
            if (!m_Factory.IsValidationEnabled())
            {
                return;
            }

            if (FAILED(m_Device.As(&m_InfoQueue)))
            {
                TR_CORE_WARN("D3D12: this runtime has no ID3D12InfoQueue1, so debug layer messages only reach an attached debugger");

                return;
            }

            if (FAILED(m_InfoQueue->RegisterMessageCallback(&D3D12Device::OnDebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &m_MessageCallbackCookie)))
            {
                TR_CORE_WARN("D3D12: the debug layer message callback could not be registered, so messages only reach an attached debugger");
                m_InfoQueue.Reset();
            }
        }

        // The debug layer calls this from whichever thread made the call it reports on
        void CALLBACK D3D12Device::OnDebugMessage([[maybe_unused]] D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context)
        {
            D3D12Device* l_Device = static_cast<D3D12Device*>(context);

            switch (severity)
            {
                case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                case D3D12_MESSAGE_SEVERITY_ERROR:
                {
                    l_Device->m_MessageCount.fetch_add(1, std::memory_order_relaxed);
                    TR_CORE_ERROR("D3D12 debug layer: {} (message {})", description, static_cast<int>(id));

                    break;
                }
                case D3D12_MESSAGE_SEVERITY_WARNING:
                {
                    l_Device->m_MessageCount.fetch_add(1, std::memory_order_relaxed);
                    TR_CORE_WARN("D3D12 debug layer: {} (message {})", description, static_cast<int>(id));

                    break;
                }
                case D3D12_MESSAGE_SEVERITY_INFO:
                case D3D12_MESSAGE_SEVERITY_MESSAGE:
                {
                    TR_CORE_TRACE("D3D12 debug layer: {}", description);

                    break;
                }
            }
        }

        // Runs after every D3D12 and DXGI object of the device is released, so anything still reported is a leak
        void D3D12Device::ReportLiveObjects() const
        {
            ComPtr<IDXGIDebug1> l_Debug;
            ComPtr<IDXGIInfoQueue> l_Queue;
            if (FAILED(::DXGIGetDebugInterface1(0, IID_PPV_ARGS(&l_Debug))) || FAILED(::DXGIGetDebugInterface1(0, IID_PPV_ARGS(&l_Queue))))
            {
                return;
            }

            l_Debug->ReportLiveObjects(DXGI_DEBUG_ALL, static_cast<DXGI_DEBUG_RLO_FLAGS>(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));

            const UINT64 l_Count = l_Queue->GetNumStoredMessages(DXGI_DEBUG_ALL);
            for (UINT64 it_Index = 0; it_Index < l_Count; ++it_Index)
            {
                SIZE_T l_Size = 0;
                l_Queue->GetMessage(DXGI_DEBUG_ALL, it_Index, nullptr, &l_Size);

                std::vector<std::byte> l_Storage(l_Size);
                DXGI_INFO_QUEUE_MESSAGE* l_Message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(l_Storage.data());
                if (SUCCEEDED(l_Queue->GetMessage(DXGI_DEBUG_ALL, it_Index, l_Message, &l_Size)))
                {
                    TR_CORE_WARN("DXGI debug: {}", std::string_view(l_Message->pDescription, l_Message->DescriptionByteLength > 0 ? l_Message->DescriptionByteLength - 1 : 0));
                }
            }

            l_Queue->ClearStoredMessages(DXGI_DEBUG_ALL);

            if (l_Count == 0)
            {
                TR_CORE_INFO("D3D12: no live objects after the device was destroyed");
            }
        }
    }
}