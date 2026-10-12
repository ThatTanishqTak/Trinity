#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Scripting/ScriptField.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Trinity
{
    struct ScriptEntry;

    // One named value of an enum field, in the order the enum declares them
    struct ScriptEnumValue
    {
        TaggedString<MemoryTag::Scripting> Name;
        std::int64_t Value = 0;
    };

    // A field a script class shows in Properties: its name, its type through its default value, and for an enum its named values, or for an asset reference the asset type it takes, empty for any
    struct ScriptFieldDescription
    {
        TaggedString<MemoryTag::Scripting> Name;
        ScriptFieldValue Default;
        std::vector<ScriptEnumValue, TaggedAllocator<ScriptEnumValue, MemoryTag::Scripting>> EnumValues;
        TaggedString<MemoryTag::Scripting> AssetType;

        [[nodiscard]] ScriptFieldType GetType() const { return Default.GetType(); }
    };

    // A script class some loaded code has, by its full name, such as Game.PlayerController, with its fields in declaration order
    struct ScriptClassDescription
    {
        TaggedString<MemoryTag::Scripting> Name;
        ScriptLanguage Language = ScriptLanguage::CSharp;
        std::vector<ScriptFieldDescription, TaggedAllocator<ScriptFieldDescription, MemoryTag::Scripting>> Fields;

        [[nodiscard]] const ScriptFieldDescription* FindField(std::string_view name) const
        {
            const auto a_Found = std::ranges::find_if(Fields, [name](const ScriptFieldDescription& field) { return std::string_view(field.Name) == name; });

            return a_Found != Fields.end() ? &*a_Found : nullptr;
        }
    };

    // The script classes the loaded C++ modules and C# assemblies have, which each language's backend registers as its code loads and unregisters before it unloads. A Script component's entries name classes by language and name, and an entry whose class is not here is unknown and kept as it is. Main thread only
    namespace ScriptClassRegistry
    {
        TRINITY_API void Initialize();
        TRINITY_API void Shutdown();

        // Copies the description. Refused when the language already has a class of that name, or a field name repeats or does not match its enum values
        TRINITY_API bool Register(const ScriptClassDescription& description);
        TRINITY_API void Unregister(ScriptLanguage language, std::string_view name);
        // Every class of one language, as its code unloads for a reload
        TRINITY_API void UnregisterAll(ScriptLanguage language);

        // Valid until the next Register or Unregister
        [[nodiscard]] TRINITY_API const ScriptClassDescription* Find(ScriptLanguage language, std::string_view name);
        // Sorted by language, then by name, valid until the next Register or Unregister
        [[nodiscard]] TRINITY_API std::vector<const ScriptClassDescription*> GetClasses();
        // Changes on every Register and Unregister, so a list built from the classes knows to build itself again
        [[nodiscard]] TRINITY_API std::uint64_t GetGeneration();

        // An entry for the class with every field at its default, enabled
        [[nodiscard]] TRINITY_API ScriptEntry CreateEntry(const ScriptClassDescription& description);
        // The value the entry's instance gets for a field of its class: the entry's own when it has one of the field's type, else the field's default
        [[nodiscard]] TRINITY_API const ScriptFieldValue& GetFieldValue(const ScriptEntry& entry, const ScriptFieldDescription& field);
    }
}