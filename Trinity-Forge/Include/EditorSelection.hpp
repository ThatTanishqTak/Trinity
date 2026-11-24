#pragma once

#include <Trinity.hpp>

#include <cstddef>
#include <vector>

// The entities selected in a scene, in the order they were picked. The last picked is the primary: the one Properties shows and the gizmo holds
class EditorSelection
{
public:
    [[nodiscard]] Trinity::UUID GetPrimary() const { return m_Entities.empty() ? Trinity::UUID() : m_Entities.back(); }
    [[nodiscard]] const std::vector<Trinity::UUID>& GetEntities() const { return m_Entities; }
    [[nodiscard]] std::size_t GetCount() const { return m_Entities.size(); }
    [[nodiscard]] bool IsEmpty() const { return m_Entities.empty(); }
    [[nodiscard]] bool Contains(Trinity::UUID entity) const;

    // The entity alone, or nothing for the invalid UUID
    void Set(Trinity::UUID entity);
    // Makes the entity the primary, whether or not it was selected already
    void Add(Trinity::UUID entity);
    void Remove(Trinity::UUID entity);
    // Out if it was in, and in as the primary if it was out
    void Toggle(Trinity::UUID entity);
    void Clear() { m_Entities.clear(); }
    // One entity's place given to another, as a stand-in's is to the instance made from it
    void Replace(Trinity::UUID from, Trinity::UUID to);
    // Lets go of the entities that are no longer in the scene
    void Prune(Trinity::Scene& scene);

private:
    std::vector<Trinity::UUID> m_Entities;
};