#include "Importers/EnvironmentImportBatch.hpp"

#include <algorithm>
#include <utility>

// The import writes into the project's Cache, which must still be mounted when it finishes
EnvironmentImportBatch::~EnvironmentImportBatch()
{
    if (m_Running)
    {
        m_Running->Stop.store(true, std::memory_order_relaxed);
        Trinity::JobSystem::Wait(m_Jobs);
    }
}

// An environment with nothing cooked yet is held in the asset manager until its import ends, so nothing reads the file before it exists, and a scene lit by it keeps the ambient light meanwhile
void EnvironmentImportBatch::Start(std::vector<Trinity::AssetRecord> records)
{
    TR_ASSERT(!m_Running, "An environment import batch is already running");

    for (const Trinity::AssetRecord& it_Record : records)
    {
        if (!Trinity::FileSystem::Exists(Trinity::GetCookedEnvironmentPath(it_Record.ID)))
        {
            Trinity::AssetManager::Hold(it_Record.ID);
            m_Held.push_back(it_Record.ID);
        }
    }

    m_Running = std::make_unique<Running>();
    m_Running->Report.Environments = records.size();
    m_Running->Records = std::move(records);

    Running* l_Running = m_Running.get();
    Trinity::JobSystem::Submit([l_Running] { Run(*l_Running); }, &m_Jobs);
}

// On the main thread, once a frame. Each finished environment is loaded by everything holding it, and the report comes back once the batch is over
std::optional<EnvironmentImportReport> EnvironmentImportBatch::Update()
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

    // Environments left by Stop stay held, and the next batch takes them up again
    const EnvironmentImportReport l_Report = m_Running->Report;
    m_Running.reset();
    m_Held.clear();

    return l_Report;
}

EnvironmentImportReport EnvironmentImportBatch::Wait()
{
    if (!m_Running)
    {
        return {};
    }

    Trinity::JobSystem::Wait(m_Jobs);

    return Update().value_or(EnvironmentImportReport{});
}

// After the environment being filtered
void EnvironmentImportBatch::Stop()
{
    if (m_Running)
    {
        m_Running->Stop.store(true, std::memory_order_relaxed);
    }
}

void EnvironmentImportBatch::Run(Running& running)
{
    for (const Trinity::AssetRecord& it_Record : running.Records)
    {
        if (running.Stop.load(std::memory_order_relaxed))
        {
            break;
        }

        const EnvironmentImporter::Result l_Result = EnvironmentImporter::Import(it_Record);

        std::scoped_lock l_Lock(running.Mutex);
        running.Completed.push_back({ it_Record.ID, l_Result });
        switch (l_Result)
        {
            case EnvironmentImporter::Result::Cached:
            {
                ++running.Report.Cached;
                break;
            }
            case EnvironmentImporter::Result::Filtered:
            {
                ++running.Report.Filtered;
                break;
            }
            case EnvironmentImporter::Result::Failed:
            {
                ++running.Report.Failed;
                break;
            }
        }
    }

    {
        std::scoped_lock l_Lock(running.Mutex);
        running.Report.Stopped = running.Report.Environments - running.Report.Cached - running.Report.Filtered - running.Report.Failed;
    }

    running.Done.store(true, std::memory_order_release);
}

// A held environment loads now, and one already loaded is loaded again if its cooked file changed
void EnvironmentImportBatch::TakeFinished()
{
    std::vector<Finished> l_Finished;
    {
        std::scoped_lock l_Lock(m_Running->Mutex);
        l_Finished.swap(m_Running->Completed);
    }

    for (const Finished& it_Finished : l_Finished)
    {
        const auto a_Held = std::ranges::find(m_Held, it_Finished.ID);
        if (a_Held != m_Held.end())
        {
            m_Held.erase(a_Held);
            Trinity::AssetManager::Resume(it_Finished.ID);
        }
        else if (it_Finished.Result == EnvironmentImporter::Result::Filtered)
        {
            Trinity::AssetManager::Reload(it_Finished.ID);
        }
    }
}