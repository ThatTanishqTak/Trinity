#include "Trinity/RHI/D3D12/D3D12Device.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Platform.hpp"

#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
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

            std::string FormatResult(HRESULT result)
            {
                return std::format("HRESULT 0x{:08X}", static_cast<std::uint32_t>(result));
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

            void ReportMissing(std::string_view feature, int step)
            {
                TR_CORE_ERROR("D3D12: {} arrives in M2 step {}", feature, step);
            }
        }

        void D3D12CommandList::TextureBarrier([[maybe_unused]] TextureHandle texture, [[maybe_unused]] ResourceState before, [[maybe_unused]] ResourceState after)
        {
            ReportMissing("TextureBarrier", 10);
        }

        void D3D12CommandList::BufferBarrier([[maybe_unused]] BufferHandle buffer, [[maybe_unused]] ResourceState before, [[maybe_unused]] ResourceState after)
        {
            ReportMissing("BufferBarrier", 10);
        }

        void D3D12CommandList::BeginRendering([[maybe_unused]] const RenderingDescription& description)
        {
            ReportMissing("BeginRendering", 10);
        }

        void D3D12CommandList::EndRendering()
        {
            ReportMissing("EndRendering", 10);
        }

        void D3D12CommandList::SetPipeline([[maybe_unused]] PipelineHandle pipeline)
        {
            ReportMissing("SetPipeline", 12);
        }

        void D3D12CommandList::SetViewport([[maybe_unused]] const Viewport& viewport)
        {
            ReportMissing("SetViewport", 12);
        }

        void D3D12CommandList::SetScissor([[maybe_unused]] const Rect& scissor)
        {
            ReportMissing("SetScissor", 12);
        }

        void D3D12CommandList::PushConstants([[maybe_unused]] std::span<const std::byte> data)
        {
            ReportMissing("PushConstants", 12);
        }

        void D3D12CommandList::Draw([[maybe_unused]] std::uint32_t vertexCount, [[maybe_unused]] std::uint32_t instanceCount, [[maybe_unused]] std::uint32_t firstVertex, [[maybe_unused]] std::uint32_t firstInstance)
        {
            ReportMissing("Draw", 12);
        }

        void D3D12CommandList::CopyBuffer([[maybe_unused]] BufferHandle source, [[maybe_unused]] std::uint64_t sourceOffset, [[maybe_unused]] BufferHandle destination, [[maybe_unused]] std::uint64_t destinationOffset, [[maybe_unused]] std::uint64_t size)
        {
            ReportMissing("CopyBuffer", 10);
        }

        void D3D12CommandList::CopyTextureToBuffer([[maybe_unused]] TextureHandle source, [[maybe_unused]] BufferHandle destination)
        {
            ReportMissing("CopyTextureToBuffer", 13);
        }

        Scope<D3D12Device> D3D12Device::Create(const DeviceSpecification& specification, std::string& error)
        {
            Scope<D3D12Device> l_Device = CreateScope<D3D12Device>();
            if (!l_Device->Initialize(specification, error))
            {
                return nullptr;
            }

            return l_Device;
        }

        D3D12Device::~D3D12Device()
        {
            if (m_InfoQueue && m_MessageCallbackCookie != 0)
            {
                m_InfoQueue->UnregisterMessageCallback(m_MessageCallbackCookie);
            }

            const bool l_Report = m_Validation && m_Device;

            m_InfoQueue.Reset();
            m_Device.Reset();
            m_Adapter.Reset();
            m_Factory.Reset();

            if (l_Report)
            {
                TR_CORE_INFO("D3D12 debug layer: {} warning(s) or error(s) while the device lived", m_MessageCount.load());
                ReportLiveObjects();
            }
        }

        bool D3D12Device::Initialize(const DeviceSpecification& specification, std::string& error)
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

            HRESULT l_Result = ::CreateDXGIFactory2(l_FactoryFlags, IID_PPV_ARGS(&m_Factory));
            if (FAILED(l_Result))
            {
                error = std::format("CreateDXGIFactory2 failed with {}", FormatResult(l_Result));

                return false;
            }

            std::string l_Skipped;
            ComPtr<IDXGIAdapter1> l_Adapter;
            for (UINT it_Index = 0; m_Factory->EnumAdapterByGpuPreference(it_Index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&l_Adapter)) != DXGI_ERROR_NOT_FOUND; ++it_Index)
            {
                DXGI_ADAPTER_DESC1 l_Description{};
                l_Adapter->GetDesc1(&l_Description);
                if ((l_Description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                {
                    continue;
                }

                const std::string l_Name = ToUtf8(l_Description.Description);

                ComPtr<ID3D12Device10> l_Device;
                l_Result = ::D3D12CreateDevice(l_Adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&l_Device));
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

                m_Adapter = l_Adapter;
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
            LogRuntime();

            return true;
        }

        void D3D12Device::EnableDebugMessages()
        {
            if (!m_Validation)
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

        // Says which D3D12Core.dll the process loaded: the Agility SDK's beside the executable, or the one built into Windows
        void D3D12Device::LogRuntime() const
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

        BufferHandle D3D12Device::CreateBuffer([[maybe_unused]] const BufferDescription& description)
        {
            ReportMissing("CreateBuffer", 9);

            return {};
        }

        void D3D12Device::DestroyBuffer([[maybe_unused]] BufferHandle buffer)
        {

        }

        std::span<std::byte> D3D12Device::GetMappedData([[maybe_unused]] BufferHandle buffer)
        {
            return {};
        }

        TextureHandle D3D12Device::CreateTexture([[maybe_unused]] const TextureDescription& description)
        {
            ReportMissing("CreateTexture", 9);

            return {};
        }

        void D3D12Device::DestroyTexture([[maybe_unused]] TextureHandle texture)
        {

        }

        PipelineHandle D3D12Device::CreateGraphicsPipeline([[maybe_unused]] const GraphicsPipelineDescription& description)
        {
            ReportMissing("CreateGraphicsPipeline", 12);

            return {};
        }

        void D3D12Device::DestroyPipeline([[maybe_unused]] PipelineHandle pipeline)
        {

        }

        Scope<SwapChain> D3D12Device::CreateSwapChain([[maybe_unused]] const SwapChainSpecification& specification)
        {
            ReportMissing("CreateSwapChain", 10);

            return nullptr;
        }

        CommandList& D3D12Device::BeginFrame()
        {
            ReportMissing("BeginFrame", 10);

            return m_CommandList;
        }

        void D3D12Device::EndFrame()
        {
            ReportMissing("EndFrame", 10);
        }

        void D3D12Device::WaitIdle()
        {

        }
    }
}