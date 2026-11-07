#pragma once

#include <Trinity.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>

// Asks for a file or folder as Forge does: with the platform's own dialog, which opens between frames so it never blocks with one open, or with a text field in a popup where the platform has none
class HubPathPrompt
{
public:
    enum class Kind : std::uint8_t
    {
        OpenFile,
        Folder
    };

    struct Request
    {
        Kind Type = Kind::OpenFile;
        std::string Title;
        // Kept as given, so they must outlive the request, as a constant's do
        std::span<const Trinity::FileDialogFilter> Filters;
        std::filesystem::path Folder;
        std::function<void(const std::filesystem::path&)> OnChosen;
    };

    // While drawing. What was chosen is handed on at the next Update
    void Ask(Request request);
    // Between frames, once a frame
    void Update();
    // The text field's popup, where there is no dialog
    void Draw();

private:
    std::optional<Request> m_Dialog;
    std::optional<Request> m_Prompt;
    std::string m_Text;
    bool m_OpenPopup = false;
    std::function<void()> m_Chosen;
};