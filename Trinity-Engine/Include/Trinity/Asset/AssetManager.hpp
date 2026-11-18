#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/UUID.hpp"

#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Trinity
{
    class AssetRegistry;

    namespace AssetManager
    {
        // Makes a built-in asset's bytes on a worker, in place of reading a file
        using BuiltInSource = Expected<std::vector<std::byte>, std::string>(*)(UUID id);

        TRINITY_API void Initialize();
        TRINITY_API void Shutdown();
        TRINITY_API void Update();

        TRINITY_API void SetRegistry(const AssetRegistry* registry);
        // Where assets are found, null until a registry is set
        [[nodiscard]] TRINITY_API const AssetRegistry* GetRegistry();
        TRINITY_API void RegisterLoader(const AssetLoader& loader);
        TRINITY_API void UnregisterLoader(std::string_view assetType);

        // The asset type must outlive the registration. Copies already made stay until released
        TRINITY_API void RegisterBuiltIn(UUID id, std::string_view assetType, BuiltInSource source);
        TRINITY_API void UnregisterBuiltIn(UUID id);
        [[nodiscard]] TRINITY_API bool IsBuiltIn(UUID id);

        TRINITY_API void Acquire(UUID id);
        // Loads a still-loading asset now, on the main thread, for a caller that cannot go on without it
        TRINITY_API AssetState LoadNow(UUID id);
        TRINITY_API void Release(UUID id);
        TRINITY_API void Reload(UUID id);
        TRINITY_API void Hold(UUID id);
        TRINITY_API void Resume(UUID id);

        [[nodiscard]] TRINITY_API AssetState GetState(UUID id);
        [[nodiscard]] TRINITY_API const Asset* GetAsset(UUID id);
        [[nodiscard]] TRINITY_API std::size_t GetEntryCount();
    }

    template<std::derived_from<Asset> T>
    class AssetRef
    {
    public:
        AssetRef() = default;

        explicit AssetRef(UUID id) : m_ID(id)
        {
            AssetManager::Acquire(m_ID);
        }

        ~AssetRef()
        {
            AssetManager::Release(m_ID);
        }

        AssetRef(const AssetRef& other) : m_ID(other.m_ID)
        {
            AssetManager::Acquire(m_ID);
        }

        AssetRef(AssetRef&& other) noexcept : m_ID(std::exchange(other.m_ID, UUID()))
        {

        }

        AssetRef& operator=(const AssetRef& other)
        {
            if (this != &other)
            {
                AssetManager::Acquire(other.m_ID);
                AssetManager::Release(m_ID);
                m_ID = other.m_ID;
            }

            return *this;
        }

        AssetRef& operator=(AssetRef&& other) noexcept
        {
            if (this != &other)
            {
                AssetManager::Release(m_ID);
                m_ID = std::exchange(other.m_ID, UUID());
            }

            return *this;
        }

        [[nodiscard]] UUID GetID() const { return m_ID; }
        [[nodiscard]] AssetState GetState() const { return AssetManager::GetState(m_ID); }
        [[nodiscard]] bool IsReady() const { return GetState() == AssetState::Ready; }

        [[nodiscard]] const T* Get() const
        {
            const Asset* l_Asset = AssetManager::GetAsset(m_ID);
            TR_CORE_ASSERT(l_Asset == nullptr || l_Asset->GetAssetType() == T::c_AssetType, "Asset {} is a {}, not a {}", m_ID, l_Asset->GetAssetType(), T::c_AssetType);

            return static_cast<const T*>(l_Asset);
        }

    private:
        UUID m_ID;
    };
}