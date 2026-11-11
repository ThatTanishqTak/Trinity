#include "Trinity/RHI/D3D12/D3D12Texture.hpp"
#include "Trinity/RHI/D3D12/D3D12Allocator.hpp"
#include "Trinity/RHI/D3D12/D3D12Device.hpp"
#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

namespace Trinity
{
    namespace RHI
    {
        namespace
        {
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

            // A view per mip of each layer, as a single 2D texture for a Texture2D and as one slice of an array otherwise
            void CreateViews(const D3D12Device& device, D3D12DescriptorHeap& renderTargetViews, D3D12DescriptorHeap& depthStencilViews, D3D12Texture& texture)
            {
                const bool l_Depth = IsDepthFormat(texture.TextureFormat);
                const bool l_Array = texture.Dimension != TextureDimension::Texture2D;
                const bool l_Multisampled = texture.SampleCount > 1;
                D3D12DescriptorIndices& l_Views = l_Depth ? texture.DepthStencilViews : texture.RenderTargetViews;
                D3D12DescriptorHeap& l_Heap = l_Depth ? depthStencilViews : renderTargetViews;

                l_Views.reserve(std::size_t{ texture.MipLevels } * texture.ArrayLayers);
                for (std::uint32_t it_Layer = 0; it_Layer < texture.ArrayLayers; ++it_Layer)
                {
                    for (std::uint32_t it_Mip = 0; it_Mip < texture.MipLevels; ++it_Mip)
                    {
                        const std::uint32_t l_View = l_Heap.Allocate();
                        TR_CORE_ASSERT(l_View != c_NoDescriptor, "Out of {} views.", l_Depth ? "depth stencil" : "render target");
                        l_Views.push_back(l_View);
                        if (l_View == c_NoDescriptor)
                        {
                            continue;
                        }

                        if (l_Depth)
                        {
                            D3D12_DEPTH_STENCIL_VIEW_DESC l_Description{};
                            l_Description.Format = ToDXGIFormat(texture.TextureFormat);
                            l_Description.ViewDimension = l_Array ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY : l_Multisampled ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
                            if (l_Array)
                            {
                                l_Description.Texture2DArray = { it_Mip, it_Layer, 1 };
                            }
                            else
                            {
                                l_Description.Texture2D.MipSlice = it_Mip;
                            }

                            device.GetHandle()->CreateDepthStencilView(texture.Resource, &l_Description, l_Heap.GetHandle(l_View));
                        }
                        else
                        {
                            D3D12_RENDER_TARGET_VIEW_DESC l_Description{};
                            l_Description.Format = ToDXGIFormat(texture.TextureFormat);
                            l_Description.ViewDimension = l_Array ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY : l_Multisampled ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
                            if (l_Array)
                            {
                                l_Description.Texture2DArray = { it_Mip, it_Layer, 1, 0 };
                            }
                            else
                            {
                                l_Description.Texture2D.MipSlice = it_Mip;
                            }

                            device.GetHandle()->CreateRenderTargetView(texture.Resource, &l_Description, l_Heap.GetHandle(l_View));
                        }
                    }
                }
            }

            // The shader resource view covers every mip and layer, and the unordered access view mip 0 of every layer, a cube's faces as an array since there are no cube unordered access views
            void CreateShaderViews(const D3D12Device& device, D3D12DescriptorHeap& resourceHeap, D3D12Texture& texture, TextureUsage usage)
            {
                if (HasFlag(usage, TextureUsage::ShaderResource))
                {
                    texture.ShaderResourceIndex = resourceHeap.Allocate();
                    TR_CORE_ASSERT(texture.ShaderResourceIndex != c_NoDescriptor, "Out of bindless resource indices.");
                    if (texture.ShaderResourceIndex != c_NoDescriptor)
                    {
                        D3D12_SHADER_RESOURCE_VIEW_DESC l_View{};
                        l_View.Format = texture.ResourceFormat == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_R32_FLOAT : texture.ResourceFormat;
                        l_View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        switch (texture.Dimension)
                        {
                            case TextureDimension::Texture2DArray:
                            {
                                l_View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                                l_View.Texture2DArray.MipLevels = texture.MipLevels;
                                l_View.Texture2DArray.ArraySize = texture.ArrayLayers;

                                break;
                            }
                            case TextureDimension::TextureCube:
                            {
                                l_View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                                l_View.TextureCube.MipLevels = texture.MipLevels;

                                break;
                            }
                            default:
                            {
                                l_View.ViewDimension = texture.SampleCount > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
                                l_View.Texture2D.MipLevels = texture.MipLevels;

                                break;
                            }
                        }
                        device.GetHandle()->CreateShaderResourceView(texture.Resource, &l_View, resourceHeap.GetHandle(texture.ShaderResourceIndex));
                    }
                }

                if (HasFlag(usage, TextureUsage::UnorderedAccess))
                {
                    texture.UnorderedAccessIndex = resourceHeap.Allocate();
                    TR_CORE_ASSERT(texture.UnorderedAccessIndex != c_NoDescriptor, "Out of bindless resource indices.");
                    if (texture.UnorderedAccessIndex != c_NoDescriptor)
                    {
                        D3D12_UNORDERED_ACCESS_VIEW_DESC l_View{};
                        l_View.Format = texture.ResourceFormat;
                        l_View.ViewDimension = texture.Dimension == TextureDimension::Texture2D ? D3D12_UAV_DIMENSION_TEXTURE2D : D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                        if (texture.Dimension != TextureDimension::Texture2D)
                        {
                            l_View.Texture2DArray.ArraySize = texture.ArrayLayers;
                        }

                        device.GetHandle()->CreateUnorderedAccessView(texture.Resource, nullptr, &l_View, resourceHeap.GetHandle(texture.UnorderedAccessIndex));
                    }
                }
            }
        }

        // Every usage asked for must be in the format's support. Shaders read a depth texture through an R32_FLOAT view, so that is the format to check for reading
        bool D3D12Texture::IsFormatSupported(const D3D12Device& device, Format format, TextureUsage usage, std::uint32_t sampleCount)
        {
            const DXGI_FORMAT l_Format = ToDXGIFormat(format);
            if (l_Format == DXGI_FORMAT_UNKNOWN || (HasStencil(format) && usage != TextureUsage::DepthStencil))
            {
                return false;
            }

            const auto a_Query = [&device](DXGI_FORMAT queried) -> D3D12_FORMAT_SUPPORT1
            {
                D3D12_FEATURE_DATA_FORMAT_SUPPORT l_Data{ queried, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
                if (FAILED(device.GetHandle()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &l_Data, sizeof(l_Data))))
                {
                    return D3D12_FORMAT_SUPPORT1_NONE;
                }

                return l_Data.Support1;
            };

            const D3D12_FORMAT_SUPPORT1 l_Support = a_Query(l_Format);
            const D3D12_FORMAT_SUPPORT1 l_ReadSupport = format == Format::D32Float ? a_Query(DXGI_FORMAT_R32_FLOAT) : l_Support;

            const auto a_Has = [](D3D12_FORMAT_SUPPORT1 support, D3D12_FORMAT_SUPPORT1 needed)
            {
                return (support & needed) == needed;
            };

            if (!a_Has(l_Support, D3D12_FORMAT_SUPPORT1_TEXTURE2D))
            {
                return false;
            }

            if (HasFlag(usage, TextureUsage::ShaderResource) && !a_Has(l_ReadSupport, D3D12_FORMAT_SUPPORT1_SHADER_LOAD))
            {
                return false;
            }

            if (HasFlag(usage, TextureUsage::UnorderedAccess) && !a_Has(l_Support, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW))
            {
                return false;
            }

            if (HasFlag(usage, TextureUsage::RenderTarget) && !a_Has(l_Support, D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
            {
                return false;
            }

            if (HasFlag(usage, TextureUsage::DepthStencil) && !a_Has(l_Support, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL))
            {
                return false;
            }

            if (sampleCount == 1)
            {
                return true;
            }

            const bool l_Attachment = HasFlag(usage, TextureUsage::RenderTarget) || HasFlag(usage, TextureUsage::DepthStencil);
            if ((l_Attachment && !a_Has(l_Support, D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET)) || (HasFlag(usage, TextureUsage::ShaderResource) && !a_Has(l_ReadSupport, D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD)))
            {
                return false;
            }

            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS l_Levels{ l_Format, sampleCount, D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 0 };

            return SUCCEEDED(device.GetHandle()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &l_Levels, sizeof(l_Levels))) && l_Levels.NumQualityLevels > 0;
        }

        // A depth texture that shaders also read is typeless, so a shader view can read it as R32_FLOAT
        std::optional<D3D12Texture> D3D12Texture::Create(const D3D12Device& device, const D3D12Allocator& allocator, D3D12DescriptorHeap& renderTargetViews, D3D12DescriptorHeap& depthStencilViews, D3D12DescriptorHeap& resourceHeap, const TextureDescription& description)
        {
            const bool l_TypelessDepth = description.TextureFormat == Format::D32Float && HasFlag(description.Usage, TextureUsage::ShaderResource);

            D3D12MA::ALLOCATION_DESC l_Allocation{};
            l_Allocation.HeapType = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC1 l_Resource{};
            l_Resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            l_Resource.Width = description.Width;
            l_Resource.Height = description.Height;
            l_Resource.DepthOrArraySize = static_cast<UINT16>(description.ArrayLayers);
            l_Resource.MipLevels = static_cast<UINT16>(description.MipLevels);
            l_Resource.Format = l_TypelessDepth ? DXGI_FORMAT_R32_TYPELESS : ToDXGIFormat(description.TextureFormat);
            l_Resource.SampleDesc.Count = description.SampleCount;
            l_Resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            l_Resource.Flags = ToResourceFlags(description.Usage);

            D3D12_CLEAR_VALUE l_ClearValue{};
            const D3D12_CLEAR_VALUE* l_OptimizedClear = nullptr;
            if (description.OptimizedClear && HasFlag(description.Usage, TextureUsage::RenderTarget))
            {
                l_ClearValue.Format = ToDXGIFormat(description.TextureFormat);
                std::ranges::copy(description.ClearColor, l_ClearValue.Color);
                l_OptimizedClear = &l_ClearValue;
            }
            else if (description.OptimizedClear && HasFlag(description.Usage, TextureUsage::DepthStencil))
            {
                l_ClearValue.Format = ToDXGIFormat(description.TextureFormat);
                l_ClearValue.DepthStencil.Depth = description.ClearDepth;
                l_OptimizedClear = &l_ClearValue;
            }

            D3D12Texture l_Texture;
            const HRESULT l_Result = allocator.GetHandle()->CreateResource3(&l_Allocation, &l_Resource, D3D12_BARRIER_LAYOUT_COMMON, l_OptimizedClear, 0, nullptr, &l_Texture.Allocation, IID_NULL, nullptr);
            if (FAILED(l_Result))
            {
                TR_CORE_ERROR("D3D12: texture '{}' ({}x{} {}) could not be created ({})", description.DebugName, description.Width, description.Height, ToString(description.TextureFormat), FormatResult(l_Result));

                return {};
            }

            l_Texture.Resource = l_Texture.Allocation->GetResource();
            l_Texture.TextureFormat = description.TextureFormat;
            l_Texture.ResourceFormat = l_Resource.Format;
            l_Texture.Dimension = description.Dimension;
            l_Texture.Width = description.Width;
            l_Texture.Height = description.Height;
            l_Texture.MipLevels = description.MipLevels;
            l_Texture.ArrayLayers = description.ArrayLayers;
            l_Texture.SampleCount = description.SampleCount;
            SetDebugName(l_Texture.Resource, description.DebugName);

            if (HasFlag(description.Usage, TextureUsage::RenderTarget) || HasFlag(description.Usage, TextureUsage::DepthStencil))
            {
                CreateViews(device, renderTargetViews, depthStencilViews, l_Texture);
            }

            CreateShaderViews(device, resourceHeap, l_Texture, description.Usage);

            return l_Texture;
        }

        D3D12Texture D3D12Texture::FromSwapChainBuffer(const D3D12Device& device, D3D12DescriptorHeap& renderTargetViews, D3D12DescriptorHeap& depthStencilViews, ID3D12Resource* resource, Format format, DXGI_FORMAT resourceFormat, std::uint32_t width, std::uint32_t height)
        {
            D3D12Texture l_Texture;
            l_Texture.Resource = resource;
            l_Texture.TextureFormat = format;
            l_Texture.ResourceFormat = resourceFormat;
            l_Texture.Width = width;
            l_Texture.Height = height;
            l_Texture.MipLevels = 1;
            l_Texture.ArrayLayers = 1;
            CreateViews(device, renderTargetViews, depthStencilViews, l_Texture);

            return l_Texture;
        }

        D3D12Release D3D12Texture::ToRelease()
        {
            D3D12Release l_Release;
            l_Release.Allocation = std::move(Allocation);
            l_Release.RenderTargetViews = std::move(RenderTargetViews);
            l_Release.DepthStencilViews = std::move(DepthStencilViews);
            l_Release.ShaderResourceIndex = ShaderResourceIndex;
            l_Release.UnorderedAccessIndex = UnorderedAccessIndex;

            return l_Release;
        }
    }
}