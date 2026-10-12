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
        // Makes a built-in asset's bytes on a worker, in place of the file a project's asset is read from, which its loader then loads as it would the file
        using BuiltInSource = Expected<std::vector<std::byte>, std::string>(*)(UUID id);

        TRINITY_API void Initialize();
        TRINITY_API void Shutdown();
        TRINITY_API void Update();

        TRINITY_API void SetRegistry(const AssetRegistry* registry);
        TRINITY_API void RegisterLoader(const AssetLoader& loader);
        TRINITY_API void UnregisterLoader(std::string_view assetType);

        // An asset the engine makes itself, which every project has under the same UUID whatever its registry holds, such as the built-in meshes. The asset type must outlive the registration, as a c_AssetType does. Copies already made stay until they are released
        TRINITY_API void RegisterBuiltIn(UUID id, std::string_view assetType, BuiltInSource source);
        TRINITY_API void UnregisterBuiltIn(UUID id);
        [[nodiscard]] TRINITY_API bool IsBuiltIn(UUID id);

        TRINITY_API void Acquire(UUID id);
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