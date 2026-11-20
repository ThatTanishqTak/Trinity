#pragma once

#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"
#include "Trinity/Scripting/ScriptClassRegistry.hpp"
#include "Trinity/Scripting/ScriptField.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Trinity
{
    class Entity;

    // By its full name, such as Game.Health, with its fields in declaration order, typed as a script's
    struct NativeComponentDescription
    {
        TaggedString<MemoryTag::Scripting> Name;
        std::vector<ScriptFieldDescription, TaggedAllocator<ScriptFieldDescription, MemoryTag::Scripting>> Fields;

        [[nodiscard]] const ScriptFieldDescription* FindField(std::string_view name) const;
    };

    // A scene keeps one as YAML while its module is unloaded, and loads it again after. Main thread
    namespace NativeComponents
    {
        // A field's value read from a component, and given to one
        using GetFieldFunction = ScriptFieldValue(*)(const void* component);
        using SetFieldFunction = void (*)(void* component, const ScriptFieldValue& value);

        struct FieldAccess
        {
            GetFieldFunction Get = nullptr;
            SetFieldFunction Set = nullptr;
        };

        // Refused for a name already known, a repeated field name, or a field without its functions
        TRINITY_API bool Register(const NativeComponentDescription& description, std::span<const FieldAccess> fields, const ComponentSerializer& serializer);
        // The serializer first, which keeps each component as YAML while its module's code is still loaded
        TRINITY_API void Unregister(std::string_view name);

        // Valid until the next Register or Unregister
        [[nodiscard]] TRINITY_API const NativeComponentDescription* Find(std::string_view name);
        // Changes on every Register and Unregister
        [[nodiscard]] TRINITY_API std::uint64_t GetGeneration();

        // Each field of the component, written with its type, as a script's field is
        TRINITY_API void WriteFields(std::string_view name, const void* component, ComponentWriter& writer);
        // A field that is missing, or written as another type, keeps the value the component has
        TRINITY_API void ReadFields(std::string_view name, void* component, const ComponentReader& reader);

        // None when the entity is without it, no loaded code knows it, or it has no such field
        [[nodiscard]] TRINITY_API std::optional<ScriptFieldValue> GetField(Entity entity, std::string_view component, std::string_view field);
        // Refused as GetField finds nothing, or for a value of another type than the field's
        TRINITY_API bool SetField(Entity entity, std::string_view component, std::string_view field, const ScriptFieldValue& value);
    }
}