#include "Trinity/Asset/EnvironmentLoader.hpp"

#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/Assert.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/Project/Project.hpp"

#include <ktx.h>

#include <array>
#include <cstring>
#include <format>
#include <memory>

namespace Trinity
{
    namespace
    {
        // VK_FORMAT_R16G16B16A16_SFLOAT, the only format a cooked environment holds
        constexpr ktx_uint32_t c_VkFormatRGBA16Float = 97;
        constexpr RHI::Format c_Format = RHI::Format::RGBA16Float;
        constexpr std::array<std::string_view, static_cast<std::size_t>(EnvironmentImage::Count)> c_ImageNames{ "specular cubemap", "irradiance cubemap", "BRDF lookup table" };

        struct KtxDeleter
        {
            void operator()(ktxTexture2* texture) const
            {
                ktxTexture2_Destroy(texture);
            }
        };

        std::uint64_t AlignCopyOffset(std::uint64_t offset)
        {
            return (offset + RHI::c_TextureCopyOffsetAlignment - 1) / RHI::c_TextureCopyOffsetAlignment * RHI::c_TextureCopyOffsetAlignment;
        }
    }

    std::string GetCookedEnvironmentPath(UUID id)
    {
        return std::format("{}/Environments/{}.trenv", Project::c_CacheMount, id);
    }

    EnvironmentAsset::~EnvironmentAsset()
    {
        if (m_Loader != nullptr)
        {
            m_Loader->Release(*this);
        }
    }

    EnvironmentLoader::EnvironmentLoader(RHI::Device& device) : m_Device(device)
    {

    }

    EnvironmentLoader::~EnvironmentLoader() = default;

    std::string EnvironmentLoader::GetLoadPath(const AssetRecord& record) const
    {
        return GetCookedEnvironmentPath(record.ID);
    }

    // On a worker. Each image must be square RGBA16Float, the cubemaps with six faces and the lookup table with one, so a damaged or foreign file fails here and not on the GPU
    Expected<Asset*, std::string> EnvironmentLoader::Load(std::span<const std::byte> data) const
    {
        TR_PROFILE_FUNCTION();

        EnvironmentFileHeader l_Header;
        if (data.size() < sizeof(l_Header))
        {
            return Unexpected{ std::string("too small to be a cooked environment") };
        }

        std::memcpy(&l_Header, data.data(), sizeof(l_Header));
        if (l_Header.Magic != EnvironmentFileHeader::c_Magic)
        {
            return Unexpected{ std::string("not a cooked environment") };
        }

        if (l_Header.Version != EnvironmentFileHeader::c_Version)
        {
            return Unexpected{ std::format("version {}, where this build reads {}", l_Header.Version, EnvironmentFileHeader::c_Version) };
        }

        EnvironmentAsset* l_Environment = Memory::New<EnvironmentAsset>(MemoryTag::Assets);
        for (std::size_t it_Image = 0; it_Image < l_Environment->m_Images.size(); ++it_Image)
        {
            const std::uint64_t l_Offset = l_Header.Offsets[it_Image];
            const std::uint64_t l_Size = l_Header.Sizes[it_Image];
            if (l_Offset > data.size() || l_Size > data.size() - l_Offset)
            {
                Memory::Delete(l_Environment);

                return Unexpected{ std::format("its {} lies past the end of the file", c_ImageNames[it_Image]) };
            }

            ktxTexture2* l_Created = nullptr;
            const KTX_error_code l_Read = ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(data.data() + l_Offset), l_Size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &l_Created);
            if (l_Read != KTX_SUCCESS)
            {
                Memory::Delete(l_Environment);

                return Unexpected{ std::format("its {} is not a readable KTX2 file ({})", c_ImageNames[it_Image], ktxErrorString(l_Read)) };
            }

            const std::unique_ptr<ktxTexture2, KtxDeleter> l_Ktx(l_Created);
            const std::uint32_t l_Faces = it_Image == static_cast<std::size_t>(EnvironmentImage::BrdfLookup) ? 1 : 6;
            if (l_Ktx->vkFormat != c_VkFormatRGBA16Float || l_Ktx->numFaces != l_Faces || l_Ktx->numLayers != 1 || l_Ktx->numDimensions != 2 || l_Ktx->baseWidth != l_Ktx->baseHeight || l_Ktx->numLevels == 0)
            {
                Memory::Delete(l_Environment);

                return Unexpected{ std::format("its {} is not a square RGBA16Float image with {} face(s)", c_ImageNames[it_Image], l_Faces) };
            }

            EnvironmentAsset::Image& l_Image = l_Environment->m_Images[it_Image];
            l_Image.Size = l_Ktx->baseWidth;
            l_Image.Levels = l_Ktx->numLevels;
            l_Image.Faces = l_Faces;

            ktxTexture* l_Base = ktxTexture(l_Ktx.get());
            const std::byte* l_Texels = reinterpret_cast<const std::byte*>(ktxTexture_GetData(l_Base));
            for (std::uint32_t it_Level = 0; it_Level < l_Image.Levels; ++it_Level)
            {
                const std::uint32_t l_Width = RHI::GetMipSize(l_Image.Size, it_Level);
                const std::uint64_t l_Expected = std::uint64_t{ l_Width } * l_Width * RHI::GetFormatSize(c_Format);
                for (std::uint32_t it_Face = 0; it_Face < l_Faces; ++it_Face)
                {
                    ktx_size_t l_FaceOffset = 0;
                    if (ktxTexture_GetImageOffset(l_Base, it_Level, 0, it_Face, &l_FaceOffset) != KTX_SUCCESS || ktxTexture_GetImageSize(l_Base, it_Level) != l_Expected)
                    {
                        Memory::Delete(l_Environment);

                        return Unexpected{ std::format("its {} has no {}-byte face {} at mip {}", c_ImageNames[it_Image], l_Expected, it_Face, it_Level) };
                    }

                    l_Image.Data.emplace_back(l_Texels + l_FaceOffset, l_Texels + l_FaceOffset + l_Expected);
                }
            }
        }

        return l_Environment;
    }

    Expected<void, std::string> EnvironmentLoader::Finish(Asset& asset) const
    {
        EnvironmentAsset& l_Environment = static_cast<EnvironmentAsset&>(asset);
        for (std::size_t it_Image = 0; it_Image < l_Environment.m_Images.size(); ++it_Image)
        {
            EnvironmentAsset::Image& l_Image = l_Environment.m_Images[it_Image];

            RHI::TextureDescription l_Description;
            l_Description.Width = l_Image.Size;
            l_Description.Height = l_Image.Size;
            l_Description.MipLevels = l_Image.Levels;
            l_Description.ArrayLayers = l_Image.Faces;
            l_Description.Dimension = l_Image.Faces == 6 ? RHI::TextureDimension::TextureCube : RHI::TextureDimension::Texture2D;
            l_Description.TextureFormat = c_Format;
            l_Description.Usage = RHI::TextureUsage::ShaderResource | RHI::TextureUsage::CopyDestination;
            l_Description.DebugName = std::format("Environment {}", c_ImageNames[it_Image]);

            l_Image.Texture = m_Device.CreateTexture(l_Description);
            if (!l_Image.Texture)
            {
                Release(l_Environment);

                return Unexpected{ std::format("the device could not create its {}x{} {}", l_Image.Size, l_Image.Size, c_ImageNames[it_Image]) };
            }

            l_Image.ShaderResourceIndex = m_Device.GetShaderResourceIndex(l_Image.Texture);
        }

        l_Environment.m_Loader = this;
        m_Pending.push_back(&l_Environment);

        return {};
    }

    // Before any layer draws, so an environment that became ready this frame already holds its texels when it is sampled
    void EnvironmentLoader::RecordUploads(RHI::CommandList& commands)
    {
        TR_PROFILE_FUNCTION();

        for (EnvironmentAsset* it_Environment : m_Pending)
        {
            Upload(commands, *it_Environment);
        }

        m_Pending.clear();
    }

    // An environment released before its upload was recorded is dropped from the queue, and the release queue keeps the GPU textures until frames using them have finished
    void EnvironmentLoader::Release(EnvironmentAsset& environment) const
    {
        TR_CORE_ASSERT(MainThread::IsMainThread(), "Environments are released on the main thread.");

        std::erase(m_Pending, &environment);
        for (EnvironmentAsset::Image& it_Image : environment.m_Images)
        {
            m_Device.DestroyTexture(it_Image.Texture);
            it_Image.Texture = {};
            it_Image.ShaderResourceIndex = RHI::c_NoBindlessIndex;
        }

        environment.m_Loader = nullptr;
    }

    // Each image goes through a staging buffer of its own, destroyed at once and kept by the release queue until the copies have run
    void EnvironmentLoader::Upload(RHI::CommandList& commands, EnvironmentAsset& environment)
    {
        for (std::size_t it_Image = 0; it_Image < environment.m_Images.size(); ++it_Image)
        {
            EnvironmentAsset::Image& l_Image = environment.m_Images[it_Image];
            std::vector<std::uint64_t> l_Offsets(l_Image.Data.size());
            std::uint64_t l_Size = 0;
            for (std::size_t it_Slice = 0; it_Slice < l_Image.Data.size(); ++it_Slice)
            {
                const std::uint32_t l_Width = RHI::GetMipSize(l_Image.Size, static_cast<std::uint32_t>(it_Slice / l_Image.Faces));
                l_Offsets[it_Slice] = AlignCopyOffset(l_Size);
                l_Size = l_Offsets[it_Slice] + RHI::GetTextureCopySize(c_Format, l_Width, l_Width);
            }

            RHI::BufferDescription l_Description;
            l_Description.Size = l_Size;
            l_Description.Memory = RHI::MemoryType::Upload;
            l_Description.DebugName = "Environment staging";

            const RHI::BufferHandle l_Staging = m_Device.CreateBuffer(l_Description);
            const std::span<std::byte> l_Data = l_Staging ? m_Device.GetMappedData(l_Staging) : std::span<std::byte>();
            if (l_Data.size() < l_Size)
            {
                TR_CORE_ERROR("Environments: no staging memory for a {}x{} {}, so it stays empty", l_Image.Size, l_Image.Size, c_ImageNames[it_Image]);
                m_Device.DestroyBuffer(l_Staging);

                continue;
            }

            commands.TextureBarrier(l_Image.Texture, RHI::ResourceState::Undefined, RHI::ResourceState::CopyDestination);
            for (std::size_t it_Slice = 0; it_Slice < l_Image.Data.size(); ++it_Slice)
            {
                const std::uint32_t l_Level = static_cast<std::uint32_t>(it_Slice / l_Image.Faces);
                const std::uint32_t l_Face = static_cast<std::uint32_t>(it_Slice % l_Image.Faces);
                const std::uint32_t l_Width = RHI::GetMipSize(l_Image.Size, l_Level);
                const std::uint64_t l_RowPitch = RHI::GetTextureCopyRowPitch(c_Format, l_Width);
                const std::uint64_t l_RowSize = std::uint64_t{ l_Width } * RHI::GetFormatSize(c_Format);
                for (std::uint32_t it_Row = 0; it_Row < l_Width; ++it_Row)
                {
                    std::memcpy(l_Data.data() + l_Offsets[it_Slice] + it_Row * l_RowPitch, l_Image.Data[it_Slice].data() + it_Row * l_RowSize, static_cast<std::size_t>(l_RowSize));
                }

                commands.CopyBufferToTexture(l_Staging, l_Offsets[it_Slice], l_Image.Texture, l_Level, l_Face, { 0, 0, l_Width, l_Width });
            }

            commands.TextureBarrier(l_Image.Texture, RHI::ResourceState::CopyDestination, RHI::ResourceState::ShaderResource);
            m_Device.DestroyBuffer(l_Staging);

            // The GPU copy is all that is needed from here, and an emptied vector would keep its capacity
            decltype(l_Image.Data)().swap(l_Image.Data);
        }
    }
}