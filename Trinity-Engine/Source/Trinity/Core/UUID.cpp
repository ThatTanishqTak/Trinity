#include "Trinity/Core/UUID.hpp"

#include <charconv>
#include <random>
#include <system_error>

namespace Trinity
{
    UUID UUID::Generate()
    {
        thread_local std::mt19937_64 t_Engine = []
        {
            std::random_device l_Device;
            std::seed_seq l_Seed{ l_Device(), l_Device(), l_Device(), l_Device(), l_Device(), l_Device(), l_Device(), l_Device() };

            return std::mt19937_64(l_Seed);
        }();

        std::uint64_t l_Value = 0;
        while (l_Value == 0)
        {
            l_Value = t_Engine();
        }

        return UUID(l_Value);
    }

    std::optional<UUID> UUID::Parse(std::string_view text)
    {
        if (text.size() != 16)
        {
            return std::nullopt;
        }

        std::uint64_t l_Value = 0;
        const auto [a_End, a_Error] = std::from_chars(text.data(), text.data() + text.size(), l_Value, 16);
        if (a_Error != std::errc{} || a_End != text.data() + text.size())
        {
            return std::nullopt;
        }

        return UUID(l_Value);
    }
}