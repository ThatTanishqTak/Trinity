#pragma once

#include "EditorSession.hpp"

#include <Trinity.hpp>
#include <Trinity/Tools/Thumbnails.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>

// With no project open: Forge's name and version, New Project and Open Project, the recent projects, newest first, each beside its picture, and the help links
class StartScreen
{
public:
    explicit StartScreen(EditorSession& session) : m_Session(session) {}

    // Once a frame, between frames, which is where a picture's texture is let go of
    void Update();
    void Draw();

private:
    void DrawHeader() const;
    // Its picture, its name over its folder, and when it was opened or that it is missing
    void DrawProject(std::size_t index, std::optional<std::filesystem::path>& removed);
    void DrawLinks() const;
    // By its ID in the shortcut registry, so a link here and its item in the Help menu do the same
    void Run(std::string_view command) const;

    EditorSession& m_Session;
    // Each recent project's Cache/Thumbnail.png, as Hub shows it, kept only while the screen is shown
    Trinity::Tools::Thumbnails m_Thumbnails;
    bool m_Shown = false;
};