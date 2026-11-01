#include "Trinity/UI/GameUISystemInterface.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Platform.hpp"
#include "Trinity/UI/UIAssets.hpp"

namespace Trinity
{
    double GameUISystemInterface::GetElapsedTime()
    {
        return m_Time;
    }

    // As a link from the document is resolved wherever the UI reads one, so the build walk finds what RmlUi will read
    void GameUISystemInterface::JoinPath(Rml::String& translatedPath, const Rml::String& documentPath, const Rml::String& path)
    {
        translatedPath = UIAssets::ResolveLink(documentPath, path);
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