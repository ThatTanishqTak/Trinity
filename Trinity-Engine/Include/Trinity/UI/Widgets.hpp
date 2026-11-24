#pragma once

#include "Trinity/Core/Export.hpp"

#include <imgui.h>

#include <string>

namespace Trinity
{
    // What Forge and Hub draw alike, in the theme's colours. Kept out of Trinity.hpp, as it brings imgui.h with it
    namespace Widgets
    {
        // A text field with a magnifier before its hint and, while it holds text, a cross that empties it. True as InputText is for the flags, and as the cross empties it unless the flags ask for Enter alone
        TRINITY_API bool SearchField(const char* id, const char* hint, std::string& text, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr);

        // A button lit in the accent while what it stands for is on. True as it is pressed, which changes nothing by itself
        TRINITY_API bool ToggleButton(const char* label, bool on, const ImVec2& size = ImVec2(0.0f, 0.0f));

        // The accent's bar down the left of the last item, which marks a selected row while it is hovered too
        TRINITY_API void DrawSelectionEdge();
    }
}