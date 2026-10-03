#include "Trinity/Core/ConsoleVariable.hpp"

#include "Trinity/Core/Application.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <system_error>

namespace Trinity
{
    namespace
    {
        std::map<std::string_view, ConsoleVariableBase*> s_Variables;
        bool s_Initialized = false;

        bool EqualsIgnoreCase(std::string_view left, std::string_view right)
        {
            return std::ranges::equal(left, right, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
        }

        void Register(ConsoleVariableBase* variable)
        {
            if (!s_Variables.emplace(variable->GetName(), variable).second)
            {
                TR_CORE_ERROR("Console variable '{}' is defined more than once; only one definition is reachable by name", variable->GetName());
            }
        }

        bool SetVariable(std::string_view name, std::string_view value, bool allowReadOnly)
        {
            ConsoleVariableBase* l_Variable = ConsoleVariables::Find(name);
            if (l_Variable == nullptr)
            {
                TR_CORE_WARN("Unknown console variable '{}'", name);

                return false;
            }

            if (!allowReadOnly && HasFlag(l_Variable->GetFlags(), ConsoleVariableFlags::ReadOnly))
            {
                TR_CORE_WARN("Console variable '{}' is read-only; set it on the command line with --set={}=<value>", name, name);

                return false;
            }

            if (!l_Variable->SetFromString(value))
            {
                TR_CORE_WARN("Console variable '{}' expects type {}, got '{}'", name, ToString(l_Variable->GetType()), value);

                return false;
            }

            TR_CORE_INFO("{} = {}", name, l_Variable->ToString());

            return true;
        }
    }

    constinit ConsoleVariableBase* ConsoleVariableBase::s_First = nullptr;

    std::string_view ToString(ConsoleVariableType type)
    {
        switch (type)
        {
            case ConsoleVariableType::Bool:
            {
                return "bool";
            }
            case ConsoleVariableType::Int:
            {
                return "int";
            }
            case ConsoleVariableType::Float:
            {
                return "float";
            }
            case ConsoleVariableType::String:
            {
                return "string";
            }
        }

        return "unknown";
    }

    ConsoleVariableBase::ConsoleVariableBase(std::string_view name, std::string_view description, ConsoleVariableType type, ConsoleVariableFlags flags) : m_Name(name), m_Description(description), m_Type(type), m_Flags(flags), m_Next(s_First)
    {
        s_First = this;

        // Variables that exist at startup are indexed by Initialize; one from a module loaded later is indexed here
        if (s_Initialized)
        {
            Register(this);
        }
    }

    ConsoleVariableBase::~ConsoleVariableBase()
    {
        if (s_Initialized)
        {
            const auto a_Iterator = s_Variables.find(m_Name);
            if (a_Iterator != s_Variables.end() && a_Iterator->second == this)
            {
                s_Variables.erase(a_Iterator);
            }
        }

        for (ConsoleVariableBase** it_Link = &s_First; *it_Link != nullptr; it_Link = &(*it_Link)->m_Next)
        {
            if (*it_Link == this)
            {
                *it_Link = m_Next;

                break;
            }
        }
    }

    std::optional<bool> ConsoleVariableBase::ParseBool(std::string_view text)
    {
        for (const std::string_view it_True : { "1", "true", "on", "yes" })
        {
            if (EqualsIgnoreCase(text, it_True))
            {
                return true;
            }
        }

        for (const std::string_view it_False : { "0", "false", "off", "no" })
        {
            if (EqualsIgnoreCase(text, it_False))
            {
                return false;
            }
        }

        return std::nullopt;
    }

    std::optional<std::int32_t> ConsoleVariableBase::ParseInt(std::string_view text)
    {
        std::int32_t l_Value = 0;
        const auto [a_End, a_Error] = std::from_chars(text.data(), text.data() + text.size(), l_Value);
        if (text.empty() || a_Error != std::errc{} || a_End != text.data() + text.size())
        {
            return std::nullopt;
        }

        return l_Value;
    }

    std::optional<float> ConsoleVariableBase::ParseFloat(std::string_view text)
    {
        float l_Value = 0.0f;
        const auto [a_End, a_Error] = std::from_chars(text.data(), text.data() + text.size(), l_Value);
        if (text.empty() || a_Error != std::errc{} || a_End != text.data() + text.size() || !std::isfinite(l_Value))
        {
            return std::nullopt;
        }

        return l_Value;
    }

    std::string ConsoleVariableBase::Format(bool value)
    {
        return value ? "true" : "false";
    }

    std::string ConsoleVariableBase::Format(std::int32_t value)
    {
        return std::format("{}", value);
    }

    std::string ConsoleVariableBase::Format(float value)
    {
        return std::format("{}", value);
    }

    namespace ConsoleVariables
    {
        void Initialize(const ApplicationCommandLineArgs& args)
        {
            TR_CORE_ASSERT(!s_Initialized, "Console variables are already initialized.");

            for (ConsoleVariableBase* it_Variable = ConsoleVariableBase::GetFirst(); it_Variable != nullptr; it_Variable = it_Variable->GetNext())
            {
                Register(it_Variable);
            }

            s_Initialized = true;

            for (const std::string_view it_Assignment : args.GetAllOptions("set"))
            {
                const std::size_t l_Equals = it_Assignment.find('=');
                if (l_Equals == std::string_view::npos || l_Equals == 0)
                {
                    TR_CORE_WARN("Ignoring --set={}: expected --set=name=value", it_Assignment);

                    continue;
                }

                SetVariable(it_Assignment.substr(0, l_Equals), it_Assignment.substr(l_Equals + 1), true);
            }

            TR_CORE_INFO("{} console variable(s) registered", s_Variables.size());
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_Initialized, "Console variables are not initialized.");

            s_Variables = std::map<std::string_view, ConsoleVariableBase*>();
            s_Initialized = false;
        }

        ConsoleVariableBase* Find(std::string_view name)
        {
            TR_CORE_ASSERT(s_Initialized, "Console variables are not initialized.");

            const auto a_Iterator = s_Variables.find(name);

            return a_Iterator != s_Variables.end() ? a_Iterator->second : nullptr;
        }

        bool Set(std::string_view name, std::string_view value)
        {
            return SetVariable(name, value, false);
        }

        void LogAll()
        {
            TR_CORE_INFO("Console variables:");

            for (const auto& [a_Name, a_Variable] : s_Variables)
            {
                TR_CORE_INFO("  {} = {} ({}, default {}{}) - {}", a_Name, a_Variable->ToString(), ToString(a_Variable->GetType()), a_Variable->DefaultToString(), HasFlag(a_Variable->GetFlags(), ConsoleVariableFlags::ReadOnly) ? ", read-only" : "", a_Variable->GetDescription());
            }
        }
    }
}