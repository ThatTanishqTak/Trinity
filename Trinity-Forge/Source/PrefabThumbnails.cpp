#include "PrefabThumbnails.hpp"

#include "EditorCamera.hpp"
#include "EditorCamera3D.hpp"
#include "EditorSession.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace
{
    // A material's texture slots, each of which it draws with once its texture has loaded
    constexpr std::array<Trinity::MaterialTexture Trinity::MaterialData::*, 5> c_MaterialTextures{ &Trinity::MaterialData::BaseColorTexture, &Trinity::MaterialData::MetallicRoughnessTexture, &Trinity::MaterialData::NormalTexture, &Trinity::MaterialData::OcclusionTexture, &Trinity::MaterialData::EmissiveTexture };

    // The asset as the renderer reads it, or null for one not loaded, or of another type
    template<typename T>
    const T* FindLoaded(Trinity::UUID id)
    {
        const Trinity::Asset* l_Asset = Trinity::AssetManager::GetAsset(id);

        return l_Asset != nullptr && l_Asset->GetAssetType() == T::c_AssetType ? static_cast<const T*>(l_Asset) : nullptr;
    }

    template<typename T>
    void HoldOnce(std::vector<Trinity::AssetRef<T>>& held, Trinity::UUID id)
    {
        if (id && std::ranges::find(held, id, &Trinity::AssetRef<T>::GetID) == held.end())
        {
            held.emplace_back(id);
        }
    }

    // The smallest size at least as big as the picture is shown, or the largest
    std::uint32_t GetDrawnSize(float pixels)
    {
        const auto a_Size = std::ranges::find_if(PrefabThumbnails::c_Sizes, [pixels](std::uint32_t size) { return static_cast<float>(size) >= pixels; });

        return a_Size != PrefabThumbnails::c_Sizes.end() ? *a_Size : PrefabThumbnails::c_Sizes.back();
    }

    // As F frames the whole scene in the Scene panel: through the 3D editor camera at its first angle around the meshes' bounds and the sprites' quads, or with sprites alone through the 2D editor camera around their quads, head-on. Nothing for an instance with neither, a mesh that failed to load drawing nothing
    std::optional<Trinity::RenderView> FrameInstance(Trinity::Scene& scene, std::uint32_t size)
    {
        constexpr std::array<glm::vec2, 4> c_SpriteCorners{ glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, -0.5f), glm::vec2(0.5f, 0.5f), glm::vec2(-0.5f, 0.5f) };

        glm::vec3 l_Minimum(std::numeric_limits<float>::max());
        glm::vec3 l_Maximum(std::numeric_limits<float>::lowest());
        const auto a_Add = [&](const glm::vec4& point)
        {
            l_Minimum = glm::min(l_Minimum, glm::vec3(point));
            l_Maximum = glm::max(l_Maximum, glm::vec3(point));
        };

        Trinity::SceneRegistry& l_Registry = scene.GetRegistry();
        bool l_Meshes = false;
        for (const auto [it_Entity, it_Renderer, it_World] : l_Registry.view<Trinity::MeshRendererComponent, Trinity::WorldTransformComponent>().each())
        {
            const Trinity::MeshAsset* l_Mesh = Trinity::AssetManager::GetState(it_Renderer.Mesh) == Trinity::AssetState::Ready ? FindLoaded<Trinity::MeshAsset>(it_Renderer.Mesh) : nullptr;
            if (l_Mesh == nullptr)
            {
                continue;
            }

            const Trinity::MeshBounds& l_Bounds = l_Mesh->GetBounds();
            for (std::uint32_t it_Corner = 0; it_Corner < 8; ++it_Corner)
            {
                a_Add(it_World.Matrix * glm::vec4((it_Corner & 1) != 0 ? l_Bounds.Max.x : l_Bounds.Min.x, (it_Corner & 2) != 0 ? l_Bounds.Max.y : l_Bounds.Min.y, (it_Corner & 4) != 0 ? l_Bounds.Max.z : l_Bounds.Min.z, 1.0f));
            }

            l_Meshes = true;
        }

        bool l_Sprites = false;
        for (const entt::entity it_Entity : l_Registry.view<Trinity::SpriteRendererComponent>())
        {
            const glm::mat4& l_World = l_Registry.get<Trinity::WorldTransformComponent>(it_Entity).Matrix;
            for (const glm::vec2& it_Corner : c_SpriteCorners)
            {
                a_Add(l_World * glm::vec4(it_Corner, 0.0f, 1.0f));
            }

            l_Sprites = true;
        }

        const glm::vec2 l_Size(static_cast<float>(size));
        if (l_Meshes)
        {
            EditorCamera3D l_Camera;
            l_Camera.Frame((l_Minimum + l_Maximum) * 0.5f, glm::length(l_Maximum - l_Minimum) * 0.5f, l_Size);

            return Trinity::RenderView::FromMatrices(l_Camera.GetView(), l_Camera.GetProjection(l_Size), false);
        }

        if (!l_Sprites)
        {
            return std::nullopt;
        }

        // The editor camera's projection holds its place, so its view is the world's own axes, as the Scene panel's is in 2D
        EditorCamera l_Camera;
        l_Camera.Frame(glm::vec2(l_Minimum), glm::vec2(l_Maximum), l_Size);
        Trinity::RenderView l_View = Trinity::RenderView::FromMatrices(glm::mat4(1.0f), l_Camera.GetViewProjection(l_Size), true);
        l_View.Position = glm::vec3(l_Camera.GetPosition(), 0.0f);

        return l_View;
    }
}

PrefabThumbnails::~PrefabThumbnails()
{
    Clear();
}

// A prefab asked for again in the same frame, as a search can list it twice, is drawn as large as the last asked
std::uint32_t PrefabThumbnails::Find(Trinity::UUID prefab, float pixels)
{
    Entry& l_Entry = m_Entries[prefab];
    if (l_Entry.Asked != m_Frame)
    {
        l_Entry.Asked = m_Frame;
        m_Asked.push_back(prefab);
    }

    l_Entry.Wanted = GetDrawnSize(pixels);
    if (!l_Entry.Shown)
    {
        return Trinity::RHI::c_NoBindlessIndex;
    }

    Trinity::Application& l_Application = Trinity::Application::Get();
    const Trinity::RHI::TextureHandle l_Target = l_Application.GetRenderer().GetDisplayTarget(l_Entry.View);

    return l_Target ? l_Application.GetDevice().GetShaderResourceIndex(l_Target) : Trinity::RHI::c_NoBindlessIndex;
}

// A view let go of is released once no frame in flight still shows it, so one the UI drew this frame can go before the frame is rendered
void PrefabThumbnails::EndFrame()
{
    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    std::erase_if(m_Entries, [this, &l_Renderer](auto& entry)
    {
        if (entry.second.Asked == m_Frame)
        {
            return false;
        }

        DestroyView(entry.second, l_Renderer);

        return true;
    });

    if (m_Entries.empty())
    {
        decltype(m_Entries)().swap(m_Entries);
    }

    std::size_t l_Draws = 0;
    for (const Trinity::UUID it_Prefab : m_Asked)
    {
        if (l_Draws == c_DrawsPerFrame)
        {
            break;
        }

        if (Update(it_Prefab, m_Entries.at(it_Prefab), l_Renderer))
        {
            ++l_Draws;
        }
    }

    m_Asked.clear();
    ++m_Frame;
}

void PrefabThumbnails::Prepare()
{
    Trinity::SceneOptions l_Options;
    l_Options.SampleCount = Trinity::Renderer3D::c_SampleCount;
    // No layer draws into these views, so the Renderer draws their sprites
    l_Options.Sprites = true;

    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    for (auto& [it_Prefab, it_Entry] : m_Entries)
    {
        if (it_Entry.Camera)
        {
            l_Renderer.SubmitScene(*it_Entry.Instance, *it_Entry.Camera, l_Options, it_Entry.View);
            it_Entry.Camera.reset();
            it_Entry.Shown = true;
        }
    }
}

void PrefabThumbnails::Clear()
{
    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    for (auto& [it_Prefab, it_Entry] : m_Entries)
    {
        DestroyView(it_Entry, l_Renderer);
    }

    m_Entries.clear();
    m_Asked.clear();
}

// The instance made again once its template is, and drawn again once what it shows has changed or it is shown at another size, as soon as every mesh in it has loaded, since they frame it. Until then the picture it had stays. Whether this took one of the frame's draws
bool PrefabThumbnails::Update(Trinity::UUID prefab, Entry& entry, Trinity::Renderer& renderer)
{
    const Trinity::Scene* l_Template = m_Session.FindPrefabTemplate(prefab);
    const std::uint64_t l_Version = l_Template != nullptr ? m_Session.GetPrefabTemplateVersion(prefab) : 0;
    if (l_Version == 0)
    {
        Forget(entry, renderer);

        return false;
    }

    const bool l_Made = l_Version != entry.Version;
    if (l_Made)
    {
        MakeInstance(prefab, entry, renderer);
        entry.Version = l_Version;
    }

    if (!entry.Instance || !AreMeshesLoaded(entry))
    {
        return l_Made;
    }

    Signature l_Signature = Sign(entry);
    if (entry.Drawn == l_Signature && (entry.Empty || entry.Size == entry.Wanted))
    {
        return l_Made;
    }

    entry.Drawn = std::move(l_Signature);
    std::optional<Trinity::RenderView> l_Camera = FrameInstance(*entry.Instance, entry.Wanted);
    entry.Empty = !l_Camera;
    if (entry.Empty || entry.Size != entry.Wanted)
    {
        DestroyView(entry, renderer);
    }

    if (!entry.Empty && entry.View == Trinity::Renderer::c_MainView)
    {
        entry.View = renderer.CreateView(entry.Wanted, entry.Wanted);
        entry.Size = entry.Wanted;
    }

    entry.Camera = std::move(l_Camera);

    return true;
}

// Through the session's resolver, which finds each prefab nested in it as the scene's instances find it. What it draws with is held before what the last instance drew with is let go of, so an asset both use is not loaded again, and the last picture stays until this one is drawn. One that cannot be made is logged once for each version of its template, and keeps its icon
void PrefabThumbnails::MakeInstance(Trinity::UUID prefab, Entry& entry, Trinity::Renderer& renderer)
{
    Trinity::Scope<Trinity::Scene> l_Instance = Trinity::CreateScope<Trinity::Scene>();
    if (const Trinity::Expected<Trinity::Entity, std::string> l_Root = Trinity::SceneSerializer::InstantiatePrefab(*l_Instance, prefab, Trinity::UUID::Generate(), {}, {}); !l_Root)
    {
        TR_ERROR("Forge: the thumbnail of {} cannot be drawn: {}", m_Session.GetPrefabName(prefab), l_Root.GetError());
        Forget(entry, renderer);

        return;
    }

    l_Instance->UpdateWorldTransforms();
    std::vector<Trinity::AssetRef<Trinity::MeshAsset>> l_Meshes;
    std::vector<Trinity::AssetRef<Trinity::MaterialAsset>> l_Materials;
    std::vector<Trinity::AssetRef<Trinity::TextureAsset>> l_Textures;
    std::vector<Trinity::AssetRef<Trinity::EnvironmentAsset>> l_Environments;
    Trinity::SceneRegistry& l_Registry = l_Instance->GetRegistry();
    for (const auto [it_Entity, it_Renderer] : l_Registry.view<Trinity::MeshRendererComponent>().each())
    {
        HoldOnce(l_Meshes, it_Renderer.Mesh);
        for (const Trinity::UUID it_Material : it_Renderer.Materials)
        {
            HoldOnce(l_Materials, it_Material);
        }
    }

    for (const auto [it_Entity, it_Sprite] : l_Registry.view<Trinity::SpriteRendererComponent>().each())
    {
        HoldOnce(l_Textures, it_Sprite.Texture);
    }

    for (const auto [it_Entity, it_Environment] : l_Registry.view<Trinity::EnvironmentComponent>().each())
    {
        HoldOnce(l_Environments, it_Environment.Environment);
    }

    entry.Instance = std::move(l_Instance);
    entry.Meshes = std::move(l_Meshes);
    entry.Materials = std::move(l_Materials);
    entry.Textures = std::move(l_Textures);
    entry.Environments = std::move(l_Environments);
    entry.Drawn.reset();
    entry.Camera.reset();
}

// Nothing to draw, as for a model not imported yet, which keeps its icon until its template is found
void PrefabThumbnails::Forget(Entry& entry, Trinity::Renderer& renderer)
{
    DestroyView(entry, renderer);
    entry.Instance.reset();
    entry.Meshes.clear();
    entry.Materials.clear();
    entry.Textures.clear();
    entry.Environments.clear();
    entry.Version = 0;
    entry.Drawn.reset();
    entry.Empty = false;
}

void PrefabThumbnails::DestroyView(Entry& entry, Trinity::Renderer& renderer)
{
    if (entry.View != Trinity::Renderer::c_MainView)
    {
        renderer.DestroyView(entry.View);
    }

    entry.View = Trinity::Renderer::c_MainView;
    entry.Size = 0;
    entry.Camera.reset();
    entry.Shown = false;
}

// As the renderer draws a mesh: once it is ready and its GPU copy made. One that failed, or is not a mesh, draws nothing, so it is waited for no longer
bool PrefabThumbnails::AreMeshesLoaded(const Entry& entry)
{
    return std::ranges::none_of(entry.Meshes, [](const Trinity::AssetRef<Trinity::MeshAsset>& mesh)
    {
        const Trinity::AssetState l_State = mesh.GetState();
        const Trinity::MeshAsset* l_Mesh = l_State == Trinity::AssetState::Ready ? FindLoaded<Trinity::MeshAsset>(mesh.GetID()) : nullptr;

        return l_State == Trinity::AssetState::Loading || (l_Mesh != nullptr && (!l_Mesh->GetBuffer() || l_Mesh->GetShaderResourceIndex() == Trinity::RHI::c_NoBindlessIndex));
    });
}

// A material still loading draws as the default, and a texture still loading as its placeholder, so the picture is drawn again as each arrives
PrefabThumbnails::Signature PrefabThumbnails::Sign(const Entry& entry)
{
    Signature l_Signature;
    const auto a_Stamp = [&l_Signature](Trinity::UUID id, std::uint32_t index) { l_Signature.Stamps.push_back({ Trinity::AssetManager::GetAsset(id), Trinity::AssetManager::GetState(id), index }); };
    const auto a_StampTexture = [&a_Stamp](Trinity::UUID id)
    {
        const Trinity::TextureAsset* l_Texture = FindLoaded<Trinity::TextureAsset>(id);
        a_Stamp(id, l_Texture != nullptr ? l_Texture->GetShaderResourceIndex() : 0);
    };

    for (const Trinity::AssetRef<Trinity::MeshAsset>& it_Mesh : entry.Meshes)
    {
        const Trinity::MeshAsset* l_Mesh = FindLoaded<Trinity::MeshAsset>(it_Mesh.GetID());
        a_Stamp(it_Mesh.GetID(), l_Mesh != nullptr ? l_Mesh->GetShaderResourceIndex() : 0);
    }

    for (const Trinity::AssetRef<Trinity::MaterialAsset>& it_Material : entry.Materials)
    {
        const Trinity::MaterialAsset* l_Material = FindLoaded<Trinity::MaterialAsset>(it_Material.GetID());
        a_Stamp(it_Material.GetID(), l_Material != nullptr ? l_Material->GetTableIndex() : 0);
        if (l_Material == nullptr)
        {
            continue;
        }

        l_Signature.Materials.push_back(l_Material->GetData());
        for (Trinity::MaterialTexture Trinity::MaterialData::* it_Slot : c_MaterialTextures)
        {
            if (const Trinity::UUID l_Texture = (l_Material->GetData().*it_Slot).Texture)
            {
                a_StampTexture(l_Texture);
            }
        }
    }

    for (const Trinity::AssetRef<Trinity::TextureAsset>& it_Texture : entry.Textures)
    {
        a_StampTexture(it_Texture.GetID());
    }

    for (const Trinity::AssetRef<Trinity::EnvironmentAsset>& it_Environment : entry.Environments)
    {
        a_Stamp(it_Environment.GetID(), 0);
    }

    return l_Signature;
}