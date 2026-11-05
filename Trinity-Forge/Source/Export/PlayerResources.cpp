#include "Export/PlayerResources.hpp"

#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>

namespace
{
    constexpr std::uint16_t c_IconType = 3;
    constexpr std::uint16_t c_GroupIconType = 14;
    constexpr std::uint16_t c_VersionType = 16;
    constexpr std::uint16_t c_Language = 0x0409;
    // UTF-16, as the string table's key 040904B0 says
    constexpr std::uint16_t c_CodePage = 1200;
    constexpr std::uint32_t c_LargestBitmap = 64;

    struct ImageDeleter
    {
        void operator()(stbi_uc* pixels) const
        {
            stbi_image_free(pixels);
        }
    };

    template<typename T>
    void Append(std::vector<char>& bytes, T value)
    {
        const std::size_t l_At = bytes.size();
        bytes.resize(l_At + sizeof(T));
        std::memcpy(bytes.data() + l_At, &value, sizeof(T));
    }

    void AlignTo4(std::vector<char>& bytes)
    {
        bytes.resize((bytes.size() + 3) / 4 * 4, '\0');
    }

    // Each sequence that is not UTF-8 becomes U+FFFD, as Windows shows text it cannot read
    std::u16string ToUtf16(std::string_view text)
    {
        constexpr std::uint32_t c_Replacement = 0xFFFD;
        constexpr std::array<std::uint32_t, 5> c_Smallest{ 0, 0, 0x80, 0x800, 0x10000 };

        std::u16string l_Text;
        for (std::size_t l_At = 0; l_At < text.size();)
        {
            const auto a_Byte = [&text](std::size_t index) { return static_cast<std::uint8_t>(text[index]); };
            const std::uint8_t l_Lead = a_Byte(l_At);
            std::size_t l_Length = l_Lead < 0x80 ? 1 : (l_Lead >> 5) == 0x6 ? 2 : (l_Lead >> 4) == 0xE ? 3 : (l_Lead >> 3) == 0x1E ? 4 : 0;
            std::uint32_t l_Code = l_Length == 1 ? l_Lead : l_Length == 2 ? (l_Lead & 0x1Fu) : l_Length == 3 ? (l_Lead & 0x0Fu) : (l_Lead & 0x07u);
            bool l_Valid = l_Length != 0 && l_At + l_Length <= text.size();
            for (std::size_t it_Byte = 1; l_Valid && it_Byte < l_Length; ++it_Byte)
            {
                l_Valid = (a_Byte(l_At + it_Byte) & 0xC0) == 0x80;
                l_Code = (l_Code << 6) | (a_Byte(l_At + it_Byte) & 0x3Fu);
            }

            if (!l_Valid || (l_Length > 1 && l_Code < c_Smallest[l_Length]) || l_Code > 0x10FFFF || (l_Code >= 0xD800 && l_Code <= 0xDFFF))
            {
                l_Code = c_Replacement;
                l_Length = 1;
            }

            if (l_Code >= 0x10000)
            {
                l_Text += static_cast<char16_t>(0xD800 + ((l_Code - 0x10000) >> 10));
                l_Text += static_cast<char16_t>(0xDC00 + ((l_Code - 0x10000) & 0x3FF));
            }
            else
            {
                l_Text += static_cast<char16_t>(l_Code);
            }

            l_At += l_Length;
        }

        return l_Text;
    }

    // The whole image scaled into the square, centred, its colours blended as sRGB with their alpha
    std::vector<std::uint8_t> MakeFrame(const stbi_uc* pixels, int width, int height, std::uint32_t size)
    {
        const double l_Scale = static_cast<double>(size) / static_cast<double>(std::max(width, height));
        const int l_Width = std::max(1, static_cast<int>(std::lround(width * l_Scale)));
        const int l_Height = std::max(1, static_cast<int>(std::lround(height * l_Scale)));
        std::vector<std::uint8_t> l_Scaled(static_cast<std::size_t>(l_Width) * static_cast<std::size_t>(l_Height) * 4);
        stbir_resize_uint8_srgb(pixels, width, height, 0, l_Scaled.data(), l_Width, l_Height, 0, STBIR_RGBA);

        std::vector<std::uint8_t> l_Frame(static_cast<std::size_t>(size) * size * 4, 0);
        const std::size_t l_Left = (size - static_cast<std::uint32_t>(l_Width)) / 2;
        const std::size_t l_Top = (size - static_cast<std::uint32_t>(l_Height)) / 2;
        for (std::size_t it_Row = 0; it_Row < static_cast<std::size_t>(l_Height); ++it_Row)
        {
            std::memcpy(l_Frame.data() + ((l_Top + it_Row) * size + l_Left) * 4, l_Scaled.data() + it_Row * static_cast<std::size_t>(l_Width) * 4, static_cast<std::size_t>(l_Width) * 4);
        }

        return l_Frame;
    }

    // As an icon holds a bitmap: its header at twice the height, for the colours and the mask below them, then the colours as BGRA from the bottom row up, then the mask, a bit set where nothing is drawn
    std::vector<char> EncodeBitmap(const std::vector<std::uint8_t>& frame, std::uint32_t size)
    {
        const std::uint32_t l_MaskRow = (size + 31) / 32 * 4;
        std::vector<char> l_Bytes;
        Append<std::uint32_t>(l_Bytes, 40);
        Append<std::int32_t>(l_Bytes, static_cast<std::int32_t>(size));
        Append<std::int32_t>(l_Bytes, static_cast<std::int32_t>(size * 2));
        Append<std::uint16_t>(l_Bytes, 1);
        Append<std::uint16_t>(l_Bytes, 32);
        Append<std::uint32_t>(l_Bytes, 0);
        Append<std::uint32_t>(l_Bytes, size * size * 4 + l_MaskRow * size);
        for (int it_Field = 0; it_Field < 4; ++it_Field)
        {
            Append<std::uint32_t>(l_Bytes, 0);
        }

        for (std::uint32_t it_Row = size; it_Row-- > 0;)
        {
            for (std::uint32_t it_Column = 0; it_Column < size; ++it_Column)
            {
                const std::uint8_t* l_Pixel = frame.data() + (static_cast<std::size_t>(it_Row) * size + it_Column) * 4;
                l_Bytes.push_back(static_cast<char>(l_Pixel[2]));
                l_Bytes.push_back(static_cast<char>(l_Pixel[1]));
                l_Bytes.push_back(static_cast<char>(l_Pixel[0]));
                l_Bytes.push_back(static_cast<char>(l_Pixel[3]));
            }
        }

        for (std::uint32_t it_Row = size; it_Row-- > 0;)
        {
            std::vector<std::uint8_t> l_Mask(l_MaskRow, 0);
            for (std::uint32_t it_Column = 0; it_Column < size; ++it_Column)
            {
                if (frame[(static_cast<std::size_t>(it_Row) * size + it_Column) * 4 + 3] == 0)
                {
                    l_Mask[it_Column / 8] = static_cast<std::uint8_t>(l_Mask[it_Column / 8] | (0x80u >> (it_Column % 8)));
                }
            }

            l_Bytes.insert(l_Bytes.end(), l_Mask.begin(), l_Mask.end());
        }

        return l_Bytes;
    }

    std::vector<char> EncodePng(const std::vector<std::uint8_t>& frame, std::uint32_t size)
    {
        std::vector<char> l_Bytes;
        const int l_Side = static_cast<int>(size);
        stbi_write_png_to_func([](void* context, void* data, int length)
        {
            std::vector<char>& l_Out = *static_cast<std::vector<char>*>(context);
            l_Out.insert(l_Out.end(), static_cast<const char*>(data), static_cast<const char*>(data) + length);
        }, &l_Bytes, l_Side, l_Side, 4, frame.data(), l_Side * 4);

        return l_Bytes;
    }

    // One block of VS_VERSIONINFO: its length with the blocks below, its value's length, in characters for text, whether the value is text, its key, then the value and the blocks below, each on 4 bytes
    struct VersionBlock
    {
        std::u16string Key;
        std::vector<char> Value;
        std::uint16_t ValueLength = 0;
        bool Text = true;
        std::vector<VersionBlock> Children;
    };

    void WriteBlock(std::vector<char>& bytes, const VersionBlock& block)
    {
        AlignTo4(bytes);
        const std::size_t l_Start = bytes.size();
        Append<std::uint16_t>(bytes, 0);
        Append<std::uint16_t>(bytes, block.ValueLength);
        Append<std::uint16_t>(bytes, block.Text ? 1 : 0);
        for (const char16_t it_Unit : block.Key)
        {
            Append<std::uint16_t>(bytes, it_Unit);
        }

        Append<std::uint16_t>(bytes, 0);
        AlignTo4(bytes);
        bytes.insert(bytes.end(), block.Value.begin(), block.Value.end());
        for (const VersionBlock& it_Child : block.Children)
        {
            WriteBlock(bytes, it_Child);
        }

        const std::uint16_t l_Length = static_cast<std::uint16_t>(bytes.size() - l_Start);
        std::memcpy(bytes.data() + l_Start, &l_Length, sizeof(l_Length));
    }

    // A string with the zero after it, which its length counts
    VersionBlock MakeString(std::u16string key, std::string_view value)
    {
        const std::u16string l_Value = ToUtf16(value);
        VersionBlock l_Block;
        l_Block.Key = std::move(key);
        l_Block.ValueLength = static_cast<std::uint16_t>(l_Value.size() + 1);
        for (const char16_t it_Unit : l_Value)
        {
            Append<std::uint16_t>(l_Block.Value, it_Unit);
        }

        Append<std::uint16_t>(l_Block.Value, 0);

        return l_Block;
    }
}

namespace PlayerResources
{
    Version ParseVersion(std::string_view text)
    {
        const auto a_IsDigit = [&text](std::size_t index) { return index < text.size() && text[index] >= '0' && text[index] <= '9'; };

        Version l_Version;
        std::size_t l_At = 0;
        for (std::size_t it_Part = 0; it_Part < l_Version.Numbers.size() && a_IsDigit(l_At); ++it_Part)
        {
            std::uint32_t l_Number = 0;
            for (; a_IsDigit(l_At); ++l_At)
            {
                l_Number = std::min<std::uint32_t>(l_Number * 10 + static_cast<std::uint32_t>(text[l_At] - '0'), 0xFFFF);
            }

            l_Version.Numbers[it_Part] = static_cast<std::uint16_t>(l_Number);
            l_Version.Numeric = true;
            if (it_Part + 1 == l_Version.Numbers.size() || l_At >= text.size() || text[l_At] != '.' || !a_IsDigit(l_At + 1))
            {
                break;
            }

            ++l_At;
        }

        l_Version.PreRelease = l_Version.Numeric && l_At < text.size() && text[l_At] == '-';

        return l_Version;
    }

    Trinity::Expected<std::vector<PortableExecutable::Resource>, std::string> MakeIcon(const std::filesystem::path& source)
    {
        std::ifstream l_File(source, std::ios::binary);
        const std::vector<char> l_Encoded{ std::istreambuf_iterator<char>(l_File), std::istreambuf_iterator<char>() };
        if (!l_File && !l_File.eof())
        {
            return Trinity::Unexpected{ std::string("it could not be read") };
        }

        int l_Width = 0;
        int l_Height = 0;
        int l_Channels = 0;
        const std::unique_ptr<stbi_uc, ImageDeleter> l_Pixels(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(l_Encoded.data()), static_cast<int>(l_Encoded.size()), &l_Width, &l_Height, &l_Channels, 4));
        if (l_Pixels == nullptr)
        {
            return Trinity::Unexpected{ std::format("it could not be decoded: {}", stbi_failure_reason() != nullptr ? stbi_failure_reason() : "unknown") };
        }

        std::vector<PortableExecutable::Resource> l_Resources;
        PortableExecutable::Resource l_Group;
        l_Group.Type.Number = c_GroupIconType;
        l_Group.Name.Number = c_IconGroup;
        l_Group.Language = c_Language;
        Append<std::uint16_t>(l_Group.Data, 0);
        Append<std::uint16_t>(l_Group.Data, 1);
        Append<std::uint16_t>(l_Group.Data, static_cast<std::uint16_t>(c_IconSizes.size()));
        for (std::size_t it_Frame = 0; it_Frame < c_IconSizes.size(); ++it_Frame)
        {
            const std::uint32_t l_Size = c_IconSizes[it_Frame];
            const std::vector<std::uint8_t> l_Frame = MakeFrame(l_Pixels.get(), l_Width, l_Height, l_Size);
            PortableExecutable::Resource& l_Icon = l_Resources.emplace_back();
            l_Icon.Type.Number = c_IconType;
            l_Icon.Name.Number = static_cast<std::uint16_t>(it_Frame + 1);
            l_Icon.Language = c_Language;
            l_Icon.Data = l_Size > c_LargestBitmap ? EncodePng(l_Frame, l_Size) : EncodeBitmap(l_Frame, l_Size);
            if (l_Icon.Data.empty())
            {
                return Trinity::Unexpected{ std::format("its {} px frame could not be encoded", l_Size) };
            }

            // Its width and height, 0 meaning 256, no palette, one plane of 32 bits, its size and which RT_ICON it is
            Append<std::uint8_t>(l_Group.Data, static_cast<std::uint8_t>(l_Size >= 256 ? 0 : l_Size));
            Append<std::uint8_t>(l_Group.Data, static_cast<std::uint8_t>(l_Size >= 256 ? 0 : l_Size));
            Append<std::uint8_t>(l_Group.Data, 0);
            Append<std::uint8_t>(l_Group.Data, 0);
            Append<std::uint16_t>(l_Group.Data, 1);
            Append<std::uint16_t>(l_Group.Data, 32);
            Append<std::uint32_t>(l_Group.Data, static_cast<std::uint32_t>(l_Icon.Data.size()));
            Append<std::uint16_t>(l_Group.Data, l_Icon.Name.Number);
        }

        l_Resources.push_back(std::move(l_Group));

        return l_Resources;
    }

    PortableExecutable::Resource MakeVersion(const Trinity::ExportSettings& settings, std::string_view fileName)
    {
        constexpr std::uint32_t c_PreRelease = 0x2;
        constexpr std::uint32_t c_WindowsNT = 0x00040004;
        constexpr std::uint32_t c_Application = 0x1;

        const Version l_Version = ParseVersion(settings.Version);
        const std::uint32_t l_High = (static_cast<std::uint32_t>(l_Version.Numbers[0]) << 16) | l_Version.Numbers[1];
        const std::uint32_t l_Low = (static_cast<std::uint32_t>(l_Version.Numbers[2]) << 16) | l_Version.Numbers[3];

        VersionBlock l_Root;
        l_Root.Key = u"VS_VERSION_INFO";
        l_Root.Text = false;
        for (const std::uint32_t it_Field : { 0xFEEF04BDu, 0x00010000u, l_High, l_Low, l_High, l_Low, 0x3Fu, l_Version.PreRelease ? c_PreRelease : 0u, c_WindowsNT, c_Application, 0u, 0u, 0u })
        {
            Append<std::uint32_t>(l_Root.Value, it_Field);
        }

        l_Root.ValueLength = static_cast<std::uint16_t>(l_Root.Value.size());

        VersionBlock l_Table;
        l_Table.Key = u"040904B0";
        l_Table.Children.push_back(MakeString(u"CompanyName", settings.Company));
        l_Table.Children.push_back(MakeString(u"FileDescription", settings.Product));
        l_Table.Children.push_back(MakeString(u"FileVersion", settings.Version));
        l_Table.Children.push_back(MakeString(u"InternalName", settings.Product));
        l_Table.Children.push_back(MakeString(u"LegalCopyright", std::format("Copyright \xC2\xA9 {}", settings.Company)));
        l_Table.Children.push_back(MakeString(u"OriginalFilename", fileName));
        l_Table.Children.push_back(MakeString(u"ProductName", settings.Product));
        l_Table.Children.push_back(MakeString(u"ProductVersion", settings.Version));

        VersionBlock l_Strings;
        l_Strings.Key = u"StringFileInfo";
        l_Strings.Children.push_back(std::move(l_Table));

        VersionBlock l_Translation;
        l_Translation.Key = u"Translation";
        l_Translation.Text = false;
        Append<std::uint16_t>(l_Translation.Value, c_Language);
        Append<std::uint16_t>(l_Translation.Value, c_CodePage);
        l_Translation.ValueLength = static_cast<std::uint16_t>(l_Translation.Value.size());

        VersionBlock l_Variables;
        l_Variables.Key = u"VarFileInfo";
        l_Variables.Children.push_back(std::move(l_Translation));

        l_Root.Children.push_back(std::move(l_Strings));
        l_Root.Children.push_back(std::move(l_Variables));

        PortableExecutable::Resource l_Resource;
        l_Resource.Type.Number = c_VersionType;
        l_Resource.Name.Number = 1;
        l_Resource.Language = c_Language;
        WriteBlock(l_Resource.Data, l_Root);

        return l_Resource;
    }
}