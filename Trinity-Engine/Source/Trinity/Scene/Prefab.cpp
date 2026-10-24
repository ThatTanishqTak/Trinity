#include "Trinity/Scene/Prefab.hpp"

#include "Trinity/Scene/LiveScenes.hpp"
#include "Trinity/Scene/PrefabLoaders.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        // splitmix64's finalizer, which takes each 64-bit value to another and has an inverse
        constexpr std::uint64_t Mix(std::uint64_t value)
        {
            value ^= value >> 30;
            value *= 0xBF58476D1CE4E5B9ULL;
            value ^= value >> 27;
            value *= 0x94D049BB133111EBULL;
            value ^= value >> 31;

            return value;
        }

        // value ^= value >> shift, undone
        constexpr std::uint64_t UnshiftRight(std::uint64_t value, std::uint32_t shift)
        {
            std::uint64_t l_Result = value;
            for (std::uint32_t it_Shift = shift; it_Shift < 64; it_Shift += shift)
            {
                l_Result ^= value >> it_Shift;
            }

            return l_Result;
        }

        // An odd number's inverse modulo 2^64, by Newton's method, each step doubling the bits that are right
        constexpr std::uint64_t InverseOf(std::uint64_t odd)
        {
            std::uint64_t l_Inverse = odd;
            for (int it_Step = 0; it_Step < 5; ++it_Step)
            {
                l_Inverse *= 2 - odd * l_Inverse;
            }

            return l_Inverse;
        }

        constexpr std::uint64_t Unmix(std::uint64_t value)
        {
            value = UnshiftRight(value, 31);
            value *= InverseOf(0x94D049BB133111EBULL);
            value = UnshiftRight(value, 27);
            value *= InverseOf(0xBF58476D1CE4E5B9ULL);
            value = UnshiftRight(value, 30);

            return value;
        }

        static_assert(Unmix(Mix(0x0123456789ABCDEFULL)) == 0x0123456789ABCDEFULL && Unmix(Mix(0xFFFFFFFFFFFFFFFFULL)) == 0xFFFFFFFFFFFFFFFFULL);

        // Each instance's own, so its entities' UUIDs are its own
        constexpr std::uint64_t GetKey(UUID instance)
        {
            return Mix(instance.GetValue() + 0x9E3779B97F4A7C15ULL);
        }

        PrefabLoader* s_Loader = nullptr;
    }

    namespace Prefab
    {
        UUID MakeEntityUUID(UUID instance, UUID local)
        {
            return UUID(Mix(local.GetValue() ^ GetKey(instance)));
        }

        UUID FindLocalID(UUID instance, UUID entity)
        {
            return UUID(Unmix(entity.GetValue()) ^ GetKey(instance));
        }

        // A prefab's own entity is made from the local ID alone, and one of an instance nested in it is made by that instance's prefab, a level down
        UUID GetInstanceEntityUUID(UUID instance, Entity member)
        {
            const Entity l_Nested = FindInstanceRoot(member);
            const PrefabInstanceComponent* l_Instance = l_Nested ? &l_Nested.Get<PrefabInstanceComponent>() : nullptr;
            Scene* l_Template = l_Instance != nullptr && !l_Instance->IsMissing() ? SceneSerializer::FindPrefabTemplate(l_Instance->Prefab) : nullptr;
            const Entity l_Inner = l_Template != nullptr ? FindTemplateEntity(*l_Template, l_Instance->Instance, member.GetUUID()) : Entity();
            if (!l_Inner)
            {
                return MakeEntityUUID(instance, member.GetUUID());
            }

            return GetInstanceEntityUUID(MakeEntityUUID(instance, l_Instance->Instance), l_Inner);
        }

        // Most are the prefab's own, told back from the UUID at once. One of an instance nested in it is found in that instance's prefab, under the UUID the nested instance has in this one
        Entity FindTemplateEntity(Scene& prefab, UUID instance, UUID entity)
        {
            const Entity l_Own = prefab.FindEntityByUUID(FindLocalID(instance, entity));
            const Entity l_Nested = l_Own ? FindInstanceRoot(l_Own) : Entity();
            if (l_Own && (!l_Nested || l_Nested.Get<PrefabInstanceComponent>().IsMissing()))
            {
                return l_Own;
            }

            for (const auto [it_Root, it_Instance] : prefab.GetRegistry().view<PrefabInstanceComponent>().each())
            {
                Scene* l_Template = it_Instance.IsMissing() ? nullptr : SceneSerializer::FindPrefabTemplate(it_Instance.Prefab);
                const Entity l_Inner = l_Template != nullptr ? FindTemplateEntity(*l_Template, MakeEntityUUID(instance, it_Instance.Instance), entity) : Entity();
                if (l_Inner)
                {
                    return prefab.FindEntityByUUID(GetInstanceEntityUUID(it_Instance.Instance, l_Inner));
                }
            }

            return {};
        }

        // An instance's own entities stay under its root where its prefab has them, so the nearest instance root above is the one to ask, and an entity added under an instance has no local ID in its prefab
        Entity FindInstanceRoot(Entity entity)
        {
            for (Entity it_Entity = entity; it_Entity; it_Entity = it_Entity.GetParent())
            {
                if (!it_Entity.Has<PrefabInstanceComponent>())
                {
                    continue;
                }

                if (it_Entity == entity)
                {
                    return it_Entity;
                }

                const PrefabInstanceComponent& l_Instance = it_Entity.Get<PrefabInstanceComponent>();
                Scene* l_Template = l_Instance.IsMissing() ? nullptr : SceneSerializer::FindPrefabTemplate(l_Instance.Prefab);

                return l_Template != nullptr && FindTemplateEntity(*l_Template, l_Instance.Instance, entity.GetUUID()) ? it_Entity : Entity();
            }

            return {};
        }

        // Each level's instance is the one nested in the template above it, its root in the scene the UUID the level above gives that instance's root
        std::vector<Level> GetLevels(Entity root, Entity member)
        {
            std::vector<Level> l_Levels;
            const PrefabInstanceComponent* l_Outer = root && root.Has<PrefabInstanceComponent>() ? &root.Get<PrefabInstanceComponent>() : nullptr;
            if (l_Outer == nullptr || l_Outer->IsMissing() || !member)
            {
                return l_Levels;
            }

            Scene& l_Scene = *root.GetScene();
            Level l_Level{ .Prefab = l_Outer->Prefab, .Instance = l_Outer->Instance, .Root = root, .Template = member };
            for (;;)
            {
                l_Levels.push_back(l_Level);
                const Entity l_Nested = FindInstanceRoot(l_Level.Template);
                if (!l_Nested || l_Nested.Get<PrefabInstanceComponent>().IsMissing())
                {
                    return l_Levels;
                }

                const PrefabInstanceComponent& l_Inner = l_Nested.Get<PrefabInstanceComponent>();
                Scene* l_Template = SceneSerializer::FindPrefabTemplate(l_Inner.Prefab);
                const Entity l_Member = l_Template != nullptr ? FindTemplateEntity(*l_Template, l_Inner.Instance, l_Level.Template.GetUUID()) : Entity();
                if (!l_Member)
                {
                    return l_Levels;
                }

                l_Level = Level{ .Prefab = l_Inner.Prefab, .Instance = MakeEntityUUID(l_Level.Instance, l_Inner.Instance), .Root = l_Scene.FindEntityByUUID(GetInstanceEntityUUID(l_Level.Instance, l_Nested)), .Template = l_Member };
            }
        }

        std::vector<Level> GetLevels(Entity entity)
        {
            const Entity l_Root = FindInstanceRoot(entity);
            const PrefabInstanceComponent* l_Instance = l_Root ? &l_Root.Get<PrefabInstanceComponent>() : nullptr;
            Scene* l_Template = l_Instance != nullptr && !l_Instance->IsMissing() ? SceneSerializer::FindPrefabTemplate(l_Instance->Prefab) : nullptr;
            const Entity l_Member = l_Template != nullptr ? FindTemplateEntity(*l_Template, l_Instance->Instance, entity.GetUUID()) : Entity();

            return GetLevels(l_Root, l_Member);
        }

        // Through the templates, each prefab once, so one held twice is looked into once. One standing in for a missing prefab names it still, so it counts
        bool Holds(UUID prefab, UUID other)
        {
            if (prefab == other)
            {
                return true;
            }

            std::vector<UUID> l_Pending{ prefab };
            std::unordered_set<UUID> l_Seen{ prefab };
            while (!l_Pending.empty())
            {
                Scene* l_Template = SceneSerializer::FindPrefabTemplate(l_Pending.back());
                l_Pending.pop_back();
                if (l_Template == nullptr)
                {
                    continue;
                }

                for (const auto [it_Root, it_Instance] : l_Template->GetRegistry().view<PrefabInstanceComponent>().each())
                {
                    if (it_Instance.Prefab == other)
                    {
                        return true;
                    }

                    if (l_Seen.insert(it_Instance.Prefab).second)
                    {
                        l_Pending.push_back(it_Instance.Prefab);
                    }
                }
            }

            return false;
        }

        std::vector<std::pair<Entity, PrefabInstanceComponent>> GetNestedInstances(Entity root)
        {
            std::vector<std::pair<Entity, PrefabInstanceComponent>> l_Nested;
            const PrefabInstanceComponent* l_Instance = root && root.Has<PrefabInstanceComponent>() ? &root.Get<PrefabInstanceComponent>() : nullptr;
            Scene* l_Template = l_Instance != nullptr && !l_Instance->IsMissing() ? SceneSerializer::FindPrefabTemplate(l_Instance->Prefab) : nullptr;
            if (l_Template == nullptr)
            {
                return l_Nested;
            }

            Scene& l_Scene = *root.GetScene();
            for (const auto [it_Root, it_Nested] : l_Template->GetRegistry().view<PrefabInstanceComponent>().each())
            {
                const Entity l_Own = it_Nested.IsMissing() ? Entity() : l_Scene.FindEntityByUUID(GetInstanceEntityUUID(l_Instance->Instance, Entity(it_Root, l_Template)));
                if (l_Own)
                {
                    l_Nested.emplace_back(l_Own, PrefabInstanceComponent{ .Prefab = it_Nested.Prefab, .Instance = MakeEntityUUID(l_Instance->Instance, it_Nested.Instance), .Kept = {} });
                }
            }

            return l_Nested;
        }
    }

    PrefabAsset::PrefabAsset(std::string_view text) : m_Text(text)
    {

    }

    PrefabAsset::~PrefabAsset() = default;

    Scene* PrefabAsset::GetTemplate() const
    {
        if (!m_Template && !m_Text.empty() && !m_Making && IsComplete())
        {
            MakeTemplate();
        }

        return m_Template.get();
    }

    // A prefab that failed to load has its instances kept as their text, so it counts, and one not asked for yet, or still loading, is waited for. One nested in itself is met again while it is checked and counts as there, so the serializer refuses that instance as the template is made
    bool PrefabAsset::IsComplete() const
    {
        if (m_Checking)
        {
            return true;
        }

        m_Checking = true;
        const bool l_Complete = std::ranges::all_of(m_Nested, [](UUID nested)
        {
            const AssetState l_State = AssetManager::GetState(nested);
            const Asset* l_Asset = l_State == AssetState::Ready ? AssetManager::GetAsset(nested) : nullptr;
            if (l_Asset == nullptr)
            {
                return l_State == AssetState::Failed;
            }

            return l_Asset->GetAssetType() != PrefabAsset::c_AssetType || static_cast<const PrefabAsset*>(l_Asset)->IsComplete();
        });
        m_Checking = false;

        return l_Complete;
    }

    // From a copy of the text, which the reading keeps views into, and only once
    void PrefabAsset::MakeTemplate() const
    {
        const std::string l_Text(m_Text);
        TaggedString<MemoryTag::Assets>().swap(m_Text);
        m_Making = true;
        Scope<Scene> l_Template = CreateScope<Scene>();
        const Expected<void, std::string> l_Loaded = SceneSerializer::LoadPrefabFromText(*l_Template, l_Text);
        m_Making = false;
        if (!l_Loaded)
        {
            TR_CORE_ERROR("Scene: a prefab could not be read, so its instances are kept as their text: {}", l_Loaded.GetError());

            return;
        }

        m_Template = std::move(l_Template);
    }

    // Only the text is kept on the worker, since a scene belongs to the main thread, with the prefabs its instances are of
    Expected<Asset*, std::string> PrefabLoader::Load(std::span<const std::byte> data) const
    {
        const std::string_view l_Text(reinterpret_cast<const char*>(data.data()), data.size());
        PrefabAsset* l_Prefab = Memory::New<PrefabAsset>(MemoryTag::Assets, l_Text);
        const std::vector<UUID> l_Nested = SceneSerializer::GetPrefabReferences(l_Text);
        l_Prefab->m_Nested.assign(l_Nested.begin(), l_Nested.end());

        return l_Prefab;
    }

    // A prefab holding no other is read here, so one that cannot be read fails to load. One that holds others waits for them, and is read on first use
    Expected<void, std::string> PrefabLoader::Finish(Asset& asset) const
    {
        PrefabAsset& l_Prefab = static_cast<PrefabAsset&>(asset);
        if (!l_Prefab.m_Nested.empty())
        {
            return {};
        }

        Scope<Scene> l_Template = CreateScope<Scene>();
        const Expected<void, std::string> l_Loaded = SceneSerializer::LoadPrefabFromText(*l_Template, l_Prefab.m_Text);
        if (!l_Loaded)
        {
            return Unexpected{ l_Loaded.GetError() };
        }

        l_Prefab.m_Template = std::move(l_Template);
        TaggedString<MemoryTag::Assets>().swap(l_Prefab.m_Text);

        return {};
    }

    namespace PrefabLoaders
    {
        void Register()
        {
            TR_CORE_ASSERT(s_Loader == nullptr, "The prefab loader is registered once.");

            s_Loader = Memory::New<PrefabLoader>(MemoryTag::Engine);
            AssetManager::RegisterLoader(*s_Loader);
        }

        void Unregister()
        {
            if (s_Loader == nullptr)
            {
                return;
            }

            AssetManager::UnregisterLoader(s_Loader->GetAssetType());
            Memory::Delete(s_Loader);
            s_Loader = nullptr;
        }
    }
}