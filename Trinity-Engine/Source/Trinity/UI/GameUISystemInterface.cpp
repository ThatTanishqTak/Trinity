#include "Trinity/UI/GameUISystemInterface.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Platform.hpp"

#include <algorithm>
#include <string_view>

namespace Trinity
{
    double GameUISystemInterface::GetElapsedTime()
    {
        return m_Time;
    }

    // A path from the root of the virtual file system stays as it is, and any other is the document's folder's, so a document finds what lies beside it wherever its folder is mounted. Either way its . and .. are resolved, which the file system does not take, and a .. above the root stays at the root
    void GameUISystemInterface::JoinPath(Rml::String& translatedPath, const Rml::String& documentPath, const Rml::String& path)
    {
        Rml::String l_Joined;
        if (path.starts_with('/') || path.starts_with('\\'))
        {
            l_Joined = path;
        }
        else
        {
            const std::size_t l_Folder = documentPath.find_last_of("/\\");
            l_Joined = l_Folder != Rml::String::npos ? documentPath.substr(0, l_Folder + 1) + path : "/" + path;
        }

        translatedPath.clear();
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
                translatedPath.resize(std::min(translatedPath.size(), translatedPath.rfind('/')));

                continue;
            }

            translatedPath += '/';
            translatedPath += l_Component;
        }

        if (translatedPath.empty())
        {
            translatedPath = "/";
        }
    }

    // An assert inside RmlUi is logged as an error and play goes on, as it does in a player
    bool GameUISystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message)
    {
        switch (type)
        {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
        {
            TR_CORE_ERROR("GameUI: {}", message);

            break;
        }
        case Rml::Log::LT_WARNING:
        {
            TR_CORE_WARN("GameUI: {}", message);

            break;
        }
        case Rml::Log::LT_DEBUG:
        {
            TR_CORE_TRACE("GameUI: {}", message);

            break;
        }
        default:
        {
            TR_CORE_INFO("GameUI: {}", message);

            break;
        }
        }

        return true;
    }

    void GameUISystemInterface::SetClipboardText(const Rml::String& text)
    {
        Platform::SetClipboardText(text);
    }

    void GameUISystemInterface::GetClipboardText(Rml::String& text)
    {
        text = Platform::GetClipboardText();
    }
}