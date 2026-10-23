#include "Trinity/Scene/Prefab.hpp"

#include "Trinity/Scene/LiveScenes.hpp"
#include "Trinity/Scene/PrefabLoaders.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Scene/Entity.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"

#include <utility>

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

                return l_Template != nullptr && l_Template->FindEntityByUUID(FindLocalID(l_Instance.Instance, entity.GetUUID())) ? it_Entity : Entity();
            }

            return {};
        }
    }

    PrefabAsset::PrefabAsset(std::string_view text) : m_Text(text)
    {

    }

    PrefabAsset::~PrefabAsset() = default;

    // Only the text is kept on the worker, since a scene belongs to the main thread
    Expected<Asset*, std::string> PrefabLoader::Load(std::span<const std::byte> data) const
    {
        return Memory::New<PrefabAsset>(MemoryTag::Assets, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
    }

    Expected<void, std::string> PrefabLoader::Finish(Asset& asset) const
    {
        PrefabAsset& l_Prefab = static_cast<PrefabAsset&>(asset);
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