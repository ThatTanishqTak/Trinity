#include "Trinity/Scripting/Interop.hpp"

#include "Trinity/Physics/PhysicsTypes.hpp"
#include "Trinity/Renderer/ToneMapping.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scripting/ScriptApi.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Trinity
{
    namespace Interop
    {

        namespace
        {
            // Each component and enum of ScriptComponents.def by its name there
            namespace Components
            {
#define TR_ENUM(Name, Type)
#define TR_ENUM_VALUE(Enum, Name, Number)
#define TR_COMPONENT(Name, Type) using Name = ::Trinity::Type;
#define TR_PROPERTY(Component, Name, Kind, Member)
#include "Trinity/Scripting/ScriptComponents.def"
#undef TR_PROPERTY
#undef TR_COMPONENT
#undef TR_ENUM_VALUE
#undef TR_ENUM
            }

            namespace Enums
            {
#define TR_ENUM(Name, Type) using Name = ::Trinity::Type;
#define TR_ENUM_VALUE(Enum, Name, Number)
#define TR_COMPONENT(Name, Type)
#define TR_PROPERTY(Component, Name, Kind, Member)
#include "Trinity/Scripting/ScriptComponents.def"
#undef TR_PROPERTY
#undef TR_COMPONENT
#undef TR_ENUM_VALUE
#undef TR_ENUM
            }

            // C# numbers each enum's values as the engine does
#define TR_ENUM(Name, Type)
#define TR_ENUM_VALUE(Enum, Name, Number) static_assert(std::to_underlying(Enums::Enum::Name) == Number, "ScriptComponents.def: " #Enum "::" #Name " is not " #Number " in the engine");
#define TR_COMPONENT(Name, Type)
#define TR_PROPERTY(Component, Name, Kind, Member)
#include "Trinity/Scripting/ScriptComponents.def"
#undef TR_PROPERTY
#undef TR_COMPONENT
#undef TR_ENUM_VALUE
#undef TR_ENUM

        // Whether an enum of the list has that value, so C# cannot write one the engine does not know
            template<typename E>
            bool IsEnumValue(std::int64_t value)
            {
#define TR_ENUM(Name, Type)
#define TR_ENUM_VALUE(Enum, Name, Number) if constexpr (std::is_same_v<E, Enums::Enum>) { if (value == Number) { return true; } }
#define TR_COMPONENT(Name, Type)
#define TR_PROPERTY(Component, Name, Kind, Member)
#include "Trinity/Scripting/ScriptComponents.def"
#undef TR_PROPERTY
#undef TR_COMPONENT
#undef TR_ENUM_VALUE
#undef TR_ENUM

                return false;
            }

            // Whether a member's type is one the kind the list gives it crosses as
            template<typename T>
            constexpr bool IsKind(std::string_view kind)
            {
                if constexpr (std::is_same_v<T, bool>) { return kind == "Bool"; }
                else if constexpr (std::is_same_v<T, std::int32_t>) { return kind == "Int32"; }
                else if constexpr (std::is_same_v<T, std::uint32_t>) { return kind == "UInt32"; }
                else if constexpr (std::is_same_v<T, float>) { return kind == "Float"; }
                else if constexpr (std::is_same_v<T, glm::vec2>) { return kind == "Vector2"; }
                else if constexpr (std::is_same_v<T, glm::vec3>) { return kind == "Vector3" || kind == "Color"; }
                else if constexpr (std::is_same_v<T, glm::vec4>) { return kind == "Vector4" || kind == "Color"; }
                else if constexpr (std::is_same_v<T, glm::quat>) { return kind == "Quaternion"; }
                else if constexpr (std::is_same_v<T, glm::bvec3>) { return kind == "Axes"; }
                else if constexpr (std::is_same_v<T, UUID>) { return kind.starts_with("Asset(") || kind == "Entity"; }
                else if constexpr (std::is_enum_v<T>) { return kind.starts_with("Enum("); }
                else { return false; }
            }

            template<typename T>
            void ToField(const T& value, FieldValue& field)
            {
                field = {};
                if constexpr (std::is_same_v<T, bool>) { field.Integer = value ? 1 : 0; }
                else if constexpr (std::is_same_v<T, glm::vec2>) { field.Floats = { value.x, value.y, 0.0f, 0.0f }; }
                else if constexpr (std::is_same_v<T, glm::vec3>) { field.Floats = { value.x, value.y, value.z, 1.0f }; }
                else if constexpr (std::is_same_v<T, glm::vec4>) { field.Floats = { value.x, value.y, value.z, value.w }; }
                else if constexpr (std::is_same_v<T, glm::quat>) { field.Floats = { value.x, value.y, value.z, value.w }; }
                else if constexpr (std::is_same_v<T, glm::bvec3>) { field.Integer = (value.x ? 1 : 0) | (value.y ? 2 : 0) | (value.z ? 4 : 0); }
                else if constexpr (std::is_same_v<T, UUID>) { field.Integer = static_cast<Int64>(value.GetValue()); }
                else if constexpr (std::is_enum_v<T>) { field.Integer = static_cast<Int64>(std::to_underlying(value)); }
                else if constexpr (std::is_same_v<T, float>) { field.Real = value; }
                else { field.Integer = static_cast<Int64>(value); }
            }

            // False, leaving the value as it was, for an enum value the engine does not know or a collision layer past the last
            template<typename T>
            bool FromField(const FieldValue& field, T& value)
            {
                if constexpr (std::is_same_v<T, bool>) { value = field.Integer != 0; }
                else if constexpr (std::is_same_v<T, glm::vec2>) { value = { field.Floats[0], field.Floats[1] }; }
                else if constexpr (std::is_same_v<T, glm::vec3>) { value = { field.Floats[0], field.Floats[1], field.Floats[2] }; }
                else if constexpr (std::is_same_v<T, glm::vec4>) { value = { field.Floats[0], field.Floats[1], field.Floats[2], field.Floats[3] }; }
                else if constexpr (std::is_same_v<T, glm::quat>) { value = glm::quat(field.Floats[3], field.Floats[0], field.Floats[1], field.Floats[2]); }
                else if constexpr (std::is_same_v<T, glm::bvec3>) { value = { (field.Integer & 1) != 0, (field.Integer & 2) != 0, (field.Integer & 4) != 0 }; }
                else if constexpr (std::is_same_v<T, UUID>) { value = UUID(static_cast<std::uint64_t>(field.Integer)); }
                else if constexpr (std::is_enum_v<T>)
                {
                    if (!IsEnumValue<T>(field.Integer))
                    {
                        return false;
                    }

                    value = static_cast<T>(field.Integer);
                }
                else if constexpr (std::is_same_v<T, float>) { value = static_cast<float>(field.Real); }
                else if constexpr (std::is_same_v<T, std::uint32_t>)
                {
                    if (field.Integer < 0 || field.Integer > static_cast<Int64>(UINT32_MAX))
                    {
                        return false;
                    }

                    value = static_cast<std::uint32_t>(field.Integer);
                }
                else
                {
                    if (field.Integer < INT32_MIN || field.Integer > INT32_MAX)
                    {
                        return false;
                    }

                    value = static_cast<std::int32_t>(field.Integer);
                }

                return true;
            }

            // A collision layer is one of the project's
            template<auto Member>
            constexpr bool IsLayer()
            {
                if constexpr (std::is_same_v<decltype(Member), decltype(&ColliderSettings::Layer)>) { return Member == &ColliderSettings::Layer; }
                else if constexpr (std::is_same_v<decltype(Member), decltype(&Collider2DSettings::Layer)>) { return Member == &Collider2DSettings::Layer; }
                else { return false; }
            }

            struct ComponentAccess
            {
                bool (*Has)(const SceneRegistry& registry, entt::entity entity) = nullptr;
                bool (*Add)(SceneRegistry& registry, entt::entity entity) = nullptr;
                bool (*Remove)(SceneRegistry& registry, entt::entity entity) = nullptr;
            };

            // Every entity keeps its Transform until it is destroyed
            template<typename T>
            constexpr ComponentAccess MakeComponentAccess()
            {
                ComponentAccess l_Access;
                l_Access.Has = [](const SceneRegistry& registry, entt::entity entity) { return registry.all_of<T>(entity); };
                if constexpr (std::is_same_v<T, TransformComponent>)
                {
                    l_Access.Add = [](SceneRegistry&, entt::entity) { return false; };
                    l_Access.Remove = [](SceneRegistry&, entt::entity) { return false; };
                }
                else
                {
                    l_Access.Add = [](SceneRegistry& registry, entt::entity entity)
                    {
                        if (registry.all_of<T>(entity))
                        {
                            return false;
                        }

                        registry.emplace<T>(entity);

                        return true;
                    };
                    l_Access.Remove = [](SceneRegistry& registry, entt::entity entity)
                    {
                        if (!registry.all_of<T>(entity))
                        {
                            return false;
                        }

                        registry.remove<T>(entity);

                        return true;
                    };
                }

                return l_Access;
            }

            struct PropertyAccess
            {
                AccessResult(*Get)(SceneRegistry& registry, entt::entity entity, FieldValue& value) = nullptr;
                AccessResult(*Set)(SceneRegistry& registry, entt::entity entity, const FieldValue& value) = nullptr;
            };

            // A write goes through patch, so a running scene's physics and renderer see it
            template<typename T, auto Member>
            constexpr PropertyAccess MakePropertyAccess()
            {
                using Value = std::remove_cvref_t<decltype(std::declval<T&>().*Member)>;

                PropertyAccess l_Access;
                l_Access.Get = [](SceneRegistry& registry, entt::entity entity, FieldValue& value)
                {
                    const T* l_Component = registry.try_get<T>(entity);
                    if (l_Component == nullptr)
                    {
                        return AccessResult::NoComponent;
                    }

                    ToField(l_Component->*Member, value);

                    return AccessResult::Done;
                };
                l_Access.Set = [](SceneRegistry& registry, entt::entity entity, const FieldValue& value)
                {
                    if (!registry.all_of<T>(entity))
                    {
                        return AccessResult::NoComponent;
                    }

                    Value l_Value{};
                    if (!FromField(value, l_Value))
                    {
                        return AccessResult::BadValue;
                    }

                    if constexpr (IsLayer<Member>())
                    {
                        if (l_Value >= c_MaxCollisionLayers)
                        {
                            return AccessResult::BadValue;
                        }
                    }

                    registry.patch<T>(entity, [&l_Value](T& component) { component.*Member = l_Value; });

                    return AccessResult::Done;
                };

                return l_Access;
            }

#define TR_ENUM(Name, Type)
#define TR_ENUM_VALUE(Enum, Name, Number)
#define TR_COMPONENT(Name, Type)
#define TR_PROPERTY(Component, Name, Kind, Member) static_assert(IsKind<std::remove_cvref_t<decltype(std::declval<Components::Component&>().Member)>>(#Kind), "ScriptComponents.def: " #Component "." #Name " is not a " #Kind " in the engine");
#include "Trinity/Scripting/ScriptComponents.def"
#undef TR_PROPERTY
#undef TR_COMPONENT
#undef TR_ENUM_VALUE
#undef TR_ENUM

            constexpr ComponentAccess c_Components[] = {
    #define TR_ENUM(Name, Type)
    #define TR_ENUM_VALUE(Enum, Name, Number)
    #define TR_COMPONENT(Name, Type) MakeComponentAccess<Components::Name>(),
    #define TR_PROPERTY(Component, Name, Kind, Member)
    #include "Trinity/Scripting/ScriptComponents.def"
    #undef TR_PROPERTY
    #undef TR_COMPONENT
    #undef TR_ENUM_VALUE
    #undef TR_ENUM
            };

            constexpr PropertyAccess c_Properties[] = {
    #define TR_ENUM(Name, Type)
    #define TR_ENUM_VALUE(Enum, Name, Number)
    #define TR_COMPONENT(Name, Type)
    #define TR_PROPERTY(Component, Name, Kind, Member) MakePropertyAccess<Components::Component, &Components::Component::Member>(),
    #include "Trinity/Scripting/ScriptComponents.def"
    #undef TR_PROPERTY
    #undef TR_COMPONENT
    #undef TR_ENUM_VALUE
    #undef TR_ENUM
            };

            // The component's access, or null for a number past the list
            const ComponentAccess* FindComponent(Int32 component)
            {
                return component >= 0 && static_cast<std::size_t>(component) < std::size(c_Components) ? &c_Components[component] : nullptr;
            }
        }

        Bool Component_Has(EntityId entity, Int32 component)
        {
            const Entity l_Entity = FindEntity(entity);
            const ComponentAccess* l_Access = FindComponent(component);

            return l_Entity && l_Access != nullptr && l_Access->Has(l_Entity.GetScene()->GetRegistry(), l_Entity.GetHandle());
        }

        Int32 Component_Add(EntityId entity, Int32 component)
        {
            const Entity l_Entity = FindEntity(entity);
            const ComponentAccess* l_Access = FindComponent(component);
            if (!l_Entity || l_Access == nullptr)
            {
                return std::to_underlying(AccessResult::NoEntity);
            }

            return std::to_underlying(l_Access->Add(l_Entity.GetScene()->GetRegistry(), l_Entity.GetHandle()) ? AccessResult::Done : AccessResult::BadValue);
        }

        Int32 Component_Remove(EntityId entity, Int32 component)
        {
            const Entity l_Entity = FindEntity(entity);
            const ComponentAccess* l_Access = FindComponent(component);
            if (!l_Entity || l_Access == nullptr)
            {
                return std::to_underlying(AccessResult::NoEntity);
            }

            return std::to_underlying(l_Access->Remove(l_Entity.GetScene()->GetRegistry(), l_Entity.GetHandle()) ? AccessResult::Done : AccessResult::NoComponent);
        }

        Int32 Component_Get(EntityId entity, Int32 property, FieldValue* value)
        {
            const Entity l_Entity = FindEntity(entity);
            if (!l_Entity || property < 0 || static_cast<std::size_t>(property) >= std::size(c_Properties))
            {
                return std::to_underlying(AccessResult::NoEntity);
            }

            return std::to_underlying(c_Properties[property].Get(l_Entity.GetScene()->GetRegistry(), l_Entity.GetHandle(), *value));
        }

        Int32 Component_Set(EntityId entity, Int32 property, const FieldValue* value)
        {
            const Entity l_Entity = FindEntity(entity);
            if (!l_Entity || property < 0 || static_cast<std::size_t>(property) >= std::size(c_Properties))
            {
                return std::to_underlying(AccessResult::NoEntity);
            }

            return std::to_underlying(c_Properties[property].Set(l_Entity.GetScene()->GetRegistry(), l_Entity.GetHandle(), *value));
        }

        // A slot past the end has no material, as the renderer draws it
        Int32 MeshRenderer_GetMaterialCount(EntityId entity)
        {
            const Entity l_Entity = FindEntity(entity);

            return l_Entity && l_Entity.Has<MeshRendererComponent>() ? static_cast<Int32>(l_Entity.Get<MeshRendererComponent>().Materials.size()) : 0;
        }

        UInt64 MeshRenderer_GetMaterial(EntityId entity, Int32 slot)
        {
            const Entity l_Entity = FindEntity(entity);
            if (!l_Entity || !l_Entity.Has<MeshRendererComponent>() || slot < 0)
            {
                return 0;
            }

            const auto& l_Materials = l_Entity.Get<MeshRendererComponent>().Materials;

            return static_cast<std::size_t>(slot) < l_Materials.size() ? l_Materials[static_cast<std::size_t>(slot)].GetValue() : 0;
        }

        // The list grows to reach the slot, with no material in the slots between
        Int32 MeshRenderer_SetMaterial(EntityId entity, Int32 slot, UInt64 material)
        {
            Entity l_Entity = FindEntity(entity);
            if (!l_Entity)
            {
                return std::to_underlying(AccessResult::NoEntity);
            }

            if (!l_Entity.Has<MeshRendererComponent>())
            {
                return std::to_underlying(AccessResult::NoComponent);
            }

            if (slot < 0 || slot >= 256)
            {
                return std::to_underlying(AccessResult::BadValue);
            }

            l_Entity.Patch<MeshRendererComponent>([slot, material](MeshRendererComponent& component)
            {
                const std::size_t l_Slot = static_cast<std::size_t>(slot);
                if (component.Materials.size() <= l_Slot)
                {
                    component.Materials.resize(l_Slot + 1);
                }

                component.Materials[l_Slot] = UUID(material);
            });

            return std::to_underlying(AccessResult::Done);
        }

        Int32 PolygonCollider2D_GetPointCount(EntityId entity)
        {
            const Entity l_Entity = FindEntity(entity);

            return l_Entity && l_Entity.Has<PolygonCollider2DComponent>() ? static_cast<Int32>(l_Entity.Get<PolygonCollider2DComponent>().Points.size()) : 0;
        }

        Bool PolygonCollider2D_GetPoint(EntityId entity, Int32 index, Vector2* point)
        {
            const Entity l_Entity = FindEntity(entity);
            if (!l_Entity || !l_Entity.Has<PolygonCollider2DComponent>() || index < 0)
            {
                return false;
            }

            const auto& l_Points = l_Entity.Get<PolygonCollider2DComponent>().Points;
            if (static_cast<std::size_t>(index) >= l_Points.size())
            {
                return false;
            }

            *point = l_Points[static_cast<std::size_t>(index)];

            return true;
        }

        Int32 PolygonCollider2D_SetPoints(EntityId entity, const Vector2* points, Int32 count)
        {
            Entity l_Entity = FindEntity(entity);
            if (!l_Entity)
            {
                return std::to_underlying(AccessResult::NoEntity);
            }

            if (!l_Entity.Has<PolygonCollider2DComponent>())
            {
                return std::to_underlying(AccessResult::NoComponent);
            }

            if (points == nullptr || count < 3 || static_cast<std::size_t>(count) > PolygonCollider2DComponent::c_MaxPoints)
            {
                return std::to_underlying(AccessResult::BadValue);
            }

            const std::span<const Vector2> l_Points(points, static_cast<std::size_t>(count));
            l_Entity.Patch<PolygonCollider2DComponent>([l_Points](PolygonCollider2DComponent& component) { component.Points.assign(l_Points.begin(), l_Points.end()); });

            return std::to_underlying(AccessResult::Done);
        }
    }
}