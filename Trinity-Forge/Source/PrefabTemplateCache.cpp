#include "PrefabTemplateCache.hpp"

#include <string>
#include <utility>

Trinity::Scene* PrefabTemplateCache::Find(Trinity::UUID prefab, const Trinity::AssetRegistry* registry)
{
    if (const auto a_Template = m_Templates.find(prefab); a_Template != m_Templates.end())
    {
        return a_Template->second.Template.get();
    }

    const Trinity::AssetRecord* l_Record = registry != nullptr ? registry->Find(prefab) : nullptr;
    if (l_Record == nullptr || l_Record->Importer != Trinity::PrefabAsset::c_AssetType)
    {
        return nullptr;
    }

    Entry l_Entry{ .Template = Trinity::CreateScope<Trinity::Scene>(), .Text = {}, .Refused = {} };
    Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(l_Record->Path);
    const Trinity::Expected<void, std::string> l_Loaded = l_Text ? Trinity::SceneSerializer::LoadPrefabFromText(*l_Entry.Template, *l_Text) : Trinity::Expected<void, std::string>(Trinity::Unexpected{ std::string(Trinity::ToString(l_Text.GetError())) });
    if (!l_Loaded)
    {
        TR_ERROR("Forge: prefab {} cannot be read, so its instances are kept as the scene file has them: {}", l_Record->Path, l_Loaded.GetError());
        l_Entry.Template.reset();
    }
    else
    {
        l_Entry.Text = std::move(*l_Text);
    }

    return m_Templates.emplace(prefab, std::move(l_Entry)).first->second.Template.get();
}

const std::string* PrefabTemplateCache::FindText(Trinity::UUID prefab) const
{
    const auto a_Template = m_Templates.find(prefab);

    return a_Template != m_Templates.end() && a_Template->second.Template ? &a_Template->second.Text : nullptr;
}

Trinity::Expected<void, std::string> PrefabTemplateCache::Replace(Trinity::UUID prefab, std::string text)
{
    Entry& l_Entry = m_Templates[prefab];
    Trinity::Scope<Trinity::Scene> l_Template = Trinity::CreateScope<Trinity::Scene>();
    if (Trinity::Expected<void, std::string> l_Loaded = Trinity::SceneSerializer::LoadPrefabFromText(*l_Template, text); !l_Loaded)
    {
        l_Entry.Refused = std::move(text);

        return l_Loaded;
    }

    l_Entry.Template = std::move(l_Template);
    l_Entry.Text = std::move(text);
    l_Entry.Refused.clear();

    return {};
}

std::vector<std::pair<Trinity::UUID, std::string>> PrefabTemplateCache::FindChanged(const Trinity::AssetRegistry* registry) const
{
    std::vector<std::pair<Trinity::UUID, std::string>> l_Changed;
    for (const auto& [it_Prefab, it_Entry] : m_Templates)
    {
        const Trinity::AssetRecord* l_Record = registry != nullptr ? registry->Find(it_Prefab) : nullptr;
        if (!it_Entry.Template || l_Record == nullptr)
        {
            continue;
        }

        Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(l_Record->Path);
        if (l_Text && *l_Text != it_Entry.Text && *l_Text != it_Entry.Refused)
        {
            l_Changed.emplace_back(it_Prefab, std::move(*l_Text));
        }
    }

    return l_Changed;
}