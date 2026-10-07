#include "Importers/ModelImportBatch.hpp"

#include "Importers/TextureImporter.hpp"

#include <algorithm>
#include <utility>

namespace
{
    bool HasSameSubAssets(const std::vector<Trinity::SubAsset>& recorded, const std::vector<Trinity::SubAsset>& planned)
    {
        return recorded.size() == planned.size() && std::ranges::all_of(planned, [&recorded](const Trinity::SubAsset& subAsset)
        {
            const auto a_Found = std::ranges::find(recorded, subAsset.Key, &Trinity::SubAsset::Key);

            return a_Found != recorded.end() && a_Found->Importer == subAsset.Importer;
        });
    }
}

// The cook writes into the project's Cache, which must still be mounted when it finishes
ModelImportBatch::~ModelImportBatch()
{
    if (m_Running)
    {
        m_Running->Stop.store(true, std::memory_order_relaxed);
        Trinity::JobSystem::Wait(m_Jobs);
    }
}

void ModelImportBatch::Start(Trinity::AssetRegistry& registry, std::vector<Trinity::AssetRecord> records)
{
    TR_ASSERT(!m_Running, "A model import batch is already running");

    m_Registry = &registry;
    m_Running = std::make_unique<Running>();
    m_Running->Report.Models = records.size();
    m_Running->Records = std::move(records);

    Running* l_Running = m_Running.get();
    Trinity::JobSystem::Submit([l_Running] { RunPlans(*l_Running); }, &m_Jobs);
}

// On the main thread, once a frame. The plans are taken up, then the cook started, and the report comes back once the cook is over or nothing needs one
std::optional<ModelImportReport> ModelImportBatch::Update()
{
    if (!m_Running || !m_Running->Done.load(std::memory_order_acquire))
    {
        return std::nullopt;
    }

    Trinity::JobSystem::Wait(m_Jobs);
    if (!m_Running->Planned)
    {
        TakePlans();
        if (!m_Running->Cooking.empty())
        {
            m_Running->Done.store(false, std::memory_order_relaxed);

            Running* l_Running = m_Running.get();
            Trinity::JobSystem::Submit([l_Running] { RunCooks(*l_Running); }, &m_Jobs);

            return std::nullopt;
        }
    }
    else
    {
        TakeCooked();
    }

    for (const Trinity::UUID it_ID : m_Held)
    {
        Trinity::AssetManager::Resume(it_ID);
    }

    const ModelImportReport l_Report = m_Running->Report;
    m_Running.reset();
    m_Held.clear();

    return l_Report;
}

ModelImportReport ModelImportBatch::Wait()
{
    while (m_Running)
    {
        Trinity::JobSystem::Wait(m_Jobs);
        if (const std::optional<ModelImportReport> l_Report = Update())
        {
            return *l_Report;
        }
    }

    return {};
}

// After the texture being encoded, which the encoder cannot leave halfway
void ModelImportBatch::Stop()
{
    if (m_Running)
    {
        m_Running->Stop.store(true, std::memory_order_relaxed);
    }
}

void ModelImportBatch::RunPlans(Running& running)
{
    for (const Trinity::AssetRecord& it_Record : running.Records)
    {
        if (running.Stop.load(std::memory_order_relaxed))
        {
            break;
        }

        running.Plans.push_back(ModelImporter::PlanImport(it_Record));
    }

    running.Done.store(true, std::memory_order_release);
}

void ModelImportBatch::RunCooks(Running& running)
{
    for (ToCook& it_Model : running.Cooking)
    {
        it_Model.Cooked = running.Stop.load(std::memory_order_relaxed) ? ModelImporter::Cooked{ ModelImporter::Result::Stopped, {}, 0, 0 } : ModelImporter::Cook(it_Model.Record, it_Model.Plan, it_Model.ExternalTextures, running.Stop);
    }

    running.Done.store(true, std::memory_order_release);
}

// A texture beside a model is set to be encoded as the model uses it, which the texture import that follows picks up. A model whose key, sub-assets and files all match is left as it is. Any other gets its sub-assets' UUIDs, the same ones for keys it had before, and those not cooked yet are held until the cook makes them
void ModelImportBatch::TakePlans()
{
    Running& l_Running = *m_Running;
    l_Running.Planned = true;
    if (l_Running.Stop.load(std::memory_order_relaxed))
    {
        l_Running.Report.Stopped = l_Running.Records.size();

        return;
    }

    for (std::size_t it_Model = 0; it_Model < l_Running.Plans.size(); ++it_Model)
    {
        const Trinity::AssetRecord& l_Planned = l_Running.Records[it_Model];
        ModelImporter::Plan& l_Plan = l_Running.Plans[it_Model];
        const Trinity::AssetRecord* l_Record = m_Registry->Find(l_Planned.ID);
        if (!l_Plan.Read || l_Record == nullptr || l_Record->Path != l_Planned.Path)
        {
            ++l_Running.Report.Failed;

            continue;
        }

        std::vector<Trinity::UUID> l_ExternalTextures;
        for (const ModelImporter::ExternalTexture& it_Texture : l_Plan.ExternalTextures)
        {
            const Trinity::AssetRecord* l_Texture = m_Registry->FindByPath(it_Texture.Path);
            if (l_Texture == nullptr || l_Texture->Importer != TextureImporter::c_Importer)
            {
                TR_WARN("Models: {} uses {}, which is not a texture in the project, so materials using it go without", l_Planned.Path, it_Texture.Path);
                l_ExternalTextures.emplace_back();

                continue;
            }

            l_ExternalTextures.push_back(l_Texture->ID);

            TextureImportSettings l_Settings = TextureImporter::ReadSettings(*l_Texture);
            const TextureImportSettings l_Before = l_Settings;
            l_Settings.Srgb = it_Texture.Usage == ModelImporter::TextureUsage::Color;
            l_Settings.NormalMap = it_Texture.Usage == ModelImporter::TextureUsage::Normal;
            if (l_Settings != l_Before && m_Registry->SetSettings(l_Texture->ID, TextureImporter::MakeSettings(l_Settings)))
            {
                TR_INFO("Models: {} is now encoded as {}, as {} uses it", it_Texture.Path, l_Settings.NormalMap ? "a normal map" : (l_Settings.Srgb ? "sRGB colour" : "linear data"), l_Planned.Path);
            }
        }

        l_Record = m_Registry->Find(l_Planned.ID);
        if (HasSameSubAssets(l_Record->SubAssets, l_Plan.SubAssets) && l_Plan.CookedFilesExist && l_Plan.CachedKey == ModelImporter::GetCacheKey(*l_Record, l_Plan, l_ExternalTextures))
        {
            ++l_Running.Report.Cached;

            continue;
        }

        if (!m_Registry->SetSubAssets(l_Planned.ID, l_Plan.SubAssets))
        {
            TR_ERROR("Models: the sub-assets of {} could not be recorded in its .meta", l_Planned.Path);
            ++l_Running.Report.Failed;

            continue;
        }

        l_Record = m_Registry->Find(l_Planned.ID);
        for (const Trinity::SubAsset& it_SubAsset : l_Record->SubAssets)
        {
            if (!Trinity::FileSystem::Exists(ModelImporter::GetCookedPath(it_SubAsset)))
            {
                Trinity::AssetManager::Hold(it_SubAsset.ID);
                m_Held.push_back(it_SubAsset.ID);
            }
        }

        l_Running.Cooking.push_back({ *l_Record, std::move(l_Plan), std::move(l_ExternalTextures), {} });
    }

    l_Running.Report.Stopped = l_Running.Records.size() - l_Running.Plans.size();
}

// A held sub-asset is resumed with the rest when the batch ends. One already loaded is loaded again if its file was rewritten
void ModelImportBatch::TakeCooked()
{
    Running& l_Running = *m_Running;
    for (const ToCook& it_Model : l_Running.Cooking)
    {
        switch (it_Model.Cooked.Outcome)
        {
            case ModelImporter::Result::Imported:
            {
                ++l_Running.Report.Imported;

                break;
            }
            case ModelImporter::Result::Stopped:
            {
                ++l_Running.Report.Stopped;

                break;
            }
            default:
            {
                ++l_Running.Report.Failed;

                break;
            }
        }

        l_Running.Report.TexturesEncoded += it_Model.Cooked.TexturesEncoded;
        l_Running.Report.TexturesCached += it_Model.Cooked.TexturesCached;
        for (const Trinity::UUID it_ID : it_Model.Cooked.Written)
        {
            if (std::ranges::find(m_Held, it_ID) == m_Held.end())
            {
                Trinity::AssetManager::Reload(it_ID);
            }
        }
    }
}