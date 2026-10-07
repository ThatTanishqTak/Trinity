#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Asset/MeshAsset.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Device.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // Loads the mesh files cooked into /cache. A worker checks the file and copies out the GPU buffer's bytes, the main thread creates the buffer, and the renderer uploads it at the start of the next frame. A mesh has no placeholder, so it draws nothing until it is ready
    class MeshLoader final : public AssetLoader
    {
    public:
        explicit MeshLoader(RHI::Device& device);

        MeshLoader(const MeshLoader&) = delete;
        MeshLoader& operator=(const MeshLoader&) = delete;

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return MeshAsset::c_AssetType;
        }

        [[nodiscard]] std::string GetLoadPath(const AssetRecord& record) const override;
        [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override;
        [[nodiscard]] Expected<void, std::string> Finish(Asset& asset) const override;

        void RecordUploads(RHI::CommandList& commands);
        void Release(MeshAsset& mesh) const;

    private:
        void Upload(RHI::CommandList& commands, MeshAsset& mesh);

        RHI::Device& m_Device;

        // Finish and Release are const like the rest of the loader interface, and only ever run on the main thread
        mutable std::vector<MeshAsset*, TaggedAllocator<MeshAsset*, MemoryTag::Engine>> m_Pending;
    };
}