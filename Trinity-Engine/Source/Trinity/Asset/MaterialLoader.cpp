#include "Trinity/Asset/MaterialLoader.hpp"

#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Profiler.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>

namespace Trinity
{
    namespace
    {
        constexpr RHI::BufferUsage c_TableUsage = RHI::BufferUsage::ShaderResource | RHI::BufferUsage::CopyDestination | RHI::BufferUsage::CopySource;
        constexpr std::uint64_t c_StagingAlignment = 16;

        constexpr std::array<MaterialTexture MaterialData::*, MaterialLoader::c_TextureSlots> c_TextureMembers{ &MaterialData::BaseColorTexture, &MaterialData::MetallicRoughnessTexture, &MaterialData::NormalTexture, &MaterialData::OcclusionTexture, &MaterialData::EmissiveTexture };

        void Store(MaterialLoader::Record& record, std::size_t offset, std::uint32_t value)
        {
            std::memcpy(record.data() + offset, &value, sizeof(value));
        }

        void Store(MaterialLoader::Record& record, std::size_t offset, float value)
        {
            Store(record, offset, std::bit_cast<std::uint32_t>(value));
        }
    }

    MaterialAsset::~MaterialAsset()
    {
        if (m_Loader != nullptr)
        {
            m_Loader->Release(*this);
        }
    }

    // Record 0 is the default material, which is also the placeholder every material reads as until it is loaded
    MaterialLoader::MaterialLoader(RHI::Device& device) : m_Device(device)
    {
        m_Slots.emplace_back().Material = &m_Default;
        if (!EnsureCapacity())
        {
            TR_CORE_ERROR("Materials: the device could not create the material table, so meshes draw without materials");
        }
    }

    // The textures records still hold are released while their loader is still registered
    MaterialLoader::~MaterialLoader()
    {
        m_Released.clear();
        for (Slot& it_Slot : m_Slots)
        {
            if (it_Slot.Material != nullptr && it_Slot.Material != &m_Default)
            {
                it_Slot.Material->m_Loader = nullptr;
            }

            it_Slot.Textures = {};
        }

        m_Device.DestroyBuffer(m_Table);
    }

    // A model's material is cooked into the cache, and a material file of its own is read where it is
    std::string MaterialLoader::GetLoadPath(const AssetRecord& record) const
    {
        return record.Parent.IsValid() ? GetCookedMaterialPath(record.ID) : record.Path;
    }

    // On a worker
    Expected<Asset*, std::string> MaterialLoader::Load(std::span<const std::byte> data) const
    {
        TR_PROFILE_FUNCTION();

        const Expected<MaterialData, std::string> l_Data = ParseMaterialData(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
        if (!l_Data)
        {
            return Unexpected{ l_Data.GetError() };
        }

        MaterialAsset* l_Material = Memory::New<MaterialAsset>(MemoryTag::Assets);
        l_Material->m_Data = *l_Data;

        return l_Material;
    }

    // A free record if there is one, and its textures start loading. The record is written at the start of the next frame
    Expected<void, std::string> MaterialLoader::Finish(Asset& asset) const
    {
        MaterialAsset& l_Material = static_cast<MaterialAsset&>(asset);

        std::uint32_t l_Index = 0;
        if (!m_FreeSlots.empty())
        {
            l_Index = m_FreeSlots.back();
            m_FreeSlots.pop_back();
        }
        else
        {
            l_Index = static_cast<std::uint32_t>(m_Slots.size());
            m_Slots.emplace_back();
        }

        m_Slots[l_Index].Material = &l_Material;
        m_Slots[l_Index].Textures = AcquireTextures(l_Material.m_Data);
        l_Material.m_TableIndex = l_Index;
        l_Material.m_Loader = this;

        return {};
    }

    // The new textures are taken before the old are let go, so a texture both use stays loaded
    bool MaterialLoader::Apply(UUID id, const MaterialData& data)
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "Materials are edited on the main thread.");

        if (AssetManager::GetState(id) != AssetState::Ready)
        {
            return false;
        }

        const Asset* l_Asset = AssetManager::GetAsset(id);
        if (l_Asset == nullptr || l_Asset->GetAssetType() != MaterialAsset::c_AssetType)
        {
            return false;
        }

        // The loader owns the data of the materials it made, which nothing else may change
        MaterialAsset& l_Material = const_cast<MaterialAsset&>(static_cast<const MaterialAsset&>(*l_Asset));
        if (l_Material.m_Loader != this)
        {
            return false;
        }

        l_Material.m_Data = data;
        m_Slots[l_Material.m_TableIndex].Textures = AcquireTextures(data);

        return true;
    }

    // Textures released since the last frame go first, then every record is packed again and the ones that changed are uploaded in one copy
    void MaterialLoader::RecordUploads(RHI::CommandList& commands)
    {
        TR_PROFILE_FUNCTION();

        m_Released.clear();
        if (!EnsureCapacity())
        {
            return;
        }

        std::uint64_t l_First = m_Rewrite ? 0 : m_Records.size();
        std::uint64_t l_End = m_Rewrite ? m_Slots.size() * c_RecordSize : 0;
        for (std::size_t it_Index = 0; it_Index < m_Slots.size(); ++it_Index)
        {
            const Slot& l_Slot = m_Slots[it_Index];
            if (l_Slot.Material == nullptr)
            {
                continue;
            }

            const Record l_Record = Pack(l_Slot.Material->m_Data, ResolveTextures(l_Slot.Material->m_Data));
            std::byte* l_Stored = m_Records.data() + it_Index * c_RecordSize;
            if (std::memcmp(l_Stored, l_Record.data(), c_RecordSize) != 0)
            {
                std::memcpy(l_Stored, l_Record.data(), c_RecordSize);
                l_First = std::min<std::uint64_t>(l_First, it_Index * c_RecordSize);
                l_End = std::max<std::uint64_t>(l_End, (it_Index + 1) * c_RecordSize);
            }
        }

        m_Rewrite = false;
        if (l_First < l_End)
        {
            Upload(commands, l_First, l_End - l_First);
        }
    }

    // The record is left as it was, since nothing reads it until it is given out again
    void MaterialLoader::Release(MaterialAsset& material) const
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "Materials are released on the main thread.");

        Slot& l_Slot = m_Slots[material.m_TableIndex];
        m_Released.push_back(std::move(l_Slot.Textures));
        l_Slot = Slot();
        m_FreeSlots.push_back(material.m_TableIndex);
        material.m_Loader = nullptr;
    }

    MaterialLoader::Record MaterialLoader::Pack(const MaterialData& material, const TextureIndices& textures)
    {
        std::uint32_t l_Flags = static_cast<std::uint32_t>(material.AlphaMode) & c_AlphaModeMask;
        l_Flags |= material.DoubleSided ? c_DoubleSidedBit : 0u;
        for (std::size_t it_Slot = 0; it_Slot < c_TextureSlots; ++it_Slot)
        {
            l_Flags |= (material.*c_TextureMembers[it_Slot]).TexCoord == 1 ? 1u << (c_TexCoordShift + it_Slot) : 0u;
        }

        Record l_Record{};
        for (glm::length_t it_Channel = 0; it_Channel < 4; ++it_Channel)
        {
            Store(l_Record, static_cast<std::size_t>(it_Channel) * 4, material.BaseColorFactor[it_Channel]);
        }

        for (glm::length_t it_Channel = 0; it_Channel < 3; ++it_Channel)
        {
            Store(l_Record, 16 + static_cast<std::size_t>(it_Channel) * 4, material.EmissiveFactor[it_Channel]);
        }

        Store(l_Record, 28, material.EmissiveStrength);
        Store(l_Record, 32, material.MetallicFactor);
        Store(l_Record, 36, material.RoughnessFactor);
        Store(l_Record, 40, material.NormalScale);
        Store(l_Record, 44, material.OcclusionStrength);
        Store(l_Record, 48, material.AlphaCutoff);
        Store(l_Record, 52, l_Flags);
        for (std::size_t it_Slot = 0; it_Slot < c_TextureSlots; ++it_Slot)
        {
            Store(l_Record, 56 + it_Slot * 4, textures[it_Slot]);
        }

        return l_Record;
    }

    MaterialLoader::TextureRefs MaterialLoader::AcquireTextures(const MaterialData& data)
    {
        TextureRefs l_Textures;
        for (std::size_t it_Slot = 0; it_Slot < c_TextureSlots; ++it_Slot)
        {
            l_Textures[it_Slot] = AssetRef<TextureAsset>((data.*c_TextureMembers[it_Slot]).Texture);
        }

        return l_Textures;
    }

    // A texture still loading, or one that failed, leaves its slot empty rather than showing a placeholder that would be wrong for a normal map. The record's slot keeps the texture loaded, so it is looked up by its UUID
    MaterialLoader::TextureIndices MaterialLoader::ResolveTextures(const MaterialData& material)
    {
        TextureIndices l_Indices{};
        for (std::size_t it_Slot = 0; it_Slot < c_TextureSlots; ++it_Slot)
        {
            const UUID l_ID = (material.*c_TextureMembers[it_Slot]).Texture;
            const Asset* l_Asset = l_ID.IsValid() && AssetManager::GetState(l_ID) == AssetState::Ready ? AssetManager::GetAsset(l_ID) : nullptr;
            const TextureAsset* l_Texture = l_Asset != nullptr && l_Asset->GetAssetType() == TextureAsset::c_AssetType ? static_cast<const TextureAsset*>(l_Asset) : nullptr;
            const bool l_Usable = l_Texture != nullptr && l_Texture->GetTexture() && l_Texture->GetShaderResourceIndex() != RHI::c_NoBindlessIndex;
            l_Indices[it_Slot] = l_Usable ? l_Texture->GetShaderResourceIndex() | (l_Texture->GetFilter() == RHI::Filter::Nearest ? c_NearestBit : 0u) : c_NoTexture;
        }

        return l_Indices;
    }

    // Doubled until every record fits. The new table is written whole, and the old one is kept by the release queue until frames reading it have finished
    bool MaterialLoader::EnsureCapacity()
    {
        if (m_Table && m_Slots.size() <= m_Capacity)
        {
            return true;
        }

        std::uint32_t l_Capacity = std::max(m_Capacity, c_InitialCapacity);
        while (l_Capacity < m_Slots.size())
        {
            l_Capacity *= 2;
        }

        RHI::BufferDescription l_Description;
        l_Description.Size = l_Capacity * c_RecordSize;
        l_Description.Usage = c_TableUsage;
        l_Description.DebugName = "Material table";

        const RHI::BufferHandle l_Table = m_Device.CreateBuffer(l_Description);
        if (!l_Table)
        {
            return false;
        }

        m_Device.DestroyBuffer(m_Table);
        m_Table = l_Table;
        m_TableIndex = m_Device.GetShaderResourceIndex(m_Table);
        m_TableState = RHI::ResourceState::Undefined;
        m_Capacity = l_Capacity;
        m_Records.resize(static_cast<std::size_t>(l_Capacity * c_RecordSize));
        m_Rewrite = true;

        return true;
    }

    // Through the upload ring when the range is small, as an edit's is, otherwise a staging buffer of its own, destroyed at once and kept by the release queue until the copy has run
    void MaterialLoader::Upload(RHI::CommandList& commands, std::uint64_t first, std::uint64_t size)
    {
        RHI::BufferHandle l_Staging;
        std::uint64_t l_Base = 0;
        std::span<std::byte> l_Data;
        const bool l_FromRing = size <= m_Device.GetUploadCapacity() / 4;
        if (l_FromRing)
        {
            const RHI::UploadAllocation l_Allocation = m_Device.AllocateUpload(size, c_StagingAlignment);
            l_Staging = l_Allocation.Buffer;
            l_Base = l_Allocation.Offset;
            l_Data = l_Allocation.Data;
        }
        else
        {
            RHI::BufferDescription l_Description;
            l_Description.Size = size;
            l_Description.Usage = RHI::BufferUsage::CopySource;
            l_Description.Memory = RHI::MemoryType::Upload;
            l_Description.DebugName = "Material table staging";

            l_Staging = m_Device.CreateBuffer(l_Description);
            l_Data = l_Staging ? m_Device.GetMappedData(l_Staging) : std::span<std::byte>();
        }

        if (l_Data.size() < size)
        {
            TR_CORE_ERROR("Materials: no staging memory for {} of material records, so they are written next frame", Memory::FormatBytes(size));
            m_Device.DestroyBuffer(l_FromRing ? RHI::BufferHandle{} : l_Staging);
            m_Rewrite = true;

            return;
        }

        std::memcpy(l_Data.data(), m_Records.data() + first, static_cast<std::size_t>(size));
        commands.BufferBarrier(m_Table, m_TableState, RHI::ResourceState::CopyDestination);
        commands.CopyBuffer(l_Staging, l_Base, m_Table, first, size);
        commands.BufferBarrier(m_Table, RHI::ResourceState::CopyDestination, RHI::ResourceState::ShaderResource);
        m_TableState = RHI::ResourceState::ShaderResource;

        if (!l_FromRing)
        {
            m_Device.DestroyBuffer(l_Staging);
        }
    }
}