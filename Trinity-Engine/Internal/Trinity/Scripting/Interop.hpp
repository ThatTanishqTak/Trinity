#pragma once

#include "Trinity/Core/Log.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace Trinity::Interop
{
    // Each has a C# type of the same layout, which the engine checks at start
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

    [[nodiscard]] inline Utf8 ToUtf8(std::string_view text)
    {
        return { text.data(), static_cast<Int32>(text.size()) };
    }

    // Integer, Real, Floats or Text by its type. A list is its length, its elements crossing after it
    struct FieldValue
    {
        Int32 Type;
        Int32 ElementType;
        Int64 Integer;
        Double Real;
        std::array<Float, 4> Floats;
        Utf8 Text;
    };

    // What a script's physics callback is told, as ScriptCollision has it
    struct Collision
    {
        EntityId Other;
        Vector3 Point;
        Vector3 Normal;
        Bool Is2D;
    };

    // As PhysicsQueryFilter3D and 2D have it, the entity it passes over by its UUID
    struct QueryFilter
    {
        UInt32 Layers;
        Bool HitTriggers;
        EntityId Ignore;
    };

    // As PhysicsQueryShape3D and 2D have it, its type as PhysicsQueryShapeType3D or 2D numbers it
    struct QueryShape
    {
        Int32 Type;
        Float Radius;
        Vector3 HalfExtents;
        Float Height;
    };

    struct QueryShape2D
    {
        Int32 Type;
        Float Radius;
        Vector2 HalfExtents;
        Float Height;
    };

    // A collider a script's query met, as PhysicsHit3D and 2D have it, its entities by their UUIDs
    struct PhysicsHit
    {
        EntityId Entity;
        EntityId Body;
        Vector3 Point;
        Vector3 Normal;
        Float Distance;
        Bool Trigger;
    };

    struct PhysicsHit2D
    {
        EntityId Entity;
        EntityId Body;
        Vector2 Point;
        Vector2 Normal;
        Float Distance;
        Bool Trigger;
    };

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

    // A shared type's size and its fields' offsets, laid out as Trinity.Interop.TypeLayout
    struct TypeLayout
    {
        static constexpr std::size_t c_MaxFields = 16;

        Int32 Size;
        Int32 FieldCount;
        std::array<Int32, c_MaxFields> Offsets;
    };
}