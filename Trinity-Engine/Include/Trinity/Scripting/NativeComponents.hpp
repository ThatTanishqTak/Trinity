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

    // A component a C++ module has, by its full name, such as Game.Health, with its fields in declaration order. Its fields are typed as a script's are, and a field's default is the value a default-made component has
    struct NativeComponentDescription
    {
        TaggedString<MemoryTag::Scripting> Name;
        std::vector<ScriptFieldDescription, TaggedAllocator<ScriptFieldDescription, MemoryTag::Scripting>> Fields;

        [[nodiscard]] const ScriptFieldDescription* FindField(std::string_view name) const;
    };

    // Components that C++ modules describe by their fields, which save, load and show in Properties through those fields, typed like a script's. The scene serializer knows them as it knows the engine's, so a scene keeps one as YAML while its module is unloaded and loads it again once the module is back. Main thread only
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

        // Registers the description, with a getter and a setter for each field in their order, then the serializer under the same name, whose Save and Load call WriteFields and ReadFields. Refused when a component of that name is known already, a field name repeats or does not match its enum values, or a field lacks its functions
        TRINITY_API bool Register(const NativeComponentDescription& description, std::span<const FieldAccess> fields, const ComponentSerializer& serializer);
        // The serializer first, which keeps every one of the component in each scene as YAML while its module's code is still loaded
        TRINITY_API void Unregister(std::string_view name);

        // Valid until the next Register or Unregister
        [[nodiscard]] TRINITY_API const NativeComponentDescription* Find(std::string_view name);
        // Changes on every Register and Unregister
        [[nodiscard]] TRINITY_API std::uint64_t GetGeneration();

        // Each field of the component, written with its type, as a script's field is
        TRINITY_API void WriteFields(std::string_view name, const void* component, ComponentWriter& writer);
        // Each field the text has with the type the component gives it. A field missing, or written as another type, keeps the value the component has
        TRINITY_API void ReadFields(std::string_view name, void* component, const ComponentReader& reader);

        // A field of the entity's component, or none when the entity is without it, no loaded code knows it, or it has no such field
        [[nodiscard]] TRINITY_API std::optional<ScriptFieldValue> GetField(Entity entity, std::string_view component, std::string_view field);
        // Refused as GetField finds nothing, or for a value of another type than the field's
        TRINITY_API bool SetField(Entity entity, std::string_view component, std::string_view field, const ScriptFieldValue& value);
    }
}