#include "Trinity/RHI/D3D12/D3D12RootSignature.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/RHI/Types.hpp"

#include <format>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            using Microsoft::WRL::ComPtr;
        }

        D3D12RootSignature::D3D12RootSignature(const D3D12Device& device) : m_Device(device)
        {

        }

        bool D3D12RootSignature::Initialize(std::string& error)
        {
            D3D12_ROOT_PARAMETER1 l_PushConstants{};
            l_PushConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            l_PushConstants.Constants.ShaderRegister = 0;
            l_PushConstants.Constants.RegisterSpace = 0;
            l_PushConstants.Constants.Num32BitValues = c_MaxPushConstantSize / 4;
            l_PushConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC l_Description{};
            l_Description.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
            l_Description.Desc_1_1.NumParameters = 1;
            l_Description.Desc_1_1.pParameters = &l_PushConstants;
            l_Description.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

            ComPtr<ID3DBlob> l_Blob;
            ComPtr<ID3DBlob> l_Errors;
            HRESULT l_Result = ::D3D12SerializeVersionedRootSignature(&l_Description, &l_Blob, &l_Errors);
            if (SUCCEEDED(l_Result))
            {
                l_Result = m_Device.GetHandle()->CreateRootSignature(0, l_Blob->GetBufferPointer(), l_Blob->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature));
            }

            if (FAILED(l_Result))
            {
                const std::string_view l_Reason = l_Errors ? std::string_view(static_cast<const char*>(l_Errors->GetBufferPointer()), l_Errors->GetBufferSize()) : std::string_view();
                error = std::format("the bindless root signature could not be created on {} ({}{}{})", m_Device.GetInfo().AdapterName, FormatResult(l_Result), l_Reason.empty() ? "" : ": ", l_Reason);

                return false;
            }

            m_RootSignature->SetName(L"Trinity bindless root signature");

            return true;
        }

        void D3D12RootSignature::Reset()
        {
            m_RootSignature.Reset();
        }
    }
}