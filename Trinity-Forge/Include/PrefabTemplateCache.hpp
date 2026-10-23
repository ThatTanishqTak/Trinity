#pragma once

#include <Trinity.hpp>

#include <unordered_map>

// The prefabs the open scene's instances are made from, each read from its file the first time the serializer asks for it and kept from then on, so the instances save against the prefab they were made from, even once its file is deleted or changes. Cleared as another scene opens, which reads them again
class PrefabTemplateCache
{
public:
    // Null for a prefab the registry does not have, which is asked of it again each time, and for one whose file cannot be read, which is logged once and kept as null
    [[nodiscard]] Trinity::Scene* Find(Trinity::UUID prefab, const Trinity::AssetRegistry* registry);
    void Clear() { m_Templates.clear(); }

private:
    std::unordered_map<Trinity::UUID, Trinity::Scope<Trinity::Scene>> m_Templates;
};