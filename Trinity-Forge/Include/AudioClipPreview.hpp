#pragma once

#include <Trinity.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>

struct ImDrawList;
struct ImVec2;

// One clip at a time, outside any scene, as Unity's Project window plays them. Keeps cooked headers
class AudioClipPreview
{
public:
    AudioClipPreview() = default;
    ~AudioClipPreview();

    AudioClipPreview(const AudioClipPreview&) = delete;
    AudioClipPreview& operator=(const AudioClipPreview&) = delete;

    // In place of any clip playing, from its start once the clip has loaded
    void Play(Trinity::UUID clip);
    void Stop();
    // Once a frame. Headers are read again after a recook, and let go when no panel asked last frame
    void Update(std::uint64_t importCount);
    // Everything let go, as the project closes, so nothing holds a file in its Cache
    void Clear();

    // From Play until the clip ends or is stopped, its load included
    [[nodiscard]] bool IsPlaying(Trinity::UUID clip) const;
    // How far through the clip the preview has got, from 0 to 1, once it has started
    [[nodiscard]] std::optional<float> GetPlayhead(Trinity::UUID clip) const;
    // The clip's cooked header, or null without one that reads, as before its first import
    [[nodiscard]] const Trinity::AudioClipHeader* FindHeader(Trinity::UUID clip);

private:
    struct CachedHeader
    {
        std::optional<Trinity::AudioClipHeader> Header;
        std::uint64_t LastUsed = 0;
    };

    void StartWhenLoaded();

    Trinity::AssetRef<Trinity::AudioClipAsset> m_Clip;
    bool m_Started = false;
    std::unordered_map<Trinity::UUID, CachedHeader> m_Headers;
    std::uint64_t m_ImportCount = 0;
    std::uint64_t m_Frame = 0;
};

// Each column's lowest and highest sample, the channels together or a lane each, and the playhead
void DrawAudioWaveform(ImDrawList& drawList, const Trinity::AudioClipHeader& header, ImVec2 min, ImVec2 max, bool lanes, std::optional<float> playhead);