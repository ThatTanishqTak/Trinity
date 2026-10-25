#pragma once

#include "Trinity/Audio/AudioClipData.hpp"
#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/JobSystem.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"

#include <miniaudio.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Trinity
{
    using AudioBytes = std::vector<std::byte, TaggedAllocator<std::byte, MemoryTag::Audio>>;

    // Everything miniaudio allocates goes through these, under the Audio tag. Defined in AudioEngine.cpp
    [[nodiscard]] ma_allocation_callbacks GetAudioAllocationCallbacks();

    [[nodiscard]] ma_format ToMiniaudio(AudioSampleFormat format);
    [[nodiscard]] ma_encoding_format ToMiniaudio(AudioEncoding encoding);

    enum class AudioClipStatus : std::uint8_t
    {
        Loading,
        Loaded,
        Failed
    };

    // A clip's samples as its load type holds them, shared by its asset and every voice playing it, so a voice can outlive the asset and a load in the background can outlive both
    struct AudioClipStorage
    {
        AudioClipHeader Header;
        // Decompress On Load: the decoded frames, in the header's format. Compressed In Memory: the file's bytes
        AudioBytes Samples;
        // Streaming: the file, which each voice's stream reads its own way through
        Ref<FileReadHandle> File;
        std::atomic<AudioClipStatus> Status{ AudioClipStatus::Loading };
    };

    // A clip's file as miniaudio's decoder reads it, through a read handle, from a position of its own. Its callbacks come first, so miniaudio takes it for a file system with one file in it
    struct AudioFileView
    {
        ma_vfs_callbacks Callbacks;
        const FileReadHandle* File = nullptr;
        std::uint64_t Offset = 0;
    };

    // A decoder for the clip's file into the header's format, at the file's own channels and rate. The view must outlive the decoder
    [[nodiscard]] ma_result OpenClipDecoder(const AudioClipHeader& header, const FileReadHandle& file, AudioFileView& view, ma_decoder& decoder);
    // A decoder for the file's bytes in memory, likewise
    [[nodiscard]] ma_result OpenClipDecoder(const AudioClipHeader& header, std::span<const std::byte> file, ma_decoder& decoder);

    // A streaming clip's samples for one voice: pages decoded ahead on workers from the clip's file, which the audio thread plays through without waiting, locking or allocating. The main thread starts each refill, since a job's submission can allocate, and a page not decoded in time plays as silence, the stream waiting on it rather than skipping ahead. It loops by itself, so a loop never waits on a seek
    class AudioStream
    {
    public:
        static constexpr std::uint32_t c_PageCount = 4;
        static constexpr std::uint64_t c_PageFrames = 16384;

        explicit AudioStream(Ref<AudioClipStorage> storage);
        ~AudioStream();

        AudioStream(const AudioStream&) = delete;
        AudioStream& operator=(const AudioStream&) = delete;

        [[nodiscard]] ma_data_source* GetDataSource() { return &m_Source.Base; }
        // Whether the first page is in, so a voice can start without a gap
        [[nodiscard]] bool IsPrimed() const;
        // Main thread, once a frame: a refill on a worker when a page is free or a seek waits
        void RequestRefill();
        // For offline mixing: every free page filled on this thread, so a render gives the same samples each time
        void RefillNow();

    private:
        enum class PageState : std::uint32_t
        {
            Free,
            Ready
        };

        // Written by the refill while free, and read by the audio thread once ready
        struct Page
        {
            AudioBytes Samples;
            std::atomic<PageState> State{ PageState::Free };
            std::uint64_t Frames = 0;
            std::uint64_t StartFrame = 0;
            bool End = false;
        };

        // What miniaudio holds, its data source first, so the pointer it hands back finds the stream
        struct Source
        {
            ma_data_source_base Base;
            AudioStream* Stream;
        };

        static ma_result OnRead(ma_data_source* source, void* frames, ma_uint64 frameCount, ma_uint64* framesRead);
        static ma_result OnSeek(ma_data_source* source, ma_uint64 frame);
        static ma_result OnGetDataFormat(ma_data_source* source, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate, ma_channel* channelMap, size_t channelMapCapacity);
        static ma_result OnGetCursor(ma_data_source* source, ma_uint64* cursor);
        static ma_result OnGetLength(ma_data_source* source, ma_uint64* length);
        static ma_result OnSetLooping(ma_data_source* source, ma_bool32 looping);

        [[nodiscard]] bool NeedsRefill() const;
        void Refill();
        void FillPage(Page& page);

        Ref<AudioClipStorage> m_Storage;
        Source m_Source{};
        std::array<Page, c_PageCount> m_Pages;
        std::uint64_t m_FrameSize = 0;

        // The audio thread's own, apart from the cursor, which anyone reads
        std::uint32_t m_ReadPage = 0;
        std::uint64_t m_ReadOffset = 0;
        std::uint32_t m_SeekSeen = 0;
        bool m_Ended = false;
        std::atomic<std::uint64_t> m_Cursor{ 0 };
        // A seek from the audio thread, which the refill carries out, the stream playing silence until it has
        std::atomic<std::uint64_t> m_SeekTarget{ 0 };
        std::atomic<std::uint32_t> m_SeekRequested{ 0 };
        std::atomic<std::uint32_t> m_SeekDone{ 0 };
        std::atomic<bool> m_Looping{ false };

        // The refill's own, one refill at a time
        AudioFileView m_View{};
        ma_decoder m_Decoder{};
        bool m_DecoderOpen = false;
        bool m_DecoderFailed = false;
        std::uint32_t m_FillPage = 0;
        std::uint64_t m_DecodeFrame = 0;
        std::atomic<bool> m_AtEnd{ false };
        std::atomic<bool> m_Refilling{ false };
        JobCounter m_Jobs;
    };

    // One playing instance of a clip as a data source of miniaudio's, with a position of its own, over a view of the decoded samples, a decoder of the bytes in memory, or a stream of the file. It always gives 32-bit float, turning 16-bit samples into it as it reads, since miniaudio 0.11.25's engine drops the last frames before the end of a source in any other format. Started and stopped on the main thread
    class AudioClipVoice
    {
    public:
        AudioClipVoice() = default;
        ~AudioClipVoice();

        AudioClipVoice(const AudioClipVoice&) = delete;
        AudioClipVoice& operator=(const AudioClipVoice&) = delete;

        // Once the clip's samples are loaded. False for a clip that failed, which plays nothing
        [[nodiscard]] bool Start(Ref<AudioClipStorage> storage);
        // Once no sound reads it any more
        void Stop();

        [[nodiscard]] bool IsStarted() const { return m_Started; }
        // Null until started
        [[nodiscard]] ma_data_source* GetDataSource() { return m_Started ? &m_Source.Base : nullptr; }
        // Null unless the clip streams
        [[nodiscard]] AudioStream* GetStream() { return m_Stream; }

    private:
        // What miniaudio holds, its data source first, so the pointer it hands back finds the voice
        struct Source
        {
            ma_data_source_base Base;
            AudioClipVoice* Voice;
        };

        static ma_result OnRead(ma_data_source* source, void* frames, ma_uint64 frameCount, ma_uint64* framesRead);
        static ma_result OnSeek(ma_data_source* source, ma_uint64 frame);
        static ma_result OnGetDataFormat(ma_data_source* source, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate, ma_channel* channelMap, size_t channelMapCapacity);
        static ma_result OnGetCursor(ma_data_source* source, ma_uint64* cursor);
        static ma_result OnGetLength(ma_data_source* source, ma_uint64* length);
        static ma_result OnSetLooping(ma_data_source* source, ma_bool32 looping);

        // What the voice reads from, in the clip's own format
        [[nodiscard]] ma_data_source* GetInner();

        Ref<AudioClipStorage> m_Storage;
        Source m_Source{};
        ma_audio_buffer_ref m_Buffer{};
        ma_decoder m_Decoder{};
        AudioStream* m_Stream = nullptr;
        bool m_Started = false;
    };
}