#include "HubText.hpp"

#include <array>
#include <charconv>
#include <format>
#include <system_error>

namespace HubText
{
    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    std::string FormatDate(std::string_view date)
    {
        static constexpr std::array<std::string_view, 12> s_Months{ "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

        int l_Year = 0;
        int l_Month = 0;
        int l_Day = 0;
        const bool l_Read = date.size() >= 10 && date[4] == '-' && date[7] == '-'
            && std::from_chars(date.data(), date.data() + 4, l_Year).ec == std::errc()
            && std::from_chars(date.data() + 5, date.data() + 7, l_Month).ec == std::errc()
            && std::from_chars(date.data() + 8, date.data() + 10, l_Day).ec == std::errc();
        if (!l_Read || l_Month < 1 || l_Month > 12)
        {
            return date.empty() ? std::string("unknown") : std::string(date);
        }

        return std::format("{} {} {}", l_Day, s_Months[static_cast<std::size_t>(l_Month - 1)], l_Year);
    }
}