#include "Modules/IdeLauncher.hpp"

#include <Trinity.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace
{
    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string l_Text = path.u8string();

        return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
    }

    std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    // A folder from the environment, or empty when the variable is not set
    std::filesystem::path GetFolder(const Trinity::Process::Environment& environment, std::string_view name)
    {
        const std::optional<std::string> l_Value = Trinity::Process::FindVariable(environment, name);

        return l_Value && !l_Value->empty() ? FromUtf8(*l_Value) : std::filesystem::path();
    }

    bool IsFile(const std::filesystem::path& path)
    {
        std::error_code l_Error;

        return !path.empty() && std::filesystem::is_regular_file(path, l_Error);
    }

    std::filesystem::path FirstFile(std::initializer_list<std::filesystem::path> candidates)
    {
        for (const std::filesystem::path& it_Candidate : candidates)
        {
            if (IsFile(it_Candidate))
            {
                return it_Candidate;
            }
        }

        return {};
    }

#if defined(TR_PLATFORM_WINDOWS)
    // The program a launcher script starts, the first quoted path in it ending in .exe, so the IDE runs as itself, with no cmd.exe between it and its arguments. Toolbox writes rider.cmd so
    std::filesystem::path ReadLauncherScript(const std::filesystem::path& script)
    {
        std::ifstream l_File(script, std::ios::binary);
        const std::string l_Text((std::istreambuf_iterator<char>(l_File)), std::istreambuf_iterator<char>());
        for (std::size_t l_Open = l_Text.find('"'); l_Open != std::string::npos; l_Open = l_Text.find('"', l_Open + 1))
        {
            const std::size_t l_Close = l_Text.find('"', l_Open + 1);
            if (l_Close == std::string::npos)
            {
                break;
            }

            const std::string_view l_Quoted = std::string_view(l_Text).substr(l_Open + 1, l_Close - l_Open - 1);
            std::string l_Lower(l_Quoted);
            std::ranges::transform(l_Lower, l_Lower.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
            if (l_Lower.ends_with(".exe") && IsFile(FromUtf8(l_Quoted)))
            {
                return FromUtf8(l_Quoted);
            }

            l_Open = l_Close;
        }

        return {};
    }

    // The newest of the folders in the parent whose names start so, by name, which puts the latest version last
    std::filesystem::path FindNewestFolder(const std::filesystem::path& parent, std::string_view prefix, const std::filesystem::path& program)
    {
        std::error_code l_Error;
        std::vector<std::filesystem::path> l_Found;
        for (const std::filesystem::directory_entry& it_Entry : std::filesystem::directory_iterator(parent, l_Error))
        {
            if (it_Entry.is_directory(l_Error) && ToUtf8(it_Entry.path().filename()).starts_with(prefix) && IsFile(it_Entry.path() / program))
            {
                l_Found.push_back(it_Entry.path() / program);
            }
        }

        std::ranges::sort(l_Found);

        return l_Found.empty() ? std::filesystem::path() : l_Found.back();
    }
#endif

    // vswhere, which every Visual Studio installer puts in the same place, names the newest install's devenv.exe
    std::filesystem::path FindVisualStudio([[maybe_unused]] const Trinity::Process::Environment& environment)
    {
#if defined(TR_PLATFORM_WINDOWS)
        const std::filesystem::path l_Where = GetFolder(environment, "ProgramFiles(x86)") / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        if (!IsFile(l_Where))
        {
            return {};
        }

        std::string l_Product;
        const std::array<std::string, 6> l_Arguments{ "-latest", "-prerelease", "-products", "*", "-property", "productPath" };
        Trinity::ProcessOptions l_Options;
        l_Options.OnLine = [&l_Product](std::string_view line)
        {
            if (l_Product.empty() && !line.empty())
            {
                l_Product = line;
            }
        };

        const Trinity::Expected<int, std::string> l_Ran = Trinity::Process::Run(l_Where, l_Arguments, l_Options);
        if (!l_Ran || *l_Ran != 0)
        {
            return {};
        }

        const std::filesystem::path l_Devenv = FromUtf8(l_Product);

        return IsFile(l_Devenv) ? l_Devenv : std::filesystem::path();
#else
        return {};
#endif
    }

    std::filesystem::path FindRider(const Trinity::Process::Environment& environment)
    {
#if defined(TR_PLATFORM_WINDOWS)
        const std::filesystem::path l_Toolbox = GetFolder(environment, "LOCALAPPDATA") / "JetBrains" / "Toolbox" / "scripts" / "rider.cmd";
        if (IsFile(l_Toolbox))
        {
            if (std::filesystem::path l_Program = ReadLauncherScript(l_Toolbox); !l_Program.empty())
            {
                return l_Program;
            }
        }

        if (std::filesystem::path l_Installed = FindNewestFolder(GetFolder(environment, "ProgramFiles") / "JetBrains", "JetBrains Rider", std::filesystem::path("bin") / "rider64.exe"); !l_Installed.empty())
        {
            return l_Installed;
        }

        return FirstFile({ GetFolder(environment, "LOCALAPPDATA") / "Programs" / "Rider" / "bin" / "rider64.exe" });
#elif defined(TR_PLATFORM_MACOS)
        const std::filesystem::path l_Home = GetFolder(environment, "HOME");

        return FirstFile({ l_Home / "Library" / "Application Support" / "JetBrains" / "Toolbox" / "scripts" / "rider", "/Applications/Rider.app/Contents/MacOS/rider", Trinity::Process::FindExecutable("rider", &environment) });
#else
        const std::filesystem::path l_Home = GetFolder(environment, "HOME");

        return FirstFile({ Trinity::Process::FindExecutable("rider", &environment), l_Home / ".local" / "share" / "JetBrains" / "Toolbox" / "scripts" / "rider", "/snap/bin/rider", Trinity::Process::FindExecutable("rider.sh", &environment) });
#endif
    }

    // On Windows code.cmd on the PATH sits in bin, below the folder that holds Code.exe
    std::filesystem::path FindVSCode(const Trinity::Process::Environment& environment)
    {
#if defined(TR_PLATFORM_WINDOWS)
        const std::filesystem::path l_OnPath = Trinity::Process::FindExecutable("code", &environment);
        const std::filesystem::path l_BesidePath = l_OnPath.empty() ? std::filesystem::path() : l_OnPath.parent_path().parent_path() / "Code.exe";

        return FirstFile({ GetFolder(environment, "LOCALAPPDATA") / "Programs" / "Microsoft VS Code" / "Code.exe", GetFolder(environment, "ProgramFiles") / "Microsoft VS Code" / "Code.exe", l_BesidePath });
#elif defined(TR_PLATFORM_MACOS)
        return FirstFile({ "/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code", Trinity::Process::FindExecutable("code", &environment) });
#else
        return FirstFile({ Trinity::Process::FindExecutable("code", &environment), "/usr/share/code/bin/code", "/snap/bin/code" });
#endif
    }

    // VS Code opens folders, so a solution opens as the folder it is in, where C# Dev Kit finds it
    std::filesystem::path GetFolderOf(const std::filesystem::path& workspace)
    {
        std::error_code l_Error;

        return std::filesystem::is_directory(workspace, l_Error) ? workspace : workspace.parent_path();
    }

    bool Launch(ScriptEditor editor, std::span<const std::string> arguments, const std::filesystem::path& opened)
    {
        const std::filesystem::path& l_Program = IdeLauncher::Find(editor);
        if (l_Program.empty())
        {
            TR_ERROR("Forge: {} is not installed here, so {} was not opened. Pick another editor under Scripts > Script Editor", IdeLauncher::ToString(editor), ToUtf8(opened));

            return false;
        }

        if (const Trinity::Expected<void, std::string> l_Started = Trinity::Process::Start(l_Program, arguments); !l_Started)
        {
            TR_ERROR("Forge: {} was not opened in {}, since {}", ToUtf8(opened), IdeLauncher::ToString(editor), l_Started.GetError());

            return false;
        }

        TR_INFO("Forge: opened {} in {}", ToUtf8(opened), IdeLauncher::ToString(editor));

        return true;
    }

    bool OpenWithSystem(const std::filesystem::path& path)
    {
        if (const Trinity::Expected<void, std::string> l_Opened = Trinity::Process::OpenWithSystem(path); !l_Opened)
        {
            TR_ERROR("Forge: {}", l_Opened.GetError());

            return false;
        }

        TR_INFO("Forge: opened {} with the system's program for it", ToUtf8(path));

        return true;
    }
}

namespace IdeLauncher
{
    std::string_view ToString(ScriptEditor editor)
    {
        switch (editor)
        {
            case ScriptEditor::System:
            {
                return "System default";
            }
            case ScriptEditor::VisualStudio:
            {
                return "Visual Studio";
            }
            case ScriptEditor::Rider:
            {
                return "Rider";
            }
            case ScriptEditor::VSCode:
            {
                return "VS Code";
            }
        }

        return "System default";
    }

    std::string_view ToKey(ScriptEditor editor)
    {
        switch (editor)
        {
            case ScriptEditor::System:
            {
                return "System";
            }
            case ScriptEditor::VisualStudio:
            {
                return "VisualStudio";
            }
            case ScriptEditor::Rider:
            {
                return "Rider";
            }
            case ScriptEditor::VSCode:
            {
                return "VSCode";
            }
        }

        return "System";
    }

    std::optional<ScriptEditor> FromKey(std::string_view key)
    {
        for (const ScriptEditor it_Editor : c_ScriptEditors)
        {
            if (ToKey(it_Editor) == key)
            {
                return it_Editor;
            }
        }

        return std::nullopt;
    }

    const std::filesystem::path& Find(ScriptEditor editor)
    {
        static std::array<std::optional<std::filesystem::path>, c_ScriptEditors.size()> s_Found;
        std::optional<std::filesystem::path>& l_Found = s_Found[static_cast<std::size_t>(editor)];
        if (!l_Found)
        {
            const Trinity::Process::Environment l_Environment = Trinity::Process::GetEnvironment();
            switch (editor)
            {
                case ScriptEditor::System:
                {
                    l_Found = std::filesystem::path();

                    break;
                }
                case ScriptEditor::VisualStudio:
                {
                    l_Found = FindVisualStudio(l_Environment);
                    
                    break;
                }
                case ScriptEditor::Rider:
                {
                    l_Found = FindRider(l_Environment);
                    
                    break;
                }
                case ScriptEditor::VSCode:
                {
                    l_Found = FindVSCode(l_Environment);
                    
                    break;
                }
            }

            if (editor != ScriptEditor::System)
            {
                TR_TRACE("Forge: {} {}", ToString(editor), l_Found->empty() ? std::string("was not found") : std::format("is {}", ToUtf8(*l_Found)));
            }
        }

        return *l_Found;
    }

    bool IsAvailable(ScriptEditor editor)
    {
        return editor == ScriptEditor::System || !Find(editor).empty();
    }

    // Visual Studio opens the file in the instance running, or a new one, and goes to the line through its command window. Rider and VS Code load the solution or folder with it
    bool OpenFile(ScriptEditor editor, const std::filesystem::path& file, std::uint32_t line, const std::filesystem::path& workspace)
    {
        const std::string l_File = ToUtf8(file);
        std::vector<std::string> l_Arguments;
        switch (editor)
        {
            case ScriptEditor::System:
            {
                return OpenWithSystem(file);
            }
            case ScriptEditor::VisualStudio:
            {
                l_Arguments = { "/Edit", l_File };
                if (line > 0)
                {
                    l_Arguments.insert(l_Arguments.end(), { "/Command", std::format("Edit.GoTo {}", line) });
                }

                break;
            }
            case ScriptEditor::Rider:
            {
                l_Arguments = { ToUtf8(workspace) };
                if (line > 0)
                {
                    l_Arguments.insert(l_Arguments.end(), { "--line", std::to_string(line) });
                }

                l_Arguments.push_back(l_File);

                break;
            }
            case ScriptEditor::VSCode:
            {
                l_Arguments = { ToUtf8(GetFolderOf(workspace)), "--goto", line > 0 ? std::format("{}:{}", l_File, line) : l_File };

                break;
            }
        }

        return Launch(editor, l_Arguments, file);
    }

    bool OpenWorkspace(ScriptEditor editor, const std::filesystem::path& workspace)
    {
        if (editor == ScriptEditor::System)
        {
            return OpenWithSystem(workspace);
        }

        const std::array<std::string, 1> l_Arguments{ ToUtf8(editor == ScriptEditor::VSCode ? GetFolderOf(workspace) : workspace) };

        return Launch(editor, l_Arguments, workspace);
    }

    // Visual Studio's picker is the just-in-time debugger it installs in System32, which attaches the instance chosen in it
    bool AttachDebugger(ScriptEditor editor, std::uint32_t processID, const std::filesystem::path& workspace)
    {
        const std::string l_Process = std::format("Forge (process {})", processID);
        switch (editor)
        {
            case ScriptEditor::System:
            {
                TR_ERROR("Forge: the system's program has no debugger, so none was attached. Pick Visual Studio, Rider or VS Code under Scripts > Script Editor, or attach a debugger to process {}", processID);

                return false;
            }
            case ScriptEditor::VisualStudio:
            {
                std::filesystem::path l_Picker;
#if defined(TR_PLATFORM_WINDOWS)
                const Trinity::Process::Environment l_Environment = Trinity::Process::GetEnvironment();
                l_Picker = FirstFile({ GetFolder(l_Environment, "SystemRoot") / "System32" / "vsjitdebugger.exe", Trinity::Process::FindExecutable("vsjitdebugger.exe", &l_Environment) });
#endif
                if (l_Picker.empty() || !IsAvailable(editor))
                {
                    TR_ERROR("Forge: Visual Studio's debugger is not installed here, so none was attached. In Visual Studio, use Debug > Attach to Process with Native and Managed (.NET Core, .NET 5+) code, and pick process {}", processID);

                    return false;
                }

                const std::array<std::string, 2> l_Arguments{ "-p", std::to_string(processID) };
                if (const Trinity::Expected<void, std::string> l_Started = Trinity::Process::Start(l_Picker, l_Arguments); !l_Started)
                {
                    TR_ERROR("Forge: Visual Studio's debugger was not attached to {}, since {}", l_Process, l_Started.GetError());

                    return false;
                }

                TR_INFO("Forge: Visual Studio's debugger picker is attaching to {}. Choose the instance with the scripts open, and to debug both languages, Manually choose the debugging engines with Native and Managed (.NET Core, .NET 5+)", l_Process);

                return true;
            }
            case ScriptEditor::Rider:
            {
                const std::filesystem::path& l_Program = Find(editor);
                if (l_Program.empty())
                {
                    TR_ERROR("Forge: Rider is not installed here, so no debugger was attached to {}. Pick another editor under Scripts > Script Editor", l_Process);

                    return false;
                }

                // Rider picks the .NET or the native debugger by what the process has loaded
                const std::array<std::string, 3> l_Arguments{ "attach-to-process", std::to_string(processID), ToUtf8(workspace) };
                if (const Trinity::Expected<void, std::string> l_Started = Trinity::Process::Start(l_Program, l_Arguments); !l_Started)
                {
                    TR_ERROR("Forge: Rider's debugger was not attached to {}, since {}", l_Process, l_Started.GetError());

                    return false;
                }

                TR_INFO("Forge: Rider is attaching its debugger to {}, with {} open", l_Process, ToUtf8(workspace));

                return true;
            }
            case ScriptEditor::VSCode:
            {
                if (!OpenWorkspace(editor, workspace))
                {
                    return false;
                }

                TR_INFO("Forge: VS Code cannot be asked to attach, so in Run and Debug pick Attach to Forge: C# or Attach to Forge: C++, then process {}", processID);

                return true;
            }
        }

        return false;
    }
}