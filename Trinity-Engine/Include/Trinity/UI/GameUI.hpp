#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Renderer/Renderer.hpp"
#include "Trinity/RHI/Device.hpp"
#include "Trinity/UI/UIElement.hpp"
#include "Trinity/UI/UIPanelSettings.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    class Scene;
    struct UIDocumentComponent;

    // A document loaded into the UI over a view
    using UIDocumentID = std::uint32_t;

    struct GameUIStatistics
    {
        std::uint32_t Contexts = 0;
        std::uint32_t Documents = 0;
        // Of those, the documents a scene's UIDocuments keep, and those in World mode, of which the last frame drew WorldPasses into their textures
        std::uint32_t SceneDocuments = 0;
        std::uint32_t WorldDocuments = 0;
        std::uint32_t WorldPasses = 0;
        std::uint32_t Geometries = 0;
        std::uint32_t Textures = 0;
        std::uint64_t GeometryBytes = 0;
        std::uint64_t TextureBytes = 0;
        // What the documents recorded to draw in the last update
        std::uint32_t Commands = 0;
        // Documents reloaded whole, and those whose stylesheets alone were, since the UI started
        std::uint32_t DocumentReloads = 0;
        std::uint32_t StyleSheetReloads = 0;
        // Elements scripts hold handles to that still live, and the UI events queued for scripts that have not yet been let go
        std::uint32_t Elements = 0;
        std::uint32_t QueuedEvents = 0;
        // The data models scripts made, and the values and lists bound into them
        std::uint32_t DataModels = 0;
        std::uint32_t DataVariables = 0;
    };

    // A game's UI, drawn by RmlUi: documents of RML styled by RCSS, with a base stylesheet under each that lets buttons, inputs, selects and textareas take the focus, read through the virtual file system by their paths in the project, which a cooked game's asset table finds in its pack. Each document has a context of its own over the view it shows on, which takes the view's size, scales by the document's Panel Settings through RmlUi's dp unit, and draws, in the documents' sort order, over the view's tone mapped image as its overlay, so colours are what the stylesheet says. Fonts load from a stylesheet's @font-face, or from here, each face once however often its stylesheet is read, and images are the project's textures, by their paths. The interactive view's documents take the scene's input before its actions: the pointer goes to the topmost document under it, but for a document's body, which lets it through to the game, a text field with the focus takes the keyboard, and the UI map's Navigate moves the focus in document order, Submit presses what has it and Cancel takes it away, with the keys, buttons and sticks they read kept from the game while an element has the focus. A world document draws into the scene instead: laid out in its own size, at its Panel Settings' Scale, it is drawn into a texture of that size from the frame graph's pool before any view draws, which every view of its scene shows on its quad. Where no screen document takes the pointer, the cursor's ray through the interactive view, or a pointer code gives, reaches the world documents of the scene running there, unless a nearer collider or surface hides them, and they take the focus, the keyboard and Navigate as screen documents do. RmlUi runs on the main thread alone, on the frames' clock, and what it and its FreeType allocate is counted under GameUI, but for what RmlUi keeps in its statics past its shutdown. Scripts reach a scene's documents through UIDocument and UIElement, and the events of their elements are queued for the scene's ScriptRuntime to give its handlers. Data models are the scripts', by the scene, the entity and the name, and every document the entity shows is linked to them, so the values they bind show in {{ }} expressions and data-for lists. No public header includes RmlUi, and only one GameUI lives at a time, as RmlUi is the process's
    class TRINITY_API GameUI
    {
    public:
        static constexpr UIDocumentID c_NoDocument = 0;

        GameUI(Renderer& renderer, RHI::Device& device);
        ~GameUI();

        GameUI(const GameUI&) = delete;
        GameUI& operator=(const GameUI&) = delete;

        // A document read from the virtual file system into a context of its own over the view, and shown, at sort order 0 and one dp to a pixel. c_NoDocument when the view does not exist or the document cannot be read, which is logged
        [[nodiscard]] UIDocumentID LoadDocument(std::string_view path, ViewID view = Renderer::c_MainView);
        void UnloadDocument(UIDocumentID document);
        [[nodiscard]] bool HasDocument(UIDocumentID document) const;
        void SetVisible(UIDocumentID document, bool visible);
        [[nodiscard]] bool IsVisible(UIDocumentID document) const;
        // Drawn over the view's documents of a lower order, and over those of the same order loaded before it
        void SetSortOrder(UIDocumentID document, std::int32_t order);
        [[nodiscard]] std::int32_t GetSortOrder(UIDocumentID document) const;
        void SetPanelSettings(UIDocumentID document, const UIPanelSettings& settings);
        // The view's pixels to one of its dp, as its Panel Settings gave it at the last Update, and 1 for a document there is not
        [[nodiscard]] float GetScale(UIDocumentID document) const;

        // Once a frame, before Update, while the scene is shown over the view: each UIDocument's document is loaded as it appears or names another, kept in step with its sort order, visibility and Panel Settings, and unloaded as the component goes, or at the Update of a frame the scene was not shown in, unless the scene's scripts run, when it stays for them over no view. A document is kept by its entity's UUID, so one on an entity that moves into the next scene, as KeepAcrossScenes does, goes on as it was. Those of the same sort order are drawn as the hierarchy has them. One that names no UIDocument asset shows nothing, which is logged once. Interactive, the view's documents take the scene's input before its actions read it, as the scene's frame begins, and the view is the only one that does. Not, as in Forge's Edit mode, they take none. A UIDocument in World mode is kept as ShowWorld keeps it
        void ShowScene(Scene& scene, ViewID view = Renderer::c_MainView, bool interactive = true);
        // As ShowScene does for the scene's world documents alone, which are the scene's whichever views show it, for a view that shows a scene without its screen documents, as Forge's Scene panel does. A scene shown both ways in a frame keeps one document for each UIDocument
        void ShowWorld(Scene& scene);
        // The documents ShowScene keeps over the view are unloaded, with the world documents of the scene it last showed there, so the next ShowScene loads them afresh, as Forge's Game panel does as its scene starts and stops playing
        void ForgetScene(ViewID view);
        // The screen document ShowScene keeps for the entity over the view, or c_NoDocument
        [[nodiscard]] UIDocumentID FindSceneDocument(UUID entity, ViewID view = Renderer::c_MainView) const;
        // The world document ShowScene or ShowWorld keeps for the entity in the scene, or c_NoDocument
        [[nodiscard]] UIDocumentID FindWorldDocument(const Scene& scene, UUID entity) const;
        // A world document's texture's width and height in pixels at the last Update, 0 for one that has no size or is not a world document
        [[nodiscard]] glm::uvec2 GetTextureSize(UIDocumentID document) const;
        // The id of the element of the document that has the focus, and empty when none has, or it has none
        [[nodiscard]] std::string GetFocus(UIDocumentID document) const;
        // A pointer code drives in place of the mouse for the interactive view's world documents, as a crosshair or a VR laser does, taken at the scene's next input turn: a ray from the origin along the direction, which need not be of unit length, meeting nothing behind the origin, and whether its button, the primary one, is held. Called every frame it points. A turn without one gives the world documents back to the mouse, and ends a press it held. Colliders hide documents from it as from the mouse, and the depth read back under the cursor does not, as the ray need not pass through it
        void SetWorldPointer(const glm::vec3& origin, const glm::vec3& direction, bool pressed);

        // The document of the entity's UIDocument in the scene, as its scripts reach it: the one ShowScene or ShowWorld keeps, or, for a scene not shown yet, one loaded at once, undrawn and taking no input, which the view showing the scene takes over. c_NoDocument for an entity without a UIDocument, or naming no document that loads, which is logged once
        UIDocumentID GetSceneDocument(Scene& scene, UUID entity);
        // While the scene's scripts run, as its ScriptRuntime has it: its documents stay for them, undrawn, while no view shows the scene, and those loaded for them go once it is released as often as it was retained
        void RetainScene(const Scene& scene);
        void ReleaseScene(const Scene& scene);
        // The UI events of the scene's documents queued after the sequence number given, in the order they happened, each kept until the Update of the frame after the one it happened in, copied into the first entries of the list, which keeps any past them for the next time. The number copied
        std::size_t GetEvents(const Scene& scene, std::uint64_t after, std::vector<UIQueuedEvent>& events) const;
        // The last sequence number given to an event of any scene, 0 before the first
        [[nodiscard]] std::uint64_t GetEventSequence() const;

        // Files changed on disk, by their paths in the project, as Forge's watch of the assets tells them. A changed stylesheet has every document read its stylesheets again, keeping its state, and a changed document has every document read whole again, as any may be a template another uses. Neither changes a font face already loaded. Paths of other files are passed over
        void ReloadFiles(std::span<const std::string> paths);
        // Every document read whole again, as when it is not known what changed
        void ReloadAll();

        // A font face from the virtual file system for every document, named by the family and style it holds. A fallback face gives the glyphs other faces lack. Faces stay until the UI ends, as RmlUi keeps them. False when it cannot be read, which RmlUi logs with the reason
        bool LoadFontFace(std::string_view path, bool fallback = false);

        // Once a frame, on the main thread, after the layers have updated: the clock advances, each document's context takes its view's size and scale, or a world document's its own, and lays out what changed, and what it draws is recorded for the view's overlay pass, or the world document's own pass, in the frame rendered next. A document whose view was destroyed is unloaded
        void Update(Timestep timestep);

        [[nodiscard]] GameUIStatistics GetStatistics() const;

    private:
        struct State;
        friend struct GameUIAccess;

        void Reload(bool documents);
        // The scene document at that index loaded, or loaded again, when it names another document or its document went, and given its UIDocument's sort order, visibility and settings. Whether it has a document
        bool Refresh(std::size_t entry, const UIDocumentComponent& component);
        // The scene's UIDocuments: its world documents, and its screen documents over the view when there is one
        void ShowDocuments(Scene& scene, std::optional<ViewID> view);
        // The interactive view's turn, as InputActions gives it to the UI each frame of the running scene, with Input reading as the UI's
        void ProcessInput(Timestep timestep);

        Renderer& m_Renderer;
        Scope<State> m_State;
    };
}