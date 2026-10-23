#include "PrefabTemplateCache.hpp"

#include <string>
#include <utility>

Trinity::Scene* PrefabTemplateCache::Find(Trinity::UUID prefab, const Trinity::AssetRegistry* registry)
{
    if (const auto a_Template = m_Templates.find(prefab); a_Template != m_Templates.end())
    {
        return a_Template->second.get();
    }

    const Trinity::AssetRecord* l_Record = registry != nullptr ? registry->Find(prefab) : nullptr;
    if (l_Record == nullptr || l_Record->Importer != Trinity::PrefabAsset::c_AssetType)
    {
        return nullptr;
    }

    Trinity::Scope<Trinity::Scene> l_Template = Trinity::CreateScope<Trinity::Scene>();
    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(l_Record->Path);
    const Trinity::Expected<void, std::string> l_Loaded = l_Text ? Trinity::SceneSerializer::LoadPrefabFromText(*l_Template, *l_Text) : Trinity::Expected<void, std::string>(Trinity::Unexpected{ std::string(Trinity::ToString(l_Text.GetError())) });
    if (!l_Loaded)
    {
        TR_ERROR("Forge: prefab {} cannot be read, so its instances are kept as the scene file has them: {}", l_Record->Path, l_Loaded.GetError());
        l_Template.reset();
    }

    return m_Templates.emplace(prefab, std::move(l_Template)).first->second.get();
}