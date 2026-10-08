#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/MaterialAsset.hpp"
#include "Trinity/Asset/TextureAsset.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Device.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // Loads material files into one GPU table of fixed-size records, read by shaders through its bindless index as Material.slang lays them out. A worker parses the file, the main thread gives it a record and loads its textures, and each frame every record is brought up to date with its textures' bindless indices before any layer draws, so an edit or a texture that finished loading shows that frame
    class TRINITY_API MaterialLoader final : public AssetLoader
    {
    public:
        // Base colour, then emissive and its strength, then metallic, roughness, normal scale and occlusion strength, then the alpha cutoff, the flags and the five textures, in slot order, with a word of padding
        static constexpr std::uint64_t c_RecordSize = 80;
        static constexpr std::uint32_t c_DefaultIndex = 0;
        static constexpr std::uint32_t c_InitialCapacity = 256;
        // A texture is its bindless index, with the nearest filter in the top bit, or this for an empty slot or one still loading
        static constexpr std::uint32_t c_NoTexture = 0xFFFFFFFFu;
        static constexpr std::uint32_t c_NearestBit = 0x80000000u;
        // The alpha mode in the two lowest bits of the flags, then double-sided, and from bit 8 one bit for each texture slot sampled with the second UV set
        static constexpr std::uint32_t c_AlphaModeMask = 0x3u;
        static constexpr std::uint32_t c_DoubleSidedBit = 0x4u;
        static constexpr std::uint32_t c_TexCoordShift = 8;
        static constexpr std::size_t c_TextureSlots = 5;

        using Record = std::array<std::byte, c_RecordSize>;
        using TextureIndices = std::array<std::uint32_t, c_TextureSlots>;

        explicit MaterialLoader(RHI::Device& device);
        ~MaterialLoader() override;

        MaterialLoader(const MaterialLoader&) = delete;
        MaterialLoader& operator=(const MaterialLoader&) = delete;

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return MaterialAsset::c_AssetType;
        }

        [[nodiscard]] std::string GetLoadPath(const AssetRecord& record) const override;
        [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override;
        [[nodiscard]] Expected<void, std::string> Finish(Asset& asset) const override;

        [[nodiscard]] const Asset* GetPlaceholder() const override
        {
            return &m_Default;
        }

        // A loaded material takes the data at once, as an editor's edit does, without its file being read again. False when it is not loaded
        bool Apply(UUID id, const MaterialData& data);

        void RecordUploads(RHI::CommandList& commands);
        void Release(MaterialAsset& material) const;

        // Recreated larger when the records outgrow it, so its handle and bindless index are read each frame
        [[nodiscard]] RHI::BufferHandle GetTable() const { return m_Table; }
        [[nodiscard]] std::uint32_t GetTableShaderResourceIndex() const { return m_TableIndex; }
        [[nodiscard]] std::uint32_t GetCapacity() const { return m_Capacity; }
        // What the table holds once this frame's uploads have run, a record for each index up to the capacity
        [[nodiscard]] std::span<const std::byte> GetRecords() const { return m_Records; }

        [[nodiscard]] static Record Pack(const MaterialData& material, const TextureIndices& textures);
        // Each slot's texture as a record names it, once that texture has loaded
        [[nodiscard]] static TextureIndices ResolveTextures(const MaterialData& material);

    private:
        using TextureRefs = std::array<AssetRef<TextureAsset>, c_TextureSlots>;

        // A record's material and the textures it keeps loaded, which outlive the material until the next frame, as it can go while the asset manager is busy with its entries
        struct Slot
        {
            MaterialAsset* Material = nullptr;
            TextureRefs Textures;
        };

        [[nodiscard]] static TextureRefs AcquireTextures(const MaterialData& data);
        [[nodiscard]] bool EnsureCapacity();
        void Upload(RHI::CommandList& commands, std::uint64_t first, std::uint64_t size);

        RHI::Device& m_Device;
        MaterialAsset m_Default;

        // Finish and Release are const like the rest of the loader interface, and only ever run on the main thread
        mutable std::vector<Slot, TaggedAllocator<Slot, MemoryTag::Renderer>> m_Slots;
        mutable std::vector<std::uint32_t, TaggedAllocator<std::uint32_t, MemoryTag::Renderer>> m_FreeSlots;
        mutable std::vector<TextureRefs, TaggedAllocator<TextureRefs, MemoryTag::Renderer>> m_Released;

        std::vector<std::byte, TaggedAllocator<std::byte, MemoryTag::Renderer>> m_Records;
        RHI::BufferHandle m_Table;
        std::uint32_t m_TableIndex = RHI::c_NoBindlessIndex;
        std::uint32_t m_Capacity = 0;
        RHI::ResourceState m_TableState = RHI::ResourceState::Undefined;
        bool m_Rewrite = true;
    };
}