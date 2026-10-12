#include "Trinity/Core/Process.hpp"

#include "Trinity/Core/Platform.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <ranges>
#include <system_error>
#include <vector>

namespace Trinity
{
    namespace
    {
        using FileSystemUtilities::FromUtf8;
        using FileSystemUtilities::ToUtf8;

        bool IsSameName(std::string_view left, std::string_view right)
        {
#if defined(TR_PLATFORM_WINDOWS)
            return std::ranges::equal(left, right, [](char first, char second) { return std::tolower(static_cast<unsigned char>(first)) == std::tolower(static_cast<unsigned char>(second)); });
#else
            return left == right;
#endif
        }

        // A file that runs: on Windows any file, its extension having been chosen from PATHEXT, and elsewhere one with a permission to run
        bool IsProgram(const std::filesystem::path& path)
        {
            std::error_code l_Error;
            const std::filesystem::file_status l_Status = std::filesystem::status(path, l_Error);
            if (l_Error || !std::filesystem::is_regular_file(l_Status))
            {
                return false;
            }

#if defined(TR_PLATFORM_WINDOWS)
            return true;
#else
            constexpr std::filesystem::perms c_Run = std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec;

            return (l_Status.permissions() & c_Run) != std::filesystem::perms::none;
#endif
        }
    }

    namespace Process
    {
        Expected<int, std::string> Run(const std::filesystem::path& executable, std::span<const std::string> arguments, const ProcessOptions& options)
        {
            TR_PROFILE_FUNCTION();

            Platform::ProcessRequest l_Request;
            l_Request.Executable = executable;
            l_Request.Arguments = arguments;
            l_Request.WorkingDirectory = options.WorkingDirectory;
            l_Request.Environment = options.Environment ? &*options.Environment : nullptr;
            l_Request.Cancel = options.Cancel;

            std::string l_Error;
            const std::optional<int> l_ExitCode = Platform::RunProcess(l_Request, options.OnLine, l_Error);
            if (!l_ExitCode)
            {
                return Unexpected{ std::format("{} could not be run: {}", ToUtf8(executable), l_Error) };
            }

            return *l_ExitCode;
        }

        Environment GetEnvironment()
        {
            return Platform::GetEnvironment();
        }

        std::filesystem::path GetExecutableDirectory()
        {
            return Platform::GetExecutableDirectory();
        }

        std::optional<std::string> FindVariable(const Environment& environment, std::string_view name)
        {
            const auto a_Found = std::ranges::find_if(environment, [name](const auto& variable) { return IsSameName(variable.first, name); });

            return a_Found != environment.end() ? std::optional<std::string>(a_Found->second) : std::nullopt;
        }

        void SetVariable(Environment& environment, std::string_view name, std::string_view value)
        {
            const auto a_Found = std::ranges::find_if(environment, [name](const auto& variable) { return IsSameName(variable.first, name); });
            if (a_Found != environment.end())
            {
                a_Found->second = value;
            }
            else
            {
                environment.emplace_back(name, value);
            }
        }

        std::filesystem::path FindExecutable(std::string_view name, const Environment* environment)
        {
            if (name.empty())
            {
                return {};
            }

            const Environment l_Own = environment == nullptr ? GetEnvironment() : Environment();
            const Environment& l_Environment = environment != nullptr ? *environment : l_Own;
            const std::filesystem::path l_Name = FromUtf8(name);

#if defined(TR_PLATFORM_WINDOWS)
            constexpr char c_Separator = ';';
            std::vector<std::string> l_Extensions{ std::string() };
            if (!l_Name.has_extension())
            {
                const std::string l_PathExtensions = FindVariable(l_Environment, "PATHEXT").value_or(".COM;.EXE;.BAT;.CMD");
                l_Extensions.clear();
                for (const auto it_Extension : std::views::split(l_PathExtensions, ';'))
                {
                    if (!std::ranges::empty(it_Extension))
                    {
                        l_Extensions.emplace_back(std::ranges::begin(it_Extension), std::ranges::end(it_Extension));
                    }
                }
            }
#else
            constexpr char c_Separator = ':';
            const std::array<std::string, 1> l_Extensions{};
#endif

            const auto a_Find = [&l_Extensions](const std::filesystem::path& base) -> std::filesystem::path
            {
                for (const std::string& it_Extension : l_Extensions)
                {
                    std::filesystem::path l_Candidate = base;
                    l_Candidate += FromUtf8(it_Extension);
                    if (IsProgram(l_Candidate))
                    {
                        return l_Candidate;
                    }
                }

                return {};
            };

            if (l_Name.has_parent_path())
            {
                return a_Find(l_Name);
            }

            const std::string l_Path = FindVariable(l_Environment, "PATH").value_or(std::string());
            for (const auto it_Folder : std::views::split(l_Path, c_Separator))
            {
                std::string_view l_Folder(std::ranges::begin(it_Folder), std::ranges::end(it_Folder));
                // Windows lets a folder on the PATH be quoted
                if (l_Folder.size() >= 2 && l_Folder.front() == '"' && l_Folder.back() == '"')
                {
                    l_Folder = l_Folder.substr(1, l_Folder.size() - 2);
                }

                if (l_Folder.empty())
                {
                    continue;
                }

                if (std::filesystem::path l_Found = a_Find(FromUtf8(l_Folder) / l_Name); !l_Found.empty())
                {
                    return l_Found;
                }
            }

            return {};
        }
    }
}