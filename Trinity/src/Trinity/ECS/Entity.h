#include <entt/entt.hpp>
#include <utility>

namespace Trinity
{
    class Entity
    {
    public:
        Entity() = default;
        Entity(entt::entity entity, entt::registry* registry);
        ~Entity() = default;

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            return m_Registry->emplace<T>(m_Entity, std::forward<Args>(args)...);
        }

        template<typename T>
        T& GetComponent()
        {
            return m_Registry->get<T>(m_Entity);
        }

        void Destroy();

    private:
        entt::entity m_Entity{ entt::null };
        entt::registry* m_Registry = nullptr;
    };
}