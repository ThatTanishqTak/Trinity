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

    // A clip as the importer cooked it: its header from the start, and its samples once they load, which for a clip that preloads is before it reads as ready, so a scene waits for it, and for any other is on a worker after. A clip played before its samples are in starts once they are, and one whose samples fail to load plays as silence. Everything it holds is under the Audio tag
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
        // What its samples hold under Audio: the decoded frames, the file's bytes, or nothing for a clip that streams, whose pages are its voices'
        [[nodiscard]] std::uint64_t GetSampleMemory() const;

        // Shared with every voice playing the clip, which can outlive the asset
        [[nodiscard]] const Ref<AudioClipStorage>& GetStorage() const { return m_Storage; }

    private:
        Ref<AudioClipStorage> m_Storage;
    };
}