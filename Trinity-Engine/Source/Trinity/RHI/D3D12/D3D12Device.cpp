#include "Trinity/RHI/D3D12/D3D12Device.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Platform.hpp"

#include <array>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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

            std::wstring ToWide(std::string_view text)
            {
                if (text.empty())
                {
                    return {};
                }

                const int l_Size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
                std::wstring l_Result(static_cast<std::size_t>(l_Size), L'\0');
                ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), l_Result.data(), l_Size);

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

            // D3D12MemoryAllocator's bookkeeping, counted under the Renderer tag, what the driver allocates is out of reach, since D3D12 takes no allocator
            void* AllocateHostMemory(std::size_t size, std::size_t alignment, [[maybe_unused]] void* userData)
            {
                return Memory::TryAllocate(size, MemoryTag::Renderer, alignment);
            }

            void FreeHostMemory(void* memory, [[maybe_unused]] void* userData)
            {
                Memory::Free(memory);
            }

            constexpr D3D12MA::ALLOCATION_CALLBACKS c_HostAllocator{ &AllocateHostMemory, &FreeHostMemory, nullptr };

            void ReleaseAllocation(ComPtr<D3D12MA::Allocation>& allocation)
            {
                allocation.Reset();
            }

            DXGI_FORMAT ToDXGIFormat(Format format)
            {
                switch (format)
                {
                    case Format::RGBA8Unorm:
                    {
                        return DXGI_FORMAT_R8G8B8A8_UNORM;
                    }
                    case Format::RGBA8Srgb:
                    {
                        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                    }
                    case Format::BGRA8Unorm:
                    {
                        return DXGI_FORMAT_B8G8R8A8_UNORM;
                    }
                    case Format::BGRA8Srgb:
                    {
                        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                    }
                    case Format::RGBA16Float:
                    {
                        return DXGI_FORMAT_R16G16B16A16_FLOAT;
                    }
                    case Format::R32Float:
                    {
                        return DXGI_FORMAT_R32_FLOAT;
                    }
                    case Format::R32Uint:
                    {
                        return DXGI_FORMAT_R32_UINT;
                    }
                    case Format::RG32Float:
                    {
                        return DXGI_FORMAT_R32G32_FLOAT;
                    }
                    case Format::RGB32Float:
                    {
                        return DXGI_FORMAT_R32G32B32_FLOAT;
                    }
                    case Format::RGBA32Float:
                    {
                        return DXGI_FORMAT_R32G32B32A32_FLOAT;
                    }
                    case Format::D32Float:
                    {
                        return DXGI_FORMAT_D32_FLOAT;
                    }
                    default:
                    {
                        return DXGI_FORMAT_UNKNOWN;
                    }
                }
            }

            D3D12_HEAP_TYPE ToHeapType(MemoryType memory)
            {
                switch (memory)
                {
                    case MemoryType::Upload:
                    {
                        return D3D12_HEAP_TYPE_UPLOAD;
                    }
                    case MemoryType::Readback:
                    {
                        return D3D12_HEAP_TYPE_READBACK;
                    }
                    default:
                    {
                        return D3D12_HEAP_TYPE_DEFAULT;
                    }
                }
            }

            D3D12_RESOURCE_FLAGS ToResourceFlags(TextureUsage usage)
            {
                D3D12_RESOURCE_FLAGS l_Flags = D3D12_RESOURCE_FLAG_NONE;
                if (HasFlag(usage, TextureUsage::UnorderedAccess))
                {
                    l_Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                }

                if (HasFlag(usage, TextureUsage::RenderTarget))
                {
                    l_Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                }

                if (HasFlag(usage, TextureUsage::DepthStencil))
                {
                    l_Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
                    if (!HasFlag(usage, TextureUsage::ShaderResource))
                    {
                        l_Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
                    }
                }

                return l_Flags;
            }

            void SetDebugName(ID3D12Resource* resource, std::string_view name)
            {
                if (!name.empty())
                {
                    resource->SetName(ToWide(name).c_str());
                }
            }
        }

        void D3D12CommandList::TextureBarrier([[maybe_unused]] TextureHandle texture, [[maybe_unused]] ResourceState before, [[maybe_unused]] ResourceState after)
        {

        }

        void D3D12CommandList::BufferBarrier([[maybe_unused]] BufferHandle buffer, [[maybe_unused]] ResourceState before, [[maybe_unused]] ResourceState after)
        {

        }

        void D3D12CommandList::BeginRendering([[maybe_unused]] const RenderingDescription& description)
        {

        }

        void D3D12CommandList::EndRendering()
        {

        }

        void D3D12CommandList::SetPipeline([[maybe_unused]] PipelineHandle pipeline)
        {

        }

        void D3D12CommandList::SetViewport([[maybe_unused]] const Viewport& viewport)
        {

        }

        void D3D12CommandList::SetScissor([[maybe_unused]] const Rect& scissor)
        {

        }

        void D3D12CommandList::PushConstants([[maybe_unused]] std::span<const std::byte> data)
        {

        }

        void D3D12CommandList::Draw([[maybe_unused]] std::uint32_t vertexCount, [[maybe_unused]] std::uint32_t instanceCount, [[maybe_unused]] std::uint32_t firstVertex, [[maybe_unused]] std::uint32_t firstInstance)
        {

        }

        void D3D12CommandList::CopyBuffer([[maybe_unused]] BufferHandle source, [[maybe_unused]] std::uint64_t sourceOffset, [[maybe_unused]] BufferHandle destination, [[maybe_unused]] std::uint64_t destinationOffset, [[maybe_unused]] std::uint64_t size)
        {

        }

        void D3D12CommandList::CopyTextureToBuffer([[maybe_unused]] TextureHandle source, [[maybe_unused]] BufferHandle destination)
        {

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
            if (m_Buffers.GetCount() != 0 || m_Textures.GetCount() != 0)
            {
                TR_CORE_WARN("D3D12: the device was destroyed with {} buffer(s) and {} texture(s) still alive", m_Buffers.GetCount(), m_Textures.GetCount());
            }

            m_Releases.ReleaseAll(&ReleaseAllocation);
            m_Buffers.ForEach([](D3D12Buffer& buffer) { buffer.Allocation.Reset(); });
            m_Textures.ForEach([](D3D12Texture& texture) { texture.Allocation.Reset(); });
            m_Allocator.Reset();

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

            return CreateAllocator(error);
        }

        bool D3D12Device::CreateAllocator(std::string& error)
        {
            D3D12MA::ALLOCATOR_DESC l_Description{};
            // D3D12MemoryAllocator's flag operators are global, and RHI's own operator| hides them in here
            l_Description.Flags = static_cast<D3D12MA::ALLOCATOR_FLAGS>(D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS);
            l_Description.pDevice = m_Device.Get();
            l_Description.pAdapter = m_Adapter.Get();
            l_Description.pAllocationCallbacks = &c_HostAllocator;

            const HRESULT l_Result = D3D12MA::CreateAllocator(&l_Description, &m_Allocator);
            if (FAILED(l_Result))
            {
                error = std::format("D3D12MemoryAllocator could not be created on {} ({})", m_Info.AdapterName, FormatResult(l_Result));

                return false;
            }

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

        // Buffers always start in the undefined layout under enhanced barriers. Upload and Readback heaps are coherent and stay mapped
        BufferHandle D3D12Device::CreateBuffer(const BufferDescription& description)
        {
            TR_CORE_ASSERT(description.Size != 0, "Buffer '{}' has no size.", description.DebugName);

            D3D12MA::ALLOCATION_DESC l_Allocation{};
            l_Allocation.HeapType = ToHeapType(description.Memory);

            D3D12_RESOURCE_DESC1 l_Resource{};
            l_Resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            l_Resource.Width = description.Size;
            l_Resource.Height = 1;
            l_Resource.DepthOrArraySize = 1;
            l_Resource.MipLevels = 1;
            l_Resource.Format = DXGI_FORMAT_UNKNOWN;
            l_Resource.SampleDesc.Count = 1;
            l_Resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            l_Resource.Flags = HasFlag(description.Usage, BufferUsage::UnorderedAccess) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

            D3D12Buffer l_Buffer;
            HRESULT l_Result = m_Allocator->CreateResource3(&l_Allocation, &l_Resource, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 0, nullptr, &l_Buffer.Allocation, IID_NULL, nullptr);
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: buffer '{}' of {} could not be created ({})", description.DebugName, Memory::FormatBytes(description.Size), FormatResult(l_Result));

                return {};
            }

            ID3D12Resource* l_Native = l_Buffer.Allocation->GetResource();
            if (description.Memory != MemoryType::GPU)
            {
                // The CPU only writes Upload buffers, so mapping one says nothing will be read
                const D3D12_RANGE l_NoRead{ 0, 0 };
                void* l_Mapped = nullptr;
                l_Result = l_Native->Map(0, description.Memory == MemoryType::Upload ? &l_NoRead : nullptr, &l_Mapped);
                if (FAILED(l_Result))
                {
                    TR_CORE_ERROR("D3D12: buffer '{}' could not be mapped ({})", description.DebugName, FormatResult(l_Result));

                    return {};
                }

                l_Buffer.Mapped = static_cast<std::byte*>(l_Mapped);
            }

            l_Buffer.Size = description.Size;
            SetDebugName(l_Native, description.DebugName);

            return m_Buffers.Add(std::move(l_Buffer));
        }

        void D3D12Device::DestroyBuffer(BufferHandle buffer)
        {
            if (!buffer)
            {
                return;
            }

            std::optional<D3D12Buffer> l_Buffer = m_Buffers.Remove(buffer);
            TR_CORE_ASSERT(l_Buffer.has_value(), "DestroyBuffer on a buffer that was already destroyed.");

            if (l_Buffer)
            {
                m_Releases.Push(std::move(l_Buffer->Allocation));
            }
        }

        std::span<std::byte> D3D12Device::GetMappedData(BufferHandle buffer)
        {
            const D3D12Buffer* l_Buffer = m_Buffers.Get(buffer);
            TR_CORE_ASSERT(l_Buffer != nullptr, "GetMappedData on a destroyed or invalid buffer.");

            if (l_Buffer == nullptr || l_Buffer->Mapped == nullptr)
            {
                return {};
            }

            return { l_Buffer->Mapped, static_cast<std::size_t>(l_Buffer->Size) };
        }

        // A depth texture that shaders also read is typeless, so a shader view can read it as R32_FLOAT
        TextureHandle D3D12Device::CreateTexture(const TextureDescription& description)
        {
            TR_CORE_ASSERT(description.Width != 0 && description.Height != 0 && description.MipLevels != 0, "Texture '{}' has a zero size or no mips.", description.DebugName);
            TR_CORE_ASSERT(description.TextureFormat != Format::Unknown, "Texture '{}' has no format.", description.DebugName);

            const bool l_TypelessDepth = description.TextureFormat == Format::D32Float && HasFlag(description.Usage, TextureUsage::ShaderResource);

            D3D12MA::ALLOCATION_DESC l_Allocation{};
            l_Allocation.HeapType = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC1 l_Resource{};
            l_Resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            l_Resource.Width = description.Width;
            l_Resource.Height = description.Height;
            l_Resource.DepthOrArraySize = 1;
            l_Resource.MipLevels = static_cast<UINT16>(description.MipLevels);
            l_Resource.Format = l_TypelessDepth ? DXGI_FORMAT_R32_TYPELESS : ToDXGIFormat(description.TextureFormat);
            l_Resource.SampleDesc.Count = 1;
            l_Resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            l_Resource.Flags = ToResourceFlags(description.Usage);

            D3D12Texture l_Texture;
            const HRESULT l_Result = m_Allocator->CreateResource3(&l_Allocation, &l_Resource, D3D12_BARRIER_LAYOUT_COMMON, nullptr, 0, nullptr, &l_Texture.Allocation, IID_NULL, nullptr);
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: texture '{}' ({}x{} {}) could not be created ({})", description.DebugName, description.Width, description.Height, ToString(description.TextureFormat), FormatResult(l_Result));

                return {};
            }

            l_Texture.ResourceFormat = l_Resource.Format;
            l_Texture.Width = description.Width;
            l_Texture.Height = description.Height;
            l_Texture.MipLevels = description.MipLevels;
            SetDebugName(l_Texture.Allocation->GetResource(), description.DebugName);

            return m_Textures.Add(std::move(l_Texture));
        }

        void D3D12Device::DestroyTexture(TextureHandle texture)
        {
            if (!texture)
            {
                return;
            }

            std::optional<D3D12Texture> l_Texture = m_Textures.Remove(texture);
            TR_CORE_ASSERT(l_Texture.has_value(), "DestroyTexture on a texture that was already destroyed.");

            if (l_Texture)
            {
                m_Releases.Push(std::move(l_Texture->Allocation));
            }
        }

        PipelineHandle D3D12Device::CreateGraphicsPipeline([[maybe_unused]] const GraphicsPipelineDescription& description)
        {
            return {};
        }

        void D3D12Device::DestroyPipeline([[maybe_unused]] PipelineHandle pipeline)
        {

        }

        Scope<SwapChain> D3D12Device::CreateSwapChain([[maybe_unused]] const SwapChainSpecification& specification)
        {
            return nullptr;
        }

        CommandList& D3D12Device::BeginFrame()
        {
            TR_CORE_ASSERT(!m_InFrame, "BeginFrame was called twice without EndFrame.");

            m_InFrame = true;
            m_Releases.BeginFrame(&ReleaseAllocation);

            return m_CommandList;
        }

        void D3D12Device::EndFrame()
        {
            TR_CORE_ASSERT(m_InFrame, "EndFrame without BeginFrame.");

            m_Releases.EndFrame();
            m_InFrame = false;
        }

        void D3D12Device::WaitIdle()
        {
            m_Releases.ReleaseIdle(&ReleaseAllocation);
        }
    }
}