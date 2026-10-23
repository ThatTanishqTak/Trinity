#pragma once

#include <Trinity.hpp>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// The prefabs the open scene's instances are made from, each read from its file the first time the serializer asks for it and kept from then on, with the text it was made from, so the instances save against the prefab they were made from, even once its file is deleted. Cleared as another scene opens, which reads them again
class PrefabTemplateCache
{
public:
    // Null for a prefab the registry does not have, which is asked of it again each time, and for one whose file cannot be read, which is logged once and kept as null
    [[nodiscard]] Trinity::Scene* Find(Trinity::UUID prefab, const Trinity::AssetRegistry* registry);
    // The text the prefab's template was made from, or null for a prefab without one kept
    [[nodiscard]] const std::string* FindText(Trinity::UUID prefab) const;
    // The prefab's template made again from the text, in place of the one kept, which goes at once. The text is what the file holds from then on, so it is not found changed. Refused, keeping that one, for text that is not a prefab, which is not found changed either until the file changes again
    [[nodiscard]] Trinity::Expected<void, std::string> Replace(Trinity::UUID prefab, std::string text);
    // Each prefab kept whose file holds other text now, with that text. One whose file is gone, or which was kept as null, is left as it is until another scene opens
    [[nodiscard]] std::vector<std::pair<Trinity::UUID, std::string>> FindChanged(const Trinity::AssetRegistry* registry) const;
    void Clear() { m_Templates.clear(); }

private:
    // The template and the text it was made from, and the last text that would not read, so it is logged once
    struct Entry
    {
        Trinity::Scope<Trinity::Scene> Template;
        std::string Text;
        std::string Refused;
    };

    std::unordered_map<Trinity::UUID, Entry> m_Templates;
};