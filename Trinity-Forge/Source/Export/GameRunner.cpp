#include "Export/GameRunner.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace
{
    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }
}

GameRunner::~GameRunner()
{
    if (m_Running != nullptr)
    {
        m_Running->Detach.store(true, std::memory_order_relaxed);
        m_Running->Thread.join();
    }
}

void GameRunner::Start(const std::filesystem::path& executable, std::string product)
{
    Stop();

    m_Running = std::make_unique<Running>();
    m_Running->Executable = executable;
    m_Running->Product = std::move(product);
    TR_INFO("Game: started {} from {}", m_Running->Product, ToUtf8(executable));

    Running* l_Running = m_Running.get();
    m_Running->Thread = std::thread([l_Running] { Run(*l_Running); });
}

void GameRunner::Stop()
{
    if (m_Running == nullptr)
    {
        return;
    }

    m_Running->Stop.store(true, std::memory_order_relaxed);
    m_Running->Thread.join();
    m_Running.reset();
}

void GameRunner::Update()
{
    if (m_Running != nullptr && m_Running->Done.load(std::memory_order_acquire))
    {
        m_Running->Thread.join();
        m_Running.reset();
    }
}

std::filesystem::path GameRunner::GetFolder() const
{
    return IsRunning() ? m_Running->Executable.parent_path() : std::filesystem::path();
}

// The engine's console lines are [HH:MM:SS.mmm], the channel padded to 7, a space, the level padded to 8, a space, then the text
std::pair<Trinity::LogLevel, std::string_view> GameRunner::ReadLine(std::string_view line)
{
    constexpr std::size_t c_LevelWidth = 8;
    constexpr std::array<std::pair<std::string_view, Trinity::LogLevel>, 6> c_Levels{ { { "trace", Trinity::LogLevel::Trace },{ "debug", Trinity::LogLevel::Trace },{ "info", Trinity::LogLevel::Info },{ "warning", Trinity::LogLevel::Warn },{ "error", Trinity::LogLevel::Error },{ "critical", Trinity::LogLevel::Critical } } };

    const std::size_t l_Close = line.find("] ");
    if (line.empty() || line.front() != '[' || l_Close == std::string_view::npos)
    {
        return { Trinity::LogLevel::Info, line };
    }

    const std::string_view l_Rest = line.substr(l_Close + 2);
    const std::size_t l_ChannelEnd = l_Rest.find(' ');
    const std::size_t l_Level = l_ChannelEnd == std::string_view::npos ? std::string_view::npos : l_Rest.find_first_not_of(' ', l_ChannelEnd);
    if (l_Level == std::string_view::npos)
    {
        return { Trinity::LogLevel::Info, line };
    }

    const std::string_view l_Name = l_Rest.substr(l_Level, l_Rest.find(' ', l_Level) - l_Level);
    const auto a_Level = std::ranges::find(c_Levels, l_Name, &std::pair<std::string_view, Trinity::LogLevel>::first);
    if (a_Level == c_Levels.end())
    {
        return { Trinity::LogLevel::Info, line };
    }

    const std::size_t l_Text = l_Level + std::max(c_LevelWidth, l_Name.size()) + 1;

    return { a_Level->second, l_Text < l_Rest.size() ? l_Rest.substr(l_Text) : std::string_view() };
}

// Run blocks until the game ends, is stopped or is left, so it has a thread of its own rather than a job's, which would hold one of the job system's workers for as long as the game plays
void GameRunner::Run(Running& running)
{
    Trinity::ProcessOptions l_Options;
    l_Options.WorkingDirectory = running.Executable.parent_path();
    l_Options.Cancel = &running.Stop;
    l_Options.Detach = &running.Detach;
    l_Options.OnLine = [](std::string_view line)
    {
        const auto [l_Level, l_Text] = ReadLine(line);
        Trinity::Log::Print(Trinity::LogChannel::Client, l_Level, "Game: {}", l_Text);
    };

    const std::vector<std::string> l_Arguments;
    const Trinity::Expected<int, std::string> l_Exited = Trinity::Process::Run(running.Executable, l_Arguments, l_Options);
    if (running.Detach.load(std::memory_order_relaxed))
    {
        // Forge is closing, and its log with it
    }
    else if (!l_Exited)
    {
        TR_ERROR("Game: {}", l_Exited.GetError());
    }
    else if (running.Stop.load(std::memory_order_relaxed))
    {
        TR_INFO("Game: {} was stopped", running.Product);
    }
    else if (*l_Exited != 0)
    {
        TR_ERROR("Game: {} ended with exit code {}", running.Product, *l_Exited);
    }
    else
    {
        TR_INFO("Game: {} ended", running.Product);
    }

    running.Done.store(true, std::memory_order_release);
}