#include "Modules/DebuggerSettings.hpp"

#include <Trinity.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    // Both files are JSON with comments, which VS Code, Visual Studio and Rider read
    constexpr std::string_view c_Marker = "// Written by Forge, which keeps this file up to date while this line is here. Take the line out to keep changes of your own";

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    // A JSON string, quoted, such as a Windows path with its backslashes
    std::string Quote(std::string_view text)
    {
        std::string l_Quoted = "\"";
        for (const char it_Character : text)
        {
            switch (it_Character)
            {
                case '"': l_Quoted += "\\\""; break;
                case '\\': l_Quoted += "\\\\"; break;
                case '\n': l_Quoted += "\\n"; break;
                case '\r': l_Quoted += "\\r"; break;
                case '\t': l_Quoted += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(it_Character) < 0x20)
                    {
                        l_Quoted += std::format("\\u{:04x}", static_cast<unsigned int>(static_cast<unsigned char>(it_Character)));
                    }
                    else
                    {
                        l_Quoted += it_Character;
                    }

                    break;
            }
        }

        return l_Quoted + "\"";
    }

    // What each launch starts: this Forge, in its own folder, on the project
    struct Launch
    {
        std::string Program;
        std::string Folder;
        std::string Project;
    };

    Launch GetLaunch(const Trinity::Project& project)
    {
        return { ToUtf8(Trinity::Process::GetExecutablePath()), ToUtf8(Trinity::Process::GetExecutableDirectory()), ToUtf8(project.GetFilePath()) };
    }

    // Launching Forge on the project, and attaching to a Forge running, which VS Code asks to pick from the processes, for each language the project has. C# runs in .NET's debugger, and C++ in Visual Studio's on Windows and in GDB, or LLDB on macOS, elsewhere
    std::string MakeLaunchJson(const Trinity::Project& project)
    {
        const Launch l_Launch = GetLaunch(project);
        const std::string l_Program = Quote(l_Launch.Program);
        const std::string l_Folder = Quote(l_Launch.Folder);
        const std::string l_Arguments = Quote(std::format("--project={}", l_Launch.Project));

        std::vector<std::string> l_Configurations;
        if (project.HasAssembly())
        {
            l_Configurations.push_back(std::format("    {{\n      \"name\": \"Forge: C#\",\n      \"type\": \"coreclr\",\n      \"request\": \"launch\",\n      \"program\": {},\n      \"args\": [{}],\n      \"cwd\": {}\n    }}", l_Program, l_Arguments, l_Folder));
            l_Configurations.push_back("    {\n      \"name\": \"Attach to Forge: C#\",\n      \"type\": \"coreclr\",\n      \"request\": \"attach\",\n      \"processId\": \"${command:pickProcess}\"\n    }");
        }

        if (project.HasModule())
        {
#if defined(TR_PLATFORM_WINDOWS)
            l_Configurations.push_back(std::format("    {{\n      \"name\": \"Forge: C++\",\n      \"type\": \"cppvsdbg\",\n      \"request\": \"launch\",\n      \"program\": {},\n      \"args\": [{}],\n      \"cwd\": {}\n    }}", l_Program, l_Arguments, l_Folder));
            l_Configurations.push_back("    {\n      \"name\": \"Attach to Forge: C++\",\n      \"type\": \"cppvsdbg\",\n      \"request\": \"attach\",\n      \"processId\": \"${command:pickProcess}\"\n    }");
#else
#if defined(TR_PLATFORM_MACOS)
            constexpr std::string_view c_Debugger = "lldb";
#else
            constexpr std::string_view c_Debugger = "gdb";
#endif
            l_Configurations.push_back(std::format("    {{\n      \"name\": \"Forge: C++\",\n      \"type\": \"cppdbg\",\n      \"request\": \"launch\",\n      \"program\": {},\n      \"args\": [{}],\n      \"cwd\": {},\n      \"MIMode\": \"{}\"\n    }}", l_Program, l_Arguments, l_Folder, c_Debugger));
            l_Configurations.push_back(std::format("    {{\n      \"name\": \"Attach to Forge: C++\",\n      \"type\": \"cppdbg\",\n      \"request\": \"attach\",\n      \"program\": {},\n      \"processId\": \"${{command:pickProcess}}\",\n      \"MIMode\": \"{}\"\n    }}", l_Program, c_Debugger));
#endif
        }

        std::string l_Joined;
        for (const std::string& it_Configuration : l_Configurations)
        {
            l_Joined += l_Joined.empty() ? it_Configuration : ",\n" + it_Configuration;
        }

        return std::format("{}\n{{\n  \"version\": \"0.2.0\",\n  \"configurations\": [\n{}\n  ]\n}}\n", c_Marker, l_Joined);
    }

    // Forge started as a program of the C# project's, with Visual Studio's debugger on its native code as well as its managed code, which Rider leaves to its own choice. The project's path is quoted, since the arguments are one line split as a shell would
    std::string MakeLaunchSettings(const Trinity::Project& project)
    {
        const Launch l_Launch = GetLaunch(project);

        return std::format("{}\n{{\n  \"profiles\": {{\n    \"Forge\": {{\n      \"commandName\": \"Executable\",\n      \"executablePath\": {},\n      \"commandLineArgs\": {},\n      \"workingDirectory\": {},\n      \"nativeDebugging\": true\n    }}\n  }}\n}}\n", c_Marker, Quote(l_Launch.Program), Quote(std::format("--project=\"{}\"", l_Launch.Project)), Quote(l_Launch.Folder));
    }

    // Written only when it changes, so an IDE that watches it does not reload it each time Forge opens
    void WriteOwned(const std::filesystem::path& path, const std::string& text)
    {
        std::error_code l_Error;
        if (std::filesystem::exists(path, l_Error))
        {
            std::ifstream l_Existing(path, std::ios::binary);
            const std::string l_Text((std::istreambuf_iterator<char>(l_Existing)), std::istreambuf_iterator<char>());
            if (!l_Text.starts_with(c_Marker))
            {
                TR_TRACE("Forge: {} is kept as it is, since Forge's first line was taken out of it", ToUtf8(path));

                return;
            }

            if (l_Text == text)
            {
                return;
            }
        }

        std::filesystem::create_directories(path.parent_path(), l_Error);
        std::ofstream l_File(path, std::ios::binary | std::ios::trunc);
        l_File << text;
        if (!l_File)
        {
            TR_WARN("Forge: {} could not be written, so an IDE has no settings to debug the scripts in Forge with", ToUtf8(path));

            return;
        }

        TR_INFO("Forge: wrote {}, which an IDE debugs the scripts in this Forge with", ToUtf8(path));
    }
}

namespace DebuggerSettings
{
    void Write(const Trinity::Project& project)
    {
        if (!project.HasModule() && !project.HasAssembly())
        {
            return;
        }

        const std::string l_LaunchJson = MakeLaunchJson(project);
        WriteOwned(project.GetDirectory() / ".vscode" / "launch.json", l_LaunchJson);
        if (project.HasModule())
        {
            WriteOwned(project.GetSourceDirectory() / ".vscode" / "launch.json", l_LaunchJson);
        }

        if (project.HasAssembly())
        {
            WriteOwned(project.GetScriptsDirectory() / "Properties" / "launchSettings.json", MakeLaunchSettings(project));
        }
    }
}