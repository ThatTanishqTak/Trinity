#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Asset/TextureAsset.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Device.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // A worker transcodes to BC7, or BC5 for a normal map, where the device samples it, else RGBA8
    class TextureLoader final : public AssetLoader
    {
    public:
        explicit TextureLoader(RHI::Device& device);
        ~TextureLoader() override;

        TextureLoader(const TextureLoader&) = delete;
        TextureLoader& operator=(const TextureLoader&) = delete;

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return TextureAsset::c_AssetType;
        }

        [[nodiscard]] std::string GetLoadPath(const AssetRecord& record) const override;
        [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override;
        [[nodiscard]] Expected<void, std::string> Finish(Asset& asset) const override;
        [[nodiscard]] const Asset* GetPlaceholder() const override;

        void RecordUploads(RHI::CommandList& commands);
        void Release(TextureAsset& texture) const;

    private:
        [[nodiscard]] bool CreateTexture(TextureAsset& texture, std::string_view debugName) const;
        void Upload(RHI::CommandList& commands, TextureAsset& texture);

        RHI::Device& m_Device;
        bool m_BC7Supported = false;
        bool m_BC5Supported = false;
        TextureAsset m_Placeholder;

        // Finish and Release are const as the loader interface has them, and run on the main thread only
        mutable std::vector<TextureAsset*, TaggedAllocator<TextureAsset*, MemoryTag::Engine>> m_Pending;
    };
}