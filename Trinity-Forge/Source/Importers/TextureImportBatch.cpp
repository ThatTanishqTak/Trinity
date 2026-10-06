#include "Importers/TextureImportBatch.hpp"

#include <algorithm>
#include <utility>

namespace
{
    double SecondsSince(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
}

// The import writes into the project's Cache, which must still be mounted when it finishes
TextureImportBatch::~TextureImportBatch()
{
    if (m_Running)
    {
        m_Running->Stop.store(true, std::memory_order_relaxed);
        Trinity::JobSystem::Wait(m_Jobs);
    }
}

// On the job system, one texture at a time, since the encoder already uses every core. A texture with nothing cooked yet is held in the asset manager until its import ends, so nothing reads the file before it exists, and a sprite shows the placeholder meanwhile
void TextureImportBatch::Start(std::vector<Trinity::AssetRecord> records)
{
    TR_ASSERT(!m_Running, "A texture import batch is already running");

    for (const Trinity::AssetRecord& it_Record : records)
    {
        if (!Trinity::FileSystem::Exists(Trinity::GetCookedTexturePath(it_Record.ID)))
        {
            Trinity::AssetManager::Hold(it_Record.ID);
            m_Held.push_back(it_Record.ID);
        }
    }

    m_Running = std::make_unique<Running>();
    m_Running->Report.Textures = records.size();
    m_Running->Records = std::move(records);
    m_Start = Clock::now();

    Running* l_Running = m_Running.get();
    Trinity::JobSystem::Submit([l_Running] { Run(*l_Running); }, &m_Jobs);
}

// On the main thread, once a frame. Each finished texture is loaded by everything holding it, and the report comes back once the batch is over
std::optional<TextureImportReport> TextureImportBatch::Update()
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

    // Textures left by Stop stay held, and the next batch takes them up again
    const TextureImportReport l_Report = m_Running->Report;
    if (m_Running->EncodeSeconds > 0.0)
    {
        m_TexelsPerSecond = static_cast<double>(m_Running->EncodedTexels) / m_Running->EncodeSeconds;
    }

    m_Running.reset();
    m_Held.clear();

    return l_Report;
}

TextureImportReport TextureImportBatch::Wait()
{
    if (!m_Running)
    {
        return {};
    }

    Trinity::JobSystem::Wait(m_Jobs);

    return Update().value_or(TextureImportReport{});
}

// After the texture being encoded, which the encoder cannot leave halfway
void TextureImportBatch::Stop()
{
    if (m_Running)
    {
        m_Running->Stop.store(true, std::memory_order_relaxed);
    }
}

TextureImportBatch::Progress TextureImportBatch::GetProgress() const
{
    if (!m_Running)
    {
        return {};
    }

    const Running& l_Running = *m_Running;
    std::scoped_lock l_Lock(l_Running.Mutex);

    Progress l_Progress;
    l_Progress.Textures = l_Running.Report.Textures;
    l_Progress.Planned = l_Running.Planned;
    l_Progress.ToEncode = l_Running.ToEncode;
    l_Progress.Encoding = l_Running.Encoding;
    l_Progress.Path = l_Running.Path;
    l_Progress.Width = l_Running.Width;
    l_Progress.Height = l_Running.Height;
    l_Progress.DoneTexels = l_Running.DoneTexels;
    l_Progress.TotalTexels = l_Running.TotalTexels;
    l_Progress.CurrentTexels = l_Running.CurrentTexels;
    l_Progress.CurrentSeconds = l_Running.Encoding != 0 ? SecondsSince(l_Running.CurrentStart) : 0.0;
    l_Progress.ElapsedSeconds = SecondsSince(m_Start);
    l_Progress.TexelsPerSecond = l_Running.EncodeSeconds > 0.0 ? std::optional(static_cast<double>(l_Running.EncodedTexels) / l_Running.EncodeSeconds) : m_TexelsPerSecond;
    l_Progress.Stopping = l_Running.Stop.load(std::memory_order_relaxed);

    return l_Progress;
}

// First every texture is planned, which reads its file but encodes nothing, so the whole amount of work is known before the first encode starts
void TextureImportBatch::Run(Running& running)
{
    struct ToEncode
    {
        const Trinity::AssetRecord* Record = nullptr;
        TextureImporter::Plan Plan;
    };

    const auto a_Finish = [&running](Trinity::UUID id, TextureImporter::Result result)
    {
        running.Completed.push_back({ id, result });
        switch (result)
        {
            case TextureImporter::Result::Cached:
            {
                ++running.Report.Cached;
                break;
            }
            case TextureImporter::Result::Encoded:
            {
                ++running.Report.Encoded;
                break;
            }
            case TextureImporter::Result::Failed:
            {
                ++running.Report.Failed;
                break;
            }
        }
    };

    std::vector<ToEncode> l_ToEncode;
    for (const Trinity::AssetRecord& it_Record : running.Records)
    {
        if (running.Stop.load(std::memory_order_relaxed))
        {
            break;
        }

        const TextureImporter::Plan l_Plan = TextureImporter::PlanImport(it_Record);
        std::scoped_lock l_Lock(running.Mutex);
        if (l_Plan.Outcome == TextureImporter::Result::Encoded)
        {
            l_ToEncode.push_back({ &it_Record, l_Plan });
            running.TotalTexels += l_Plan.Texels;
        }
        else
        {
            a_Finish(it_Record.ID, l_Plan.Outcome);
        }
    }

    {
        std::scoped_lock l_Lock(running.Mutex);
        running.Planned = true;
        running.ToEncode = l_ToEncode.size();
    }

    for (std::size_t it_Index = 0; it_Index < l_ToEncode.size() && !running.Stop.load(std::memory_order_relaxed); ++it_Index)
    {
        const ToEncode& l_Texture = l_ToEncode[it_Index];
        {
            std::scoped_lock l_Lock(running.Mutex);
            running.Encoding = it_Index + 1;
            running.Path = l_Texture.Record->Path;
            running.Width = l_Texture.Plan.Width;
            running.Height = l_Texture.Plan.Height;
            running.CurrentTexels = l_Texture.Plan.Texels;
            running.CurrentStart = Clock::now();
        }

        const TextureImporter::Result l_Result = TextureImporter::Import(*l_Texture.Record);

        std::scoped_lock l_Lock(running.Mutex);
        const double l_Seconds = SecondsSince(running.CurrentStart);
        running.Encoding = 0;
        running.DoneTexels += l_Texture.Plan.Texels;
        if (l_Result == TextureImporter::Result::Encoded)
        {
            running.EncodedTexels += l_Texture.Plan.Texels;
            running.EncodeSeconds += l_Seconds;
        }

        a_Finish(l_Texture.Record->ID, l_Result);
    }

    {
        std::scoped_lock l_Lock(running.Mutex);
        running.Report.Stopped = running.Report.Textures - running.Report.Cached - running.Report.Encoded - running.Report.Failed;
    }

    running.Done.store(true, std::memory_order_release);
}

// A held texture loads now, and one already loaded is loaded again if its cooked file changed
void TextureImportBatch::TakeFinished()
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
        else if (it_Finished.Result == TextureImporter::Result::Encoded)
        {
            Trinity::AssetManager::Reload(it_Finished.ID);
        }
    }
}