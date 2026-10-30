#include "Modules/BuildOutput.hpp"

#include <Trinity.hpp>

#include <charconv>
#include <format>
#include <regex>
#include <utility>

namespace
{
    std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    // With forward slashes, as CMake and the Console show paths
    std::string ToGenericUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.generic_u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    std::uint32_t ToNumber(const std::ssub_match& match)
    {
        const std::string l_Text = match.str();
        std::uint32_t l_Number = 0;
        std::from_chars(l_Text.data(), l_Text.data() + l_Text.size(), l_Number);

        return l_Number;
    }

    // MSBuild ends each line it passes on with the project it came from, in brackets, which says nothing the file does not
    std::string WithoutProject(std::string line)
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        {
            line.pop_back();
        }

        const std::size_t l_Open = line.rfind(" [");
        if (l_Open != std::string::npos && line.ends_with("proj]"))
        {
            line.resize(l_Open);
        }

        return line;
    }
}

BuildOutput::BuildOutput(std::filesystem::path projectDirectory, std::filesystem::path sourceDirectory, std::filesystem::path buildDirectory) : m_Project(std::move(projectDirectory)), m_Source(std::move(sourceDirectory)), m_Build(std::move(buildDirectory))
{

}

void BuildOutput::Read(std::string_view text)
{
    const std::string l_Line = WithoutProject(std::string(text));
    if (m_CMake)
    {
        // CMake indents its message below its first line, with blank lines around it, and ends it with a line that is not indented
        if (l_Line.empty())
        {
            return;
        }

        if (l_Line.starts_with("  "))
        {
            const std::size_t l_Start = l_Line.find_first_not_of(' ');
            m_CMake->Message += m_CMake->Message.empty() ? "" : " ";
            m_CMake->Message += l_Line.substr(l_Start);

            return;
        }

        Finish();
    }

    static const std::regex c_CMake(R"(^CMake (Error|Warning)(?: \(dev\))?(?: at (.+?):(\d+)(?: \([^)]*\))?)?:\s*(.*)$)");
    // NativeAOT's compiler says Trim analysis warning or AOT analysis warning where the others say warning
    static const std::regex c_Msvc(R"(^\s*(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(?:(?:Trim|AOT) analysis )?(fatal error|error|warning)\s*([A-Za-z]+\d+)?\s*:\s*(.*)$)");
    static const std::regex c_Tool(R"(^\s*(.+?)\s*:\s*(?:(?:Trim|AOT) analysis )?(fatal error|error|warning)\s+([A-Z]+\d+)\s*:\s*(.*)$)");
    static const std::regex c_Gnu(R"(^(.+?):(\d+):(?:(\d+):)?\s*(fatal error|error|warning):\s*(.*)$)");

    std::smatch l_Match;
    if (std::regex_match(l_Line, l_Match, c_CMake))
    {
        // CMake names a file in the sources by its path from the source folder
        const std::filesystem::path l_File = FromUtf8(l_Match[2].str());
        m_CMake = Diagnostic{ l_Match[2].matched && l_File.is_relative() ? ToUtf8(m_Source / l_File) : l_Match[2].str(), ToNumber(l_Match[3]), 0, l_Match[1].str() == "Error",{}, l_Match[4].str() };
    }
    else if (std::regex_match(l_Line, l_Match, c_Msvc))
    {
        Report({ l_Match[1].str(), ToNumber(l_Match[2]), ToNumber(l_Match[3]), l_Match[4].str() != "warning", l_Match[5].str(), l_Match[6].str() });
    }
    else if (std::regex_match(l_Line, l_Match, c_Tool))
    {
        Report({ l_Match[1].str(), 0, 0, l_Match[2].str() != "warning", l_Match[3].str(), l_Match[4].str() });
    }
    else if (std::regex_match(l_Line, l_Match, c_Gnu))
    {
        Report({ l_Match[1].str(), ToNumber(l_Match[2]), ToNumber(l_Match[3]), l_Match[4].str() != "warning",{}, l_Match[5].str() });
    }
    else if (l_Line.find("error:") != std::string::npos || l_Line.find("undefined reference") != std::string::npos)
    {
        if (m_Reported.insert(l_Line).second)
        {
            ++m_Errors;
            LogError(std::format("Build: {}", l_Line));
        }
    }
    else if (!l_Line.empty())
    {
        TR_TRACE("Build: {}", l_Line);
    }
}

void BuildOutput::LogError(std::string text)
{
    TR_ERROR("{}", text);
    m_ErrorLines.push_back(std::move(text));
}

void BuildOutput::Finish()
{
    if (std::optional<Diagnostic> l_Message = std::exchange(m_CMake, std::nullopt))
    {
        Report(*l_Message);
    }
}

// As Build: Source/Player.cpp(12,5): error C2065: message, the file relative to the project when it is in it
void BuildOutput::Report(const Diagnostic& diagnostic)
{
    std::string l_Text = "Build: ";
    if (!diagnostic.File.empty())
    {
        l_Text += ShowPath(diagnostic.File);
        if (diagnostic.Line != 0)
        {
            l_Text += diagnostic.Column != 0 ? std::format("({},{})", diagnostic.Line, diagnostic.Column) : std::format("({})", diagnostic.Line);
        }

        l_Text += ": ";
    }

    l_Text += diagnostic.Error ? "error" : "warning";
    if (!diagnostic.Code.empty())
    {
        l_Text += ' ';
        l_Text += diagnostic.Code;
    }

    l_Text += ": ";
    l_Text += diagnostic.Message;
    if (!m_Reported.insert(l_Text).second)
    {
        return;
    }

    if (diagnostic.Error)
    {
        ++m_Errors;
        LogError(std::move(l_Text));
    }
    else
    {
        ++m_Warnings;
        TR_WARN("{}", l_Text);
    }
}

// A relative path is the build tree's, where the compilers run. A tool that names itself, such as MSBUILD or LINK, is shown as it is
std::string BuildOutput::ShowPath(std::string_view text) const
{
    std::filesystem::path l_Path = FromUtf8(text);
    if (!l_Path.has_parent_path() && !l_Path.has_extension())
    {
        return std::string(text);
    }

    if (l_Path.is_relative())
    {
        l_Path = m_Build / l_Path;
    }

    l_Path = l_Path.lexically_normal();
    const std::filesystem::path l_Relative = l_Path.lexically_relative(m_Project);
    if (!l_Relative.empty() && *l_Relative.begin() != "..")
    {
        return ToGenericUtf8(l_Relative);
    }

    return ToUtf8(l_Path);
}