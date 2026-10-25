#pragma once

#include <Trinity.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

class EditorSession;

// The pictures the Content Browser shows of prefabs and models, as Unity's Project window previews them. Each is drawn from an instance of its own into a square view of the renderer's: from in front, a little above and to one side, around its meshes and sprites, or head-on in 2D when it has only sprites, lit by the default sun unless it brings lights of its own. It is drawn again once its template is made again, as a save, an Apply, a change outside Forge or one to a prefab nested in it makes it, and once a mesh, material or texture it draws with loads, reloads or is edited. A few are drawn each frame, and each is let go of once the browser stops asking for it, scrolled away or in another folder
class PrefabThumbnails
{
public:
    // Drawn, or made again, in one frame at most, so a folder of them opening costs a few frames rather than one long one
    static constexpr std::size_t c_DrawsPerFrame = 4;
    // The sizes pictures are drawn at, the smallest at least as big as the browser shows one, so dragging its slider draws them again only as it passes one
    static constexpr std::array<std::uint32_t, 4> c_Sizes{ 64, 128, 256, 512 };

    explicit PrefabThumbnails(EditorSession& session) : m_Session(session) {}
    // Layers are destroyed before the Renderer, so the views can still go
    ~PrefabThumbnails();

    PrefabThumbnails(const PrefabThumbnails&) = delete;
    PrefabThumbnails& operator=(const PrefabThumbnails&) = delete;

    // While the UI is drawn: the bindless index of the prefab's picture, which is asked for at least so many pixels across, until it is drawn at that size showing the one it has. RHI::c_NoBindlessIndex until it is first drawn, and for one with nothing to draw, which keeps its icon
    [[nodiscard]] std::uint32_t Find(Trinity::UUID prefab, float pixels);
    // Once the UI is drawn, between frames: lets go of each picture not asked for since the last EndFrame, and readies those to be drawn this frame, c_DrawsPerFrame at most, in the order they were asked for
    void EndFrame();
    // In OnPrepareRender: each instance readied submitted to its own view
    void Prepare();
    // Every picture let go of, as the project closes
    void Clear();

private:
    // What an asset the picture shows was as it was drawn: the asset loaded, how far it has loaded, and the index it is drawn through, its GPU copy's for a mesh or a texture, which comes after it loads, and its record's for a material
    struct Stamp
    {
        const Trinity::Asset* Loaded = nullptr;
        Trinity::AssetState State = Trinity::AssetState::None;
        std::uint32_t Index = 0;

        [[nodiscard]] bool operator==(const Stamp&) const = default;
    };

    // What the picture shows, so it is drawn again once any of it changes: a stamp of each asset, the textures its materials name among them, and each material's values, which Properties edits in place
    struct Signature
    {
        std::vector<Stamp> Stamps;
        std::vector<Trinity::MaterialData> Materials;

        [[nodiscard]] bool operator==(const Signature&) const = default;
    };

    // A prefab's instance, made from the version of its template it was made from, with the meshes, materials, sprite textures and environments it draws with, kept loaded while it is shown. Its view, of the size it was made, the signature it was last drawn with, and the camera Prepare submits it through when it is to be drawn this frame. Empty for an instance with nothing to draw
    struct Entry
    {
        Trinity::Scope<Trinity::Scene> Instance;
        std::vector<Trinity::AssetRef<Trinity::MeshAsset>> Meshes;
        std::vector<Trinity::AssetRef<Trinity::MaterialAsset>> Materials;
        std::vector<Trinity::AssetRef<Trinity::TextureAsset>> Textures;
        std::vector<Trinity::AssetRef<Trinity::EnvironmentAsset>> Environments;
        std::uint64_t Version = 0;
        std::optional<Signature> Drawn;
        std::optional<Trinity::RenderView> Camera;
        Trinity::ViewID View = Trinity::Renderer::c_MainView;
        std::uint32_t Size = 0;
        std::uint32_t Wanted = 0;
        std::uint64_t Asked = 0;
        bool Shown = false;
        bool Empty = false;
    };

    [[nodiscard]] bool Update(Trinity::UUID prefab, Entry& entry, Trinity::Renderer& renderer);
    void MakeInstance(Trinity::UUID prefab, Entry& entry, Trinity::Renderer& renderer);
    static void Forget(Entry& entry, Trinity::Renderer& renderer);
    static void DestroyView(Entry& entry, Trinity::Renderer& renderer);
    [[nodiscard]] static bool AreMeshesLoaded(const Entry& entry);
    [[nodiscard]] static Signature Sign(const Entry& entry);

    EditorSession& m_Session;
    std::unordered_map<Trinity::UUID, Entry> m_Entries;
    // The prefabs asked for since the last EndFrame, each once, in the order the browser drew them
    std::vector<Trinity::UUID> m_Asked;
    // Counted from 1, so an entry not yet asked for is never taken for one asked for this frame
    std::uint64_t m_Frame = 1;
};