#pragma once

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Scene/ComponentType.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Scene.hpp"

#include <entt/entity/entity.hpp>

#include <type_traits>
#include <utility>

namespace Trinity
{
    // A handle to an entity in a scene. It owns nothing, and is valid while its scene and entity live
    class Entity
    {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene) : m_Handle(handle), m_Scene(scene)
        {

        }

        template<Component T, typename... Args>
        T& Add(Args&&... args)
        {
            TR_CORE_ASSERT(!Has<T>(), "Entity already has a {}", T::c_TypeName);

            return m_Scene->m_Registry.emplace<T>(m_Handle, std::forward<Args>(args)...);
        }

        template<Component T>
        [[nodiscard]] T& Get()
        {
            TR_CORE_ASSERT(Has<T>(), "Entity has no {}", T::c_TypeName);

            return m_Scene->m_Registry.get<T>(m_Handle);
        }

        template<Component T>
        [[nodiscard]] const T& Get() const
        {
            TR_CORE_ASSERT(Has<T>(), "Entity has no {}", T::c_TypeName);

            return std::as_const(m_Scene->m_Registry).get<T>(m_Handle);
        }

        // Reads through the const registry, so asking never creates a pool
        template<Component T>
        [[nodiscard]] bool Has() const
        {
            return std::as_const(m_Scene->m_Registry).all_of<T>(m_Handle);
        }

        template<Component T>
        void Remove()
        {
            static_assert(!std::is_same_v<T, IDComponent>, "An entity keeps its ID until it is destroyed");
            TR_CORE_ASSERT(Has<T>(), "Entity has no {}", T::c_TypeName);

            m_Scene->m_Registry.remove<T>(m_Handle);
        }

        [[nodiscard]] UUID GetUUID() const { return Get<IDComponent>().ID; }

        [[nodiscard]] bool IsValid() const { return m_Scene != nullptr && m_Scene->m_Registry.valid(m_Handle); }
        explicit operator bool() const { return IsValid(); }

        [[nodiscard]] entt::entity GetHandle() const { return m_Handle; }
        [[nodiscard]] Scene* GetScene() const { return m_Scene; }

        [[nodiscard]] bool operator==(const Entity&) const = default;

    private:
        entt::entity m_Handle = entt::null;
        Scene* m_Scene = nullptr;
    };
}