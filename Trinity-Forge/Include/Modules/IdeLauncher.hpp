#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

// What Forge opens scripts in: the program the system has for each kind of file, or an IDE found on this machine, which opens a file at its line within its solution or folder
enum class ScriptEditor : std::uint8_t
{
    System,
    VisualStudio,
    Rider,
    VSCode
};

inline constexpr std::array<ScriptEditor, 4> c_ScriptEditors{ ScriptEditor::System, ScriptEditor::VisualStudio, ScriptEditor::Rider, ScriptEditor::VSCode };

// Finds each IDE where its installer puts it: Visual Studio through vswhere, Rider through JetBrains Toolbox's scripts or its own folders, and VS Code where it installs or on the PATH. Each is looked for once, the first time it is asked about, and remembered. Main thread only
namespace IdeLauncher
{
    // As menus show it
    [[nodiscard]] std::string_view ToString(ScriptEditor editor);
    // As imgui.ini keeps it
    [[nodiscard]] std::string_view ToKey(ScriptEditor editor);
    [[nodiscard]] std::optional<ScriptEditor> FromKey(std::string_view key);

    // The IDE's program, or empty when it is not on this machine. The system's choice has none, and is always there
    [[nodiscard]] const std::filesystem::path& Find(ScriptEditor editor);
    [[nodiscard]] bool IsAvailable(ScriptEditor editor);

    // The file at its line, counted from 1, or at its top for 0, opened with the solution or folder it belongs to, which an IDE loads with it. False, logged, when the editor could not be started
    bool OpenFile(ScriptEditor editor, const std::filesystem::path& file, std::uint32_t line, const std::filesystem::path& workspace);
    // The solution or the folder itself
    bool OpenWorkspace(ScriptEditor editor, const std::filesystem::path& workspace);
    // The IDE's debugger on the process: Visual Studio through its debugger picker, which asks for the instance to use and lets native and managed code be debugged at once, and Rider attached with the solution or folder open. VS Code cannot be asked to attach, so it opens the folder, whose launch.json attaches from Run and Debug. False, logged, when the editor could not be started, or for the system's choice, which has no debugger
    bool AttachDebugger(ScriptEditor editor, std::uint32_t processID, const std::filesystem::path& workspace);
}