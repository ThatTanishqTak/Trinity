#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Asset/EnvironmentAsset.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/RHI/Device.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // A worker reads the three KTX2 images, the main thread creates them, the next frame uploads
    class EnvironmentLoader final : public AssetLoader
    {
    public:
        explicit EnvironmentLoader(RHI::Device& device);
        ~EnvironmentLoader() override;

        EnvironmentLoader(const EnvironmentLoader&) = delete;
        EnvironmentLoader& operator=(const EnvironmentLoader&) = delete;

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return EnvironmentAsset::c_AssetType;
        }

        [[nodiscard]] std::string GetLoadPath(const AssetRecord& record) const override;
        [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override;
        [[nodiscard]] Expected<void, std::string> Finish(Asset& asset) const override;

        void RecordUploads(RHI::CommandList& commands);
        void Release(EnvironmentAsset& environment) const;

    private:
        void Upload(RHI::CommandList& commands, EnvironmentAsset& environment);

        RHI::Device& m_Device;

        // Finish and Release are const as the loader interface has them, and run on the main thread only
        mutable std::vector<EnvironmentAsset*, TaggedAllocator<EnvironmentAsset*, MemoryTag::Engine>> m_Pending;
    };
}