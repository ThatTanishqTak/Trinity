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

    // So the runtime calls only those, which matters most where a call crosses into managed code
    namespace ScriptCallbacks
    {
        constexpr std::uint32_t Create = 1u << 0;
        constexpr std::uint32_t FixedUpdate = 1u << 1;
        constexpr std::uint32_t Update = 1u << 2;
        constexpr std::uint32_t LateUpdate = 1u << 3;
        constexpr std::uint32_t Destroy = 1u << 4;
        // Any of the contact, trigger, joint and character hit callbacks
        constexpr std::uint32_t Physics = 1u << 5;
        constexpr std::uint32_t Reloaded = 1u << 6;
        constexpr std::uint32_t All = Create | FixedUpdate | Update | LateUpdate | Destroy | Physics | Reloaded;
    }

    // One named value of an enum field, in the order the enum declares them
    struct ScriptEnumValue
    {
        TaggedString<MemoryTag::Scripting> Name;
        std::int64_t Value = 0;
    };

    // Its type through its default value, an enum's named values, and the asset type a reference takes
    struct ScriptFieldDescription
    {
        TaggedString<MemoryTag::Scripting> Name;
        ScriptFieldValue Default;
        std::vector<ScriptEnumValue, TaggedAllocator<ScriptEnumValue, MemoryTag::Scripting>> EnumValues;
        TaggedString<MemoryTag::Scripting> AssetType;

        [[nodiscard]] ScriptFieldType GetType() const { return Default.GetType(); }
    };

    // By its full name, such as Game.PlayerController, with its fields in declaration order
    struct ScriptClassDescription
    {
        TaggedString<MemoryTag::Scripting> Name;
        ScriptLanguage Language = ScriptLanguage::CSharp;
        std::uint32_t Callbacks = ScriptCallbacks::All;
        std::vector<ScriptFieldDescription, TaggedAllocator<ScriptFieldDescription, MemoryTag::Scripting>> Fields;

        [[nodiscard]] const ScriptFieldDescription* FindField(std::string_view name) const
        {
            const auto a_Found = std::ranges::find_if(Fields, [name](const ScriptFieldDescription& field) { return std::string_view(field.Name) == name; });

            return a_Found != Fields.end() ? &*a_Found : nullptr;
        }
    };

    // Each language's backend registers its classes as its code loads. Main thread only
    namespace ScriptClassRegistry
    {
        TRINITY_API void Initialize();
        TRINITY_API void Shutdown();

        // Copies it. Refused for a name the language already has, or a repeated field name
        TRINITY_API bool Register(const ScriptClassDescription& description);
        TRINITY_API void Unregister(ScriptLanguage language, std::string_view name);
        // Every class of one language, as its code unloads for a reload
        TRINITY_API void UnregisterAll(ScriptLanguage language);

        // Valid until the next Register or Unregister
        [[nodiscard]] TRINITY_API const ScriptClassDescription* Find(ScriptLanguage language, std::string_view name);
        // Sorted by language, then by name, valid until the next Register or Unregister
        [[nodiscard]] TRINITY_API std::vector<const ScriptClassDescription*> GetClasses();
        // Changes on every Register and Unregister, so a list built from the classes knows to rebuild
        [[nodiscard]] TRINITY_API std::uint64_t GetGeneration();

        // An entry for the class with every field at its default, enabled
        [[nodiscard]] TRINITY_API ScriptEntry CreateEntry(const ScriptClassDescription& description);
        // The entry's own value when it has one of the field's type, else the field's default
        [[nodiscard]] TRINITY_API const ScriptFieldValue& GetFieldValue(const ScriptEntry& entry, const ScriptFieldDescription& field);
    }
}