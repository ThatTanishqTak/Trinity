#pragma once

#include "Trinity/Asset/Asset.hpp"
#include "Trinity/Audio/AudioClipData.hpp"
#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Export.hpp"

#include <cstdint>
#include <string_view>

namespace Trinity
{
    struct AudioClipStorage;

    // Its header loads first. Samples load before it reads as ready only for a clip that preloads
    class TRINITY_API AudioClipAsset final : public Asset
    {
    public:
        static constexpr std::string_view c_AssetType = "AudioClip";

        explicit AudioClipAsset(Ref<AudioClipStorage> storage);

        [[nodiscard]] std::string_view GetAssetType() const override
        {
            return c_AssetType;
        }

        [[nodiscard]] const AudioClipHeader& GetHeader() const;
        // In seconds
        [[nodiscard]] double GetLength() const;
        [[nodiscard]] bool IsLoaded() const;
        [[nodiscard]] bool HasFailed() const;
        // Under Audio: the decoded frames, the file's bytes, or nothing for a clip that streams
        [[nodiscard]] std::uint64_t GetSampleMemory() const;

        // Shared with every voice playing the clip, which can outlive the asset
        [[nodiscard]] const Ref<AudioClipStorage>& GetStorage() const { return m_Storage; }

    private:
        Ref<AudioClipStorage> m_Storage;
    };
}