#pragma once

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/Scripting/ScriptClassRegistry.hpp"
#include "Trinity/Scripting/ScriptField.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// Gives an enum the names of its values, so a field can have it and Properties shows it by those names. Written once at global scope, before the TR_SCRIPT or TR_COMPONENT that uses it, with the enum's full name and every value it has, in order: TR_ENUM(Game::Team, Red, Blue, Green)
#define TR_ENUM(Type, ...) \
    template<> \
    struct Trinity::ScriptEnumTraits<Type> \
    { \
        static constexpr std::string_view c_Names = #__VA_ARGS__; \
        [[nodiscard]] static constexpr auto GetValues() \
        { \
            using enum Type; \
            return std::array{ __VA_ARGS__ }; \
        } \
    }

namespace Trinity
{
    // A linear RGBA colour, a vec4 in all but its editor, which is a colour picker. Opaque white unless given another
    struct Color : glm::vec4
    {
        Color() : glm::vec4(1.0f) {}
        Color(float red, float green, float blue, float alpha = 1.0f) : glm::vec4(red, green, blue, alpha) {}
        Color(const glm::vec4& color) : glm::vec4(color) {}
    };

    // An entity named by its UUID, for a component's field, since a component outlives the entity handles of the scene it was copied from. None when the UUID is invalid
    struct EntityReference
    {
        UUID ID;

        [[nodiscard]] bool operator==(const EntityReference&) const = default;
    };

    // An enum's values, in the order they are declared, with their names, which TR_ENUM gives an enum so a field can have it
    template<typename T>
    struct ScriptEnumTraits
    {

    };

    namespace ScriptTypes
    {
        // A function rather than a template argument, since a module imports ScriptFieldValue's functions, and on Windows an imported function's address is not a constant
        [[nodiscard]] inline ScriptFieldValue MakeValue(bool value) { return ScriptFieldValue::Bool(value); }
        [[nodiscard]] inline ScriptFieldValue MakeValue(std::int32_t value) { return ScriptFieldValue::Int32(value); }
        [[nodiscard]] inline ScriptFieldValue MakeValue(std::int64_t value) { return ScriptFieldValue::Int64(value); }
        [[nodiscard]] inline ScriptFieldValue MakeValue(std::uint32_t value) { return ScriptFieldValue::UInt32(value); }
        [[nodiscard]] inline ScriptFieldValue MakeValue(std::uint64_t value) { return ScriptFieldValue::UInt64(value); }
        [[nodiscard]] inline ScriptFieldValue MakeValue(float value) { return ScriptFieldValue::Float(value); }
        [[nodiscard]] inline ScriptFieldValue MakeValue(double value) { return ScriptFieldValue::Double(value); }
        [[nodiscard]] inline ScriptFieldValue MakeValue(const glm::vec2& value) { return ScriptFieldValue::Vector2(value); }
        [[nodiscard]] inline ScriptFieldValue MakeValue(const glm::vec3& value) { return ScriptFieldValue::Vector3(value); }
        [[nodiscard]] inline ScriptFieldValue MakeValue(const glm::vec4& value) { return ScriptFieldValue::Vector4(value); }
        [[nodiscard]] inline ScriptFieldValue MakeValue(const glm::quat& value) { return ScriptFieldValue::Quaternion(value); }

        // The names of TR_ENUM's values, written as one comma-separated list, each paired with its value
        template<typename T, std::size_t N>
        [[nodiscard]] std::vector<ScriptEnumValue, TaggedAllocator<ScriptEnumValue, MemoryTag::Scripting>> MakeEnumValues(std::string_view names, const std::array<T, N>& values)
        {
            std::vector<ScriptEnumValue, TaggedAllocator<ScriptEnumValue, MemoryTag::Scripting>> l_Values;
            l_Values.reserve(N);
            for (std::size_t it_Index = 0; it_Index < N; ++it_Index)
            {
                const std::size_t l_End = std::min(names.find(','), names.size());
                std::string_view l_Name = names.substr(0, l_End);
                names.remove_prefix(std::min(l_End + 1, names.size()));
                while (!l_Name.empty() && (l_Name.front() == ' ' || l_Name.front() == '\t' || l_Name.front() == '\n'))
                {
                    l_Name.remove_prefix(1);
                }

                while (!l_Name.empty() && (l_Name.back() == ' ' || l_Name.back() == '\t' || l_Name.back() == '\n'))
                {
                    l_Name.remove_suffix(1);
                }

                l_Values.push_back({ TaggedString<MemoryTag::Scripting>(l_Name), static_cast<std::int64_t>(values[it_Index]) });
            }

            return l_Values;
        }
    }

    // How a member of each type a field can have becomes a field's value and back. A field of a type with no traits does not compile. Scene is where an Entity member is found, and is null for a component, whose members name entities by EntityReference instead
    template<typename T>
    struct ScriptFieldTraits;

    // A type the field's value holds as it is
    template<typename T>
    struct ScriptValueTraits
    {
        static constexpr bool c_NeedsScene = false;

        static void Describe([[maybe_unused]] ScriptFieldDescription& field) {}
        [[nodiscard]] static ScriptFieldValue ToValue(const T& value) { return ScriptTypes::MakeValue(value); }
        static void FromValue(T& target, const ScriptFieldValue& value, [[maybe_unused]] Scene* scene) { target = value.Get<T>(); }
    };

    template<> struct ScriptFieldTraits<bool> : ScriptValueTraits<bool> {};
    template<> struct ScriptFieldTraits<std::int32_t> : ScriptValueTraits<std::int32_t> {};
    template<> struct ScriptFieldTraits<std::int64_t> : ScriptValueTraits<std::int64_t> {};
    template<> struct ScriptFieldTraits<std::uint32_t> : ScriptValueTraits<std::uint32_t> {};
    template<> struct ScriptFieldTraits<std::uint64_t> : ScriptValueTraits<std::uint64_t> {};
    template<> struct ScriptFieldTraits<float> : ScriptValueTraits<float> {};
    template<> struct ScriptFieldTraits<double> : ScriptValueTraits<double> {};
    template<> struct ScriptFieldTraits<glm::vec2> : ScriptValueTraits<glm::vec2> {};
    template<> struct ScriptFieldTraits<glm::vec3> : ScriptValueTraits<glm::vec3> {};
    template<> struct ScriptFieldTraits<glm::vec4> : ScriptValueTraits<glm::vec4> {};
    template<> struct ScriptFieldTraits<glm::quat> : ScriptValueTraits<glm::quat> {};

    template<>
    struct ScriptFieldTraits<Color>
    {
        static constexpr bool c_NeedsScene = false;

        static void Describe([[maybe_unused]] ScriptFieldDescription& field) {}
        [[nodiscard]] static ScriptFieldValue ToValue(const Color& value) { return ScriptFieldValue::Color(value); }
        static void FromValue(Color& target, const ScriptFieldValue& value, [[maybe_unused]] Scene* scene) { target = Color(value.Get<glm::vec4>()); }
    };

    template<>
    struct ScriptFieldTraits<std::string>
    {
        static constexpr bool c_NeedsScene = false;

        static void Describe([[maybe_unused]] ScriptFieldDescription& field) {}
        [[nodiscard]] static ScriptFieldValue ToValue(const std::string& value) { return ScriptFieldValue::String(value); }
        static void FromValue(std::string& target, const ScriptFieldValue& value, [[maybe_unused]] Scene* scene) { target.assign(std::string_view(value.Get<ScriptString>())); }
    };

    template<>
    struct ScriptFieldTraits<EntityReference>
    {
        static constexpr bool c_NeedsScene = false;

        static void Describe([[maybe_unused]] ScriptFieldDescription& field) {}
        [[nodiscard]] static ScriptFieldValue ToValue(const EntityReference& value) { return ScriptFieldValue::Entity(value.ID); }
        static void FromValue(EntityReference& target, const ScriptFieldValue& value, [[maybe_unused]] Scene* scene) { target.ID = value.Get<UUID>(); }
    };

    // The entity of that UUID in the scene the script runs in, or none when the scene has no such entity
    template<>
    struct ScriptFieldTraits<Entity>
    {
        static constexpr bool c_NeedsScene = true;

        static void Describe([[maybe_unused]] ScriptFieldDescription& field) {}
        [[nodiscard]] static ScriptFieldValue ToValue(const Entity& value) { return ScriptFieldValue::Entity(value ? value.GetUUID() : UUID()); }

        static void FromValue(Entity& target, const ScriptFieldValue& value, Scene* scene)
        {
            const UUID l_ID = value.Get<UUID>();
            target = scene != nullptr && l_ID.IsValid() ? scene->FindEntityByUUID(l_ID) : Entity();
        }
    };

    // Holds the asset loaded while the member does. The field's picker offers only assets of A's type
    template<std::derived_from<Asset> A>
    struct ScriptFieldTraits<AssetRef<A>>
    {
        static constexpr bool c_NeedsScene = false;

        static void Describe(ScriptFieldDescription& field) { field.AssetType = A::c_AssetType; }
        [[nodiscard]] static ScriptFieldValue ToValue(const AssetRef<A>& value) { return ScriptFieldValue::Asset(value.GetID()); }

        static void FromValue(AssetRef<A>& target, const ScriptFieldValue& value, [[maybe_unused]] Scene* scene)
        {
            const UUID l_ID = value.Get<UUID>();
            if (l_ID != target.GetID())
            {
                target = l_ID.IsValid() ? AssetRef<A>(l_ID) : AssetRef<A>();
            }
        }
    };

    template<typename T>
        requires std::is_enum_v<T>&& requires { ScriptEnumTraits<T>::c_Names; }
    struct ScriptFieldTraits<T>
    {
        static constexpr bool c_NeedsScene = false;

        static void Describe(ScriptFieldDescription& field) { field.EnumValues = ScriptTypes::MakeEnumValues(ScriptEnumTraits<T>::c_Names, ScriptEnumTraits<T>::GetValues()); }
        [[nodiscard]] static ScriptFieldValue ToValue(const T& value) { return ScriptFieldValue::Enum(static_cast<std::int64_t>(value)); }
        static void FromValue(T& target, const ScriptFieldValue& value, [[maybe_unused]] Scene* scene) { target = static_cast<T>(value.Get<std::int64_t>()); }
    };

    // A type a script's or a component's member may have to be a field
    template<typename T>
    concept ScriptFieldMember = requires(T & target, const T & value, ScriptFieldDescription & field, const ScriptFieldValue & data, Scene * scene)
    {
        { ScriptFieldTraits<T>::c_NeedsScene } -> std::convertible_to<bool>;
        { ScriptFieldTraits<T>::ToValue(value) } -> std::same_as<ScriptFieldValue>;
        ScriptFieldTraits<T>::FromValue(target, data, scene);
        ScriptFieldTraits<T>::Describe(field);
    };
}