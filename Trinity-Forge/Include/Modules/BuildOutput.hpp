#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

// Build output read a line at a time. A warning or an error as MSVC, Clang, GCC and the C# compiler write them, or as a tool such as the linker, MSBuild or NuGet does, is logged with its file and line, as is CMake's, with the indented lines below it that say what it means. A line repeated, as MSBuild repeats each warning and error, is logged once. Every other line goes to the log at trace, and those that still say they are an error at error
class BuildOutput
{
public:
    BuildOutput(std::filesystem::path projectDirectory, std::filesystem::path sourceDirectory, std::filesystem::path buildDirectory);

    void Read(std::string_view text);
    // A CMake message still open is logged
    void Finish();

    [[nodiscard]] std::uint32_t GetErrors() const { return m_Errors; }
    [[nodiscard]] std::uint32_t GetWarnings() const { return m_Warnings; }

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
    std::uint32_t m_Errors = 0;
    std::uint32_t m_Warnings = 0;
};