#include "Trinity/Scripting/ScriptField.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace Trinity
{
    namespace
    {
        constexpr std::array<std::pair<ScriptLanguage, std::string_view>, 2> c_LanguageNames{ { { ScriptLanguage::Cpp, "Cpp" },{ ScriptLanguage::CSharp, "CSharp" } } };

        // Written into scene files, so a name never changes once released
        constexpr std::array<std::pair<ScriptFieldType, std::string_view>, 18> c_FieldTypeNames{ {
            { ScriptFieldType::Bool, "Bool" },
            { ScriptFieldType::Int32, "Int32" },
            { ScriptFieldType::Int64, "Int64" },
            { ScriptFieldType::UInt32, "UInt32" },
            { ScriptFieldType::UInt64, "UInt64" },
            { ScriptFieldType::Float, "Float" },
            { ScriptFieldType::Double, "Double" },
            { ScriptFieldType::Vector2, "Vector2" },
            { ScriptFieldType::Vector3, "Vector3" },
            { ScriptFieldType::Vector4, "Vector4" },
            { ScriptFieldType::Quaternion, "Quaternion" },
            { ScriptFieldType::Color, "Color" },
            { ScriptFieldType::String, "String" },
            { ScriptFieldType::Enum, "Enum" },
            { ScriptFieldType::Entity, "Entity" },
            { ScriptFieldType::Asset, "Asset" },
            { ScriptFieldType::InputAction, "InputAction" },
            { ScriptFieldType::List, "List" }
        } };

        ScriptFieldValue::Storage MakeZero(ScriptFieldType type)
        {
            switch (type)
            {
                case ScriptFieldType::Bool:
                {
                    return false;
                }
                case ScriptFieldType::Int32:
                {
                    return std::int32_t{ 0 };
                }
                case ScriptFieldType::Int64:
                {
                    return std::int64_t{ 0 };
                }
                case ScriptFieldType::UInt32:
                {
                    return std::uint32_t{ 0 };
                }
                case ScriptFieldType::UInt64:
                {
                    return std::uint64_t{ 0 };
                }
                case ScriptFieldType::Float:
                {
                    return 0.0f;
                }
                case ScriptFieldType::Double:
                {
                    return 0.0;
                }
                case ScriptFieldType::Vector2:
                {
                    return glm::vec2(0.0f);
                }
                case ScriptFieldType::Vector3:
                {
                    return glm::vec3(0.0f);
                }
                case ScriptFieldType::Vector4:
                {
                    return glm::vec4(0.0f);
                }
                case ScriptFieldType::Quaternion:
                {
                    return glm::identity<glm::quat>();
                }
                case ScriptFieldType::Color:
                {
                    return glm::vec4(1.0f);
                }
                case ScriptFieldType::String:
                {
                    return ScriptString();
                }
                case ScriptFieldType::Enum:
                {
                    return std::int64_t{ 0 };
                }
                case ScriptFieldType::Entity:
                {
                    return UUID();
                }
                case ScriptFieldType::Asset:
                {
                    return UUID();
                }
                case ScriptFieldType::InputAction:
                {
                    return ScriptString();
                }
                case ScriptFieldType::List:
                {
                    return ScriptList();
                }
            }

            return false;
        }
    }

    std::string_view ToString(ScriptLanguage language)
    {
        const auto a_Found = std::ranges::find(c_LanguageNames, language, &std::pair<ScriptLanguage, std::string_view>::first);

        return a_Found != c_LanguageNames.end() ? a_Found->second : "Unknown";
    }

    std::optional<ScriptLanguage> ParseScriptLanguage(std::string_view text)
    {
        const auto a_Found = std::ranges::find(c_LanguageNames, text, &std::pair<ScriptLanguage, std::string_view>::second);

        return a_Found != c_LanguageNames.end() ? std::optional(a_Found->first) : std::nullopt;
    }

    std::string_view ToString(ScriptFieldType type)
    {
        const auto a_Found = std::ranges::find(c_FieldTypeNames, type, &std::pair<ScriptFieldType, std::string_view>::first);

        return a_Found != c_FieldTypeNames.end() ? a_Found->second : "Unknown";
    }

    std::optional<ScriptFieldType> ParseScriptFieldType(std::string_view text)
    {
        const auto a_Found = std::ranges::find(c_FieldTypeNames, text, &std::pair<ScriptFieldType, std::string_view>::second);

        return a_Found != c_FieldTypeNames.end() ? std::optional(a_Found->first) : std::nullopt;
    }

    std::size_t GetScriptFieldStorageIndex(ScriptFieldType type)
    {
        return MakeZero(type).index();
    }

    std::string DescribeType(const ScriptFieldValue& value)
    {
        return value.GetType() == ScriptFieldType::List ? std::format("List of {}", ToString(value.GetElementType())) : std::string(ToString(value.GetType()));
    }

    ScriptFieldValue::ScriptFieldValue(ScriptFieldType type) : m_Type(type), m_Value(MakeZero(type))
    {

    }

    ScriptFieldValue::ScriptFieldValue(ScriptFieldType type, Storage value) : m_Type(type), m_Value(std::move(value))
    {
        TR_CORE_ASSERT(m_Value.index() == GetScriptFieldStorageIndex(type), "ScriptFieldValue: a {} field was given a value of another type", ToString(type));
        TR_CORE_ASSERT(type != ScriptFieldType::List || std::get<ScriptList>(m_Value).ElementType != ScriptFieldType::List, "ScriptFieldValue: a list holds any field type but another list");
    }

    ScriptFieldValue ScriptFieldValue::Bool(bool value) { return { ScriptFieldType::Bool, value }; }
    ScriptFieldValue ScriptFieldValue::Int32(std::int32_t value) { return { ScriptFieldType::Int32, value }; }
    ScriptFieldValue ScriptFieldValue::Int64(std::int64_t value) { return { ScriptFieldType::Int64, value }; }
    ScriptFieldValue ScriptFieldValue::UInt32(std::uint32_t value) { return { ScriptFieldType::UInt32, value }; }
    ScriptFieldValue ScriptFieldValue::UInt64(std::uint64_t value) { return { ScriptFieldType::UInt64, value }; }
    ScriptFieldValue ScriptFieldValue::Float(float value) { return { ScriptFieldType::Float, value }; }
    ScriptFieldValue ScriptFieldValue::Double(double value) { return { ScriptFieldType::Double, value }; }
    ScriptFieldValue ScriptFieldValue::Vector2(const glm::vec2& value) { return { ScriptFieldType::Vector2, value }; }
    ScriptFieldValue ScriptFieldValue::Vector3(const glm::vec3& value) { return { ScriptFieldType::Vector3, value }; }
    ScriptFieldValue ScriptFieldValue::Vector4(const glm::vec4& value) { return { ScriptFieldType::Vector4, value }; }
    ScriptFieldValue ScriptFieldValue::Quaternion(const glm::quat& value) { return { ScriptFieldType::Quaternion, value }; }
    ScriptFieldValue ScriptFieldValue::Color(const glm::vec4& value) { return { ScriptFieldType::Color, value }; }
    ScriptFieldValue ScriptFieldValue::String(std::string_view value) { return { ScriptFieldType::String, ScriptString(value) }; }
    ScriptFieldValue ScriptFieldValue::Enum(std::int64_t value) { return { ScriptFieldType::Enum, value }; }
    ScriptFieldValue ScriptFieldValue::Entity(UUID value) { return { ScriptFieldType::Entity, value }; }
    ScriptFieldValue ScriptFieldValue::Asset(UUID value) { return { ScriptFieldType::Asset, value }; }
    ScriptFieldValue ScriptFieldValue::InputAction(std::string_view path) { return { ScriptFieldType::InputAction, ScriptString(path) }; }
    ScriptFieldValue ScriptFieldValue::List(ScriptFieldType elementType) { return { ScriptFieldType::List, ScriptList{ elementType,{} } }; }

    ScriptFieldType ScriptFieldValue::GetElementType() const
    {
        return m_Type == ScriptFieldType::List ? std::get<ScriptList>(m_Value).ElementType : m_Type;
    }

    bool ScriptFieldValue::HasSameType(const ScriptFieldValue& other) const
    {
        return m_Type == other.m_Type && GetElementType() == other.GetElementType();
    }
}