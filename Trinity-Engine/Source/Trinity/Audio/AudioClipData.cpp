#include "Trinity/Audio/AudioClipData.hpp"

#include "Trinity/Audio/AudioClipStorage.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Project/Project.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>

namespace Trinity
{
    namespace
    {
        // A 40-byte header, then the waveform, all little-endian
        constexpr std::array<char, 4> c_Magic{ 'T', 'R', 'C', 'L' };
        constexpr std::uint32_t c_Version = 1;
        constexpr std::uint64_t c_HeaderSize = 40;
        constexpr std::uint32_t c_MaxChannels = 32;
        constexpr std::uint32_t c_MaxSampleRate = 384000;
        // Frames decoded at a time while a source is checked
        constexpr std::uint64_t c_AnalysisFrames = 4096;

        template<typename T>
        void Append(std::vector<std::byte>& bytes, const T& value)
        {
            const std::size_t l_Offset = bytes.size();
            bytes.resize(l_Offset + sizeof(T));
            std::memcpy(bytes.data() + l_Offset, &value, sizeof(T));
        }

        template<typename T>
        T Load(std::span<const std::byte> bytes, std::uint64_t offset)
        {
            T l_Value{};
            std::memcpy(&l_Value, bytes.data() + offset, sizeof(T));

            return l_Value;
        }

        std::int8_t ToWaveform(float sample)
        {
            return static_cast<std::int8_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 127.0f));
        }

        // A FLAC file's bits per sample, which miniaudio decodes past without telling, from the STREAMINFO block that comes first after fLaC. An ID3v2 tag ahead of the stream is stepped over
        std::optional<std::uint32_t> GetFlacBitsPerSample(std::span<const std::byte> source)
        {
            const auto a_Byte = [&source](std::size_t index) { return std::to_integer<std::uint32_t>(source[index]); };

            std::size_t l_Start = 0;
            if (source.size() >= 10 && std::memcmp(source.data(), "ID3", 3) == 0)
            {
                l_Start = 10 + (((a_Byte(6) & 0x7F) << 21) | ((a_Byte(7) & 0x7F) << 14) | ((a_Byte(8) & 0x7F) << 7) | (a_Byte(9) & 0x7F));
            }

            // Bits per sample less one are 5 bits across STREAMINFO's 13th and 14th bytes, after the 4-byte block header
            if (source.size() < l_Start + 22 || std::memcmp(source.data() + l_Start, "fLaC", 4) != 0 || (a_Byte(l_Start + 4) & 0x7F) != 0)
            {
                return std::nullopt;
            }

            return (((a_Byte(l_Start + 20) & 0x01) << 4) | (a_Byte(l_Start + 21) >> 4)) + 1;
        }

        // A decoder of a file in memory that closes itself, for checking the file from start to end
        class ScopedDecoder
        {
        public:
            ScopedDecoder() = default;

            ~ScopedDecoder()
            {
                if (m_Open)
                {
                    ma_decoder_uninit(&m_Decoder);
                }
            }

            ScopedDecoder(const ScopedDecoder&) = delete;
            ScopedDecoder& operator=(const ScopedDecoder&) = delete;

            [[nodiscard]] ma_result Open(std::span<const std::byte> source, AudioEncoding encoding, ma_format format)
            {
                ma_decoder_config l_Config = ma_decoder_config_init(format, 0, 0);
                l_Config.encodingFormat = ToMiniaudio(encoding);
                l_Config.allocationCallbacks = GetAudioAllocationCallbacks();

                const ma_result l_Result = ma_decoder_init_memory(source.data(), source.size(), &l_Config, &m_Decoder);
                m_Open = l_Result == MA_SUCCESS;

                return l_Result;
            }

            [[nodiscard]] ma_decoder& Get() { return m_Decoder; }

        private:
            ma_decoder m_Decoder{};
            bool m_Open = false;
        };

        // Every frame of the source as 32-bit float, a piece at a time, each piece with the frame it starts at. The frames decoded, or why it stopped short of the end
        template<typename Function>
        Expected<std::uint64_t, std::string> DecodeAll(std::span<const std::byte> source, AudioEncoding encoding, std::uint32_t channels, const Function& onFrames)
        {
            ScopedDecoder l_Decoder;
            if (const ma_result l_Opened = l_Decoder.Open(source, encoding, ma_format_f32); l_Opened != MA_SUCCESS)
            {
                return Unexpected{ std::format("it could not be decoded ({})", ma_result_description(l_Opened)) };
            }

            std::vector<float, TaggedAllocator<float, MemoryTag::Audio>> l_Buffer(c_AnalysisFrames * channels);
            std::uint64_t l_Total = 0;
            for (;;)
            {
                ma_uint64 l_Read = 0;
                const ma_result l_Result = ma_decoder_read_pcm_frames(&l_Decoder.Get(), l_Buffer.data(), c_AnalysisFrames, &l_Read);
                if (l_Read > 0)
                {
                    onFrames(std::span<const float>(l_Buffer.data(), l_Read * channels), l_Total);
                    l_Total += l_Read;
                }

                if (l_Result == MA_AT_END || (l_Result == MA_SUCCESS && l_Read == 0))
                {
                    return l_Total;
                }

                if (l_Result != MA_SUCCESS)
                {
                    return Unexpected{ std::format("it stops decoding at frame {} ({})", l_Total, ma_result_description(l_Result)) };
                }
            }
        }

        // miniaudio's decoder reads a clip's file through these, as a file system holding the one file
        ma_result OnViewOpen(ma_vfs* vfs, const char*, ma_uint32, ma_vfs_file* file)
        {
            *file = vfs;

            return MA_SUCCESS;
        }

        ma_result OnViewOpenWide(ma_vfs*, const wchar_t*, ma_uint32, ma_vfs_file*)
        {
            return MA_NOT_IMPLEMENTED;
        }

        ma_result OnViewClose(ma_vfs*, ma_vfs_file)
        {
            return MA_SUCCESS;
        }

        ma_result OnViewRead(ma_vfs*, ma_vfs_file file, void* destination, size_t size, size_t* bytesRead)
        {
            AudioFileView& l_View = *static_cast<AudioFileView*>(file);
            *bytesRead = 0;

            const Expected<std::size_t, FileError> l_Read = l_View.File->Read(l_View.Offset, std::span(static_cast<std::byte*>(destination), size));
            if (!l_Read)
            {
                return MA_IO_ERROR;
            }

            l_View.Offset += *l_Read;
            *bytesRead = *l_Read;

            return *l_Read == 0 && size != 0 ? MA_AT_END : MA_SUCCESS;
        }

        ma_result OnViewWrite(ma_vfs*, ma_vfs_file, const void*, size_t, size_t*)
        {
            return MA_NOT_IMPLEMENTED;
        }

        ma_result OnViewSeek(ma_vfs*, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
        {
            AudioFileView& l_View = *static_cast<AudioFileView*>(file);
            const std::int64_t l_Size = static_cast<std::int64_t>(l_View.File->GetSize());

            std::int64_t l_Base = l_Size;
            if (origin == ma_seek_origin_start)
            {
                l_Base = 0;
            }
            else if (origin == ma_seek_origin_current)
            {
                l_Base = static_cast<std::int64_t>(l_View.Offset);
            }

            const std::int64_t l_Target = l_Base + offset;
            if (l_Target < 0 || l_Target > l_Size)
            {
                return MA_BAD_SEEK;
            }

            l_View.Offset = static_cast<std::uint64_t>(l_Target);

            return MA_SUCCESS;
        }

        ma_result OnViewTell(ma_vfs*, ma_vfs_file file, ma_int64* cursor)
        {
            *cursor = static_cast<ma_int64>(static_cast<AudioFileView*>(file)->Offset);

            return MA_SUCCESS;
        }

        ma_result OnViewInfo(ma_vfs*, ma_vfs_file file, ma_file_info* info)
        {
            info->sizeInBytes = static_cast<AudioFileView*>(file)->File->GetSize();

            return MA_SUCCESS;
        }

        // The file's own channels and rate are asked for, so the decoder converts nothing but the sample format
        ma_decoder_config GetDecoderConfig(const AudioClipHeader& header)
        {
            ma_decoder_config l_Config = ma_decoder_config_init(ToMiniaudio(header.Format), header.Channels, header.SampleRate);
            l_Config.encodingFormat = ToMiniaudio(header.Encoding);
            l_Config.allocationCallbacks = GetAudioAllocationCallbacks();

            return l_Config;
        }
    }

    std::string_view ToString(AudioLoadType type)
    {
        switch (type)
        {
            case AudioLoadType::DecompressOnLoad:
            {
                return "Decompress On Load";
            }
            case AudioLoadType::CompressedInMemory:
            {
                return "Compressed In Memory";
            }
            case AudioLoadType::Streaming:
            {
                return "Streaming";
            }
        }

        return "unknown";
    }

    std::string_view ToString(AudioEncoding encoding)
    {
        switch (encoding)
        {
            case AudioEncoding::Wav:
            {
                return "WAV";
            }
            case AudioEncoding::Flac:
            {
                return "FLAC";
            }
            case AudioEncoding::Mp3:
            {
                return "MP3";
            }
            case AudioEncoding::Vorbis:
            {
                return "OGG Vorbis";
            }
        }

        return "unknown";
    }

    std::string_view ToString(AudioSampleFormat format)
    {
        return format == AudioSampleFormat::Int16 ? "16-bit" : "32-bit float";
    }

    std::uint32_t GetSampleSize(AudioSampleFormat format)
    {
        return format == AudioSampleFormat::Int16 ? 2 : 4;
    }

    ma_format ToMiniaudio(AudioSampleFormat format)
    {
        return format == AudioSampleFormat::Int16 ? ma_format_s16 : ma_format_f32;
    }

    ma_encoding_format ToMiniaudio(AudioEncoding encoding)
    {
        switch (encoding)
        {
            case AudioEncoding::Wav:
            {
                return ma_encoding_format_wav;
            }
            case AudioEncoding::Flac:
            {
                return ma_encoding_format_flac;
            }
            case AudioEncoding::Mp3:
            {
                return ma_encoding_format_mp3;
            }
            case AudioEncoding::Vorbis:
            {
                return ma_encoding_format_vorbis;
            }
        }

        return ma_encoding_format_unknown;
    }

    std::string GetCookedAudioClipPath(UUID id)
    {
        return std::format("{}/Audio/{}.trclip", Project::c_CacheMount, id);
    }

    std::string GetCookedAudioDataPath(UUID id, AudioEncoding encoding)
    {
        constexpr std::array<std::string_view, 4> c_Extensions{ "wav", "flac", "mp3", "ogg" };

        return std::format("{}/Audio/{}.{}", Project::c_CacheMount, id, c_Extensions[static_cast<std::size_t>(encoding) % c_Extensions.size()]);
    }

    std::vector<std::byte> WriteAudioClipHeader(const AudioClipHeader& header)
    {
        std::vector<std::byte> l_File;
        l_File.reserve(static_cast<std::size_t>(c_HeaderSize) + header.Waveform.size());
        Append(l_File, c_Magic);
        Append(l_File, c_Version);
        Append(l_File, header.ID.GetValue());
        Append(l_File, static_cast<std::uint8_t>(header.Encoding));
        Append(l_File, static_cast<std::uint8_t>(header.Format));
        Append(l_File, static_cast<std::uint8_t>(header.LoadType));
        Append(l_File, static_cast<std::uint8_t>(header.Preload ? 1 : 0));
        Append(l_File, header.Channels);
        Append(l_File, header.SampleRate);
        Append(l_File, header.WaveformColumns);
        Append(l_File, header.Frames);

        for (const std::int8_t it_Value : header.Waveform)
        {
            l_File.push_back(static_cast<std::byte>(it_Value));
        }

        return l_File;
    }

    Expected<AudioClipHeader, std::string> ReadAudioClipHeader(std::span<const std::byte> file)
    {
        if (file.size() < c_HeaderSize || std::memcmp(file.data(), c_Magic.data(), c_Magic.size()) != 0)
        {
            return Unexpected{ std::string("not a cooked audio clip") };
        }

        const std::uint32_t l_Version = Load<std::uint32_t>(file, 4);
        if (l_Version != c_Version)
        {
            return Unexpected{ std::format("audio clip format {}, where this build reads {}", l_Version, c_Version) };
        }

        const std::uint8_t l_Encoding = Load<std::uint8_t>(file, 16);
        const std::uint8_t l_Format = Load<std::uint8_t>(file, 17);
        const std::uint8_t l_LoadType = Load<std::uint8_t>(file, 18);
        const std::uint8_t l_Preload = Load<std::uint8_t>(file, 19);
        if (l_Encoding > static_cast<std::uint8_t>(AudioEncoding::Vorbis) || l_Format > static_cast<std::uint8_t>(AudioSampleFormat::Float32) || l_LoadType > static_cast<std::uint8_t>(AudioLoadType::Streaming) || l_Preload > 1)
        {
            return Unexpected{ std::format("encoding {}, format {}, load type {} and preload {} are not a clip this build reads", l_Encoding, l_Format, l_LoadType, l_Preload) };
        }

        AudioClipHeader l_Header;
        l_Header.ID = UUID(Load<std::uint64_t>(file, 8));
        l_Header.Encoding = static_cast<AudioEncoding>(l_Encoding);
        l_Header.Format = static_cast<AudioSampleFormat>(l_Format);
        l_Header.LoadType = static_cast<AudioLoadType>(l_LoadType);
        l_Header.Preload = l_Preload == 1;
        l_Header.Channels = Load<std::uint32_t>(file, 20);
        l_Header.SampleRate = Load<std::uint32_t>(file, 24);
        l_Header.WaveformColumns = Load<std::uint32_t>(file, 28);
        l_Header.Frames = Load<std::uint64_t>(file, 32);
        if (l_Header.Channels == 0 || l_Header.Channels > c_MaxChannels || l_Header.SampleRate == 0 || l_Header.SampleRate > c_MaxSampleRate || l_Header.WaveformColumns > AudioClipHeader::c_WaveformColumns)
        {
            return Unexpected{ std::format("{} channel(s) at {} Hz with {} waveform columns is not a clip this build reads", l_Header.Channels, l_Header.SampleRate, l_Header.WaveformColumns) };
        }

        const std::uint64_t l_WaveformSize = std::uint64_t{ l_Header.WaveformColumns } * l_Header.Channels * 2;
        if (file.size() != c_HeaderSize + l_WaveformSize)
        {
            return Unexpected{ std::format("{} bytes, where its header and waveform take {}", file.size(), c_HeaderSize + l_WaveformSize) };
        }

        l_Header.Waveform.resize(static_cast<std::size_t>(l_WaveformSize));
        std::memcpy(l_Header.Waveform.data(), file.data() + c_HeaderSize, l_Header.Waveform.size());

        return l_Header;
    }

    // MP3 is tried last, since its decoder will find frames in almost anything
    Expected<AudioClipHeader, std::string> AnalyzeAudio(std::span<const std::byte> source)
    {
        TR_PROFILE_FUNCTION();

        constexpr std::array<AudioEncoding, 4> c_Encodings{ AudioEncoding::Wav, AudioEncoding::Flac, AudioEncoding::Vorbis, AudioEncoding::Mp3 };

        AudioClipHeader l_Header;
        ma_format l_Native = ma_format_unknown;
        bool l_Found = false;
        for (const AudioEncoding it_Encoding : c_Encodings)
        {
            ScopedDecoder l_Decoder;
            if (l_Decoder.Open(source, it_Encoding, ma_format_unknown) == MA_SUCCESS && ma_decoder_get_data_format(&l_Decoder.Get(), &l_Native, &l_Header.Channels, &l_Header.SampleRate, nullptr, 0) == MA_SUCCESS)
            {
                l_Header.Encoding = it_Encoding;
                l_Found = true;

                break;
            }
        }

        if (!l_Found)
        {
            return Unexpected{ std::string("it is not a WAV, FLAC, MP3 or OGG Vorbis file that can be decoded") };
        }

        if (l_Header.Channels == 0 || l_Header.Channels > c_MaxChannels || l_Header.SampleRate == 0 || l_Header.SampleRate > c_MaxSampleRate)
        {
            return Unexpected{ std::format("its {} channel(s) at {} Hz are more than a clip takes", l_Header.Channels, l_Header.SampleRate) };
        }

        // miniaudio decodes every FLAC as float, so a FLAC's own depth is read from the file
        if (l_Header.Encoding == AudioEncoding::Flac)
        {
            const std::optional<std::uint32_t> l_Bits = GetFlacBitsPerSample(source);
            l_Header.Format = l_Bits && *l_Bits <= 16 ? AudioSampleFormat::Int16 : AudioSampleFormat::Float32;
        }
        else
        {
            l_Header.Format = l_Native == ma_format_u8 || l_Native == ma_format_s16 ? AudioSampleFormat::Int16 : AudioSampleFormat::Float32;
        }

        // Once to count its frames, which checks every one of them, and again for the waveform, as the length some decoders report before decoding is only a guess
        const Expected<std::uint64_t, std::string> l_Frames = DecodeAll(source, l_Header.Encoding, l_Header.Channels, [](std::span<const float>, std::uint64_t) {});
        if (!l_Frames)
        {
            return Unexpected{ l_Frames.GetError() };
        }

        if (*l_Frames == 0)
        {
            return Unexpected{ std::string("it holds no samples") };
        }

        l_Header.Frames = *l_Frames;
        l_Header.WaveformColumns = static_cast<std::uint32_t>(std::min<std::uint64_t>(AudioClipHeader::c_WaveformColumns, l_Header.Frames));

        const std::size_t l_Channels = l_Header.Channels;
        const std::size_t l_Cells = std::size_t{ l_Header.WaveformColumns } * l_Channels;
        std::vector<float, TaggedAllocator<float, MemoryTag::Audio>> l_Low(l_Cells, 0.0f);
        std::vector<float, TaggedAllocator<float, MemoryTag::Audio>> l_High(l_Cells, 0.0f);
        const Expected<std::uint64_t, std::string> l_Drawn = DecodeAll(source, l_Header.Encoding, l_Header.Channels, [&](std::span<const float> samples, std::uint64_t first)
        {
            const std::size_t l_Count = samples.size() / l_Channels;
            for (std::size_t it_Frame = 0; it_Frame < l_Count; ++it_Frame)
            {
                const std::uint64_t l_Frame = std::min(first + it_Frame, l_Header.Frames - 1);
                const std::size_t l_Column = static_cast<std::size_t>(l_Frame * l_Header.WaveformColumns / l_Header.Frames);
                for (std::size_t it_Channel = 0; it_Channel < l_Channels; ++it_Channel)
                {
                    const float l_Sample = samples[it_Frame * l_Channels + it_Channel];
                    const std::size_t l_Cell = l_Column * l_Channels + it_Channel;
                    l_Low[l_Cell] = std::min(l_Low[l_Cell], l_Sample);
                    l_High[l_Cell] = std::max(l_High[l_Cell], l_Sample);
                }
            }
        });

        if (!l_Drawn)
        {
            return Unexpected{ l_Drawn.GetError() };
        }

        l_Header.Waveform.reserve(l_Cells * 2);
        for (std::size_t it_Cell = 0; it_Cell < l_Cells; ++it_Cell)
        {
            l_Header.Waveform.push_back(ToWaveform(l_Low[it_Cell]));
            l_Header.Waveform.push_back(ToWaveform(l_High[it_Cell]));
        }

        return l_Header;
    }

    ma_result OpenClipDecoder(const AudioClipHeader& header, const FileReadHandle& file, AudioFileView& view, ma_decoder& decoder)
    {
        view.Callbacks = { &OnViewOpen, &OnViewOpenWide, &OnViewClose, &OnViewRead, &OnViewWrite, &OnViewSeek, &OnViewTell, &OnViewInfo };
        view.File = &file;
        view.Offset = 0;

        const ma_decoder_config l_Config = GetDecoderConfig(header);

        return ma_decoder_init_vfs(&view, "clip", &l_Config, &decoder);
    }

    ma_result OpenClipDecoder(const AudioClipHeader& header, std::span<const std::byte> file, ma_decoder& decoder)
    {
        const ma_decoder_config l_Config = GetDecoderConfig(header);

        return ma_decoder_init_memory(file.data(), file.size(), &l_Config, &decoder);
    }
}