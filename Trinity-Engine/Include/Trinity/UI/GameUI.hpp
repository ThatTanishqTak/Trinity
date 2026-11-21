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
        // Of those: a scene's UIDocuments' documents, those in World mode, and the passes drawn last frame
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
        // Elements scripts hold handles to that still live, and the UI events queued for scripts
        std::uint32_t Elements = 0;
        std::uint32_t QueuedEvents = 0;
        // The data models scripts made, and the values and lists bound into them
        std::uint32_t DataModels = 0;
        std::uint32_t DataVariables = 0;
        // The sounds elements' attributes asked for that have not yet been let go
        std::uint32_t QueuedSounds = 0;
    };

#if !defined(TR_DISTRIBUTION)
    // The document it inspects, 0 while closed, its path, and its place from 1 among those it cycles
    struct GameUIDebugger
    {
        UIDocumentID Document = 0;
        std::string_view Path;
        std::uint32_t Position = 0;
        std::uint32_t Count = 0;
    };
#endif

    // A sound an element's sound-hover or sound-click attribute named, for its document's scene to play
    struct UIQueuedSound
    {
        std::uint64_t Sequence = 0;
        UUID Entity;
        UUID Resource;
    };

    // RmlUi behind Trinity's interface: no public header includes it. One at a time. Main thread only
    class TRINITY_API GameUI
    {
    public:
        static constexpr UIDocumentID c_NoDocument = 0;

        GameUI(Renderer& renderer, RHI::Device& device);
        ~GameUI();

        GameUI(const GameUI&) = delete;
        GameUI& operator=(const GameUI&) = delete;

        // Shown at sort order 0, one dp to a pixel. c_NoDocument, logged, if the view or file is missing
        [[nodiscard]] UIDocumentID LoadDocument(std::string_view path, ViewID view = Renderer::c_MainView);
        void UnloadDocument(UIDocumentID document);
        [[nodiscard]] bool HasDocument(UIDocumentID document) const;
        void SetVisible(UIDocumentID document, bool visible);
        [[nodiscard]] bool IsVisible(UIDocumentID document) const;
        // Over the view's documents of a lower order, and those of the same order loaded before it
        void SetSortOrder(UIDocumentID document, std::int32_t order);
        [[nodiscard]] std::int32_t GetSortOrder(UIDocumentID document) const;
        void SetPanelSettings(UIDocumentID document, const UIPanelSettings& settings);
        // The view's pixels to one of its dp, as of the last Update. 1 for a document there is not
        [[nodiscard]] float GetScale(UIDocumentID document) const;

        // Once a frame before Update. Loads, syncs and unloads each UIDocument's document, by entity UUID
        void ShowScene(Scene& scene, ViewID view = Renderer::c_MainView, bool interactive = true);
        // As ShowScene, for the scene's world documents alone, as Forge's Scene panel shows them
        void ShowWorld(Scene& scene);
        // Unloads what ShowScene keeps over the view, so the next ShowScene loads them afresh
        void ForgetScene(ViewID view);
        // The screen document ShowScene keeps for the entity over the view, or c_NoDocument
        [[nodiscard]] UIDocumentID FindSceneDocument(UUID entity, ViewID view = Renderer::c_MainView) const;
        // The world document ShowScene or ShowWorld keeps for the entity in the scene, or c_NoDocument
        [[nodiscard]] UIDocumentID FindWorldDocument(const Scene& scene, UUID entity) const;
        // A world document's texture size in pixels at the last Update. 0 for any other, or one of no size
        [[nodiscard]] glm::uvec2 GetTextureSize(UIDocumentID document) const;
        // The id of the element of the document that has the focus, and empty when none has, or it has none
        [[nodiscard]] std::string GetFocus(UIDocumentID document) const;
        // Called every frame it points. A turn without one gives the world documents back to the mouse
        void SetWorldPointer(const glm::vec3& origin, const glm::vec3& direction, bool pressed);

        // For a scene not shown yet, loaded at once, undrawn. c_NoDocument, logged once, if none loads
        UIDocumentID GetSceneDocument(Scene& scene, UUID entity);
        // While the scene's scripts run, its documents stay, undrawn, even when no view shows the scene
        void RetainScene(const Scene& scene);
        void ReleaseScene(const Scene& scene);
        // Events after the sequence number, in order, copied over the list's first entries. Their number
        std::size_t GetEvents(const Scene& scene, std::uint64_t after, std::vector<UIQueuedEvent>& events) const;
        // The last sequence number given to an event of any scene, 0 before the first
        [[nodiscard]] std::uint64_t GetEventSequence() const;
        // As GetEvents, for sounds. A disabled element asks for none. An unknown path is logged once
        std::size_t GetSounds(const Scene& scene, std::uint64_t after, std::vector<UIQueuedSound>& sounds) const;
        [[nodiscard]] std::uint64_t GetSoundSequence() const;

        // By project path, as Forge's watch reports. A changed document reloads every document whole
        void ReloadFiles(std::span<const std::string> paths);
        // Every document read whole again, as when it is not known what changed
        void ReloadAll();

        // A fallback face gives the glyphs others lack. Faces stay until the UI ends. False if unreadable
        bool LoadFontFace(std::string_view path, bool fallback = false);

        // Once a frame on the main thread, after the layers. A document whose view is gone is unloaded
        void Update(Timestep timestep);

        [[nodiscard]] GameUIStatistics GetStatistics() const;

#if !defined(TR_DISTRIBUTION)
        // Moves RmlUi's debugger to the view's next document, closing after the last. Not in Distribution
        UIDocumentID CycleDebugger(ViewID view = Renderer::c_MainView);
        [[nodiscard]] GameUIDebugger GetDebugger() const;
#endif

    private:
        struct State;
        friend struct GameUIAccess;

        void Reload(bool documents);
        // Loads the scene document at that index, or again if it names another. Whether it has a document
        bool Refresh(std::size_t entry, const UIDocumentComponent& component);
        // The scene's UIDocuments: its world documents, and its screen documents over the view if given
        void ShowDocuments(Scene& scene, std::optional<ViewID> view);
        // The interactive view's turn, as InputActions gives it each frame, with Input reading as the UI's
        void ProcessInput(Timestep timestep);

        Renderer& m_Renderer;
        Scope<State> m_State;
    };
}