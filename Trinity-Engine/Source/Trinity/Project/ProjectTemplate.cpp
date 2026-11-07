#include "Trinity/Project/ProjectTemplate.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/FileSystem/FileSystemUtilities.hpp"
#include "Trinity/Project/Project.hpp"
#include "Trinity/Project/SettingsYaml.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace Trinity
{
    namespace
    {
        using FileSystemUtilities::FromUtf8;
        using FileSystemUtilities::ToUtf8;
        using SettingsYaml::ReadString;

        std::uint32_t ReadNumber(const YAML::Node& node, const char* key)
        {
            std::uint32_t l_Value = 0;
            const std::string l_Text = ReadString(node, key, "");
            const std::from_chars_result l_Parsed = std::from_chars(l_Text.data(), l_Text.data() + l_Text.size(), l_Value);

            return l_Parsed.ec == std::errc() && l_Parsed.ptr == l_Text.data() + l_Text.size() ? l_Value : 0;
        }
    }

    Expected<ProjectTemplate, std::string> ReadProjectTemplate(const std::filesystem::path& directory)
    {
        const std::filesystem::path l_File = directory / FromUtf8(ProjectTemplate::c_FileName);
        std::ifstream l_Stream(l_File, std::ios::binary);
        if (!l_Stream)
        {
            return Unexpected{ std::format("{} has no {}", ToUtf8(directory), ProjectTemplate::c_FileName) };
        }

        const std::string l_Text((std::istreambuf_iterator<char>(l_Stream)), std::istreambuf_iterator<char>());
        YAML::Node l_Root;
        try
        {
            l_Root = YAML::Load(l_Text);
        }
        catch (const YAML::Exception& exception)
        {
            return Unexpected{ std::format("{} is not valid YAML: {}", ToUtf8(l_File), exception.what()) };
        }

        const std::uint32_t l_Format = l_Root.IsMap() ? ReadNumber(l_Root, "Format") : 0;
        if (l_Format == 0)
        {
            return Unexpected{ std::format("{} has no valid Format version", ToUtf8(l_File)) };
        }

        if (l_Format > ProjectTemplate::c_FormatVersion)
        {
            return Unexpected{ std::format("{} was written in format {} by a newer Trinity, and this one reads format {} and older", ToUtf8(l_File), l_Format, ProjectTemplate::c_FormatVersion) };
        }

        ProjectTemplate l_Template;
        l_Template.Directory = directory;
        l_Template.Name = ToUtf8(directory.filename());
        l_Template.Title = ReadString(l_Root, "Name", l_Template.Name);
        l_Template.Description = ReadString(l_Root, "Description", "");
        const std::string l_Thumbnail = ReadString(l_Root, "Thumbnail", "");
        l_Template.Thumbnail = l_Thumbnail.empty() ? std::filesystem::path() : directory / FromUtf8(l_Thumbnail);
        l_Template.ProjectFormat = ReadNumber(l_Root, "ProjectFormat");
        l_Template.StartScene = ReadString(l_Root, "StartScene", "");
        l_Template.InputActions = ReadString(l_Root, "InputActions", "");
        l_Template.Mixer = ReadString(l_Root, "Mixer", "");
        l_Template.View3D = ReadString(l_Root, "View", "2D") == "3D";
        if (l_Template.ProjectFormat == 0 || l_Template.ProjectFormat > Project::c_FormatVersion)
        {
            return Unexpected{ std::format("{} is written for project format {}, and this Trinity reads format {} and older", ToUtf8(l_File), l_Template.ProjectFormat, Project::c_FormatVersion) };
        }

        std::error_code l_Error;
        if (!std::filesystem::is_directory(l_Template.GetAssetsDirectory(), l_Error))
        {
            return Unexpected{ std::format("{} has no {} folder", ToUtf8(directory), ProjectTemplate::c_AssetsFolder) };
        }

        if (!l_Template.StartScene.empty() && !std::filesystem::is_regular_file(l_Template.GetAssetsDirectory() / FromUtf8(l_Template.StartScene), l_Error))
        {
            return Unexpected{ std::format("{} names the start scene {}, which its {} folder lacks", ToUtf8(l_File), l_Template.StartScene, ProjectTemplate::c_AssetsFolder) };
        }

        return l_Template;
    }

    std::vector<ProjectTemplate> FindProjectTemplates(const std::filesystem::path& root)
    {
        std::vector<ProjectTemplate> l_Templates;
        std::error_code l_Error;
        for (const std::filesystem::directory_entry& it_Entry : std::filesystem::directory_iterator(root / FromUtf8(ProjectTemplate::c_Folder), l_Error))
        {
            if (!it_Entry.is_directory(l_Error))
            {
                continue;
            }

            if (Expected<ProjectTemplate, std::string> l_Template = ReadProjectTemplate(it_Entry.path()))
            {
                l_Templates.push_back(std::move(*l_Template));
            }
            else
            {
                TR_CORE_WARN("Project: the template in {} is left out, as {}", ToUtf8(it_Entry.path()), l_Template.GetError());
            }
        }

        std::ranges::sort(l_Templates, {}, &ProjectTemplate::Name);

        return l_Templates;
    }
}