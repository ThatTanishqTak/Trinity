#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// Where a project's build stands, for the menu bar. Its output is the log from FirstLine to EndLine
enum class BuildState : std::uint8_t
{
    None,
    Pending,
    Building,
    Failed,
    UpToDate
};

struct BuildStatus
{
    BuildState State = BuildState::None;
    std::uint64_t FirstLine = 0;
    std::uint64_t EndLine = 0;
    std::uint32_t Errors = 0;
    std::uint32_t Warnings = 0;
};

// Read a line at a time. Warnings and errors are logged with file and line, once. The rest at trace
class BuildOutput
{
public:
    BuildOutput(std::filesystem::path projectDirectory, std::filesystem::path sourceDirectory, std::filesystem::path buildDirectory);

    void Read(std::string_view text);
    // A CMake message still open is logged
    void Finish();

    // A line saying the build went wrong, logged at error and kept with its errors, but not counted
    void LogError(std::string text);

    [[nodiscard]] std::uint32_t GetErrors() const { return m_Errors; }
    [[nodiscard]] std::uint32_t GetWarnings() const { return m_Warnings; }
    // Every line the build logged at error, so its errors can be told from another build's beside it
    [[nodiscard]] const std::vector<std::string>& GetErrorLines() const { return m_ErrorLines; }

private:
    // A warning or an error a tool reported, with where it is when it said
    struct Diagnostic
    {
        std::string File;
        std::uint32_t Line = 0;
        std::uint32_t Column = 0;
        bool Error = false;
        std::string Code;
        std::string Message;
    };

    void Report(const Diagnostic& diagnostic);
    [[nodiscard]] std::string ShowPath(std::string_view text) const;

    std::filesystem::path m_Project;
    std::filesystem::path m_Source;
    std::filesystem::path m_Build;
    std::optional<Diagnostic> m_CMake;
    std::unordered_set<std::string> m_Reported;
    std::vector<std::string> m_ErrorLines;
    std::uint32_t m_Errors = 0;
    std::uint32_t m_Warnings = 0;
};