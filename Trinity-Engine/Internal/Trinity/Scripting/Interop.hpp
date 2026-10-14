#pragma once

#include "Trinity/Core/Log.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace Trinity::Interop
{
    // The types NativeFunctions.def names, each with a C# type of the same layout, which the engine checks at start
    using Void = void;
    using Bool = bool;
    using Int32 = std::int32_t;
    using UInt32 = std::uint32_t;
    using Int64 = std::int64_t;
    using UInt64 = std::uint64_t;
    using Float = float;
    using Double = double;
    using EntityId = std::uint64_t;
    using Vector2 = glm::vec2;
    using Vector3 = glm::vec3;
    using Vector4 = glm::vec4;
    using Quaternion = glm::quat;
    using Color = glm::vec4;
    using Matrix4x4 = glm::mat4;
    using LogLevel = Trinity::LogLevel;

    // Text as UTF-8, which need not end in a zero, valid for the call it is passed to
    struct Utf8
    {
        const char* Data;
        Int32 Length;
    };

    [[nodiscard]] inline std::string_view ToStringView(Utf8 text)
    {
        return text.Data != nullptr && text.Length > 0 ? std::string_view(text.Data, static_cast<std::size_t>(text.Length)) : std::string_view();
    }

#define TR_NATIVE(Name, Return, Parameters) Return Name Parameters;
#include "Trinity/Scripting/NativeFunctions.def"
#undef TR_NATIVE

    // What ScriptCore is handed as .NET starts, in the order of NativeFunctions.def
    struct NativeFunctions
    {
#define TR_NATIVE(Name, Return, Parameters) Return (*Name) Parameters = &Interop::Name;
#include "Trinity/Scripting/NativeFunctions.def"
#undef TR_NATIVE
    };

    // A shared type's size and the offset of each of its fields, which the engine and ScriptCore each describe, laid out as Trinity.Interop.TypeLayout
    struct TypeLayout
    {
        static constexpr std::size_t c_MaxFields = 16;

        Int32 Size;
        Int32 FieldCount;
        std::array<Int32, c_MaxFields> Offsets;
    };
}