#include "Importers/TextureReimporter.hpp"

#include <algorithm>

// The import writes into the project's Cache, which must still be mounted when it finishes
TextureReimporter::~TextureReimporter()
{
    Finish();
}

// One texture at a time, since the encoder already uses every core. Applying again before an import has started replaces the settings it will use
void TextureReimporter::Queue(const Trinity::AssetRecord& record)
{
    const auto a_Queued = std::ranges::find(m_Queued, record.ID, &Trinity::AssetRecord::ID);
    if (a_Queued != m_Queued.end())
    {
        *a_Queued = record;
    }
    else
    {
        m_Queued.push_back(record);
    }

    if (!m_Running)
    {
        StartNext();
    }
}

// On the main thread. A finished texture is loaded again by everything holding it, which keeps the old one until the new one is ready
void TextureReimporter::Update()
{
    if (!m_Running || !m_Running->Finished.load(std::memory_order_acquire))
    {
        return;
    }

    Trinity::JobSystem::Wait(m_Jobs);
    if (m_Running->Result != TextureImporter::Result::Failed)
    {
        Trinity::AssetManager::Reload(m_Running->Record.ID);
    }

    m_Running.reset();
    StartNext();
}

// What has not started is dropped, since its settings are already in its .meta and the next scan imports it anyway. The import running is finished
void TextureReimporter::Finish()
{
    m_Queued.clear();
    if (m_Running)
    {
        Trinity::JobSystem::Wait(m_Jobs);
        Update();
    }
}

bool TextureReimporter::IsBusy(Trinity::UUID id) const
{
    return (m_Running && m_Running->Record.ID == id) || std::ranges::find(m_Queued, id, &Trinity::AssetRecord::ID) != m_Queued.end();
}

void TextureReimporter::StartNext()
{
    if (m_Queued.empty())
    {
        return;
    }

    m_Running = std::make_unique<Running>();
    m_Running->Record = std::move(m_Queued.front());
    m_Queued.pop_front();

    Running* l_Running = m_Running.get();
    Trinity::JobSystem::Submit([l_Running]
    {
        l_Running->Result = TextureImporter::Import(l_Running->Record);
        l_Running->Finished.store(true, std::memory_order_release);
    }, &m_Jobs);
}