#include "Trinity/Audio/AudioClipAsset.hpp"

#include "Trinity/Asset/AssetManager.hpp"
#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Audio/AudioClipStorage.hpp"
#include "Trinity/Audio/AudioLoaders.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Profiler.hpp"

#include <format>
#include <limits>
#include <utility>

namespace Trinity
{
    namespace
    {
        Ref<AudioClipStorage> MakeStorage()
        {
            return std::allocate_shared<AudioClipStorage>(TaggedAllocator<AudioClipStorage, MemoryTag::Audio>());
        }

        // What a clip reads as while it loads or after it fails: no frames, so it plays as silence
        Ref<AudioClipStorage> MakeSilence()
        {
            Ref<AudioClipStorage> l_Storage = MakeStorage();
            l_Storage->Header.Channels = 1;
            l_Storage->Header.SampleRate = 48000;
            l_Storage->Status.store(AudioClipStatus::Loaded, std::memory_order_release);

            return l_Storage;
        }

        // On a worker: the samples as the clip's load type holds them, from the file the importer wrote beside the header. A streaming clip only opens the file, and each of its voices reads it in turn
        Expected<void, std::string> LoadSamples(AudioClipStorage& storage)
        {
            TR_PROFILE_FUNCTION();

            const AudioClipHeader& l_Header = storage.Header;
            Expected<Ref<FileReadHandle>, FileError> l_File = FileSystem::OpenFile(GetCookedAudioDataPath(l_Header.ID, l_Header.Encoding));
            if (!l_File)
            {
                return Unexpected{ std::format("its samples could not be opened: {}", ToString(l_File.GetError())) };
            }

            // Decoded from the start here, so a file that cannot be decoded fails as the clip loads rather than as it first plays
            if (l_Header.LoadType == AudioLoadType::Streaming)
            {
                AudioFileView l_View;
                ma_decoder l_Decoder{};
                const ma_result l_Opened = OpenClipDecoder(l_Header, **l_File, l_View, l_Decoder);
                if (l_Opened != MA_SUCCESS)
                {
                    return Unexpected{ std::format("its samples could not be decoded ({})", ma_result_description(l_Opened)) };
                }

                ma_decoder_uninit(&l_Decoder);
                storage.File = std::move(*l_File);

                return {};
            }

            const std::uint64_t l_Size = (*l_File)->GetSize();
            if (static_cast<std::uintmax_t>(l_Size) > std::numeric_limits<std::size_t>::max())
            {
                return Unexpected{ std::string("its samples are too large to hold in memory") };
            }

            AudioBytes l_Bytes(static_cast<std::size_t>(l_Size));
            const Expected<std::size_t, FileError> l_Read = (*l_File)->Read(0, l_Bytes);
            if (!l_Read || *l_Read != l_Bytes.size())
            {
                return Unexpected{ std::format("its samples could not be read: {}", l_Read ? std::string_view("the file ended early") : ToString(l_Read.GetError())) };
            }

            ma_decoder l_Decoder{};
            const ma_result l_Opened = OpenClipDecoder(l_Header, l_Bytes, l_Decoder);
            if (l_Opened != MA_SUCCESS)
            {
                return Unexpected{ std::format("its samples could not be decoded ({})", ma_result_description(l_Opened)) };
            }

            if (l_Header.LoadType == AudioLoadType::CompressedInMemory)
            {
                ma_decoder_uninit(&l_Decoder);
                storage.Samples = std::move(l_Bytes);

                return {};
            }

            // Decompress On Load: all of it at once, in the format the importer chose, and the file's bytes let go after
            const std::uint64_t l_FrameSize = std::uint64_t{ l_Header.Channels } * GetSampleSize(l_Header.Format);
            storage.Samples.resize(static_cast<std::size_t>(l_Header.Frames * l_FrameSize));

            ma_uint64 l_Frames = 0;
            const ma_result l_Decoded = ma_decoder_read_pcm_frames(&l_Decoder, storage.Samples.data(), l_Header.Frames, &l_Frames);
            ma_decoder_uninit(&l_Decoder);
            if ((l_Decoded != MA_SUCCESS && l_Decoded != MA_AT_END) || l_Frames == 0)
            {
                storage.Samples = AudioBytes();

                return Unexpected{ std::format("its samples stop decoding at frame {} of {} ({})", l_Frames, l_Header.Frames, ma_result_description(l_Decoded)) };
            }

            storage.Samples.resize(static_cast<std::size_t>(l_Frames * l_FrameSize));

            return {};
        }

        // Reads a clip's header on a worker, and its samples there too for a clip that preloads, or for any other on a job of the loader's own once the asset reads as ready
        class AudioClipLoader final : public AssetLoader
        {
        public:
            AudioClipLoader() : m_Placeholder(MakeSilence())
            {

            }

            // A clip still loading in the background holds only its storage, which outlives the loader if it must
            ~AudioClipLoader() override
            {
                JobSystem::Wait(m_Jobs);
            }

            AudioClipLoader(const AudioClipLoader&) = delete;
            AudioClipLoader& operator=(const AudioClipLoader&) = delete;

            [[nodiscard]] std::string_view GetAssetType() const override
            {
                return AudioClipAsset::c_AssetType;
            }

            [[nodiscard]] std::string GetLoadPath(const AssetRecord& record) const override
            {
                return GetCookedAudioClipPath(record.ID);
            }

            [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override
            {
                TR_PROFILE_FUNCTION();

                Expected<AudioClipHeader, std::string> l_Header = ReadAudioClipHeader(data);
                if (!l_Header)
                {
                    return Unexpected{ l_Header.GetError() };
                }

                Ref<AudioClipStorage> l_Storage = MakeStorage();
                l_Storage->Header = std::move(*l_Header);
                if (l_Storage->Header.Preload)
                {
                    const Expected<void, std::string> l_Loaded = LoadSamples(*l_Storage);
                    if (!l_Loaded)
                    {
                        return Unexpected{ l_Loaded.GetError() };
                    }

                    l_Storage->Status.store(AudioClipStatus::Loaded, std::memory_order_release);
                }
                else
                {
                    JobSystem::Submit([l_Storage]
                    {
                        const Expected<void, std::string> l_Loaded = LoadSamples(*l_Storage);
                        if (!l_Loaded)
                        {
                            TR_CORE_ERROR("Audio: {} could not be loaded: {}, so it plays as silence", l_Storage->Header.ID, l_Loaded.GetError());
                        }

                        l_Storage->Status.store(l_Loaded ? AudioClipStatus::Loaded : AudioClipStatus::Failed, std::memory_order_release);
                    }, &m_Jobs);
                }

                return Memory::New<AudioClipAsset>(MemoryTag::Audio, std::move(l_Storage));
            }

            [[nodiscard]] const Asset* GetPlaceholder() const override
            {
                return &m_Placeholder;
            }

        private:
            AudioClipAsset m_Placeholder;
            // Load is const like the rest of the loader interface, and only counts its jobs here
            mutable JobCounter m_Jobs;
        };

        AudioClipLoader* s_Loader = nullptr;
    }

    AudioClipAsset::AudioClipAsset(Ref<AudioClipStorage> storage) : m_Storage(std::move(storage))
    {

    }

    const AudioClipHeader& AudioClipAsset::GetHeader() const
    {
        return m_Storage->Header;
    }

    double AudioClipAsset::GetLength() const
    {
        const AudioClipHeader& l_Header = m_Storage->Header;

        return l_Header.SampleRate != 0 ? static_cast<double>(l_Header.Frames) / l_Header.SampleRate : 0.0;
    }

    bool AudioClipAsset::IsLoaded() const
    {
        return m_Storage->Status.load(std::memory_order_acquire) == AudioClipStatus::Loaded;
    }

    bool AudioClipAsset::HasFailed() const
    {
        return m_Storage->Status.load(std::memory_order_acquire) == AudioClipStatus::Failed;
    }

    // Read once loaded, since a load in the background writes the samples until then
    std::uint64_t AudioClipAsset::GetSampleMemory() const
    {
        return IsLoaded() ? m_Storage->Samples.size() : 0;
    }

    namespace AudioLoaders
    {
        void Register()
        {
            TR_CORE_ASSERT(s_Loader == nullptr, "The audio loaders are registered once.");

            s_Loader = Memory::New<AudioClipLoader>(MemoryTag::Engine);
            AssetManager::RegisterLoader(*s_Loader);
        }

        void Unregister()
        {
            if (s_Loader == nullptr)
            {
                return;
            }

            AssetManager::UnregisterLoader(s_Loader->GetAssetType());
            Memory::Delete(s_Loader);
            s_Loader = nullptr;
        }
    }
}