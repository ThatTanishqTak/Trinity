#include "Trinity/Scripting/ScriptClassRegistry.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Scene/Components.hpp"

#include <algorithm>
#include <tuple>

namespace Trinity
{
    namespace
    {
        struct State
        {
            std::vector<ScriptClassDescription, TaggedAllocator<ScriptClassDescription, MemoryTag::Scripting>> Classes;
            std::uint64_t Generation = 0;
        };

        State* s_State = nullptr;

        auto FindClass(ScriptLanguage language, std::string_view name)
        {
            return std::ranges::find_if(s_State->Classes, [language, name](const ScriptClassDescription& description) { return description.Language == language && std::string_view(description.Name) == name; });
        }

        // Why a description cannot be registered, or nothing when it can
        std::string_view FindProblem(const ScriptClassDescription& description)
        {
            if (description.Name.empty())
            {
                return "it has no name";
            }

            for (auto it_Field = description.Fields.begin(); it_Field != description.Fields.end(); ++it_Field)
            {
                if (it_Field->Name.empty())
                {
                    return "a field has no name";
                }

                if (std::any_of(description.Fields.begin(), it_Field, [&it_Field](const ScriptFieldDescription& field) { return field.Name == it_Field->Name; }))
                {
                    return "a field name repeats";
                }

                if ((it_Field->GetType() == ScriptFieldType::Enum) == it_Field->EnumValues.empty())
                {
                    return "an enum field has no values, or another field has enum values";
                }
            }

            return {};
        }
    }

    namespace ScriptClassRegistry
    {
        void Initialize()
        {
            TR_CORE_ASSERT(s_State == nullptr, "The script class registry is already initialized.");

            s_State = Memory::New<State>(MemoryTag::Scripting);
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The script class registry is not initialized.");

            if (!s_State->Classes.empty())
            {
                TR_CORE_WARN("Scripting: {} script class(es) were still registered at shutdown", s_State->Classes.size());
            }

            Memory::Delete(s_State);
            s_State = nullptr;
        }

        bool Register(const ScriptClassDescription& description)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The script class registry is not initialized.");

            if (const std::string_view l_Problem = FindProblem(description); !l_Problem.empty())
            {
                TR_CORE_ERROR("Scripting: {} class {} cannot be registered, because {}", ToString(description.Language), std::string_view(description.Name), l_Problem);

                return false;
            }

            if (FindClass(description.Language, description.Name) != s_State->Classes.end())
            {
                TR_CORE_ERROR("Scripting: a {} class {} is already registered", ToString(description.Language), std::string_view(description.Name));

                return false;
            }

            s_State->Classes.push_back(description);
            std::ranges::sort(s_State->Classes, [](const ScriptClassDescription& first, const ScriptClassDescription& second) { return std::tie(first.Language, first.Name) < std::tie(second.Language, second.Name); });
            ++s_State->Generation;

            return true;
        }

        void Unregister(ScriptLanguage language, std::string_view name)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The script class registry is not initialized.");

            const auto a_Found = FindClass(language, name);
            if (a_Found != s_State->Classes.end())
            {
                s_State->Classes.erase(a_Found);
                ++s_State->Generation;
            }
        }

        void UnregisterAll(ScriptLanguage language)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The script class registry is not initialized.");

            if (std::erase_if(s_State->Classes, [language](const ScriptClassDescription& description) { return description.Language == language; }) != 0)
            {
                ++s_State->Generation;
            }
        }

        const ScriptClassDescription* Find(ScriptLanguage language, std::string_view name)
        {
            TR_CORE_ASSERT(s_State != nullptr, "The script class registry is not initialized.");

            const auto a_Found = FindClass(language, name);

            return a_Found != s_State->Classes.end() ? &*a_Found : nullptr;
        }

        std::vector<const ScriptClassDescription*> GetClasses()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The script class registry is not initialized.");

            std::vector<const ScriptClassDescription*> l_Classes;
            l_Classes.reserve(s_State->Classes.size());
            for (const ScriptClassDescription& it_Class : s_State->Classes)
            {
                l_Classes.push_back(&it_Class);
            }

            return l_Classes;
        }

        std::uint64_t GetGeneration()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The script class registry is not initialized.");

            return s_State->Generation;
        }

        ScriptEntry CreateEntry(const ScriptClassDescription& description)
        {
            ScriptEntry l_Entry;
            l_Entry.ClassName = std::string_view(description.Name);
            l_Entry.Language = description.Language;
            l_Entry.Fields.reserve(description.Fields.size());
            for (const ScriptFieldDescription& it_Field : description.Fields)
            {
                l_Entry.Fields.push_back({ TaggedString<MemoryTag::Scene>(std::string_view(it_Field.Name)), it_Field.Default });
            }

            return l_Entry;
        }

        const ScriptFieldValue& GetFieldValue(const ScriptEntry& entry, const ScriptFieldDescription& field)
        {
            const ScriptEntry::Field* l_Value = entry.FindField(field.Name);

            return l_Value != nullptr && l_Value->Value.GetType() == field.GetType() ? l_Value->Value : field.Default;
        }
    }
}