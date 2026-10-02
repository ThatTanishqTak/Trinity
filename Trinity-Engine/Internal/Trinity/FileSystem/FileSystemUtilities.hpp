#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

namespace Trinity
{
    namespace FileSystemUtilities
    {
        [[nodiscard]] inline std::filesystem::path FromUtf8(std::string_view text)
        {
            return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        [[nodiscard]] inline std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string l_Text = path.u8string();

            return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
        }

        [[nodiscard]] inline char ToLowerAscii(char character)
        {
            return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
        }

        [[nodiscard]] inline bool EqualsIgnoreAsciiCase(std::string_view left, std::string_view right)
        {
            return std::ranges::equal(left, right, [](char a, char b) { return ToLowerAscii(a) == ToLowerAscii(b); });
        }

        [[nodiscard]] inline bool StartsWithIgnoreAsciiCase(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && EqualsIgnoreAsciiCase(text.substr(0, prefix.size()), prefix);
        }
    }
}