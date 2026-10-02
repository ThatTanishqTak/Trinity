#pragma once

#include <optional>
#include <utility>
#include <variant>

namespace Trinity
{
    template<typename E>
    struct Unexpected
    {
        E Error;
    };

    template<typename T, typename E>
    class Expected
    {
    public:
        Expected(T value) : m_Storage(std::in_place_index<0>, std::move(value))
        {

        }

        Expected(Unexpected<E> error) : m_Storage(std::in_place_index<1>, std::move(error.Error))
        {

        }

        [[nodiscard]] bool HasValue() const { return m_Storage.index() == 0; }
        explicit operator bool() const { return HasValue(); }

        [[nodiscard]] T& GetValue()& { return std::get<0>(m_Storage); }
        [[nodiscard]] const T& GetValue() const& { return std::get<0>(m_Storage); }
        [[nodiscard]] T&& GetValue()&& { return std::get<0>(std::move(m_Storage)); }

        [[nodiscard]] const E& GetError() const { return std::get<1>(m_Storage); }

        [[nodiscard]] T& operator*()& { return GetValue(); }
        [[nodiscard]] const T& operator*() const& { return GetValue(); }
        [[nodiscard]] T* operator->() { return &GetValue(); }
        [[nodiscard]] const T* operator->() const { return &GetValue(); }

    private:
        std::variant<T, E> m_Storage;
    };

    template<typename E>
    class Expected<void, E>
    {
    public:
        Expected() = default;

        Expected(Unexpected<E> error) : m_Error(std::move(error.Error))
        {

        }

        [[nodiscard]] bool HasValue() const { return !m_Error.has_value(); }
        explicit operator bool() const { return HasValue(); }

        [[nodiscard]] const E& GetError() const { return *m_Error; }

    private:
        std::optional<E> m_Error;
    };
}