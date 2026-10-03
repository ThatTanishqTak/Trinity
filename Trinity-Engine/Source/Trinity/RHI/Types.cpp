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
                {
                    return 16;
                }
            }

            return 0;
        }
    }
}