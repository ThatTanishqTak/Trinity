#pragma once

#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Trinity
{
    // What a data model's value holds, numbered as C#'s UIDataType
    enum class UIDataType : std::uint8_t
    {
        Int,
        Float,
        Bool,
        String
    };

    inline constexpr std::uint32_t c_UIDataTypeCount = 4;

    [[nodiscard]] TRINITY_API std::string_view ToString(UIDataType type);

    // A field of each row of a list, by the name an expression reaches it by, as row.name
    struct UIDataField
    {
        std::string_view Name;
        UIDataType Type = UIDataType::Int;
    };

    // A value a script bound into a data model, which {{ name }} and data-if, data-class and data-value read, safe to keep after its model has gone, when every call does nothing or reads as nothing. Setting it to what it holds does nothing, so a value set every frame costs the UI nothing until it changes, and setting it allocates nothing once a string has held one as long, for a name of up to 15 characters, which RmlUi copies as it marks it changed. A control's data-value or data-checked writes into it as the player changes the control. A value of another type is refused, logged once. Main thread only
    class TRINITY_API UIDataValue
    {
    public:
        UIDataValue() = default;

        [[nodiscard]] bool IsValid() const;
        explicit operator bool() const { return IsValid(); }
        [[nodiscard]] UIDataType GetType() const;

        void Set(std::int32_t value) const;
        void Set(float value) const;
        void Set(bool value) const;
        void Set(std::string_view value) const;
        // So text is not taken as a bool
        void Set(const char* value) const { Set(std::string_view(value)); }

        [[nodiscard]] std::int32_t GetInt() const;
        [[nodiscard]] float GetFloat() const;
        [[nodiscard]] bool GetBool() const;
        [[nodiscard]] std::string GetString() const;

        // As C# holds it, 0 for none
        [[nodiscard]] std::uint64_t GetHandle() const { return m_Handle; }
        [[nodiscard]] static UIDataValue FromHandle(std::uint64_t handle)
        {
            UIDataValue l_Value;
            l_Value.m_Handle = handle;

            return l_Value;
        }

        [[nodiscard]] bool operator==(const UIDataValue&) const = default;

    private:
        std::uint64_t m_Handle = 0;
    };

    // A list a script bound into a data model, which data-for walks, as data-for="row : scores" with {{ row.name }}, and whose rows count as scores.size. Rows are set field by field, by the field's name, as values are set, and rows added by Resize hold 0, false and empty text until set. Safe to keep after its model has gone, as a value is. Main thread only
    class TRINITY_API UIDataList
    {
    public:
        UIDataList() = default;

        [[nodiscard]] bool IsValid() const;
        explicit operator bool() const { return IsValid(); }

        [[nodiscard]] std::uint32_t GetSize() const;
        void Resize(std::uint32_t rows) const;

        // A row past the end, or a field the list has not, is refused, logged once
        void Set(std::uint32_t row, std::string_view field, std::int32_t value) const;
        void Set(std::uint32_t row, std::string_view field, float value) const;
        void Set(std::uint32_t row, std::string_view field, bool value) const;
        void Set(std::uint32_t row, std::string_view field, std::string_view value) const;
        void Set(std::uint32_t row, std::string_view field, const char* value) const { Set(row, field, std::string_view(value)); }

        [[nodiscard]] std::int32_t GetInt(std::uint32_t row, std::string_view field) const;
        [[nodiscard]] float GetFloat(std::uint32_t row, std::string_view field) const;
        [[nodiscard]] bool GetBool(std::uint32_t row, std::string_view field) const;
        [[nodiscard]] std::string GetString(std::uint32_t row, std::string_view field) const;

        [[nodiscard]] std::uint64_t GetHandle() const { return m_Handle; }
        [[nodiscard]] static UIDataList FromHandle(std::uint64_t handle)
        {
            UIDataList l_List;
            l_List.m_Handle = handle;

            return l_List;
        }

        [[nodiscard]] bool operator==(const UIDataList&) const = default;

    private:
        std::uint64_t m_Handle = 0;
    };

    // A data model of a UIDocument, by the name its document gives in data-model="name", which the scene's scripts bind values and lists into, kept while the scene's scripts run, and given to every document its entity shows, those read again included. Each name is bound once: binding it again gives the value or list bound before, with what it holds, and refuses one of another type or kind, logged. A name an expression reads before it is bound reads as nothing, without an error, so a document reads before its scripts bind. A name is letters, digits and underscores, not starting with a digit. Main thread only
    class TRINITY_API UIDataModel
    {
    public:
        UIDataModel() = default;

        [[nodiscard]] bool IsValid() const;
        explicit operator bool() const { return IsValid(); }

        UIDataValue BindInt(std::string_view name, std::int32_t value = 0) const;
        UIDataValue BindFloat(std::string_view name, float value = 0.0f) const;
        UIDataValue BindBool(std::string_view name, bool value = false) const;
        UIDataValue BindString(std::string_view name, std::string_view value = {}) const;
        UIDataList BindList(std::string_view name, std::span<const UIDataField> fields) const;

        [[nodiscard]] std::uint64_t GetHandle() const { return m_Handle; }
        [[nodiscard]] static UIDataModel FromHandle(std::uint64_t handle)
        {
            UIDataModel l_Model;
            l_Model.m_Handle = handle;

            return l_Model;
        }

        [[nodiscard]] bool operator==(const UIDataModel&) const = default;

    private:
        std::uint64_t m_Handle = 0;
    };
}