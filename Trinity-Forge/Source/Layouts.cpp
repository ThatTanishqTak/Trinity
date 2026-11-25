#include "Layouts.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>

namespace
{
    constexpr std::string_view c_Folder = "/saves";
    constexpr std::string_view c_Prefix = "Layout-";
    constexpr std::string_view c_Extension = ".ini";
    // A layout's first line, before ImGui's own: this, then the open panels with commas between
    constexpr std::string_view c_PanelsKey = "Panels=";
    constexpr std::size_t c_NameLimit = 32;
    // The two kinds of section ImGui keeps an arrangement in
    constexpr std::string_view c_WindowSection = "[Window]";
    constexpr std::string_view c_DockingSection = "[Docking]";

    std::string GetPath(std::string_view name)
    {
        return std::format("{}/{}{}{}", c_Folder, c_Prefix, name, c_Extension);
    }

    // A section runs from a line that begins with a bracket to the next that does. Its lines are kept or left out together
    std::string FilterSections(std::string_view settings, bool arrangement)
    {
        std::string l_Kept;
        bool l_Keeping = false;
        std::size_t l_Start = 0;
        while (l_Start < settings.size())
        {
            const std::size_t l_End = std::min(settings.find('\n', l_Start), settings.size());
            const std::string_view l_Line = settings.substr(l_Start, l_End - l_Start);
            l_Start = l_End + 1;
            if (l_Line.starts_with('['))
            {
                l_Keeping = (l_Line.starts_with(c_WindowSection) || l_Line.starts_with(c_DockingSection)) == arrangement;
            }

            if (l_Keeping)
            {
                l_Kept += l_Line;
                l_Kept += '\n';
            }
        }

        return l_Kept;
    }
}

namespace Layouts
{
    std::vector<std::string> List()
    {
        std::vector<std::string> l_Names;
        if (const Trinity::Expected<std::vector<Trinity::DirectoryEntry>, Trinity::FileError> l_Entries = Trinity::FileSystem::List(c_Folder))
        {
            for (const Trinity::DirectoryEntry& it_Entry : *l_Entries)
            {
                const std::string_view l_File = it_Entry.Name;
                if (it_Entry.Type == Trinity::FileType::File && l_File.starts_with(c_Prefix) && l_File.ends_with(c_Extension) && l_File.size() > c_Prefix.size() + c_Extension.size())
                {
                    l_Names.emplace_back(l_File.substr(c_Prefix.size(), l_File.size() - c_Prefix.size() - c_Extension.size()));
                }
            }
        }

        std::ranges::sort(l_Names);

        return l_Names;
    }

    // Letters, digits, spaces, hyphens and underscores, without spaces at either end
    std::string MakeName(std::string_view typed)
    {
        std::string l_Name;
        for (const char it_Character : typed)
        {
            if (l_Name.size() < c_NameLimit && (std::isalnum(static_cast<unsigned char>(it_Character)) != 0 || it_Character == ' ' || it_Character == '-' || it_Character == '_'))
            {
                l_Name += it_Character;
            }
        }

        const std::size_t l_First = l_Name.find_first_not_of(' ');
        const std::size_t l_Last = l_Name.find_last_not_of(' ');

        return l_First != std::string::npos ? l_Name.substr(l_First, l_Last - l_First + 1) : std::string();
    }

    bool Save(std::string_view name, std::span<const std::string> openPanels, std::string_view settings)
    {
        std::string l_Text(c_PanelsKey);
        for (const std::string& it_Panel : openPanels)
        {
            l_Text += std::format("{}{}", &it_Panel != openPanels.data() ? "," : "", it_Panel);
        }

        l_Text += std::format("\n{}", KeepArrangement(settings));
        if (const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(GetPath(name), l_Text); !l_Written)
        {
            TR_WARN("Forge: the layout {} could not be saved: {}", name, Trinity::ToString(l_Written.GetError()));

            return false;
        }

        return true;
    }

    std::optional<Layout> Read(std::string_view name)
    {
        const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(GetPath(name));
        if (!l_Text || !l_Text->starts_with(c_PanelsKey))
        {
            return std::nullopt;
        }

        Layout l_Layout;
        const std::size_t l_LineEnd = std::min(l_Text->find('\n'), l_Text->size());
        std::string_view l_Panels = std::string_view(*l_Text).substr(c_PanelsKey.size(), l_LineEnd - c_PanelsKey.size());
        if (l_Panels.ends_with('\r'))
        {
            l_Panels.remove_suffix(1);
        }

        while (!l_Panels.empty())
        {
            const std::size_t l_Comma = l_Panels.find(',');
            l_Layout.OpenPanels.emplace_back(l_Panels.substr(0, l_Comma));
            l_Panels = l_Comma != std::string_view::npos ? l_Panels.substr(l_Comma + 1) : std::string_view();
        }

        l_Layout.Settings = KeepArrangement(std::string_view(*l_Text).substr(std::min(l_LineEnd + 1, l_Text->size())));

        return l_Layout;
    }

    bool Remove(std::string_view name)
    {
        return static_cast<bool>(Trinity::FileSystem::RemoveFile(GetPath(name)));
    }

    std::string KeepArrangement(std::string_view settings)
    {
        return FilterSections(settings, true);
    }

    std::string Merge(std::string_view current, std::string_view layout)
    {
        return FilterSections(current, false) + KeepArrangement(layout);
    }
}