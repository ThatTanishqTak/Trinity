#include "Trinity/Scene/SceneManager.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Asset/MaterialAsset.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Project/Project.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/Scene/SceneRuntime.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"
#include "Trinity/Scripting/ScriptRuntime.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        SceneManager* s_Active = nullptr;

        using AssetList = std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scene>>;

        void AddAsset(AssetList& assets, UUID id)
        {
            if (id && std::ranges::find(assets, id) == assets.end())
            {
                assets.push_back(id);
            }
        }

        // What the scene's components and its scripts' fields name, which the renderer, physics and scripts would otherwise load as they first reach them. A field naming a scene is left out, as a scene is loaded here and not by the asset manager
        void GatherAssets(Scene& scene, const AssetRegistry* registry, AssetList& assets)
        {
            SceneRegistry& l_Registry = scene.GetRegistry();
            for (const auto [it_Entity, it_Renderer] : l_Registry.view<MeshRendererComponent>().each())
            {
                AddAsset(assets, it_Renderer.Mesh);
                for (const UUID it_Material : it_Renderer.Materials)
                {
                    AddAsset(assets, it_Material);
                }
            }

            for (const auto [it_Entity, it_Sprite] : l_Registry.view<SpriteRendererComponent>().each())
            {
                AddAsset(assets, it_Sprite.Texture);
            }

            for (const auto [it_Entity, it_Environment] : l_Registry.view<EnvironmentComponent>().each())
            {
                AddAsset(assets, it_Environment.Environment);
            }

            for (const auto [it_Entity, it_Hull] : l_Registry.view<ConvexHullColliderComponent>().each())
            {
                AddAsset(assets, it_Hull.Shape);
            }

            for (const auto [it_Entity, it_Mesh] : l_Registry.view<MeshColliderComponent>().each())
            {
                AddAsset(assets, it_Mesh.Shape);
            }

            for (const auto [it_Entity, it_Scripts] : l_Registry.view<ScriptComponent>().each())
            {
                for (const ScriptEntry& it_Script : it_Scripts.Scripts)
                {
                    for (const ScriptEntry::Field& it_Field : it_Script.Fields)
                    {
                        if (it_Field.Value.GetType() != ScriptFieldType::Asset)
                        {
                            continue;
                        }

                        const UUID l_Asset = it_Field.Value.Get<UUID>();
                        const AssetRecord* l_Record = registry != nullptr && l_Asset ? registry->Find(l_Asset) : nullptr;
                        if (l_Record == nullptr || l_Record->Importer != "Scene")
                        {
                            AddAsset(assets, l_Asset);
                        }
                    }
                }
            }
        }

        void Release(AssetList& assets)
        {
            for (const UUID it_Asset : assets)
            {
                AssetManager::Release(it_Asset);
            }

            assets.clear();
        }

        // An asset not known, which never loads, counts as settled
        [[nodiscard]] std::uint32_t CountWaiting(const AssetList& assets)
        {
            return static_cast<std::uint32_t>(std::ranges::count_if(assets, [](UUID id) { return AssetManager::GetState(id) == AssetState::Loading; }));
        }
    }

    // A scene asked for: its file being read, then the scene loaded from it waiting for its assets, then for its materials' textures
    struct SceneManager::State
    {
        struct Loading
        {
            UUID Asset;
            std::string Path;
            FileRequest Request;
            bool Read = false;
            std::optional<std::string> Text;
            std::string Error;
            Scope<Scene> Next;
            AssetList Held;
            bool TexturesHeld = false;
            std::chrono::steady_clock::time_point Started = std::chrono::steady_clock::now();
        };

        PhysicsSettings Settings;
        const AssetRegistry* Registry = nullptr;
        SceneManager* Previous = nullptr;
        Scope<Scene> Current;
        Scope<SceneRuntime> Runtime;
        UUID Asset;
        AssetList Held;
        Scope<Loading> Pending;
        Statistics Counts;

        [[nodiscard]] std::string DescribeCurrent() const
        {
            const AssetRecord* l_Record = Registry != nullptr && Asset ? Registry->Find(Asset) : nullptr;

            return l_Record != nullptr ? l_Record->Path : std::string("the running scene");
        }
    };

    SceneManager::SceneManager(const PhysicsSettings& settings, const AssetRegistry* registry) : m_State(Memory::New<State>(MemoryTag::Scene))
    {
        m_State->Settings = settings;
        m_State->Registry = registry;
        m_State->Previous = s_Active;
        s_Active = this;
    }

    // The running scene's scripts have OnDestroy before the scene goes
    SceneManager::~SceneManager()
    {
        if (m_State->Pending)
        {
            Release(m_State->Pending->Held);
            m_State->Pending.reset();
        }

        m_State->Runtime.reset();
        m_State->Current.reset();
        Release(m_State->Held);
        if (s_Active == this)
        {
            s_Active = m_State->Previous;
        }

        Memory::Delete(m_State);
    }

    SceneManager* SceneManager::GetActive()
    {
        return s_Active;
    }

    void SceneManager::Run(Scope<Scene> scene, UUID asset)
    {
        State& l_State = *m_State;
        l_State.Runtime.reset();
        l_State.Current = std::move(scene);
        Release(l_State.Held);
        l_State.Asset = asset;
        if (l_State.Current)
        {
            l_State.Current->UpdateWorldTransforms();
            l_State.Runtime = CreateScope<SceneRuntime>(*l_State.Current, l_State.Settings);
        }
    }

    bool SceneManager::Load(UUID scene)
    {
        State& l_State = *m_State;
        const AssetRecord* l_Record = l_State.Registry != nullptr && scene ? l_State.Registry->Find(scene) : nullptr;
        if (l_Record == nullptr || l_Record->Importer != "Scene")
        {
            TR_CORE_ERROR("SceneManager: there is no scene {} to load, so {} goes on", scene, l_State.DescribeCurrent());

            return false;
        }

        // One loading already gives way, its read cancelled as it goes and its assets let go
        if (l_State.Pending)
        {
            Release(l_State.Pending->Held);
        }

        // Read where it lies, and parsed back on the main thread with the next frame's posted work
        l_State.Pending = CreateScope<State::Loading>();
        l_State.Pending->Asset = l_Record->ID;
        l_State.Pending->Path = l_Record->Path;
        State::Loading* l_Loading = l_State.Pending.get();
        l_Loading->Request = FileSystem::ReadFileAsync(l_Record->Path, [l_Loading](Expected<FileBuffer, FileError> result)
        {
            l_Loading->Read = true;
            if (!result)
            {
                l_Loading->Error = std::format("it could not be read: {}", ToString(result.GetError()));

                return;
            }

            l_Loading->Text = std::string(reinterpret_cast<const char*>(result->data()), result->size());
        });

        return true;
    }

    // A path in the project, which the registry knows under the assets mount
    bool SceneManager::Load(std::string_view path)
    {
        State& l_State = *m_State;
        const std::string l_Path = path.starts_with('/') ? std::string(path) : std::format("{}/{}", Project::c_AssetsMount, path);
        const AssetRecord* l_Record = l_State.Registry != nullptr ? l_State.Registry->FindByPath(l_Path) : nullptr;
        if (l_Record == nullptr)
        {
            TR_CORE_ERROR("SceneManager: there is no scene {} to load, so {} goes on", path, l_State.DescribeCurrent());

            return false;
        }

        return Load(l_Record->ID);
    }

    void SceneManager::Update(Timestep timestep)
    {
        if (m_State->Runtime)
        {
            m_State->Runtime->Update(timestep);
        }

        Advance();
    }

    void SceneManager::Step()
    {
        if (m_State->Runtime)
        {
            m_State->Runtime->Step();
        }

        Advance();
    }

    // The loading scene is parsed once read, holds its assets, then its materials' textures once its materials are in, and is switched to once nothing it holds still loads
    void SceneManager::Advance()
    {
        State& l_State = *m_State;
        if (!l_State.Pending || !l_State.Pending->Read)
        {
            return;
        }

        State::Loading& l_Loading = *l_State.Pending;
        if (!l_Loading.Next)
        {
            if (!l_Loading.Text)
            {
                TR_CORE_ERROR("SceneManager: {} was not loaded, since {}, so {} goes on", l_Loading.Path, l_Loading.Error, l_State.DescribeCurrent());
                l_State.Pending.reset();

                return;
            }

            TR_PROFILE_SCOPE("SceneManager::Parse");
            Scope<Scene> l_Next = CreateScope<Scene>();
            const Expected<void, std::string> l_Loaded = SceneSerializer::LoadFromText(*l_Next, *l_Loading.Text);
            l_Loading.Text.reset();
            if (!l_Loaded)
            {
                TR_CORE_ERROR("SceneManager: {} was not loaded, since {}, so {} goes on", l_Loading.Path, l_Loaded.GetError(), l_State.DescribeCurrent());
                l_State.Pending.reset();

                return;
            }

            l_Loading.Next = std::move(l_Next);
            GatherAssets(*l_Loading.Next, l_State.Registry, l_Loading.Held);
            for (const UUID it_Asset : l_Loading.Held)
            {
                AssetManager::Acquire(it_Asset);
            }
        }

        if (CountWaiting(l_Loading.Held) != 0)
        {
            return;
        }

        if (!l_Loading.TexturesHeld)
        {
            l_Loading.TexturesHeld = true;
            const std::size_t l_Count = l_Loading.Held.size();
            for (std::size_t it_Asset = 0; it_Asset < l_Count; ++it_Asset)
            {
                const Asset* l_Asset = AssetManager::GetState(l_Loading.Held[it_Asset]) == AssetState::Ready ? AssetManager::GetAsset(l_Loading.Held[it_Asset]) : nullptr;
                if (l_Asset == nullptr || l_Asset->GetAssetType() != MaterialAsset::c_AssetType)
                {
                    continue;
                }

                const MaterialData& l_Data = static_cast<const MaterialAsset*>(l_Asset)->GetData();
                for (const UUID it_Texture : { l_Data.BaseColorTexture.Texture, l_Data.MetallicRoughnessTexture.Texture, l_Data.NormalTexture.Texture, l_Data.OcclusionTexture.Texture, l_Data.EmissiveTexture.Texture })
                {
                    if (it_Texture && std::ranges::find(l_Loading.Held, it_Texture) == l_Loading.Held.end())
                    {
                        l_Loading.Held.push_back(it_Texture);
                        AssetManager::Acquire(it_Texture);
                    }
                }
            }

            if (CountWaiting(l_Loading.Held) != 0)
            {
                return;
            }
        }

        Switch();
    }

    // The kept subtrees are written out and their scripts taken before the old scene goes, so its scripts' OnDestroy still sees them. In the new scene any entity of a kept UUID gives way first, and each kept root goes last among the roots, where it was in the world
    void SceneManager::Switch()
    {
        TR_PROFILE_FUNCTION();

        State& l_State = *m_State;
        Scope<State::Loading> l_Loading = std::move(l_State.Pending);
        Scope<Scene> l_Next = std::move(l_Loading->Next);

        struct Kept
        {
            std::string Text;
            TransformComponent World;
            UUID Root;
        };

        std::vector<Kept, TaggedAllocator<Kept, MemoryTag::Scene>> l_Kept;
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Scene>> l_KeptEntities;
        ScriptCarry l_Carry;
        if (l_State.Current)
        {
            Scene& l_Old = *l_State.Current;
            for (Entity it_Entity = l_Old.GetFirstRoot(); it_Entity; it_Entity = l_Old.GetNextInHierarchyOrder(it_Entity))
            {
                if (!it_Entity.Has<KeepAcrossScenesComponent>())
                {
                    continue;
                }

                bool l_Inside = false;
                for (Entity it_Parent = it_Entity.GetParent(); it_Parent && !l_Inside; it_Parent = it_Parent.GetParent())
                {
                    l_Inside = it_Parent.Has<KeepAcrossScenesComponent>();
                }

                if (l_Inside)
                {
                    continue;
                }

                l_Kept.push_back({ SceneSerializer::SaveEntityToText(l_Old, it_Entity), l_Old.ComputeWorldTransform(it_Entity), it_Entity.GetUUID() });
                for (Entity it_Member = it_Entity; it_Member; it_Member = l_Old.GetNextInSubtree(it_Member, it_Entity))
                {
                    l_KeptEntities.push_back(it_Member.GetUUID());
                }
            }

            if (l_State.Runtime)
            {
                l_Carry = l_State.Runtime->GetScripts().Extract(l_KeptEntities);
            }
        }

        l_State.Runtime.reset();
        l_State.Current.reset();
        Release(l_State.Held);

        for (const UUID it_Entity : l_KeptEntities)
        {
            if (const Entity l_Existing = l_Next->FindEntityByUUID(it_Entity))
            {
                l_Next->DestroyEntity(l_Existing);
            }
        }

        std::uint32_t l_KeptCount = 0;
        for (const Kept& it_Kept : l_Kept)
        {
            const Expected<Entity, std::string> l_Root = SceneSerializer::LoadEntityFromText(*l_Next, it_Kept.Text, {}, {});
            if (!l_Root)
            {
                TR_CORE_ERROR("SceneManager: entity {} was not kept across to {}: {}", it_Kept.Root, l_Loading->Path, l_Root.GetError());

                continue;
            }

            l_Next->SetWorldTransform(*l_Root, it_Kept.World);
            for (Entity it_Member = *l_Root; it_Member; it_Member = l_Next->GetNextInSubtree(it_Member, *l_Root))
            {
                ++l_KeptCount;
            }
        }

        l_State.Current = std::move(l_Next);
        l_State.Asset = l_Loading->Asset;
        l_State.Held = std::move(l_Loading->Held);
        l_State.Current->UpdateWorldTransforms();
        l_State.Runtime = CreateScope<SceneRuntime>(*l_State.Current, l_State.Settings);
        l_State.Runtime->GetScripts().Adopt(l_Carry);
        ++l_State.Counts.Switches;
        l_State.Counts.Kept = l_KeptCount;

        const double l_Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l_Loading->Started).count();
        TR_CORE_INFO("SceneManager: switched to {}, {} entities with {} kept across and {} asset(s), {:.1f} ms after it was asked for", l_Loading->Path, l_State.Current->GetEntityCount(), l_KeptCount, l_State.Held.size(), l_Milliseconds);
    }

    Scene* SceneManager::GetScene() const
    {
        return m_State->Current.get();
    }

    SceneRuntime* SceneManager::GetRuntime() const
    {
        return m_State->Runtime.get();
    }

    UUID SceneManager::GetSceneAsset() const
    {
        return m_State->Asset;
    }

    bool SceneManager::IsLoading() const
    {
        return m_State->Pending != nullptr;
    }

    SceneManager::Statistics SceneManager::GetStatistics() const
    {
        Statistics l_Statistics = m_State->Counts;
        l_Statistics.Waiting = m_State->Pending && m_State->Pending->Next ? CountWaiting(m_State->Pending->Held) : 0;

        return l_Statistics;
    }
}