#pragma once

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace Trinity
{
    // The language a script class is written in, each run by a backend of its own
    enum class ScriptLanguage : std::uint8_t
    {
        Cpp,
        CSharp
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(ScriptLanguage language);
    [[nodiscard]] TRINITY_API std::optional<ScriptLanguage> ParseScriptLanguage(std::string_view text);

    // What a script's field holds. A colour is a linear RGBA vec4 shown with a colour editor, an enum is its value as a 64-bit integer, an entity reference is the entity's UUID, and an asset reference is the asset's UUID
    enum class ScriptFieldType : std::uint8_t
    {
        Bool,
        Int32,
        Int64,
        UInt32,
        UInt64,
        Float,
        Double,
        Vector2,
        Vector3,
        Vector4,
        Quaternion,
        Color,
        String,
        Enum,
        Entity,
        Asset
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(ScriptFieldType type);
    [[nodiscard]] TRINITY_API std::optional<ScriptFieldType> ParseScriptFieldType(std::string_view text);

    using ScriptString = TaggedString<MemoryTag::Scene>;

    // A field's value with its type, which the value's alternative always matches. Kept in the scene rather than in a script instance, so it saves, undoes and outlives a reload
    class TRINITY_API ScriptFieldValue
    {
    public:
        using Storage = std::variant<bool, std::int32_t, std::int64_t, std::uint32_t, std::uint64_t, float, double, glm::vec2, glm::vec3, glm::vec4, glm::quat, ScriptString, UUID>;

        ScriptFieldValue() = default;
        // The type's zero: false, 0, an identity rotation, opaque white, an empty string or no entity or asset
        explicit ScriptFieldValue(ScriptFieldType type);

        [[nodiscard]] static ScriptFieldValue Bool(bool value);
        [[nodiscard]] static ScriptFieldValue Int32(std::int32_t value);
        [[nodiscard]] static ScriptFieldValue Int64(std::int64_t value);
        [[nodiscard]] static ScriptFieldValue UInt32(std::uint32_t value);
        [[nodiscard]] static ScriptFieldValue UInt64(std::uint64_t value);
        [[nodiscard]] static ScriptFieldValue Float(float value);
        [[nodiscard]] static ScriptFieldValue Double(double value);
        [[nodiscard]] static ScriptFieldValue Vector2(const glm::vec2& value);
        [[nodiscard]] static ScriptFieldValue Vector3(const glm::vec3& value);
        [[nodiscard]] static ScriptFieldValue Vector4(const glm::vec4& value);
        [[nodiscard]] static ScriptFieldValue Quaternion(const glm::quat& value);
        [[nodiscard]] static ScriptFieldValue Color(const glm::vec4& value);
        [[nodiscard]] static ScriptFieldValue String(std::string_view value);
        [[nodiscard]] static ScriptFieldValue Enum(std::int64_t value);
        [[nodiscard]] static ScriptFieldValue Entity(UUID value);
        [[nodiscard]] static ScriptFieldValue Asset(UUID value);

        [[nodiscard]] ScriptFieldType GetType() const { return m_Type; }

        // The value as its type stores it, such as glm::vec4 for a colour or std::int64_t for an enum. Asking for another alternative asserts
        template<typename T>
        [[nodiscard]] const T& Get() const
        {
            const T* l_Value = std::get_if<T>(&m_Value);
            TR_CORE_ASSERT(l_Value != nullptr, "ScriptFieldValue: a {} field was read as another type", ToString(m_Type));

            return *l_Value;
        }

        template<typename T>
        [[nodiscard]] T& Get()
        {
            T* l_Value = std::get_if<T>(&m_Value);
            TR_CORE_ASSERT(l_Value != nullptr, "ScriptFieldValue: a {} field was read as another type", ToString(m_Type));

            return *l_Value;
        }

        [[nodiscard]] const Storage& GetStorage() const { return m_Value; }
        [[nodiscard]] Storage& GetStorage() { return m_Value; }

        [[nodiscard]] bool operator==(const ScriptFieldValue&) const = default;

    private:
        ScriptFieldValue(ScriptFieldType type, Storage value);

        ScriptFieldType m_Type = ScriptFieldType::Bool;
        Storage m_Value{ false };
    };

    // The alternative of ScriptFieldValue::Storage a type keeps its value in
    [[nodiscard]] TRINITY_API std::size_t GetScriptFieldStorageIndex(ScriptFieldType type);
}