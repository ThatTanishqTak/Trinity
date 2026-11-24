#include "EditorSelection.hpp"

#include <algorithm>

bool EditorSelection::Contains(Trinity::UUID entity) const
{
    return std::ranges::find(m_Entities, entity) != m_Entities.end();
}

void EditorSelection::Set(Trinity::UUID entity)
{
    m_Entities.clear();
    if (entity)
    {
        m_Entities.push_back(entity);
    }
}

void EditorSelection::Set(std::vector<Trinity::UUID> entities)
{
    m_Entities.clear();
    for (const Trinity::UUID it_Entity : entities)
    {
        Add(it_Entity);
    }
}

void EditorSelection::Add(Trinity::UUID entity)
{
    if (!entity)
    {
        return;
    }

    std::erase(m_Entities, entity);
    m_Entities.push_back(entity);
}

void EditorSelection::Remove(Trinity::UUID entity)
{
    std::erase(m_Entities, entity);
}

void EditorSelection::Toggle(Trinity::UUID entity)
{
    if (Contains(entity))
    {
        Remove(entity);
    }
    else
    {
        Add(entity);
    }
}

// Where the new one was selected too, the old one's place is dropped and the new one keeps its own
void EditorSelection::Replace(Trinity::UUID from, Trinity::UUID to)
{
    const auto a_From = std::ranges::find(m_Entities, from);
    if (a_From == m_Entities.end())
    {
        return;
    }

    if (!to || Contains(to))
    {
        m_Entities.erase(a_From);
    }
    else
    {
        *a_From = to;
    }
}

void EditorSelection::Prune(Trinity::Scene& scene)
{
    std::erase_if(m_Entities, [&scene](Trinity::UUID entity) { return !scene.FindEntityByUUID(entity); });
}