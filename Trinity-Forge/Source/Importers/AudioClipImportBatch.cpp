#include "Importers/AudioClipImportBatch.hpp"

#include <algorithm>
#include <utility>

// The import writes into the project's Cache, which must still be mounted when it finishes
AudioClipImportBatch::~AudioClipImportBatch()
{
    if (m_Running)
    {
        m_Running->Stop.store(true, std::memory_order_relaxed);
        Trinity::JobSystem::Wait(m_Jobs);
    }
}

// A clip with no header cooked yet is held in the asset manager until its import ends, so nothing reads the file before it exists
void AudioClipImportBatch::Start(std::vector<Trinity::AssetRecord> records)
{
    TR_ASSERT(!m_Running, "An audio clip import batch is already running");

    for (const Trinity::AssetRecord& it_Record : records)
    {
        if (!Trinity::FileSystem::Exists(Trinity::GetCookedAudioClipPath(it_Record.ID)))
        {
            Trinity::AssetManager::Hold(it_Record.ID);
            m_Held.push_back(it_Record.ID);
        }
    }

    m_Running = std::make_unique<Running>();
    m_Running->Report.Clips = records.size();
    m_Running->Records = std::move(records);

    Running* l_Running = m_Running.get();
    Trinity::JobSystem::Submit([l_Running] { Run(*l_Running); }, &m_Jobs);
}

// On the main thread, once a frame. Each finished clip is loaded by everything holding it, and the report comes back once the batch is over
std::optional<AudioClipImportReport> AudioClipImportBatch::Update()
{
    if (!m_Running)
    {
        return std::nullopt;
    }

    TakeFinished();
    if (!m_Running->Done.load(std::memory_order_acquire))
    {
        return std::nullopt;
    }

    Trinity::JobSystem::Wait(m_Jobs);
    TakeFinished();

    // Clips left by Stop stay held, and the next batch takes them up again
    const AudioClipImportReport l_Report = m_Running->Report;
    m_Running.reset();
    m_Held.clear();

    return l_Report;
}

AudioClipImportReport AudioClipImportBatch::Wait()
{
    if (!m_Running)
    {
        return {};
    }

    Trinity::JobSystem::Wait(m_Jobs);

    return Update().value_or(AudioClipImportReport{});
}

// After the clip being cooked
void AudioClipImportBatch::Stop()
{
    if (m_Running)
    {
        m_Running->Stop.store(true, std::memory_order_relaxed);
    }
}

void AudioClipImportBatch::Run(Running& running)
{
    for (const Trinity::AssetRecord& it_Record : running.Records)
    {
        if (running.Stop.load(std::memory_order_relaxed))
        {
            break;
        }

        const AudioClipImporter::Result l_Result = AudioClipImporter::Import(it_Record);

        std::scoped_lock l_Lock(running.Mutex);
        running.Completed.push_back({ it_Record.ID, l_Result });
        switch (l_Result)
        {
            case AudioClipImporter::Result::Cached:
            {
                ++running.Report.Cached;
                break;
            }
            case AudioClipImporter::Result::Imported:
            {
                ++running.Report.Imported;
                break;
            }
            case AudioClipImporter::Result::Failed:
            {
                ++running.Report.Failed;
                break;
            }
        }
    }

    {
        std::scoped_lock l_Lock(running.Mutex);
        running.Report.Stopped = running.Report.Clips - running.Report.Cached - running.Report.Imported - running.Report.Failed;
    }

    running.Done.store(true, std::memory_order_release);
}

// A held clip loads now, and one already loaded is loaded again if it was cooked again, so its next play takes its new load type
void AudioClipImportBatch::TakeFinished()
{
    std::vector<Finished> l_Finished;
    {
        std::scoped_lock l_Lock(m_Running->Mutex);
        l_Finished.swap(m_Running->Completed);
    }

    for (const Finished& it_Finished : l_Finished)
    {
        if (it_Finished.Result == AudioClipImporter::Result::Imported)
        {
            ++m_ImportedCount;
        }

        const auto a_Held = std::ranges::find(m_Held, it_Finished.ID);
        if (a_Held != m_Held.end())
        {
            m_Held.erase(a_Held);
            Trinity::AssetManager::Resume(it_Finished.ID);
        }
        else if (it_Finished.Result == AudioClipImporter::Result::Imported)
        {
            Trinity::AssetManager::Reload(it_Finished.ID);
        }
    }
}