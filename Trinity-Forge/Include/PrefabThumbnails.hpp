#pragma once

#include <Trinity.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

class EditorSession;

// Each is drawn from its own instance into a square view, and again when what it shows changes
class PrefabThumbnails
{
public:
    // The most drawn in one frame, so a folder of them opening costs a few frames, not one long one
    static constexpr std::size_t c_DrawsPerFrame = 4;
    // The smallest at least as big as the browser shows one, so its slider redraws only on passing one
    static constexpr std::array<std::uint32_t, 4> c_Sizes{ 64, 128, 256, 512 };

    explicit PrefabThumbnails(EditorSession& session) : m_Session(session) {}
    // Layers are destroyed before the Renderer, so the views can still go
    ~PrefabThumbnails();

    PrefabThumbnails(const PrefabThumbnails&) = delete;
    PrefabThumbnails& operator=(const PrefabThumbnails&) = delete;

    // The picture's bindless index, asked for at this size. c_NoBindlessIndex until it is first drawn
    [[nodiscard]] std::uint32_t Find(Trinity::UUID prefab, float pixels);
    // Between frames: lets go of pictures not asked for, and readies up to c_DrawsPerFrame to draw
    void EndFrame();
    // In OnPrepareRender: each instance readied submitted to its own view
    void Prepare();
    // Every picture let go of, as the project closes
    void Clear();

private:
    // The asset loaded, how far it has loaded, and the index it is drawn through
    struct Stamp
    {
        const Trinity::Asset* Loaded = nullptr;
        Trinity::AssetState State = Trinity::AssetState::None;
        std::uint32_t Index = 0;

        [[nodiscard]] bool operator==(const Stamp&) const = default;
    };

    // A stamp of each asset it shows and each material's values, so it is drawn again once any changes
    struct Signature
    {
        std::vector<Stamp> Stamps;
        std::vector<Trinity::MaterialData> Materials;

        [[nodiscard]] bool operator==(const Signature&) const = default;
    };

    // Its instance, what it draws with, kept loaded, its view and its camera. Empty if nothing draws
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