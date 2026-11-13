#include "Trinity/RHI/D3D12/D3D12Utilities.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace RHI
    {
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
                case Format::R11G11B10Float:
                {
                    return DXGI_FORMAT_R11G11B10_FLOAT;
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
                case Format::D32FloatS8:
                {
                    return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                }
                case Format::BC1Unorm:
                {
                    return DXGI_FORMAT_BC1_UNORM;
                }
                case Format::BC1Srgb:
                {
                    return DXGI_FORMAT_BC1_UNORM_SRGB;
                }
                case Format::BC3Unorm:
                {
                    return DXGI_FORMAT_BC3_UNORM;
                }
                case Format::BC3Srgb:
                {
                    return DXGI_FORMAT_BC3_UNORM_SRGB;
                }
                case Format::BC4Unorm:
                {
                    return DXGI_FORMAT_BC4_UNORM;
                }
                case Format::BC5Unorm:
                {
                    return DXGI_FORMAT_BC5_UNORM;
                }
                case Format::BC7Unorm:
                {
                    return DXGI_FORMAT_BC7_UNORM;
                }
                case Format::BC7Srgb:
                {
                    return DXGI_FORMAT_BC7_UNORM_SRGB;
                }
                default:
                {
                    return DXGI_FORMAT_UNKNOWN;
                }
            }
        }

        std::string FormatResult(HRESULT result)
        {
            return std::format("HRESULT 0x{:08X}", static_cast<std::uint32_t>(result));
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

        void SetDebugName(ID3D12Resource* resource, std::string_view name)
        {
            if (!name.empty())
            {
                resource->SetName(ToWide(name).c_str());
            }
        }
    }
}