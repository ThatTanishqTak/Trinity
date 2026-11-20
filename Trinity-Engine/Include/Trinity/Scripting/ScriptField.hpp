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
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

    // A colour is linear RGBA, an enum a 64-bit integer, a reference a UUID, an input action its path
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
        Asset,
        InputAction,
        List
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(ScriptFieldType type);
    [[nodiscard]] TRINITY_API std::optional<ScriptFieldType> ParseScriptFieldType(std::string_view text);

    using ScriptString = TaggedString<MemoryTag::Scene>;

    class ScriptFieldValue;

    // Every element is of its element type, any but a list. An empty list still knows what it holds
    struct ScriptList
    {
        ScriptFieldType ElementType = ScriptFieldType::Bool;
        std::vector<ScriptFieldValue, TaggedAllocator<ScriptFieldValue, MemoryTag::Scene>> Elements;

        [[nodiscard]] bool operator==(const ScriptList&) const = default;
    };

    // Kept in the scene, not in a script instance, so it saves, undoes and outlives a reload
    class TRINITY_API ScriptFieldValue
    {
    public:
        using Storage = std::variant<bool, std::int32_t, std::int64_t, std::uint32_t, std::uint64_t, float, double, glm::vec2, glm::vec3, glm::vec4, glm::quat, ScriptString, UUID, ScriptList>;

        ScriptFieldValue() = default;
        // The type's zero, such as false, 0, opaque white or an empty string. A list is of booleans
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
        [[nodiscard]] static ScriptFieldValue InputAction(std::string_view path);
        // An empty list of the element type, any but a list. Add elements through Get<ScriptList>()
        [[nodiscard]] static ScriptFieldValue List(ScriptFieldType elementType);

        [[nodiscard]] ScriptFieldType GetType() const { return m_Type; }
        // The type of a list's elements, or any other value's own type
        [[nodiscard]] ScriptFieldType GetElementType() const;
        // Whether the other has this value's type, which for a list is the same element type too
        [[nodiscard]] bool HasSameType(const ScriptFieldValue& other) const;

        // As its type stores it, such as glm::vec4 for a colour. Asking for another alternative asserts
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

        // Calls the function with the value, or each element of a list, of that type. It may change them
        template<typename T, typename F>
        void ForEachValue(ScriptFieldType type, F&& function)
        {
            ForEachValueOf<T>(*this, type, function);
        }

        template<typename T, typename F>
        void ForEachValue(ScriptFieldType type, F&& function) const
        {
            ForEachValueOf<T>(*this, type, function);
        }

        [[nodiscard]] bool operator==(const ScriptFieldValue&) const = default;

    private:
        ScriptFieldValue(ScriptFieldType type, Storage value);

        template<typename T, typename Self, typename F>
        static void ForEachValueOf(Self& self, ScriptFieldType type, F& function)
        {
            if (self.m_Type == type)
            {
                function(self.template Get<T>());

                return;
            }

            if (self.m_Type != ScriptFieldType::List || std::get<ScriptList>(self.m_Value).ElementType != type)
            {
                return;
            }

            for (auto& it_Element : std::get<ScriptList>(self.m_Value).Elements)
            {
                function(it_Element.template Get<T>());
            }
        }

        ScriptFieldType m_Type = ScriptFieldType::Bool;
        Storage m_Value{ false };
    };

    // The alternative of ScriptFieldValue::Storage a type keeps its value in
    [[nodiscard]] TRINITY_API std::size_t GetScriptFieldStorageIndex(ScriptFieldType type);
    // A value's type as a person reads it, such as Float, or List of Float
    [[nodiscard]] TRINITY_API std::string DescribeType(const ScriptFieldValue& value);
}