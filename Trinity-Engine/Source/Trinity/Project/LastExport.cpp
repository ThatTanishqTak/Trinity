#include "Trinity/Project/LastExport.hpp"

#include "Trinity/FileSystem/FileSystemUtilities.hpp"
#include "Trinity/Project/SettingsYaml.hpp"

#include <yaml-cpp/yaml.h>

#include <charconv>
#include <chrono>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace Trinity
{
    namespace
    {
        using FileSystemUtilities::FromUtf8;
        using FileSystemUtilities::ToUtf8;
        using SettingsYaml::ReadString;
    }

    std::filesystem::path GetLastExportPath(const std::filesystem::path& projectDirectory)
    {
        return projectDirectory / "Cache" / FromUtf8(LastExport::c_FileName);
    }

    Expected<LastExport, std::string> ReadLastExport(const std::filesystem::path& projectDirectory)
    {
        const std::filesystem::path l_File = GetLastExportPath(projectDirectory);
        std::error_code l_Error;
        if (!std::filesystem::is_regular_file(l_File, l_Error))
        {
            return Unexpected{ std::string("no export of the project has finished yet") };
        }

        std::ifstream l_Stream(l_File, std::ios::binary);
        const std::string l_Text((std::istreambuf_iterator<char>(l_Stream)), std::istreambuf_iterator<char>());
        if (l_Stream.bad())
        {
            return Unexpected{ std::format("{} could not be read", ToUtf8(l_File)) };
        }

        YAML::Node l_Root;
        try
        {
            l_Root = YAML::Load(l_Text);
        }
        catch (const YAML::Exception& exception)
        {
            return Unexpected{ std::format("{} is not valid YAML: {}", ToUtf8(l_File), exception.what()) };
        }

        if (!l_Root.IsMap())
        {
            return Unexpected{ std::format("{} holds no export", ToUtf8(l_File)) };
        }

        std::uint32_t l_Format = 0;
        const std::string l_FormatText = ReadString(l_Root, "Format", "");
        const std::from_chars_result l_Parsed = std::from_chars(l_FormatText.data(), l_FormatText.data() + l_FormatText.size(), l_Format);
        if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_FormatText.data() + l_FormatText.size() || l_Format == 0)
        {
            return Unexpected{ std::format("{} has no valid Format version", ToUtf8(l_File)) };
        }

        if (l_Format > LastExport::c_FormatVersion)
        {
            return Unexpected{ std::format("{} was written in format {} by a newer Forge, and this reads format {} and older", ToUtf8(l_File), l_Format, LastExport::c_FormatVersion) };
        }

        LastExport l_Record;
        const std::string l_Executable = ReadString(l_Root, "Executable", "");
        if (l_Executable.empty())
        {
            return Unexpected{ std::format("{} names no executable", ToUtf8(l_File)) };
        }

        l_Record.Executable = FromUtf8(l_Executable);
        l_Record.Executable.make_preferred();
        l_Record.Product = ReadString(l_Root, "Product", ToUtf8(l_Record.Executable.stem()));
        l_Record.Development = ReadString(l_Root, "Development", "false") == "true";
        l_Record.FinishedAt = ReadString(l_Root, "FinishedAt", "");

        return l_Record;
    }

    Expected<void, std::string> WriteLastExport(const std::filesystem::path& projectDirectory, LastExport record)
    {
        record.FinishedAt = std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));

        YAML::Emitter l_Emitter;
        l_Emitter << YAML::BeginMap;
        l_Emitter << YAML::Key << "Format" << YAML::Value << LastExport::c_FormatVersion;
        l_Emitter << YAML::Key << "Executable" << YAML::Value << YAML::DoubleQuoted << ToUtf8(record.Executable.generic_u8string());
        l_Emitter << YAML::Key << "Product" << YAML::Value << YAML::DoubleQuoted << record.Product;
        l_Emitter << YAML::Key << "Development" << YAML::Value << record.Development;
        l_Emitter << YAML::Key << "FinishedAt" << YAML::Value << YAML::DoubleQuoted << record.FinishedAt;
        l_Emitter << YAML::EndMap;

        const std::filesystem::path l_File = GetLastExportPath(projectDirectory);
        std::error_code l_Error;
        std::filesystem::create_directories(l_File.parent_path(), l_Error);

        std::filesystem::path l_Written = l_File;
        l_Written += ".tmp";
        {
            std::ofstream l_Stream(l_Written, std::ios::binary | std::ios::trunc);
            l_Stream << l_Emitter.c_str() << '\n';
            l_Stream.close();
            if (!l_Stream)
            {
                return Unexpected{ std::format("{} could not be written", ToUtf8(l_Written)) };
            }
        }

        std::filesystem::rename(l_Written, l_File, l_Error);
        if (l_Error)
        {
            std::filesystem::remove(l_Written, l_Error);

            return Unexpected{ std::format("{} could not be replaced: {}", ToUtf8(l_File), l_Error.message()) };
        }

        return {};
    }
}