#pragma once

#include "Trinity/Core/Expected.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/Core/UUID.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Trinity
{
    // How a clip's samples are held while it plays, as Unity's Load Type
    enum class AudioLoadType : std::uint8_t
    {
        // Decoded into memory as it loads, which costs the most memory and nothing to play
        DecompressOnLoad,
        // The file's bytes in memory, decoded on the audio thread as it plays
        CompressedInMemory,
        // Read from the file and decoded on workers a little ahead as it plays, for long music and ambience
        Streaming
    };

    enum class AudioEncoding : std::uint8_t
    {
        Wav,
        Flac,
        Mp3,
        Vorbis
    };

    // What a clip decodes into: 16-bit for a source of 16 bits or fewer, which takes half the memory, and 32-bit float for any other, so nothing is lost either way
    enum class AudioSampleFormat : std::uint8_t
    {
        Int16,
        Float32
    };

    [[nodiscard]] TRINITY_API std::string_view ToString(AudioLoadType type);
    [[nodiscard]] TRINITY_API std::string_view ToString(AudioEncoding encoding);
    [[nodiscard]] TRINITY_API std::string_view ToString(AudioSampleFormat format);
    [[nodiscard]] TRINITY_API std::uint32_t GetSampleSize(AudioSampleFormat format);

    // What the importer writes beside a clip's source bytes in /cache, which is all a game reads of a clip before it plays it
    struct AudioClipHeader
    {
        static constexpr std::uint32_t c_WaveformColumns = 512;

        UUID ID;
        AudioEncoding Encoding = AudioEncoding::Wav;
        AudioSampleFormat Format = AudioSampleFormat::Float32;
        AudioLoadType LoadType = AudioLoadType::DecompressOnLoad;
        bool Preload = true;
        std::uint32_t Channels = 0;
        std::uint32_t SampleRate = 0;
        std::uint64_t Frames = 0;
        std::uint32_t WaveformColumns = 0;
        // For each column, then each channel, its lowest and its highest sample, from -127 to 127
        std::vector<std::int8_t, TaggedAllocator<std::int8_t, MemoryTag::Audio>> Waveform;
    };

    [[nodiscard]] TRINITY_API std::string GetCookedAudioClipPath(UUID id);
    // The source file's bytes as they were, beside the header
    [[nodiscard]] TRINITY_API std::string GetCookedAudioDataPath(UUID id, AudioEncoding encoding);

    [[nodiscard]] TRINITY_API std::vector<std::byte> WriteAudioClipHeader(const AudioClipHeader& header);
    [[nodiscard]] TRINITY_API Expected<AudioClipHeader, std::string> ReadAudioClipHeader(std::span<const std::byte> file);

    // Decodes the whole of a WAV, FLAC, MP3 or OGG Vorbis file, which checks every frame of it, and gives its encoding, the format it decodes into, its channels, rate, length and waveform. The ID, load type and preload are left for the importer
    [[nodiscard]] TRINITY_API Expected<AudioClipHeader, std::string> AnalyzeAudio(std::span<const std::byte> source);
}