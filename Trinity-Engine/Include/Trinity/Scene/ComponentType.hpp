#pragma once

#include <entt/core/fwd.hpp>
#include <entt/core/hashed_string.hpp>
#include <entt/core/type_info.hpp>

#include <concepts>
#include <string_view>

namespace Trinity
{
    // A component names itself, so its pool has the same id in the engine and in every module, whichever compiler built them. EnTT would otherwise hash the compiler's spelling of the type, which differs between MSVC and Clang
    template<typename T>
    concept Component = requires
    {
        { T::c_TypeName } -> std::convertible_to<std::string_view>;
    };
}

namespace entt
{
    template<Trinity::Component Type>
    struct type_hash<Type> final
    {
        [[nodiscard]] static constexpr id_type value() noexcept
        {
            constexpr std::string_view c_Name = Type::c_TypeName;

            return hashed_string::value(c_Name.data(), c_Name.size());
        }

        [[nodiscard]] constexpr operator id_type() const noexcept
        {
            return value();
        }
    };

    template<Trinity::Component Type>
    struct type_name<Type> final
    {
        [[nodiscard]] static constexpr std::string_view value() noexcept
        {
            return Type::c_TypeName;
        }

        [[nodiscard]] constexpr operator std::string_view() const noexcept
        {
            return value();
        }
    };
}