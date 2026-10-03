#pragma once

#include "Trinity/Core/Export.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Trinity
{
    // A random 64-bit identifier. Zero is reserved as the invalid UUID
    class TRINITY_API UUID
    {
    public:
        constexpr UUID() = default;
        constexpr explicit UUID(std::uint64_t value) : m_Value(value)
        {

        }

        [[nodiscard]] static UUID Generate();

        // Accepts exactly 16 hexadecimal digits in either case
        [[nodiscard]] static std::optional<UUID> Parse(std::string_view text);

        [[nodiscard]] constexpr std::uint64_t GetValue() const { return m_Value; }
        [[nodiscard]] constexpr bool IsValid() const { return m_Value != 0; }
        constexpr explicit operator bool() const { return IsValid(); }

        [[nodiscard]] constexpr std::array<char, 16> ToHex() const
        {
            constexpr std::string_view c_Digits = "0123456789abcdef";

            std::array<char, 16> l_Result{};
            for (std::size_t it_Index = 0; it_Index < l_Result.size(); ++it_Index)
            {
                l_Result[l_Result.size() - 1 - it_Index] = c_Digits[static_cast<std::size_t>((m_Value >> (it_Index * 4)) & 0xF)];
            }

            return l_Result;
        }

        [[nodiscard]] std::string ToString() const
        {
            const std::array<char, 16> l_Hex = ToHex();

            return { l_Hex.data(), l_Hex.size() };
        }

        constexpr auto operator<=>(const UUID&) const = default;

    private:
        std::uint64_t m_Value = 0;
    };
}

template<>
struct std::hash<Trinity::UUID>
{
    std::size_t operator()(const Trinity::UUID& uuid) const noexcept
    {
        return std::hash<std::uint64_t>{}(uuid.GetValue());
    }
};

template<>
struct std::formatter<Trinity::UUID> : std::formatter<std::string_view>
{
    auto format(const Trinity::UUID& uuid, std::format_context& context) const
    {
        const std::array<char, 16> l_Hex = uuid.ToHex();

        return std::formatter<std::string_view>::format(std::string_view(l_Hex.data(), l_Hex.size()), context);
    }
};