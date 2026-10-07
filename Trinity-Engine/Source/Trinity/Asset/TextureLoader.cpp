#include "Trinity/Asset/TextureLoader.hpp"

#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/ConsoleVariable.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Project/Project.hpp"

#include <ktx.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <memory>

namespace Trinity
{
    namespace
    {
        ConsoleVariable<bool> s_TextureBC7Variable("renderer.texture_bc7", true, "Transcodes textures to BC7 where the device samples it; off transcodes them to RGBA8");

        // The VkFormat values KTX2 records for textures that need no transcoding
        constexpr ktx_uint32_t c_VkFormatRGBA8Unorm = 37;
        constexpr ktx_uint32_t c_VkFormatRGBA8Srgb = 43;

        constexpr RHI::TextureUsage c_TextureUsage = RHI::TextureUsage::ShaderResource | RHI::TextureUsage::CopyDestination;
        constexpr std::array<std::byte, 4> c_PlaceholderTexel{ std::byte{ 255 }, std::byte{ 255 }, std::byte{ 255 }, std::byte{ 255 } };

        struct KtxDeleter
        {
            void operator()(ktxTexture2* texture) const
            {
                ktxTexture2_Destroy(texture);
            }
        };

        using KtxPointer = std::unique_ptr<ktxTexture2, KtxDeleter>;

        // Bytes in one row of texels, or of blocks, with no padding: the layout KTX2 stores and the importer writes
        std::uint64_t GetTightRowSize(RHI::Format format, std::uint32_t width)
        {
            const std::uint32_t l_Block = RHI::GetFormatBlockDimension(format);

            return std::uint64_t{ (width + l_Block - 1) / l_Block } * RHI::GetFormatSize(format);
        }

        std::uint64_t AlignCopyOffset(std::uint64_t offset)
        {
            return (offset + RHI::c_TextureCopyOffsetAlignment - 1) / RHI::c_TextureCopyOffsetAlignment * RHI::c_TextureCopyOffsetAlignment;
        }
    }

    std::string GetCookedTexturePath(UUID id)
    {
        return std::format("{}/Textures/{}.ktx2", Project::c_CacheMount, id);
    }

    TextureAsset::~TextureAsset()
    {
        if (m_Loader != nullptr)
        {
            m_Loader->Release(*this);
        }
    }

    TextureLoader::TextureLoader(RHI::Device& device) : m_Device(device)
    {
        m_BC7Supported = m_Device.IsFormatSupported(RHI::Format::BC7Unorm, c_TextureUsage) && m_Device.IsFormatSupported(RHI::Format::BC7Srgb, c_TextureUsage);

        m_Placeholder.m_Width = 1;
        m_Placeholder.m_Height = 1;
        m_Placeholder.m_MipLevels = 1;
        m_Placeholder.m_Format = RHI::Format::RGBA8Unorm;
        if (!CreateTexture(m_Placeholder, "Texture placeholder"))
        {
            TR_CORE_ERROR("Textures: the placeholder could not be created, so textures show nothing while they load");
        }
    }

    TextureLoader::~TextureLoader()
    {
        Release(m_Placeholder);
    }

    std::string TextureLoader::GetLoadPath(const AssetRecord& record) const
    {
        return GetCookedTexturePath(record.ID);
    }

    // On a worker. A texture whose transfer function is sRGB gets an sRGB format, so sampling it returns linear values and filters in linear light
    Expected<Asset*, std::string> TextureLoader::Load(std::span<const std::byte> data) const
    {
        TR_PROFILE_FUNCTION();

        ktxTexture2* l_Created = nullptr;
        const KTX_error_code l_Read = ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(data.data()), data.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &l_Created);
        if (l_Read != KTX_SUCCESS)
        {
            return Unexpected{ std::format("not a readable KTX2 file ({})", ktxErrorString(l_Read)) };
        }

        const KtxPointer l_Ktx(l_Created);
        if (l_Ktx->numDimensions != 2 || l_Ktx->numLayers != 1 || l_Ktx->numFaces != 1 || l_Ktx->numLevels == 0)
        {
            return Unexpected{ std::string("only single 2D textures load for now") };
        }

        const std::uint32_t l_Width = l_Ktx->baseWidth;
        const std::uint32_t l_Height = l_Ktx->baseHeight;

        const bool l_Srgb = ktxTexture2_GetTransferFunction_e(l_Ktx.get()) == KHR_DF_TRANSFER_SRGB;
        RHI::Format l_Format = l_Srgb ? RHI::Format::RGBA8Srgb : RHI::Format::RGBA8Unorm;
        if (ktxTexture2_NeedsTranscoding(l_Ktx.get()))
        {
            // D3D12 creates a BC texture only when the top mip is whole blocks
            const bool l_BC7 = m_BC7Supported && s_TextureBC7Variable.Get() && l_Width % 4 == 0 && l_Height % 4 == 0;
            const KTX_error_code l_Transcoded = ktxTexture2_TranscodeBasis(l_Ktx.get(), l_BC7 ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32, 0);
            if (l_Transcoded != KTX_SUCCESS)
            {
                return Unexpected{ std::format("could not be transcoded to {} ({})", l_BC7 ? "BC7" : "RGBA8", ktxErrorString(l_Transcoded)) };
            }

            if (l_BC7)
            {
                l_Format = l_Srgb ? RHI::Format::BC7Srgb : RHI::Format::BC7Unorm;
            }
        }
        else if (l_Ktx->vkFormat != c_VkFormatRGBA8Unorm && l_Ktx->vkFormat != c_VkFormatRGBA8Srgb)
        {
            return Unexpected{ std::format("holds VkFormat {}, and only Basis Universal or RGBA8 textures load", l_Ktx->vkFormat) };
        }

        TextureAsset* l_Texture = Memory::New<TextureAsset>(MemoryTag::Assets);
        l_Texture->m_Width = l_Width;
        l_Texture->m_Height = l_Height;
        l_Texture->m_MipLevels = l_Ktx->numLevels;
        l_Texture->m_Format = l_Format;
        l_Texture->m_Srgb = l_Srgb;

        // The importer records how the texture should be filtered. A file without the key is filtered linearly
        unsigned int l_FilterLength = 0;
        void* l_FilterValue = nullptr;
        const std::string l_FilterKey(c_TextureFilterKey);
        if (ktxHashList_FindValue(&l_Ktx->kvDataHead, l_FilterKey.c_str(), &l_FilterLength, &l_FilterValue) == KTX_SUCCESS && std::string_view(static_cast<const char*>(l_FilterValue), l_FilterLength).starts_with("Nearest"))
        {
            l_Texture->m_Filter = RHI::Filter::Nearest;
        }

        l_Texture->m_Mips.resize(l_Ktx->numLevels);

        ktxTexture* l_Base = ktxTexture(l_Ktx.get());
        const std::byte* l_Data = reinterpret_cast<const std::byte*>(ktxTexture_GetData(l_Base));
        for (std::uint32_t it_Level = 0; it_Level < l_Ktx->numLevels; ++it_Level)
        {
            ktx_size_t l_Offset = 0;
            const KTX_error_code l_Found = ktxTexture_GetImageOffset(l_Base, it_Level, 0, 0, &l_Offset);
            const std::uint64_t l_Expected = GetTightRowSize(l_Format, RHI::GetMipSize(l_Width, it_Level)) * RHI::GetTextureCopyRowCount(l_Format, RHI::GetMipSize(l_Height, it_Level));
            const ktx_size_t l_Size = ktxTexture_GetImageSize(l_Base, it_Level);
            if (l_Found != KTX_SUCCESS || l_Size != l_Expected)
            {
                Memory::Delete(l_Texture);

                return Unexpected{ std::format("mip {} holds {} bytes where {} were expected", it_Level, l_Size, l_Expected) };
            }

            l_Texture->m_Mips[it_Level].assign(l_Data + l_Offset, l_Data + l_Offset + l_Size);
        }

        return l_Texture;
    }

    Expected<void, std::string> TextureLoader::Finish(Asset& asset) const
    {
        TextureAsset& l_Texture = static_cast<TextureAsset&>(asset);
        if (!CreateTexture(l_Texture, "Texture asset"))
        {
            return Unexpected{ std::format("the device could not create a {}x{} {} texture", l_Texture.m_Width, l_Texture.m_Height, RHI::ToString(l_Texture.m_Format)) };
        }

        return {};
    }

    const Asset* TextureLoader::GetPlaceholder() const
    {
        return m_Placeholder.m_Texture ? &m_Placeholder : nullptr;
    }

    // Before any layer draws, so a texture that became ready this frame already holds its mips when it is sampled
    void TextureLoader::RecordUploads(RHI::CommandList& commands)
    {
        TR_PROFILE_FUNCTION();

        for (TextureAsset* it_Texture : m_Pending)
        {
            Upload(commands, *it_Texture);
        }

        m_Pending.clear();
    }

    // A texture released before its upload was recorded is dropped from the queue, and the release queue keeps the GPU texture until frames using it have finished
    void TextureLoader::Release(TextureAsset& texture) const
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "Textures are released on the main thread.");

        std::erase(m_Pending, &texture);
        m_Device.DestroyTexture(texture.m_Texture);
        texture.m_Texture = {};
        texture.m_ShaderResourceIndex = RHI::c_NoBindlessIndex;
        texture.m_Loader = nullptr;
    }

    bool TextureLoader::CreateTexture(TextureAsset& texture, std::string_view debugName) const
    {
        RHI::TextureDescription l_Description;
        l_Description.Width = texture.m_Width;
        l_Description.Height = texture.m_Height;
        l_Description.MipLevels = texture.m_MipLevels;
        l_Description.TextureFormat = texture.m_Format;
        l_Description.Usage = c_TextureUsage;
        l_Description.DebugName = std::string(debugName);

        texture.m_Texture = m_Device.CreateTexture(l_Description);
        if (!texture.m_Texture)
        {
            return false;
        }

        texture.m_ShaderResourceIndex = m_Device.GetShaderResourceIndex(texture.m_Texture);
        texture.m_Loader = this;
        m_Pending.push_back(&texture);

        return true;
    }

    // Every mip goes through one staging allocation: from the upload ring when it is small, otherwise a buffer of its own, destroyed at once and kept by the release queue until the copies have run
    void TextureLoader::Upload(RHI::CommandList& commands, TextureAsset& texture)
    {
        std::vector<std::uint64_t> l_Offsets(texture.m_MipLevels);
        std::uint64_t l_Size = 0;
        for (std::uint32_t it_Level = 0; it_Level < texture.m_MipLevels; ++it_Level)
        {
            l_Offsets[it_Level] = AlignCopyOffset(l_Size);
            l_Size = l_Offsets[it_Level] + RHI::GetTextureCopySize(texture.m_Format, RHI::GetMipSize(texture.m_Width, it_Level), RHI::GetMipSize(texture.m_Height, it_Level));
        }

        RHI::BufferHandle l_Staging;
        std::uint64_t l_Base = 0;
        std::span<std::byte> l_Data;
        const bool l_FromRing = l_Size <= m_Device.GetUploadCapacity() / 4;
        if (l_FromRing)
        {
            const RHI::UploadAllocation l_Allocation = m_Device.AllocateUpload(l_Size, RHI::c_TextureCopyOffsetAlignment);
            l_Staging = l_Allocation.Buffer;
            l_Base = l_Allocation.Offset;
            l_Data = l_Allocation.Data;
        }
        else
        {
            RHI::BufferDescription l_Description;
            l_Description.Size = l_Size;
            l_Description.Memory = RHI::MemoryType::Upload;
            l_Description.DebugName = "Texture staging";

            l_Staging = m_Device.CreateBuffer(l_Description);
            l_Data = l_Staging ? m_Device.GetMappedData(l_Staging) : std::span<std::byte>();
        }

        if (l_Data.size() < l_Size)
        {
            TR_CORE_ERROR("Textures: no staging memory for a {}x{} {} texture, so it stays empty", texture.m_Width, texture.m_Height, RHI::ToString(texture.m_Format));
            m_Device.DestroyBuffer(l_FromRing ? RHI::BufferHandle{} : l_Staging);

            return;
        }

        commands.TextureBarrier(texture.m_Texture, RHI::ResourceState::Undefined, RHI::ResourceState::CopyDestination);
        for (std::uint32_t it_Level = 0; it_Level < texture.m_MipLevels; ++it_Level)
        {
            const std::uint32_t l_Width = RHI::GetMipSize(texture.m_Width, it_Level);
            const std::uint32_t l_Height = RHI::GetMipSize(texture.m_Height, it_Level);
            const std::uint64_t l_RowPitch = RHI::GetTextureCopyRowPitch(texture.m_Format, l_Width);
            const std::uint64_t l_RowSize = GetTightRowSize(texture.m_Format, l_Width);
            const std::uint32_t l_Rows = RHI::GetTextureCopyRowCount(texture.m_Format, l_Height);
            // The placeholder's texel is a constant, so Assets holds nothing until a texture loads
            const std::span<const std::byte> l_Mip = &texture == &m_Placeholder ? std::span<const std::byte>(c_PlaceholderTexel) : std::span<const std::byte>(texture.m_Mips[it_Level]);
            for (std::uint32_t it_Row = 0; it_Row < l_Rows; ++it_Row)
            {
                std::memcpy(l_Data.data() + l_Offsets[it_Level] + it_Row * l_RowPitch, l_Mip.data() + it_Row * l_RowSize, static_cast<std::size_t>(l_RowSize));
            }

            commands.CopyBufferToTexture(l_Staging, l_Base + l_Offsets[it_Level], texture.m_Texture, it_Level, 0, { 0, 0, l_Width, l_Height });
        }

        commands.TextureBarrier(texture.m_Texture, RHI::ResourceState::CopyDestination, RHI::ResourceState::ShaderResource);

        if (!l_FromRing)
        {
            m_Device.DestroyBuffer(l_Staging);
        }

        // The GPU copy is all that is needed from here, and an emptied vector would keep its capacity
        decltype(texture.m_Mips)().swap(texture.m_Mips);
    }
}