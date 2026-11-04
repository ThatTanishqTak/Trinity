#include "Trinity/UI/UIAssets.hpp"

#include <algorithm>
#include <array>

namespace Trinity
{
    namespace UIAssets
    {
        std::string ResolveLink(std::string_view from, std::string_view link)
        {
            std::string l_Joined;
            if (link.starts_with('/') || link.starts_with('\\'))
            {
                l_Joined = link;
            }
            else
            {
                const std::size_t l_Folder = from.find_last_of("/\\");
                l_Joined = l_Folder != std::string_view::npos ? std::string(from.substr(0, l_Folder + 1)).append(link) : std::string("/").append(link);
            }

            std::string l_Resolved;
            std::size_t l_Position = 0;
            while (l_Position < l_Joined.size())
            {
                const std::size_t l_End = std::min(l_Joined.find_first_of("/\\", l_Position), l_Joined.size());
                const std::string_view l_Component = std::string_view(l_Joined).substr(l_Position, l_End - l_Position);
                l_Position = l_End + 1;
                if (l_Component.empty() || l_Component == ".")
                {
                    continue;
                }

                if (l_Component == "..")
                {
                    l_Resolved.resize(std::min(l_Resolved.size(), l_Resolved.rfind('/')));

                    continue;
                }

                l_Resolved += '/';
                l_Resolved += l_Component;
            }

            if (l_Resolved.empty())
            {
                l_Resolved = "/";
            }

            return l_Resolved;
        }

        std::vector<std::string> FindLinks(std::string_view text, std::string_view path)
        {
            constexpr std::array<std::string_view, 15> c_Extensions{ ".rml", ".rcss", ".ttf", ".otf", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd", ".trsound", ".wav", ".flac", ".mp3", ".ogg" };
            constexpr std::string_view c_Separators = " \t\r\n\"'(),;:{}<>=";

            const auto a_IsLink = [&c_Extensions](std::string_view word)
            {
                return std::ranges::any_of(c_Extensions, [word](std::string_view extension)
                {
                    return word.size() > extension.size() && std::ranges::equal(word.substr(word.size() - extension.size()), extension, [](char left, char right) { return (left >= 'A' && left <= 'Z' ? left - 'A' + 'a' : left) == right; });
                });
            };

            std::vector<std::string> l_Links;
            std::size_t l_Position = 0;
            while (l_Position < text.size())
            {
                const std::size_t l_Start = text.find_first_not_of(c_Separators, l_Position);
                if (l_Start == std::string_view::npos)
                {
                    break;
                }

                const std::size_t l_End = std::min(text.find_first_of(c_Separators, l_Start), text.size());
                const std::string_view l_Word = text.substr(l_Start, l_End - l_Start);
                l_Position = l_End;

                // A scheme's colon is a separator, so the word after it starts with //, as no path in the project does
                if (!a_IsLink(l_Word) || l_Word.starts_with("//"))
                {
                    continue;
                }

                std::string l_Link = ResolveLink(path, l_Word);
                if (std::ranges::find(l_Links, l_Link) == l_Links.end())
                {
                    l_Links.push_back(std::move(l_Link));
                }
            }

            return l_Links;
        }

        // Each src declaration inside each @font-face block, a list of paths, each quoted or bare. A path from the root, or with a scheme, stays as it is
        std::string ResolveFontFaceSources(std::string_view text, std::string_view path)
        {
            constexpr std::string_view c_Rule = "@font-face";
            constexpr std::string_view c_Space = " \t\r\n";

            std::string l_Result;
            std::size_t l_Copied = 0;
            std::size_t l_Rule = 0;
            while ((l_Rule = text.find(c_Rule, l_Rule)) != std::string_view::npos)
            {
                const std::size_t l_Open = text.find('{', l_Rule);
                const std::size_t l_Close = l_Open != std::string_view::npos ? text.find('}', l_Open) : std::string_view::npos;
                if (l_Close == std::string_view::npos)
                {
                    break;
                }

                std::size_t l_Source = l_Open;
                while ((l_Source = text.find("src", l_Source)) < l_Close)
                {
                    const char l_Before = text[l_Source - 1];
                    const std::size_t l_Colon = text.find_first_not_of(c_Space, l_Source + 3);
                    if ((l_Before != '{' && l_Before != ';' && c_Space.find(l_Before) == std::string_view::npos) || l_Colon >= l_Close || text[l_Colon] != ':')
                    {
                        l_Source += 3;

                        continue;
                    }

                    const std::size_t l_End = std::min(text.find(';', l_Colon), l_Close);
                    std::string l_Value;
                    std::size_t l_Item = l_Colon + 1;
                    while (l_Item < l_End)
                    {
                        const std::size_t l_Comma = std::min(text.find(',', l_Item), l_End);
                        std::string_view l_Path = text.substr(l_Item, l_Comma - l_Item);
                        l_Path.remove_prefix(std::min(l_Path.find_first_not_of(c_Space), l_Path.size()));
                        l_Path.remove_suffix(l_Path.size() - std::min(l_Path.find_last_not_of(c_Space) + 1, l_Path.size()));
                        if (l_Path.size() >= 2 && (l_Path.front() == '"' || l_Path.front() == '\'') && l_Path.back() == l_Path.front())
                        {
                            l_Path = l_Path.substr(1, l_Path.size() - 2);
                        }

                        if (!l_Path.empty())
                        {
                            const bool l_Kept = l_Path.starts_with('/') || l_Path.find("://") != std::string_view::npos;
                            l_Value += l_Value.empty() ? " \"" : ", \"";
                            l_Value += l_Kept ? std::string(l_Path) : ResolveLink(path, l_Path);
                            l_Value += '"';
                        }

                        l_Item = l_Comma + 1;
                    }

                    l_Result.append(text.substr(l_Copied, l_Colon + 1 - l_Copied));
                    l_Result += l_Value;
                    l_Copied = l_End;
                    l_Source = l_End;
                }

                l_Rule = l_Close;
            }

            if (l_Copied == 0)
            {
                return std::string(text);
            }

            l_Result.append(text.substr(l_Copied));

            return l_Result;
        }
    }
}