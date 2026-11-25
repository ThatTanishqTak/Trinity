#pragma once

#include <Trinity.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// Short notices stacked in the main window's bottom right corner, the newest lowest: every warning and error logged, and the successes Notify is told of. Each goes by itself, though none does while the mouse is on one of them
class Toasts
{
public:
    // From any thread. A success worth telling of, as a save is, shown at the next draw. Warnings and errors need no telling, as each one logged is shown
    static void Notify(std::string text);

    // Where a click on a warning or an error leads, which is to all of what was logged
    void SetShowConsole(std::function<void()> showConsole) { m_ShowConsole = std::move(showConsole); }
    void Draw();

private:
    enum class Kind : std::uint8_t
    {
        Success,
        Warning,
        Error
    };

    struct Toast
    {
        std::uint32_t ID = 0;
        Kind Type = Kind::Success;
        std::string Text;
        // How many times over it was told of while shown
        std::uint32_t Count = 1;
        // Seconds shown, which stand still while the mouse is on any of them
        float Age = 0.0f;
        // The one that counts what the full stack had no room for
        bool Overflow = false;
    };

    void ReadLog();
    void Add(Kind kind, std::string text);

    std::vector<Toast> m_Toasts;
    std::uint32_t m_NextID = 0;
    // The first line of the log not read yet
    std::uint64_t m_NextSequence = 0;
    // Whether the mouse was on one of them as they were last drawn, which holds them all as they are, so none moves from under it
    bool m_Hovered = false;
    std::function<void()> m_ShowConsole;
};