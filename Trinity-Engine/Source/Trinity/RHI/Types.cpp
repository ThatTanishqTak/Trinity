#include "Trinity/RHI/Types.hpp"

namespace Trinity
{
    namespace RHI
    {
        std::string_view ToString(Format format)
        {
            switch (format)
            {
                case Format::Unknown:
                {
                    return "Unknown";
                }
                case Format::RGBA8Unorm:
                {
                    return "RGBA8Unorm";
                }
                case Format::RGBA8Srgb:
                {
                    return "RGBA8Srgb";
                }
                case Format::BGRA8Unorm:
                {
                    return "BGRA8Unorm";
                }
                case Format::BGRA8Srgb:
                {
                    return "BGRA8Srgb";
                }
                case Format::RGBA16Float:
                {
                    return "RGBA16Float";
                }
                case Format::R32Float:
                {
                    return "R32Float";
                }
                case Format::R32Uint:
                {
                    return "R32Uint";
                }
                case Format::RG32Float:
                {
                    return "RG32Float";
                }
                case Format::RGB32Float:
                {
                    return "RGB32Float";
                }
                case Format::RGBA32Float:
                {
                    return "RGBA32Float";
                }
                case Format::D32Float:
                {
                    return "D32Float";
                }
                case Format::BC1Unorm:
                {
                    return "BC1Unorm";
                }
                case Format::BC1Srgb:
                {
                    return "BC1Srgb";
                }
                case Format::BC3Unorm:
                {
                    return "BC3Unorm";
                }
                case Format::BC3Srgb:
                {
                    return "BC3Srgb";
                }
                case Format::BC4Unorm:
                {
                    return "BC4Unorm";
                }
                case Format::BC5Unorm:
                {
                    return "BC5Unorm";
                }
                case Format::BC7Unorm:
                {
                    return "BC7Unorm";
                }
                case Format::BC7Srgb:
                {
                    return "BC7Srgb";
                }
            }

            return "Invalid";
        }

        std::uint32_t GetFormatSize(Format format)
        {
            switch (format)
            {
                case Format::Unknown:
                {
                    return 0;
                }
                case Format::RGBA8Unorm:
                case Format::RGBA8Srgb:
                case Format::BGRA8Unorm:
                case Format::BGRA8Srgb:
                case Format::R32Float:
                case Format::R32Uint:
                case Format::D32Float:
                {
                    return 4;
                }
                case Format::RGBA16Float:
                case Format::RG32Float:
                {
                    return 8;
                }
                case Format::RGB32Float:
                {
                    return 12;
                }
                case Format::RGBA32Float:
                case Format::BC3Unorm:
                case Format::BC3Srgb:
                case Format::BC5Unorm:
                case Format::BC7Unorm:
                case Format::BC7Srgb:
                {
                    return 16;
                }
                case Format::BC1Unorm:
                case Format::BC1Srgb:
                case Format::BC4Unorm:
                {
                    return 8;
                }
            }

            return 0;
        }

        std::uint64_t GetTextureCopyRowPitch(Format format, std::uint32_t width)
        {
            const std::uint32_t l_Block = GetFormatBlockDimension(format);
            const std::uint64_t l_RowSize = (std::uint64_t{ width } + l_Block - 1) / l_Block * GetFormatSize(format);

            return (l_RowSize + c_TextureCopyRowAlignment - 1) / c_TextureCopyRowAlignment * c_TextureCopyRowAlignment;
        }

        std::uint32_t GetTextureCopyRowCount(Format format, std::uint32_t height)
        {
            const std::uint32_t l_Block = GetFormatBlockDimension(format);

            return (height + l_Block - 1) / l_Block;
        }

        std::uint64_t GetTextureCopySize(Format format, std::uint32_t width, std::uint32_t height)
        {
            return GetTextureCopyRowPitch(format, width) * GetTextureCopyRowCount(format, height);
        }
    }
}