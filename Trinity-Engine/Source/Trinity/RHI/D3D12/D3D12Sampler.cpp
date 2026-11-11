#include "Trinity/RHI/D3D12/D3D12Sampler.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"

#include "Trinity/Core/Log.hpp"

#include <optional>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
            D3D12_FILTER_TYPE ToFilterType(Filter filter)
            {
                return filter == Filter::Linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
            }

            // What D3D12_ENCODE_BASIC_FILTER builds, without its C-style casts
            D3D12_FILTER ToD3D12Filter(const SamplerDescription& description)
            {
                const UINT l_Min = static_cast<UINT>(ToFilterType(description.MinFilter)) << D3D12_MIN_FILTER_SHIFT;
                const UINT l_Mag = static_cast<UINT>(ToFilterType(description.MagFilter)) << D3D12_MAG_FILTER_SHIFT;
                const UINT l_Mip = static_cast<UINT>(ToFilterType(description.MipFilter)) << D3D12_MIP_FILTER_SHIFT;
                const UINT l_Reduction = static_cast<UINT>(D3D12_FILTER_REDUCTION_TYPE_STANDARD) << D3D12_FILTER_REDUCTION_TYPE_SHIFT;

                return static_cast<D3D12_FILTER>(l_Min | l_Mag | l_Mip | l_Reduction);
            }

            D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(AddressMode address)
            {
                switch (address)
                {
                    case AddressMode::MirroredRepeat:
                    {
                        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
                    }
                    case AddressMode::ClampToEdge:
                    {
                        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    }
                    default:
                    {
                        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                    }
                }
            }
        }

        // The comparison function only matters to comparison filters, and NEVER is valid on every runtime
        std::optional<D3D12Sampler> D3D12Sampler::Create(const D3D12Device& device, D3D12DescriptorHeap& samplerHeap, const SamplerDescription& description)
        {
            D3D12Sampler l_Sampler;
            l_Sampler.Index = samplerHeap.Allocate();
            if (l_Sampler.Index == c_NoDescriptor)
            {
                TR_CORE_ERROR("D3D12: sampler '{}' could not be created, since all {} bindless sampler indices are in use", description.DebugName, c_BindlessSamplerCapacity);

                return {};
            }

            D3D12_SAMPLER_DESC l_Description{};
            l_Description.Filter = ToD3D12Filter(description);
            l_Description.AddressU = ToD3D12AddressMode(description.AddressU);
            l_Description.AddressV = ToD3D12AddressMode(description.AddressV);
            l_Description.AddressW = ToD3D12AddressMode(description.AddressW);
            l_Description.MipLODBias = description.MipLodBias;
            l_Description.MaxAnisotropy = 1;
            l_Description.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            l_Description.MinLOD = description.MinLod;
            l_Description.MaxLOD = description.MaxLod >= c_LodUnclamped ? D3D12_FLOAT32_MAX : description.MaxLod;
            device.GetHandle()->CreateSampler(&l_Description, samplerHeap.GetHandle(l_Sampler.Index));

            return l_Sampler;
        }

        D3D12Release D3D12Sampler::ToRelease() const
        {
            D3D12Release l_Release;
            l_Release.SamplerIndex = Index;

            return l_Release;
        }
    }
}