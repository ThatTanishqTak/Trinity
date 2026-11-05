#pragma once

#include <Trinity.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

// The game Build And Run starts, as Unity's Build And Run does: run from its folder on a thread of its own, each line it writes logged in the Console as Game: at the level the game logged it at, then how it ended. Stop ends it, as the next export into its folder does, and Forge closing leaves it running, no longer read
class GameRunner
{
public:
    GameRunner() = default;
    // Leaves the game running, and waits only for its thread to let go of it
    ~GameRunner();

    GameRunner(const GameRunner&) = delete;
    GameRunner& operator=(const GameRunner&) = delete;

    // Ends a game already running first
    void Start(const std::filesystem::path& executable, std::string product);
    // Ends the game and everything it started, and waits for it
    void Stop();
    // Once a frame, so the thread of a game that has ended is let go
    void Update();

    [[nodiscard]] bool IsRunning() const { return m_Running != nullptr && !m_Running->Done.load(std::memory_order_acquire); }
    [[nodiscard]] std::string_view GetProduct() const { return m_Running != nullptr ? std::string_view(m_Running->Product) : std::string_view(); }
    // The folder the game runs from, empty when none runs
    [[nodiscard]] std::filesystem::path GetFolder() const;

    // A line as a game's log writes it, [time] channel level text, as the text and the level, or the whole line at Info when it is another program's, such as a driver's
    [[nodiscard]] static std::pair<Trinity::LogLevel, std::string_view> ReadLine(std::string_view line);

private:
    // The flags are read by both threads, and Done set by the game's once it has said how the game ended
    struct Running
    {
        std::filesystem::path Executable;
        std::string Product;
        std::atomic<bool> Stop = false;
        std::atomic<bool> Detach = false;
        std::atomic<bool> Done = false;
        std::thread Thread;
    };

    static void Run(Running& running);

    std::unique_ptr<Running> m_Running;
};