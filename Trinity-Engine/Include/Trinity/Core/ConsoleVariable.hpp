#pragma once

#include "Trinity/Core/Export.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Trinity
{
    struct ApplicationCommandLineArgs;

    enum class ConsoleVariableType : std::uint8_t
    {
        Bool,
        Int,
        Float,
        String
    };

    enum class ConsoleVariableFlags : std::uint32_t
    {
        None = 0,
        ReadOnly = 1u << 0
    };

    [[nodiscard]] constexpr ConsoleVariableFlags operator|(ConsoleVariableFlags left, ConsoleVariableFlags right)
    {
        return static_cast<ConsoleVariableFlags>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(ConsoleVariableFlags flags, ConsoleVariableFlags flag)
    {
        return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
    }

    [[nodiscard]] TRINITY_API std::string_view ToString(ConsoleVariableType type);

    class TRINITY_API ConsoleVariableBase
    {
    public:
        virtual ~ConsoleVariableBase();

        ConsoleVariableBase(const ConsoleVariableBase&) = delete;
        ConsoleVariableBase& operator=(const ConsoleVariableBase&) = delete;

        [[nodiscard]] std::string_view GetName() const { return m_Name; }
        [[nodiscard]] std::string_view GetDescription() const { return m_Description; }
        [[nodiscard]] ConsoleVariableType GetType() const { return m_Type; }
        [[nodiscard]] ConsoleVariableFlags GetFlags() const { return m_Flags; }

        [[nodiscard]] virtual bool SetFromString(std::string_view text) = 0;
        [[nodiscard]] virtual std::string ToString() const = 0;
        [[nodiscard]] virtual std::string DefaultToString() const = 0;

        [[nodiscard]] static ConsoleVariableBase* GetFirst() { return s_First; }
        [[nodiscard]] ConsoleVariableBase* GetNext() const { return m_Next; }

    protected:
        ConsoleVariableBase(std::string_view name, std::string_view description, ConsoleVariableType type, ConsoleVariableFlags flags);

        [[nodiscard]] static std::optional<bool> ParseBool(std::string_view text);
        [[nodiscard]] static std::optional<std::int32_t> ParseInt(std::string_view text);
        [[nodiscard]] static std::optional<float> ParseFloat(std::string_view text);

        [[nodiscard]] static std::string Format(bool value);
        [[nodiscard]] static std::string Format(std::int32_t value);
        [[nodiscard]] static std::string Format(float value);

    private:
        std::string_view m_Name;
        std::string_view m_Description;
        ConsoleVariableType m_Type;
        ConsoleVariableFlags m_Flags;
        ConsoleVariableBase* m_Next = nullptr;

        static ConsoleVariableBase* s_First;
    };

    template<typename T>
    class ConsoleVariable final : public ConsoleVariableBase
    {
        static_assert(std::is_same_v<T, bool> || std::is_same_v<T, std::int32_t> || std::is_same_v<T, float>, "Console variables hold bool, std::int32_t, float or std::string.");

    public:
        using ChangedCallback = void (*)(T value);

        ConsoleVariable(std::string_view name, T defaultValue, std::string_view description, ConsoleVariableFlags flags = ConsoleVariableFlags::None, ChangedCallback onChanged = nullptr)
            : ConsoleVariableBase(name, description, GetTypeOf(), flags), m_Value(defaultValue), m_Default(defaultValue), m_OnChanged(onChanged)
        {

        }

        [[nodiscard]] T Get() const { return m_Value.load(std::memory_order_relaxed); }
        [[nodiscard]] T GetDefault() const { return m_Default; }

        void Set(T value)
        {
            const T l_Previous = m_Value.exchange(value, std::memory_order_relaxed);
            if (l_Previous != value && m_OnChanged != nullptr)
            {
                m_OnChanged(value);
            }
        }

        [[nodiscard]] bool SetFromString(std::string_view text) override
        {
            std::optional<T> l_Value;
            if constexpr (std::is_same_v<T, bool>)
            {
                l_Value = ParseBool(text);
            }
            else if constexpr (std::is_same_v<T, std::int32_t>)
            {
                l_Value = ParseInt(text);
            }
            else
            {
                l_Value = ParseFloat(text);
            }

            if (!l_Value)
            {
                return false;
            }

            Set(*l_Value);

            return true;
        }

        [[nodiscard]] std::string ToString() const override { return Format(Get()); }
        [[nodiscard]] std::string DefaultToString() const override { return Format(m_Default); }

    private:
        static constexpr ConsoleVariableType GetTypeOf()
        {
            if constexpr (std::is_same_v<T, bool>)
            {
                return ConsoleVariableType::Bool;
            }
            else if constexpr (std::is_same_v<T, std::int32_t>)
            {
                return ConsoleVariableType::Int;
            }
            else
            {
                return ConsoleVariableType::Float;
            }
        }

        std::atomic<T> m_Value;
        T m_Default;
        ChangedCallback m_OnChanged;
    };

    template<>
    class ConsoleVariable<std::string> final : public ConsoleVariableBase
    {
    public:
        using ChangedCallback = void (*)(const std::string& value);

        ConsoleVariable(std::string_view name, std::string defaultValue, std::string_view description, ConsoleVariableFlags flags = ConsoleVariableFlags::None, ChangedCallback onChanged = nullptr)
            : ConsoleVariableBase(name, description, ConsoleVariableType::String, flags), m_Value(defaultValue), m_Default(std::move(defaultValue)), m_OnChanged(onChanged)
        {

        }

        [[nodiscard]] std::string Get() const
        {
            std::scoped_lock l_Lock(m_Mutex);

            return m_Value;
        }

        [[nodiscard]] const std::string& GetDefault() const { return m_Default; }

        void Set(std::string value)
        {
            bool l_Changed = false;
            {
                std::scoped_lock l_Lock(m_Mutex);
                l_Changed = m_Value != value;
                m_Value = value;
            }

            if (l_Changed && m_OnChanged != nullptr)
            {
                m_OnChanged(value);
            }
        }

        [[nodiscard]] bool SetFromString(std::string_view text) override
        {
            Set(std::string(text));

            return true;
        }

        [[nodiscard]] std::string ToString() const override { return Get(); }
        [[nodiscard]] std::string DefaultToString() const override { return m_Default; }

    private:
        mutable std::mutex m_Mutex;
        std::string m_Value;
        std::string m_Default;
        ChangedCallback m_OnChanged;
    };

    namespace ConsoleVariables
    {
        // Applies every --set=name=value on the command line; read-only variables may be set here and nowhere else.
        TRINITY_API void Initialize(const ApplicationCommandLineArgs& args);
        TRINITY_API void Shutdown();

        [[nodiscard]] TRINITY_API ConsoleVariableBase* Find(std::string_view name);

        // The path a console or config file takes: logs the outcome and refuses read-only variables.
        TRINITY_API bool Set(std::string_view name, std::string_view value);

        TRINITY_API void LogAll();

        // A console's command line: "name" shows a variable, "name value" sets it through Set, "help" lists every variable and "help name" describes one
        TRINITY_API bool Execute(std::string_view line);
    }
}