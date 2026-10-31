#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Renderer/Renderer.hpp"

#include <cstdint>
#include <string_view>

namespace Trinity
{
    // A document loaded into a view's UI
    using UIDocumentID = std::uint32_t;

    struct GameUIStatistics
    {
        std::uint32_t Contexts = 0;
        std::uint32_t Documents = 0;
        std::uint32_t Geometries = 0;
        std::uint32_t Textures = 0;
        std::uint64_t GeometryBytes = 0;
        std::uint64_t TextureBytes = 0;
        // What the contexts recorded to draw in the last update
        std::uint32_t Commands = 0;
    };

    // A game's UI, drawn by RmlUi: documents of RML styled by RCSS, read through the virtual file system, each in the context of the view it shows over, which takes the view's size. Fonts load from a stylesheet's @font-face, or from here. RmlUi runs on the main thread alone, on the frames' clock, and what it and its FreeType allocate is counted under GameUI, but for what RmlUi keeps in its statics past its shutdown. No public header includes RmlUi, and only one GameUI lives at a time, as RmlUi is the process's
    class TRINITY_API GameUI
    {
    public:
        static constexpr UIDocumentID c_NoDocument = 0;

        explicit GameUI(Renderer& renderer);
        ~GameUI();

        GameUI(const GameUI&) = delete;
        GameUI& operator=(const GameUI&) = delete;

        // A document read from the virtual file system into the view's context, made with the first document it shows, and shown. c_NoDocument when the view does not exist or the document cannot be read, which is logged
        [[nodiscard]] UIDocumentID LoadDocument(std::string_view path, ViewID view = Renderer::c_MainView);
        void UnloadDocument(UIDocumentID document);
        [[nodiscard]] bool HasDocument(UIDocumentID document) const;
        void SetVisible(UIDocumentID document, bool visible);
        [[nodiscard]] bool IsVisible(UIDocumentID document) const;

        // A font face from the virtual file system for every document, named by the family and style it holds. A fallback face gives the glyphs other faces lack. Faces stay until the UI ends, as RmlUi keeps them. False when it cannot be read, which RmlUi logs with the reason
        bool LoadFontFace(std::string_view path, bool fallback = false);

        // Once a frame, on the main thread, after the layers have updated: the clock advances, each context takes its view's size and lays out what changed, and what it draws is recorded for the view's UI pass. A context whose view was destroyed goes with its documents
        void Update(Timestep timestep);

        [[nodiscard]] GameUIStatistics GetStatistics() const;

    private:
        struct State;

        Renderer& m_Renderer;
        Scope<State> m_State;
    };
}