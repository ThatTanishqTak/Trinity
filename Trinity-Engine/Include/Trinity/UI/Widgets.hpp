#pragma once

#include "Trinity/Core/Export.hpp"

#include <imgui.h>

#include <cstdint>
#include <optional>
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

        // An icon over the right end of the last item, which has to allow overlap and stays the last item for what follows. True as it is pressed
        TRINITY_API bool OverlayButton(const char* id, const char* icon, const char* tooltip = nullptr);
        // The same in a square of a frame's height, its top left corner where given, over room the layout leaves free
        TRINITY_API bool OverlayButton(const char* id, const char* icon, const char* tooltip, const ImVec2& position);

        // A child that sizes itself to what it holds, drawn over a scene's image on the theme's overlay with rounded corners and a hairline around it. EndOverlay follows whatever this returns
        TRINITY_API bool BeginOverlay(const char* id, ImGuiWindowFlags flags = ImGuiWindowFlags_None);
        TRINITY_API void EndOverlay();
        // The same panel, behind what is drawn over a scene's image by hand
        TRINITY_API void DrawOverlay(ImDrawList& drawList, const ImVec2& minimum, const ImVec2& maximum);

        // After a row's label, which then drags the first of the drag fields below that follows it on the row, from its left end to the field's
        TRINITY_API void ScrubWithLastItem();

        // What a field shows in place of a value that several things edited together hold differently
        inline constexpr const char* c_MixedText = "\u2014";
        // The next drag field shows c_MixedText in place of its value: all of it, or the axes of a DragFloat2, 3 or 4 whose bits are set. Until it is typed into, which shows the value
        TRINITY_API void SetNextMixed(std::uint32_t axes);
        // Taken by the field it was for, and by whatever else must not leave it for a later field. None set gives 0
        [[nodiscard]] TRINITY_API std::uint32_t TakeNextMixed();

        // ImGui's DragScalar, DragFloat and DragInt, dragged from the row's label as from the field itself where ScrubWithLastItem came after the label
        TRINITY_API bool DragScalar(const char* label, ImGuiDataType type, void* value, float speed = 1.0f, const void* minimum = nullptr, const void* maximum = nullptr, const char* format = nullptr, ImGuiSliderFlags flags = ImGuiSliderFlags_None);
        TRINITY_API bool DragFloat(const char* label, float* value, float speed = 1.0f, float minimum = 0.0f, float maximum = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = ImGuiSliderFlags_None);
        TRINITY_API bool DragInt(const char* label, int* value, float speed = 1.0f, int minimum = 0, int maximum = 0, const char* format = "%d", ImGuiSliderFlags flags = ImGuiSliderFlags_None);

        // ImGui's DragFloat2, 3 and 4 with each field behind its axis's letter in that axis's colour, which drags the field as the field itself does. Given a reset value, a click on a letter sets its field to it
        TRINITY_API bool DragFloat2(const char* label, float* values, float speed = 1.0f, float minimum = 0.0f, float maximum = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = ImGuiSliderFlags_None, std::optional<float> reset = std::nullopt);
        TRINITY_API bool DragFloat3(const char* label, float* values, float speed = 1.0f, float minimum = 0.0f, float maximum = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = ImGuiSliderFlags_None, std::optional<float> reset = std::nullopt);
        TRINITY_API bool DragFloat4(const char* label, float* values, float speed = 1.0f, float minimum = 0.0f, float maximum = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = ImGuiSliderFlags_None, std::optional<float> reset = std::nullopt);
    }
}