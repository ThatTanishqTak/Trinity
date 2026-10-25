#include "PrefabTemplateCache.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

// A model is read once its import has cooked its hierarchy. What would not read is kept, so the file is found changed only once it holds something else
Trinity::Scene* PrefabTemplateCache::Find(Trinity::UUID prefab, const Trinity::AssetRegistry* registry)
{
    if (const auto a_Template = m_Templates.find(prefab); a_Template != m_Templates.end())
    {
        return a_Template->second.Template.get();
    }

    const Trinity::AssetRecord* l_Record = registry != nullptr ? registry->Find(prefab) : nullptr;
    const bool l_Model = l_Record != nullptr && l_Record->Importer == Trinity::c_ModelAssetType;
    if (l_Record == nullptr || (!l_Model && l_Record->Importer != Trinity::PrefabAsset::c_AssetType) || (l_Model && !Trinity::FileSystem::Exists(Trinity::GetCookedModelPath(prefab))))
    {
        return nullptr;
    }

    Entry l_Entry{ .Template = Trinity::CreateScope<Trinity::Scene>(), .Text = {}, .Refused = {}, .Version = ++m_LastVersion };
    Trinity::Expected<std::string, std::string> l_Text = ReadText(*l_Record);
    const Trinity::Expected<void, std::string> l_Loaded = l_Text ? Trinity::SceneSerializer::LoadPrefabFromText(*l_Entry.Template, *l_Text, prefab) : Trinity::Expected<void, std::string>(Trinity::Unexpected{ l_Text.GetError() });
    if (!l_Loaded)
    {
        TR_ERROR("Forge: {} {} cannot be read, so its instances are kept as the scene file has them: {}", l_Model ? "model" : "prefab", l_Record->Path, l_Loaded.GetError());
        l_Entry.Template.reset();
        l_Entry.Refused = l_Text ? std::move(*l_Text) : std::string();
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

std::uint64_t PrefabTemplateCache::GetVersion(Trinity::UUID prefab) const
{
    const auto a_Template = m_Templates.find(prefab);

    return a_Template != m_Templates.end() && a_Template->second.Template ? a_Template->second.Version : 0;
}

// Inner ones first: a prefab is read once each other one to be read that it holds is, or, while each left holds another, as when they hold each other, the first left. A new template is read while the one it replaces is still kept, and a text given that does not read puts back every template replaced before it
Trinity::Expected<std::vector<Trinity::UUID>, std::string> PrefabTemplateCache::Replace(std::span<const Trinity::SceneSerializer::PrefabText> texts)
{
    // A prefab to read again, from a text given or the text it was made from, with the prefabs that text holds
    struct Pending
    {
        Trinity::UUID Prefab;
        std::string Text;
        std::vector<Trinity::UUID> Nested;
        bool Given = false;
    };

    std::vector<Pending> l_Pending;
    const auto a_IsPending = [&l_Pending](Trinity::UUID prefab) { return std::ranges::find(l_Pending, prefab, &Pending::Prefab) != l_Pending.end(); };
    for (const Trinity::SceneSerializer::PrefabText& it_Text : texts)
    {
        l_Pending.push_back({ .Prefab = it_Text.Prefab, .Text = it_Text.Text, .Nested = Trinity::SceneSerializer::GetPrefabReferences(it_Text.Text), .Given = true });
    }

    for (bool l_Grew = true; l_Grew;)
    {
        l_Grew = false;
        for (const auto& [it_Prefab, it_Entry] : m_Templates)
        {
            if (!it_Entry.Template || a_IsPending(it_Prefab))
            {
                continue;
            }

            std::vector<Trinity::UUID> l_Nested = Trinity::SceneSerializer::GetPrefabReferences(it_Entry.Text);
            if (std::ranges::any_of(l_Nested, a_IsPending))
            {
                l_Pending.push_back({ .Prefab = it_Prefab, .Text = it_Entry.Text, .Nested = std::move(l_Nested), .Given = false });
                l_Grew = true;
            }
        }
    }

    std::vector<std::pair<Trinity::UUID, std::optional<Entry>>> l_Replaced;
    std::vector<Trinity::UUID> l_Made;
    while (!l_Pending.empty())
    {
        auto a_Next = std::ranges::find_if(l_Pending, [&a_IsPending](const Pending& pending) { return std::ranges::none_of(pending.Nested, [&](Trinity::UUID nested) { return nested != pending.Prefab && a_IsPending(nested); }); });
        if (a_Next == l_Pending.end())
        {
            a_Next = l_Pending.begin();
        }

        Pending l_Next = std::move(*a_Next);
        l_Pending.erase(a_Next);

        Trinity::Scope<Trinity::Scene> l_Template = Trinity::CreateScope<Trinity::Scene>();
        if (const Trinity::Expected<void, std::string> l_Loaded = Trinity::SceneSerializer::LoadPrefabFromText(*l_Template, l_Next.Text, l_Next.Prefab); !l_Loaded)
        {
            if (l_Next.Given)
            {
                for (auto it_Replaced = l_Replaced.rbegin(); it_Replaced != l_Replaced.rend(); ++it_Replaced)
                {
                    if (it_Replaced->second)
                    {
                        m_Templates[it_Replaced->first] = std::move(*it_Replaced->second);
                    }
                    else
                    {
                        m_Templates.erase(it_Replaced->first);
                    }
                }

                m_Templates[l_Next.Prefab].Refused = std::move(l_Next.Text);

                return Trinity::Unexpected{ l_Loaded.GetError() };
            }

            TR_ERROR("Forge: prefab {} holds one that changed, and cannot be read again, so its instances are kept as the scene file has them: {}", l_Next.Prefab, l_Loaded.GetError());
            l_Template.reset();
        }

        const auto a_Kept = m_Templates.find(l_Next.Prefab);
        l_Replaced.emplace_back(l_Next.Prefab, a_Kept != m_Templates.end() ? std::optional<Entry>(std::move(a_Kept->second)) : std::nullopt);
        m_Templates[l_Next.Prefab] = Entry{ .Template = std::move(l_Template), .Text = std::move(l_Next.Text), .Refused = {}, .Version = ++m_LastVersion };
        l_Made.push_back(l_Next.Prefab);
    }

    return l_Made;
}

// A model's file is the hierarchy its import cooked, so it changes as the model is imported again, or its file renamed
std::vector<std::pair<Trinity::UUID, std::string>> PrefabTemplateCache::FindChanged(const Trinity::AssetRegistry* registry) const
{
    std::vector<std::pair<Trinity::UUID, std::string>> l_Changed;
    for (const auto& [it_Prefab, it_Entry] : m_Templates)
    {
        const Trinity::AssetRecord* l_Record = registry != nullptr ? registry->Find(it_Prefab) : nullptr;
        if (l_Record == nullptr)
        {
            continue;
        }

        Trinity::Expected<std::string, std::string> l_Text = ReadText(*l_Record);
        if (l_Text && *l_Text != it_Entry.Text && *l_Text != it_Entry.Refused)
        {
            l_Changed.emplace_back(it_Prefab, std::move(*l_Text));
        }
    }

    return l_Changed;
}

std::vector<std::pair<Trinity::UUID, Trinity::UUID>> PrefabTemplateCache::FindMissing() const
{
    std::vector<std::pair<Trinity::UUID, Trinity::UUID>> l_Missing;
    for (const auto& [it_Prefab, it_Entry] : m_Templates)
    {
        if (!it_Entry.Template)
        {
            continue;
        }

        for (const auto [it_Entity, it_Instance] : it_Entry.Template->GetRegistry().view<Trinity::PrefabInstanceComponent>().each())
        {
            if (it_Instance.IsMissing())
            {
                l_Missing.emplace_back(it_Prefab, it_Instance.Prefab);
            }
        }
    }

    return l_Missing;
}

// A model's hierarchy is made into its prefab once for each text its import cooks
Trinity::Expected<std::string, std::string> PrefabTemplateCache::ReadText(const Trinity::AssetRecord& record) const
{
    const bool l_Model = record.Importer == Trinity::c_ModelAssetType;
    Trinity::Expected<std::string, Trinity::FileError> l_File = Trinity::FileSystem::ReadText(l_Model ? Trinity::GetCookedModelPath(record.ID) : record.Path);
    if (!l_File)
    {
        return Trinity::Unexpected{ std::string(Trinity::ToString(l_File.GetError())) };
    }

    if (!l_Model)
    {
        return std::move(*l_File);
    }

    ModelText& l_Made = m_Models[record.ID];
    if (l_Made.Cooked != *l_File || (l_Made.Text.empty() && l_Made.Error.empty()))
    {
        const Trinity::Expected<Trinity::ModelData, std::string> l_Data = Trinity::ParseModelData(*l_File);
        l_Made = { .Cooked = std::move(*l_File), .Text = l_Data ? Trinity::SceneSerializer::WriteModelPrefab(*l_Data) : std::string(), .Error = l_Data ? std::string() : l_Data.GetError() };
    }

    if (!l_Made.Error.empty())
    {
        return Trinity::Unexpected{ l_Made.Error };
    }

    return l_Made.Text;
}