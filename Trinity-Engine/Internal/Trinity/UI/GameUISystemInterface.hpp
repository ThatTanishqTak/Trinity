#pragma once

#include <RmlUi/Core/SystemInterface.h>

namespace Trinity
{
    // RmlUi's clock, log, paths and clipboard. Its time is the frames' own, advanced as GameUI updates
    class GameUISystemInterface final : public Rml::SystemInterface
    {
    public:
        void Advance(double seconds) { m_Time += seconds; }

        double GetElapsedTime() override;
        void JoinPath(Rml::String& translatedPath, const Rml::String& documentPath, const Rml::String& path) override;
        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
        void SetClipboardText(const Rml::String& text) override;
        void GetClipboardText(Rml::String& text) override;

    private:
        double m_Time = 0.0;
    };
}