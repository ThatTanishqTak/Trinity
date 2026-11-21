#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

// What Forge opens scripts in: the system's program for each kind of file, or an IDE found here
enum class ScriptEditor : std::uint8_t
{
    System,
    VisualStudio,
    Rider,
    VSCode
};

inline constexpr std::array<ScriptEditor, 4> c_ScriptEditors{ ScriptEditor::System, ScriptEditor::VisualStudio, ScriptEditor::Rider, ScriptEditor::VSCode };

// Finds each IDE where its installer puts it, once, the first time it is asked. Main thread only
namespace IdeLauncher
{
    // As menus show it
    [[nodiscard]] std::string_view ToString(ScriptEditor editor);
    // As imgui.ini keeps it
    [[nodiscard]] std::string_view ToKey(ScriptEditor editor);
    [[nodiscard]] std::optional<ScriptEditor> FromKey(std::string_view key);

    // The IDE's program, or empty when it is not on this machine. The system's choice has none
    [[nodiscard]] const std::filesystem::path& Find(ScriptEditor editor);
    [[nodiscard]] bool IsAvailable(ScriptEditor editor);

    // At its line, from 1, or its top for 0, with its solution or folder. False, logged, on failure
    bool OpenFile(ScriptEditor editor, const std::filesystem::path& file, std::uint32_t line, const std::filesystem::path& workspace);
    // The solution or the folder itself
    bool OpenWorkspace(ScriptEditor editor, const std::filesystem::path& workspace);
    // Visual Studio's picker, or Rider attached. VS Code only opens the folder. False, logged, if not
    bool AttachDebugger(ScriptEditor editor, std::uint32_t processID, const std::filesystem::path& workspace);
}