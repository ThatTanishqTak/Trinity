#include "HubText.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace
{
    // More than any manifest, project file or list Hub reads holds
    constexpr std::uintmax_t c_FormatFileLimit = 4 * 1024 * 1024;
}

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

    std::optional<std::chrono::sys_seconds> ParseTime(std::string_view text)
    {
        const auto l_Read = [text](std::size_t offset, std::size_t length, int& value)
        {
            return offset + length <= text.size() && std::from_chars(text.data() + offset, text.data() + offset + length, value).ec == std::errc();
        };

        int l_Year = 0;
        int l_Month = 0;
        int l_Day = 0;
        int l_Hour = 0;
        int l_Minute = 0;
        int l_Second = 0;
        if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':' || text[19] != 'Z'
            || !l_Read(0, 4, l_Year) || !l_Read(5, 2, l_Month) || !l_Read(8, 2, l_Day) || !l_Read(11, 2, l_Hour) || !l_Read(14, 2, l_Minute) || !l_Read(17, 2, l_Second))
        {
            return std::nullopt;
        }

        const std::chrono::year_month_day l_Date{ std::chrono::year(l_Year), std::chrono::month(static_cast<unsigned>(l_Month)), std::chrono::day(static_cast<unsigned>(l_Day)) };
        if (!l_Date.ok() || l_Hour > 23 || l_Minute > 59 || l_Second > 60)
        {
            return std::nullopt;
        }

        return std::chrono::sys_days(l_Date) + std::chrono::hours(l_Hour) + std::chrono::minutes(l_Minute) + std::chrono::seconds(l_Second);
    }

    std::string FormatTime(std::chrono::sys_seconds time)
    {
        return std::format("{:%Y-%m-%dT%H:%M:%SZ}", time);
    }

    std::string DescribeTime(std::chrono::sys_seconds time)
    {
        return std::format("{} at {:%H:%M} UTC", FormatDate(FormatTime(time)), time);
    }

    std::string DescribeAge(std::chrono::sys_seconds time)
    {
        const std::int64_t l_Seconds = std::max<std::int64_t>(0, (std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) - time).count());
        const auto l_Count = [](std::int64_t count, std::string_view unit) { return std::format("{} {}{} ago", count, unit, count == 1 ? "" : "s"); };
        if (l_Seconds < 60)
        {
            return "less than a minute ago";
        }

        if (l_Seconds < 3600)
        {
            return l_Count(l_Seconds / 60, "minute");
        }

        return l_Seconds < 86400 ? l_Count(l_Seconds / 3600, "hour") : l_Count(l_Seconds / 86400, "day");
    }

    std::optional<std::uint32_t> ReadFormat(std::string_view text)
    {
        YAML::Node l_Root;
        try
        {
            l_Root = YAML::Load(std::string(text));
        }
        catch (const YAML::Exception&)
        {
            return std::nullopt;
        }

        const YAML::Node l_Format = l_Root.IsMap() ? l_Root["Format"] : YAML::Node();
        if (!l_Format || !l_Format.IsScalar())
        {
            return std::nullopt;
        }

        std::uint32_t l_Value = 0;
        const std::string& l_Text = l_Format.Scalar();
        const std::from_chars_result l_Parsed = std::from_chars(l_Text.data(), l_Text.data() + l_Text.size(), l_Value);
        if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_Text.data() + l_Text.size() || l_Value == 0)
        {
            return std::nullopt;
        }

        return l_Value;
    }

    std::optional<std::uint32_t> ReadFormat(const std::filesystem::path& file)
    {
        std::error_code l_Error;
        const std::uintmax_t l_Size = std::filesystem::file_size(file, l_Error);
        if (l_Error || l_Size > c_FormatFileLimit)
        {
            return std::nullopt;
        }

        std::ifstream l_File(file, std::ios::binary);
        if (!l_File)
        {
            return std::nullopt;
        }

        const std::string l_Text{ std::istreambuf_iterator<char>(l_File), std::istreambuf_iterator<char>() };
        if (l_File.bad())
        {
            return std::nullopt;
        }

        return ReadFormat(std::string_view(l_Text));
    }
}