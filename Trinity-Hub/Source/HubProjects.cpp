#include "HubProjects.hpp"

#include "HubText.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace
{
    constexpr std::chrono::seconds c_LookInterval{ 1 };
}

HubProjects::HubProjects(std::filesystem::path file) : m_File(std::move(file))
{

}

// A read still going is let finish the project it is at, and dropped
HubProjects::~HubProjects()
{
    if (m_Reading)
    {
        m_Reading->Stop.store(true, std::memory_order_relaxed);
        Trinity::JobSystem::Wait(m_Jobs);
    }
}

void HubProjects::Refresh()
{
    if (m_Reading)
    {
        m_ReadAgain = true;

        return;
    }

    m_Reading = std::make_unique<Reading>();
    m_Reading->File = m_File;
    Reading* l_Reading = m_Reading.get();
    Trinity::JobSystem::Submit([l_Reading] { Read(*l_Reading); }, &m_Jobs);
}

void HubProjects::Update()
{
    if (!m_Reading || !m_Reading->Done.load(std::memory_order_acquire))
    {
        return;
    }

    Trinity::JobSystem::Wait(m_Jobs);
    if (m_Reading->Problem.empty())
    {
        m_Projects = std::move(m_Reading->Projects);
    }
    else if (m_Reading->Problem != m_Problem)
    {
        TR_WARN("Hub: {}", m_Reading->Problem);
    }

    m_Problem = std::move(m_Reading->Problem);
    m_ReadTime = m_Reading->Time;
    ++m_Generation;
    m_Reading.reset();
    if (std::exchange(m_ReadAgain, false))
    {
        Refresh();
    }
}

// A look at when the file was written, which costs next to nothing, once a second
void HubProjects::Watch()
{
    const auto l_Now = std::chrono::steady_clock::now();
    if (m_Reading || l_Now < m_NextLook)
    {
        return;
    }

    m_NextLook = l_Now + c_LookInterval;
    std::error_code l_Error;
    const std::filesystem::file_time_type l_Time = std::filesystem::last_write_time(m_File, l_Error);
    const std::optional<std::filesystem::file_time_type> l_Written = l_Error ? std::nullopt : std::optional(l_Time);
    if (l_Written != m_ReadTime)
    {
        Refresh();
    }
}

// When the file was written is taken before it is read, so a write in between is read at the next look
void HubProjects::Read(Reading& reading)
{
    std::error_code l_Error;
    const std::filesystem::file_time_type l_Time = std::filesystem::last_write_time(reading.File, l_Error);
    if (!l_Error)
    {
        reading.Time = l_Time;
    }

    Trinity::Expected<std::vector<Trinity::ProjectListEntry>, std::string> l_List = Trinity::ProjectList::Read(reading.File);
    if (!l_List)
    {
        reading.Problem = l_List.GetError();
        reading.Done.store(true, std::memory_order_release);

        return;
    }

    reading.Projects.reserve(l_List->size());
    for (Trinity::ProjectListEntry& it_Entry : *l_List)
    {
        if (reading.Stop.load(std::memory_order_relaxed))
        {
            break;
        }

        HubProject l_Project;
        l_Project.Entry = std::move(it_Entry);
        if (!std::filesystem::is_regular_file(l_Project.Entry.File, l_Error))
        {
            l_Project.Missing = true;
        }
        else if (Trinity::Expected<Trinity::ProjectFileInfo, std::string> l_Info = Trinity::Project::ReadInfo(l_Project.Entry.File))
        {
            l_Project.Info = std::move(*l_Info);
        }
        else
        {
            l_Project.Problem = l_Info.GetError();
        }

        l_Project.SearchText = l_Project.GetName() + '\n' + HubText::ToUtf8(l_Project.Entry.File.parent_path());
        std::ranges::transform(l_Project.SearchText, l_Project.SearchText.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
        reading.Projects.push_back(std::move(l_Project));
    }

    reading.Done.store(true, std::memory_order_release);
}