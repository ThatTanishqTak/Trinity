#include "Trinity/Audio/AudioClipStorage.hpp"

#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Profiler.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace Trinity
{
    AudioStream::AudioStream(Ref<AudioClipStorage> storage) : m_Storage(std::move(storage))
    {
        // Looping is the stream's own, so a loop never waits on a seek
        static const ma_data_source_vtable s_VTable{ &OnRead, &OnSeek, &OnGetDataFormat, &OnGetCursor, &OnGetLength, &OnSetLooping, MA_DATA_SOURCE_SELF_MANAGED_RANGE_AND_LOOP_POINT };

        ma_data_source_config l_Config = ma_data_source_config_init();
        l_Config.vtable = &s_VTable;
        static_cast<void>(ma_data_source_init(&l_Config, &m_Source.Base));
        m_Source.Stream = this;

        const AudioClipHeader& l_Header = m_Storage->Header;
        m_FrameSize = std::uint64_t{ l_Header.Channels } * GetSampleSize(l_Header.Format);
        for (Page& it_Page : m_Pages)
        {
            it_Page.Samples.resize(static_cast<std::size_t>(c_PageFrames * m_FrameSize));
        }
    }

    AudioStream::~AudioStream()
    {
        JobSystem::Wait(m_Jobs);

        if (m_DecoderOpen)
        {
            ma_decoder_uninit(&m_Decoder);
        }

        ma_data_source_uninit(&m_Source.Base);
    }

    bool AudioStream::IsPrimed() const
    {
        return m_Pages[0].State.load(std::memory_order_acquire) == PageState::Ready;
    }

    bool AudioStream::NeedsRefill() const
    {
        if (m_SeekRequested.load(std::memory_order_acquire) != m_SeekDone.load(std::memory_order_acquire))
        {
            return true;
        }

        return !m_AtEnd.load(std::memory_order_acquire) && std::ranges::any_of(m_Pages, [](const Page& page) { return page.State.load(std::memory_order_acquire) == PageState::Free; });
    }

    void AudioStream::RequestRefill()
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "Streams are refilled from the main thread.");

        if (!NeedsRefill() || m_Refilling.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        JobSystem::Submit([this]
        {
            Refill();
            m_Refilling.store(false, std::memory_order_release);
        }, &m_Jobs);
    }

    void AudioStream::RefillNow()
    {
        JobSystem::Wait(m_Jobs);
        Refill();
    }

    // The decoder opens on the first refill, on a worker, as it reads the start of the file. A seek empties every page and starts again from it, so the audio thread, which plays silence meanwhile, finds the new pages from the first
    void AudioStream::Refill()
    {
        TR_PROFILE_FUNCTION();

        const AudioClipHeader& l_Header = m_Storage->Header;
        if (!m_DecoderOpen && !m_DecoderFailed)
        {
            const ma_result l_Opened = m_Storage->File != nullptr ? OpenClipDecoder(l_Header, *m_Storage->File, m_View, m_Decoder) : MA_INVALID_FILE;
            m_DecoderOpen = l_Opened == MA_SUCCESS;
            m_DecoderFailed = !m_DecoderOpen;
            if (m_DecoderFailed)
            {
                TR_CORE_ERROR("Audio: the stream of {} could not start ({}), so it plays as silence", l_Header.ID, ma_result_description(l_Opened));
            }
        }

        for (;;)
        {
            const std::uint32_t l_Requested = m_SeekRequested.load(std::memory_order_acquire);
            if (l_Requested != m_SeekDone.load(std::memory_order_relaxed))
            {
                const std::uint64_t l_Target = std::min(m_SeekTarget.load(std::memory_order_relaxed), l_Header.Frames);
                if (m_DecoderOpen && ma_decoder_seek_to_pcm_frame(&m_Decoder, l_Target) != MA_SUCCESS)
                {
                    TR_CORE_ERROR("Audio: the stream of {} could not seek to frame {}", l_Header.ID, l_Target);
                }

                for (Page& it_Page : m_Pages)
                {
                    it_Page.State.store(PageState::Free, std::memory_order_relaxed);
                }

                m_DecodeFrame = l_Target;
                m_FillPage = 0;
                m_AtEnd.store(false, std::memory_order_relaxed);
                m_SeekDone.store(l_Requested, std::memory_order_release);
            }

            Page& l_Page = m_Pages[m_FillPage];
            if (m_AtEnd.load(std::memory_order_relaxed) || l_Page.State.load(std::memory_order_acquire) != PageState::Free)
            {
                return;
            }

            FillPage(l_Page);
            m_FillPage = (m_FillPage + 1) % c_PageCount;
        }
    }

    // A page ends short where the clip does, and a looping stream starts the clip again on the next page, so each page's start frame is where it lies in the clip
    void AudioStream::FillPage(Page& page)
    {
        const AudioClipHeader& l_Header = m_Storage->Header;

        ma_uint64 l_Read = 0;
        bool l_End = !m_DecoderOpen;
        bool l_Looped = false;
        if (m_DecoderOpen)
        {
            const ma_result l_Result = ma_decoder_read_pcm_frames(&m_Decoder, page.Samples.data(), c_PageFrames, &l_Read);
            if (l_Result != MA_SUCCESS && l_Result != MA_AT_END)
            {
                TR_CORE_ERROR("Audio: the stream of {} stopped decoding at frame {} ({})", l_Header.ID, m_DecodeFrame + l_Read, ma_result_description(l_Result));
                l_End = true;
            }
            else if (l_Read < c_PageFrames)
            {
                // A clip with nothing to loop through ends rather than filling pages with nothing
                l_Looped = m_Looping.load(std::memory_order_acquire) && (l_Read != 0 || m_DecodeFrame != 0) && ma_decoder_seek_to_pcm_frame(&m_Decoder, 0) == MA_SUCCESS;
                l_End = !l_Looped;
            }
        }

        page.Frames = l_Read;
        page.StartFrame = m_DecodeFrame;
        page.End = l_End;
        m_DecodeFrame = l_Looped ? 0 : m_DecodeFrame + l_Read;
        page.State.store(PageState::Ready, std::memory_order_release);

        if (l_End)
        {
            m_AtEnd.store(true, std::memory_order_release);
        }
    }

    // On the audio thread. It always fills what it was asked for, with silence where a page is late or a seek waits, until the last page has played
    ma_result AudioStream::OnRead(ma_data_source* source, void* frames, ma_uint64 frameCount, ma_uint64* framesRead)
    {
        AudioStream& l_Stream = *reinterpret_cast<Source*>(source)->Stream;
        std::byte* l_Output = static_cast<std::byte*>(frames);

        const std::uint32_t l_Requested = l_Stream.m_SeekRequested.load(std::memory_order_relaxed);
        const std::uint32_t l_Done = l_Stream.m_SeekDone.load(std::memory_order_acquire);
        std::uint64_t l_Produced = 0;
        bool l_AtEnd = false;
        if (l_Done == l_Requested)
        {
            if (l_Stream.m_SeekSeen != l_Done)
            {
                l_Stream.m_SeekSeen = l_Done;
                l_Stream.m_ReadPage = 0;
                l_Stream.m_ReadOffset = 0;
            }

            while (l_Produced < frameCount && !l_Stream.m_Ended)
            {
                Page& l_Page = l_Stream.m_Pages[l_Stream.m_ReadPage];
                if (l_Page.State.load(std::memory_order_acquire) != PageState::Ready)
                {
                    break;
                }

                const std::uint64_t l_Count = std::min<std::uint64_t>(frameCount - l_Produced, l_Page.Frames - l_Stream.m_ReadOffset);
                if (l_Output != nullptr && l_Count != 0)
                {
                    std::memcpy(l_Output + l_Produced * l_Stream.m_FrameSize, l_Page.Samples.data() + l_Stream.m_ReadOffset * l_Stream.m_FrameSize, static_cast<std::size_t>(l_Count * l_Stream.m_FrameSize));
                }

                l_Produced += l_Count;
                l_Stream.m_ReadOffset += l_Count;
                l_Stream.m_Cursor.store(l_Page.StartFrame + l_Stream.m_ReadOffset, std::memory_order_relaxed);
                if (l_Stream.m_ReadOffset == l_Page.Frames)
                {
                    l_Stream.m_Ended = l_Page.End;
                    l_Stream.m_ReadOffset = 0;
                    l_Page.State.store(PageState::Free, std::memory_order_release);
                    l_Stream.m_ReadPage = (l_Stream.m_ReadPage + 1) % c_PageCount;
                }
            }

            l_AtEnd = l_Stream.m_Ended;
        }

        if (!l_AtEnd && l_Produced < frameCount && l_Output != nullptr)
        {
            std::memset(l_Output + l_Produced * l_Stream.m_FrameSize, 0, static_cast<std::size_t>((frameCount - l_Produced) * l_Stream.m_FrameSize));
        }

        *framesRead = l_AtEnd ? l_Produced : frameCount;

        return l_AtEnd ? MA_AT_END : MA_SUCCESS;
    }

    // On the audio thread while the sound plays, or before it starts. The refill carries the seek out
    ma_result AudioStream::OnSeek(ma_data_source* source, ma_uint64 frame)
    {
        AudioStream& l_Stream = *reinterpret_cast<Source*>(source)->Stream;
        l_Stream.m_SeekTarget.store(frame, std::memory_order_relaxed);
        l_Stream.m_SeekRequested.store(l_Stream.m_SeekRequested.load(std::memory_order_relaxed) + 1, std::memory_order_release);
        l_Stream.m_Cursor.store(frame, std::memory_order_relaxed);
        l_Stream.m_Ended = false;

        return MA_SUCCESS;
    }

    ma_result AudioStream::OnGetDataFormat(ma_data_source* source, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate, ma_channel* channelMap, size_t channelMapCapacity)
    {
        const AudioClipHeader& l_Header = reinterpret_cast<Source*>(source)->Stream->m_Storage->Header;
        *format = ToMiniaudio(l_Header.Format);
        *channels = l_Header.Channels;
        *sampleRate = l_Header.SampleRate;
        ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCapacity, l_Header.Channels);

        return MA_SUCCESS;
    }

    ma_result AudioStream::OnGetCursor(ma_data_source* source, ma_uint64* cursor)
    {
        *cursor = reinterpret_cast<Source*>(source)->Stream->m_Cursor.load(std::memory_order_relaxed);

        return MA_SUCCESS;
    }

    ma_result AudioStream::OnGetLength(ma_data_source* source, ma_uint64* length)
    {
        *length = reinterpret_cast<Source*>(source)->Stream->m_Storage->Header.Frames;

        return MA_SUCCESS;
    }

    ma_result AudioStream::OnSetLooping(ma_data_source* source, ma_bool32 looping)
    {
        reinterpret_cast<Source*>(source)->Stream->m_Looping.store(looping == MA_TRUE, std::memory_order_release);

        return MA_SUCCESS;
    }

    AudioClipVoice::~AudioClipVoice()
    {
        Stop();
    }

    bool AudioClipVoice::Start(Ref<AudioClipStorage> storage)
    {
        TR_CORE_ASSERT(!m_Started, "A voice starts once, and is stopped before it starts again.");

        if (storage == nullptr || storage->Status.load(std::memory_order_acquire) != AudioClipStatus::Loaded)
        {
            return false;
        }

        m_Storage = std::move(storage);
        const AudioClipHeader& l_Header = m_Storage->Header;
        bool l_Inner = false;
        switch (l_Header.LoadType)
        {
            case AudioLoadType::DecompressOnLoad:
            {
                const std::uint64_t l_Frames = m_Storage->Samples.size() / (std::uint64_t{ l_Header.Channels } * GetSampleSize(l_Header.Format));
                l_Inner = ma_audio_buffer_ref_init(ToMiniaudio(l_Header.Format), l_Header.Channels, m_Storage->Samples.data(), l_Frames, &m_Buffer) == MA_SUCCESS;
                // miniaudio leaves the rate for its version 0.12 to set, and takes 0 as the engine's
                m_Buffer.sampleRate = l_Header.SampleRate;

                break;
            }
            case AudioLoadType::CompressedInMemory:
            {
                l_Inner = OpenClipDecoder(l_Header, m_Storage->Samples, m_Decoder) == MA_SUCCESS;

                break;
            }
            case AudioLoadType::Streaming:
            {
                m_Stream = Memory::New<AudioStream>(MemoryTag::Audio, m_Storage);
                l_Inner = true;

                break;
            }
        }

        if (!l_Inner)
        {
            TR_CORE_ERROR("Audio: {} could not start playing", l_Header.ID);
            m_Storage.reset();

            return false;
        }

        // What it reads loops by itself, so the voice passes looping on to it
        static const ma_data_source_vtable s_VTable{ &OnRead, &OnSeek, &OnGetDataFormat, &OnGetCursor, &OnGetLength, &OnSetLooping, MA_DATA_SOURCE_SELF_MANAGED_RANGE_AND_LOOP_POINT };

        ma_data_source_config l_Config = ma_data_source_config_init();
        l_Config.vtable = &s_VTable;
        static_cast<void>(ma_data_source_init(&l_Config, &m_Source.Base));
        m_Source.Voice = this;
        m_Started = true;

        return true;
    }

    void AudioClipVoice::Stop()
    {
        if (!m_Started)
        {
            return;
        }

        ma_data_source_uninit(&m_Source.Base);
        switch (m_Storage->Header.LoadType)
        {
            case AudioLoadType::DecompressOnLoad:
            {
                ma_audio_buffer_ref_uninit(&m_Buffer);

                break;
            }
            case AudioLoadType::CompressedInMemory:
            {
                ma_decoder_uninit(&m_Decoder);

                break;
            }
            case AudioLoadType::Streaming:
            {
                Memory::Delete(m_Stream);
                m_Stream = nullptr;

                break;
            }
        }

        m_Storage.reset();
        m_Started = false;
    }

    ma_data_source* AudioClipVoice::GetInner()
    {
        switch (m_Storage->Header.LoadType)
        {
            case AudioLoadType::DecompressOnLoad:
            {
                return &m_Buffer;
            }
            case AudioLoadType::CompressedInMemory:
            {
                return &m_Decoder;
            }
            case AudioLoadType::Streaming:
            {
                return m_Stream->GetDataSource();
            }
        }

        return nullptr;
    }

    // On the audio thread. A clip in 16-bit goes through a buffer on the stack a piece at a time, turned into float as the engine itself would turn it
    ma_result AudioClipVoice::OnRead(ma_data_source* source, void* frames, ma_uint64 frameCount, ma_uint64* framesRead)
    {
        AudioClipVoice& l_Voice = *reinterpret_cast<Source*>(source)->Voice;
        const AudioClipHeader& l_Header = l_Voice.m_Storage->Header;
        ma_data_source* l_Inner = l_Voice.GetInner();
        if (l_Header.Format == AudioSampleFormat::Float32 || frames == nullptr)
        {
            return ma_data_source_read_pcm_frames(l_Inner, frames, frameCount, framesRead);
        }

        std::array<std::int16_t, 2048> l_Samples;
        const std::uint64_t l_Capacity = l_Samples.size() / l_Header.Channels;
        float* l_Output = static_cast<float*>(frames);
        std::uint64_t l_Total = 0;
        ma_result l_Result = MA_SUCCESS;
        while (l_Total < frameCount)
        {
            const std::uint64_t l_Chunk = std::min<std::uint64_t>(frameCount - l_Total, l_Capacity);
            ma_uint64 l_Read = 0;
            l_Result = ma_data_source_read_pcm_frames(l_Inner, l_Samples.data(), l_Chunk, &l_Read);
            ma_pcm_s16_to_f32(l_Output + l_Total * l_Header.Channels, l_Samples.data(), l_Read * l_Header.Channels, ma_dither_mode_none);
            l_Total += l_Read;
            if (l_Result != MA_SUCCESS || l_Read < l_Chunk)
            {
                break;
            }
        }

        *framesRead = l_Total;

        return l_Total == 0 && l_Result != MA_SUCCESS ? l_Result : MA_SUCCESS;
    }

    ma_result AudioClipVoice::OnSeek(ma_data_source* source, ma_uint64 frame)
    {
        return ma_data_source_seek_to_pcm_frame(reinterpret_cast<Source*>(source)->Voice->GetInner(), frame);
    }

    ma_result AudioClipVoice::OnGetDataFormat(ma_data_source* source, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate, ma_channel* channelMap, size_t channelMapCapacity)
    {
        const AudioClipHeader& l_Header = reinterpret_cast<Source*>(source)->Voice->m_Storage->Header;
        *format = ma_format_f32;
        *channels = l_Header.Channels;
        *sampleRate = l_Header.SampleRate;
        ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCapacity, l_Header.Channels);

        return MA_SUCCESS;
    }

    ma_result AudioClipVoice::OnGetCursor(ma_data_source* source, ma_uint64* cursor)
    {
        return ma_data_source_get_cursor_in_pcm_frames(reinterpret_cast<Source*>(source)->Voice->GetInner(), cursor);
    }

    // The header's, since some decoders cannot tell a length without reading to the end
    ma_result AudioClipVoice::OnGetLength(ma_data_source* source, ma_uint64* length)
    {
        *length = reinterpret_cast<Source*>(source)->Voice->m_Storage->Header.Frames;

        return MA_SUCCESS;
    }

    ma_result AudioClipVoice::OnSetLooping(ma_data_source* source, ma_bool32 looping)
    {
        return ma_data_source_set_looping(reinterpret_cast<Source*>(source)->Voice->GetInner(), looping);
    }
}