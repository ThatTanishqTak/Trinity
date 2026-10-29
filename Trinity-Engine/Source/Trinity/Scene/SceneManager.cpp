#include "Trinity/Scene/SceneManager.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Asset/MaterialAsset.hpp"
#include "Trinity/Audio/AudioScene.hpp"
#include "Trinity/Audio/SoundEventData.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"
#include "Trinity/Input/Input.hpp"
#include "Trinity/Input/InputActionsRuntime.hpp"
#include "Trinity/Project/Project.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Prefab.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/Scene/SceneAssets.hpp"
#include "Trinity/Scene/SceneRuntime.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"
#include "Trinity/Scripting/ScriptRuntime.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        SceneManager* s_Active = nullptr;

        using AssetList = SceneAssets::AssetList;

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

        // The prefabs nested in each prefab held that has loaded, which its template is made from, held too. True while one was added, so it is waited for, and the prefabs nested in it in turn
        [[nodiscard]] bool HoldNestedPrefabs(AssetList& assets)
        {
            bool l_Added = false;
            for (std::size_t it_Asset = 0; it_Asset < assets.size(); ++it_Asset)
            {
                const Asset* l_Asset = AssetManager::GetState(assets[it_Asset]) == AssetState::Ready ? AssetManager::GetAsset(assets[it_Asset]) : nullptr;
                if (l_Asset == nullptr || l_Asset->GetAssetType() != PrefabAsset::c_AssetType)
                {
                    continue;
                }

                for (const UUID it_Nested : static_cast<const PrefabAsset*>(l_Asset)->GetNestedPrefabs())
                {
                    if (std::ranges::find(assets, it_Nested) == assets.end())
                    {
                        assets.push_back(it_Nested);
                        AssetManager::Acquire(it_Nested);
                        l_Added = true;
                    }
                }
            }

            return l_Added;
        }

        // Each prefab held, a model's too, whose template can be read has what the template names held as well: its meshes, materials and the rest, the prefabs nested in it and those its scripts' fields name, so an instance of it made while the scene runs is whole at once and can make instances of those in turn. One still loading is waited for, and each is gone through once, those it adds in the same call when their templates can be read already. True while one was added, which is waited for and gone through in turn
        [[nodiscard]] bool HoldPrefabContents(AssetList& assets, AssetList& seen, const AssetRegistry* registry)
        {
            bool l_Added = false;
            for (std::size_t it_Asset = 0; it_Asset < assets.size(); ++it_Asset)
            {
                const UUID l_Prefab = assets[it_Asset];
                if (std::ranges::find(seen, l_Prefab) != seen.end())
                {
                    continue;
                }

                const AssetRecord* l_Record = registry != nullptr ? registry->Find(l_Prefab) : nullptr;
                if (l_Record == nullptr || (l_Record->Importer != PrefabAsset::c_AssetType && l_Record->Importer != c_ModelAssetType))
                {
                    seen.push_back(l_Prefab);

                    continue;
                }

                // Forge's resolver reads a prefab as it is asked for, and the asset manager's has one once it has loaded
                Scene* l_Template = SceneSerializer::FindPrefabTemplate(l_Prefab);
                if (l_Template == nullptr && AssetManager::GetState(l_Prefab) == AssetState::Loading)
                {
                    continue;
                }

                seen.push_back(l_Prefab);
                if (l_Template == nullptr)
                {
                    continue;
                }

                const std::size_t l_Count = assets.size();
                SceneAssets::Gather(*l_Template, registry, assets);
                SceneAssets::GatherInstancePrefabs(*l_Template, assets);
                for (std::size_t it_New = l_Count; it_New < assets.size(); ++it_New)
                {
                    AssetManager::Acquire(assets[it_New]);
                    l_Added = true;
                }
            }

            return l_Added;
        }

        // How long the instances of each prefab the runtime instanced took, once it ends, as the scene stops or another takes its place
        void LogInstancing(SceneRuntime* runtime, const AssetRegistry* registry, std::string_view scene)
        {
            if (runtime == nullptr)
            {
                return;
            }

            for (const ScriptRuntime::InstancingTime& it_Time : runtime->GetScripts().GetInstancingTimes())
            {
                const AssetRecord* l_Record = registry != nullptr ? registry->Find(it_Time.Prefab) : nullptr;
                const std::string l_Prefab = l_Record != nullptr ? l_Record->Path : std::format("{}", it_Time.Prefab);
                TR_CORE_INFO("SceneManager: {} made {} instance(s) of prefab {} while it ran, in {:.1f} us each on average and {:.1f} us at most", scene, it_Time.Count, l_Prefab, it_Time.TotalMicroseconds / static_cast<double>(it_Time.Count), it_Time.MostMicroseconds);
            }
        }
    }

    // A scene asked for: its file being read, then waiting for the prefabs its instances are of, then the scene loaded from it waiting for its assets and what is inside the prefabs it holds, then for its materials' textures and its sound events' clips
    struct SceneManager::State
    {
        struct Loading
        {
            UUID Asset;
            std::string Path;
            FileRequest Request;
            bool Read = false;
            // The file as read: a project's text, or a cooked game's binary scene
            std::optional<std::string> Text;
            std::string Error;
            Scope<Scene> Next;
            AssetList Held;
            // The assets of Held HoldPrefabContents has gone through
            AssetList Seen;
            bool PrefabsHeld = false;
            bool TexturesHeld = false;
            std::chrono::steady_clock::time_point Started = std::chrono::steady_clock::now();
        };

        PhysicsSettings Settings;
        AudioEngine* Audio = nullptr;
        AudioSettings Mixing;
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

        // A cooked game holds only the scenes of its build, so one not there is named as one left out of it
        [[nodiscard]] std::string DescribeMissing(std::string_view scene) const
        {
            return Registry != nullptr && Registry->IsCooked() ? std::format("scene {} is not one of the build's scenes", scene) : std::format("there is no scene {} to load", scene);
        }
    };

    // The actions' players last across every scene it switches to, as the scripts kept across scenes would expect
    SceneManager::SceneManager(const PhysicsSettings& settings, const AssetRegistry* registry, UUID inputActions, AudioEngine* audio, const AudioSettings& audioSettings) : m_State(Memory::New<State>(MemoryTag::Scene))
    {
        m_State->Settings = settings;
        m_State->Audio = audio;
        m_State->Mixing = audioSettings;
        m_State->Registry = registry;
        m_State->Previous = s_Active;
        s_Active = this;
        InputActionsRuntime::Start(inputActions);
    }

    // The running scene's scripts have OnDestroy before the scene goes, and any rumble they leave stops with it, and so do its actions' players
    SceneManager::~SceneManager()
    {
        if (m_State->Pending)
        {
            Release(m_State->Pending->Held);
            m_State->Pending.reset();
        }

        LogInstancing(m_State->Runtime.get(), m_State->Registry, m_State->DescribeCurrent());
        m_State->Runtime.reset();
        Input::StopGamepadRumble();
        InputActionsRuntime::Stop();
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

    // The scene is read already, so what it names is held at once, as is what is inside the prefabs it names that can be read now, which in Forge's Play is every one, as Forge reads a prefab from its file as it is asked for
    void SceneManager::Run(Scope<Scene> scene, UUID asset)
    {
        State& l_State = *m_State;
        LogInstancing(l_State.Runtime.get(), l_State.Registry, l_State.DescribeCurrent());
        l_State.Runtime.reset();
        l_State.Current = std::move(scene);
        Release(l_State.Held);
        l_State.Asset = asset;
        if (l_State.Current)
        {
            SceneAssets::Gather(*l_State.Current, l_State.Registry, l_State.Held);
            SceneAssets::GatherInstancePrefabs(*l_State.Current, l_State.Held);
            for (const UUID it_Asset : l_State.Held)
            {
                AssetManager::Acquire(it_Asset);
            }

            AssetList l_Seen;
            static_cast<void>(HoldPrefabContents(l_State.Held, l_Seen, l_State.Registry));

            l_State.Current->UpdateWorldTransforms();
            l_State.Runtime = CreateScope<SceneRuntime>(*l_State.Current, l_State.Settings, l_State.Audio, l_State.Mixing);
        }
    }

    bool SceneManager::Load(UUID scene)
    {
        State& l_State = *m_State;
        const AssetRecord* l_Record = l_State.Registry != nullptr && scene ? l_State.Registry->Find(scene) : nullptr;
        if (l_Record == nullptr || l_Record->Importer != "Scene")
        {
            TR_CORE_ERROR("SceneManager: {}, so {} goes on", l_State.DescribeMissing(std::format("{}", scene)), l_State.DescribeCurrent());

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
        l_Loading->Request = FileSystem::ReadFileAsync(l_Record->GetReadPath(), [l_Loading](Expected<FileBuffer, FileError> result)
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
            TR_CORE_ERROR("SceneManager: {}, so {} goes on", l_State.DescribeMissing(path), l_State.DescribeCurrent());

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

    // The loading scene holds the prefabs its instances are of once read, and those nested in them as each is in, and is parsed once they are all in, as its instances are made from them. It holds its assets, then its materials' textures and its sound events' clips once its materials and events are in, so it waits for the clips that preload, and is switched to once nothing it holds still loads. A prefab that does not load leaves its instances kept as the file has them
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

            if (!l_Loading.PrefabsHeld)
            {
                l_Loading.PrefabsHeld = true;
                const std::span<const std::byte> l_Data = std::as_bytes(std::span(*l_Loading.Text));
                for (const UUID it_Prefab : SceneSerializer::IsBinary(l_Data) ? SceneSerializer::GetBinaryPrefabReferences(l_Data) : SceneSerializer::GetPrefabReferences(*l_Loading.Text))
                {
                    SceneAssets::Add(l_Loading.Held, it_Prefab);
                    AssetManager::Acquire(it_Prefab);
                }
            }

            if (CountWaiting(l_Loading.Held) != 0 || HoldNestedPrefabs(l_Loading.Held))
            {
                return;
            }

            TR_PROFILE_SCOPE("SceneManager::Parse");
            Scope<Scene> l_Next = CreateScope<Scene>();
            const std::span<const std::byte> l_Data = std::as_bytes(std::span(*l_Loading.Text));
            const Expected<void, std::string> l_Loaded = SceneSerializer::IsBinary(l_Data) ? SceneSerializer::LoadFromBinary(*l_Next, l_Data) : SceneSerializer::LoadFromText(*l_Next, *l_Loading.Text);
            l_Loading.Text.reset();
            if (!l_Loaded)
            {
                TR_CORE_ERROR("SceneManager: {} was not loaded, since {}, so {} goes on", l_Loading.Path, l_Loaded.GetError(), l_State.DescribeCurrent());
                l_State.Pending.reset();

                return;
            }

            l_Loading.Next = std::move(l_Next);
            const std::size_t l_Prefabs = l_Loading.Held.size();
            SceneAssets::Gather(*l_Loading.Next, l_State.Registry, l_Loading.Held);
            for (std::size_t it_Asset = l_Prefabs; it_Asset < l_Loading.Held.size(); ++it_Asset)
            {
                AssetManager::Acquire(l_Loading.Held[it_Asset]);
            }
        }

        if (CountWaiting(l_Loading.Held) != 0 || HoldNestedPrefabs(l_Loading.Held) || HoldPrefabContents(l_Loading.Held, l_Loading.Seen, l_State.Registry))
        {
            return;
        }

        if (!l_Loading.TexturesHeld)
        {
            l_Loading.TexturesHeld = true;
            const auto a_Hold = [&l_Loading](UUID id)
            {
                if (id && std::ranges::find(l_Loading.Held, id) == l_Loading.Held.end())
                {
                    l_Loading.Held.push_back(id);
                    AssetManager::Acquire(id);
                }
            };

            const std::size_t l_Count = l_Loading.Held.size();
            for (std::size_t it_Asset = 0; it_Asset < l_Count; ++it_Asset)
            {
                const Asset* l_Asset = AssetManager::GetState(l_Loading.Held[it_Asset]) == AssetState::Ready ? AssetManager::GetAsset(l_Loading.Held[it_Asset]) : nullptr;
                if (l_Asset != nullptr && l_Asset->GetAssetType() == SoundEventAsset::c_AssetType)
                {
                    std::ranges::for_each(static_cast<const SoundEventAsset*>(l_Asset)->GetClips(), a_Hold);
                }

                if (l_Asset == nullptr || l_Asset->GetAssetType() != MaterialAsset::c_AssetType)
                {
                    continue;
                }

                const MaterialData& l_Data = static_cast<const MaterialAsset*>(l_Asset)->GetData();
                std::ranges::for_each(l_Data.GetTextures(), a_Hold);
            }

            if (CountWaiting(l_Loading.Held) != 0)
            {
                return;
            }
        }

        Switch();
    }

    // The kept subtrees are written out and their scripts taken before the old scene goes, so its scripts' OnDestroy still sees them. In the new scene any entity of a kept UUID gives way first, and each kept root goes last among the roots, where it was in the world. The old scene's assets are let go once the kept subtrees are in, so the prefabs their instances are of are there to make them from
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
        AudioCarry l_Sounds;
        bool l_SoundsPaused = false;
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
                l_Sounds = l_State.Runtime->GetAudio().Extract(l_KeptEntities);
                l_SoundsPaused = l_State.Runtime->GetAudio().IsPaused();
            }
        }

        LogInstancing(l_State.Runtime.get(), l_State.Registry, l_State.DescribeCurrent());
        l_State.Runtime.reset();
        l_State.Current.reset();

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

        // What the kept subtrees name is the new scene's to hold too, with what is inside its prefabs, which the old scene held and so has loaded, so a kept script goes on instancing what it did
        if (l_KeptCount != 0)
        {
            const std::size_t l_Count = l_Loading->Held.size();
            SceneAssets::Gather(*l_Next, l_State.Registry, l_Loading->Held);
            SceneAssets::GatherInstancePrefabs(*l_Next, l_Loading->Held);
            for (std::size_t it_Asset = l_Count; it_Asset < l_Loading->Held.size(); ++it_Asset)
            {
                AssetManager::Acquire(l_Loading->Held[it_Asset]);
            }

            static_cast<void>(HoldPrefabContents(l_Loading->Held, l_Loading->Seen, l_State.Registry));
        }

        Release(l_State.Held);

        l_State.Current = std::move(l_Next);
        l_State.Asset = l_Loading->Asset;
        l_State.Held = std::move(l_Loading->Held);
        l_State.Current->UpdateWorldTransforms();
        l_State.Runtime = CreateScope<SceneRuntime>(*l_State.Current, l_State.Settings, l_State.Audio, l_State.Mixing);
        l_State.Runtime->GetScripts().Adopt(l_Carry);
        l_State.Runtime->GetAudio().SetPaused(l_SoundsPaused);
        l_State.Runtime->GetAudio().Adopt(std::move(l_Sounds));
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

    bool SceneManager::Holds(UUID asset) const
    {
        return asset && std::ranges::find(m_State->Held, asset) != m_State->Held.end();
    }

    SceneManager::Statistics SceneManager::GetStatistics() const
    {
        Statistics l_Statistics = m_State->Counts;
        l_Statistics.Waiting = m_State->Pending && m_State->Pending->Next ? CountWaiting(m_State->Pending->Held) : 0;

        return l_Statistics;
    }
}