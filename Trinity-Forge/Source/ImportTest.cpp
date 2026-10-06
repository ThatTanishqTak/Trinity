#include "ImportTest.hpp"

#include "EditorSession.hpp"
#include "Importers/TextureImporter.hpp"

#include <ktx.h>
#include <stb_image.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <system_error>

namespace
{
    constexpr std::string_view c_TestMount = "/forge-tests";
    constexpr std::string_view c_TestFolder = "Textures";
    constexpr std::string_view c_BC7Variable = "renderer.texture_bc7";

    // UASTC at level 2 keeps the test images between 41 and 46 dB, so a drop below this means a broken encode or transcode, not a lossy setting
    constexpr double c_MinimumPsnr = 38.0;
    constexpr std::uint64_t c_LoadTimeoutFrames = 600;

    // Frames a loaded set is kept, so the renderer records its uploads before it is released
    constexpr std::uint64_t c_HeldFrames = 2;

    struct KtxDeleter
    {
        void operator()(ktxTexture2* texture) const
        {
            ktxTexture2_Destroy(texture);
        }
    };

    struct StbDeleter
    {
        void operator()(stbi_uc* pixels) const
        {
            stbi_image_free(pixels);
        }
    };

    // Over the colour channels, and alpha as well when the source has it
    double ComputePsnr(const stbi_uc* source, const ktx_uint8_t* decoded, std::size_t texels, int channels)
    {
        double l_Sum = 0.0;
        for (std::size_t it_Texel = 0; it_Texel < texels; ++it_Texel)
        {
            for (int it_Channel = 0; it_Channel < channels; ++it_Channel)
            {
                const double l_Difference = static_cast<double>(source[it_Texel * 4 + static_cast<std::size_t>(it_Channel)]) - static_cast<double>(decoded[it_Texel * 4 + static_cast<std::size_t>(it_Channel)]);
                l_Sum += l_Difference * l_Difference;
            }
        }

        const double l_MeanSquared = l_Sum / static_cast<double>(texels * static_cast<std::size_t>(channels));

        return l_MeanSquared == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / l_MeanSquared);
    }

    bool IsTestTexture(const Trinity::AssetRecord& record)
    {
        return record.Importer == TextureImporter::c_Importer && record.Path.starts_with(std::format("{}/{}/", Trinity::Project::c_AssetsMount, c_TestFolder));
    }
}

// The project in the folder is opened, or created when there is none, so a second run finds the first run's cache
void ImportTest::Start(const std::filesystem::path& directory)
{
    TR_INFO("Import test: project in {}", directory.string());

    if (!OpenProject(directory))
    {
        TR_ERROR("Import test: no project could be opened or created in {}", directory.string());

        return;
    }

    const std::size_t l_Copied = CopyTestImages();
    m_Session.ScanAssets();
    const TextureImportReport l_First = m_Session.WaitForImports();
    m_Session.ScanAssets();
    const TextureImportReport l_Second = m_Session.WaitForImports();

    TR_INFO("Import test: {} image(s) copied in, {} texture(s): {} encoded and {} from the cache, then {} from the cache on a second pass", l_Copied, l_First.Textures, l_First.Encoded, l_First.Cached, l_Second.Cached);
    if (l_First.Textures == 0 || l_First.Failed != 0 || l_Second.Encoded != 0 || l_Second.Failed != 0 || l_Second.Cached != l_Second.Textures)
    {
        TR_ERROR("Import test: every texture should import, and a second pass should find all of them in the cache");

        return;
    }

    CheckQuality();
    BeginLoads(Phase::LoadingBC7);
}

void ImportTest::Update()
{
    if (m_Phase == Phase::Idle)
    {
        return;
    }

    ++m_PhaseFrames;

    const bool l_Failed = std::ranges::any_of(m_Textures, [](const auto& texture) { return texture.GetState() == Trinity::AssetState::Failed; });
    const bool l_Ready = std::ranges::all_of(m_Textures, [](const auto& texture) { return texture.IsReady(); });
    if (l_Failed || (!l_Ready && m_PhaseFrames > c_LoadTimeoutFrames))
    {
        TR_ERROR("Import test: the textures {} after {} frame(s)", l_Failed ? "failed to load" : "were still loading", m_PhaseFrames);
        m_Textures.clear();
        m_Phase = Phase::Idle;
        static_cast<void>(Trinity::ConsoleVariables::Set(c_BC7Variable, "true"));

        return;
    }

    if (l_Ready && ++m_ReadyFrames > c_HeldFrames)
    {
        FinishLoads();
    }
}

bool ImportTest::OpenProject(const std::filesystem::path& directory)
{
    std::error_code l_Error;
    for (const std::filesystem::directory_entry& it_Entry : std::filesystem::directory_iterator(directory, l_Error))
    {
        if (it_Entry.path().extension() == Trinity::Project::c_Extension)
        {
            m_Session.OpenProject(it_Entry.path());

            return m_Session.HasProject();
        }
    }

    m_Session.CreateProject(directory);

    return m_Session.HasProject();
}

// Only images that are missing or differ are written, so an unchanged set stays in the cache
std::size_t ImportTest::CopyTestImages()
{
    if (!Trinity::FileSystem::MountDirectory(c_TestMount, std::filesystem::path(TR_FORGE_TEST_TEXTURES)))
    {
        TR_ERROR("Import test: the test images in {} could not be mounted", TR_FORGE_TEST_TEXTURES);

        return 0;
    }

    std::size_t l_Copied = 0;
    const Trinity::Expected<std::vector<Trinity::DirectoryEntry>, Trinity::FileError> l_Entries = Trinity::FileSystem::List(c_TestMount);
    for (const Trinity::DirectoryEntry& it_Entry : l_Entries ? *l_Entries : std::vector<Trinity::DirectoryEntry>())
    {
        if (it_Entry.Type != Trinity::FileType::File || Trinity::AssetRegistry::GetDefaultImporter(it_Entry.Name) != TextureImporter::c_Importer)
        {
            continue;
        }

        const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Source = Trinity::FileSystem::ReadFile(std::format("{}/{}", c_TestMount, it_Entry.Name));
        const std::string l_Destination = std::format("{}/{}/{}", Trinity::Project::c_AssetsMount, c_TestFolder, it_Entry.Name);
        const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Existing = Trinity::FileSystem::ReadFile(l_Destination);
        if (!l_Source || (l_Existing && *l_Existing == *l_Source))
        {
            continue;
        }

        const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteFile(l_Destination, *l_Source);
        if (!l_Written)
        {
            TR_ERROR("Import test: {} could not be written: {}", l_Destination, Trinity::ToString(l_Written.GetError()));

            continue;
        }

        ++l_Copied;
    }

    static_cast<void>(Trinity::FileSystem::Unmount(c_TestMount));

    return l_Copied;
}

// The cooked file is transcoded to RGBA8 as the loader would on a device without BC7, and its top mip compared with the decoded source
void ImportTest::CheckQuality()
{
    double l_Lowest = std::numeric_limits<double>::infinity();
    std::size_t l_Checked = 0;
    for (const Trinity::AssetRecord* it_Record : m_Session.GetRegistry()->GetRecords())
    {
        if (!IsTestTexture(*it_Record))
        {
            continue;
        }

        const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Source = Trinity::FileSystem::ReadFile(it_Record->Path);
        const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Cooked = Trinity::FileSystem::ReadFile(Trinity::GetCookedTexturePath(it_Record->ID));
        if (!l_Source || !l_Cooked)
        {
            TR_ERROR("Import test: {} or its cooked texture could not be read", it_Record->Path);

            continue;
        }

        int l_Width = 0;
        int l_Height = 0;
        int l_Channels = 0;
        const std::unique_ptr<stbi_uc, StbDeleter> l_Pixels(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(l_Source->data()), static_cast<int>(l_Source->size()), &l_Width, &l_Height, &l_Channels, 4));

        ktxTexture2* l_Created = nullptr;
        KTX_error_code l_Result = ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(l_Cooked->data()), l_Cooked->size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &l_Created);
        const std::unique_ptr<ktxTexture2, KtxDeleter> l_Texture(l_Created);
        if (l_Result == KTX_SUCCESS)
        {
            l_Result = ktxTexture2_TranscodeBasis(l_Texture.get(), KTX_TTF_RGBA32, 0);
        }

        if (!l_Pixels || l_Result != KTX_SUCCESS || l_Texture->baseWidth != static_cast<ktx_uint32_t>(l_Width) || l_Texture->baseHeight != static_cast<ktx_uint32_t>(l_Height))
        {
            TR_ERROR("Import test: {} and its cooked texture could not be compared", it_Record->Path);

            continue;
        }

        const std::uint32_t l_ExpectedLevels = static_cast<std::uint32_t>(std::bit_width(static_cast<std::uint32_t>(std::max(l_Width, l_Height))));
        if (l_Texture->numLevels != l_ExpectedLevels)
        {
            TR_ERROR("Import test: {} has {} mip(s), and a full chain for {}x{} is {}", it_Record->Path, l_Texture->numLevels, l_Width, l_Height, l_ExpectedLevels);
        }

        ktx_size_t l_Offset = 0;
        static_cast<void>(ktxTexture_GetImageOffset(ktxTexture(l_Texture.get()), 0, 0, 0, &l_Offset));
        const bool l_HasAlpha = l_Channels == 2 || l_Channels == 4;
        const double l_Psnr = ComputePsnr(l_Pixels.get(), ktxTexture_GetData(ktxTexture(l_Texture.get())) + l_Offset, static_cast<std::size_t>(l_Width) * static_cast<std::size_t>(l_Height), l_HasAlpha ? 4 : 3);
        l_Lowest = std::min(l_Lowest, l_Psnr);
        ++l_Checked;

        TR_INFO("Import test: {} ({}x{}, {} mips) transcodes to RGBA8 at {:.2f} dB over {}", it_Record->Path, l_Width, l_Height, l_Texture->numLevels, l_Psnr, l_HasAlpha ? "RGBA" : "RGB");
    }

    if (l_Checked == 0 || l_Lowest < c_MinimumPsnr)
    {
        TR_ERROR("Import test: the lowest PSNR of {} texture(s) is {:.2f} dB, below the {:.0f} dB the test asks for", l_Checked, l_Lowest, c_MinimumPsnr);
    }
}

// Each set loads through the asset manager onto the GPU: first where BC7 is allowed, then with every texture transcoded to RGBA8
void ImportTest::BeginLoads(Phase phase)
{
    static_cast<void>(Trinity::ConsoleVariables::Set(c_BC7Variable, phase == Phase::LoadingBC7 ? "true" : "false"));

    m_Textures.clear();
    for (const Trinity::AssetRecord* it_Record : m_Session.GetRegistry()->GetRecords())
    {
        if (IsTestTexture(*it_Record))
        {
            m_Textures.emplace_back(it_Record->ID);
        }
    }

    m_Phase = phase;
    m_PhaseFrames = 0;
    m_ReadyFrames = 0;
}

// BC7 needs a device that samples it and a size of whole blocks. Anything else is RGBA8, and every texture keeps its full mip chain
void ImportTest::FinishLoads()
{
    const bool l_BC7Allowed = m_Phase == Phase::LoadingBC7 && Trinity::Application::Get().GetDevice().IsFormatSupported(Trinity::RHI::Format::BC7Unorm, Trinity::RHI::TextureUsage::ShaderResource | Trinity::RHI::TextureUsage::CopyDestination);

    std::string l_Loaded;
    bool l_AsExpected = true;
    for (const Trinity::AssetRef<Trinity::TextureAsset>& it_Texture : m_Textures)
    {
        const Trinity::TextureAsset* l_Texture = it_Texture.Get();
        const Trinity::AssetRecord* l_Record = m_Session.GetRegistry()->Find(it_Texture.GetID());
        const bool l_WholeBlocks = l_Texture->GetWidth() % 4 == 0 && l_Texture->GetHeight() % 4 == 0;
        const Trinity::RHI::Format l_Expected = l_BC7Allowed && l_WholeBlocks ? Trinity::RHI::Format::BC7Unorm : Trinity::RHI::Format::RGBA8Unorm;
        const std::uint32_t l_ExpectedLevels = static_cast<std::uint32_t>(std::bit_width(std::max(l_Texture->GetWidth(), l_Texture->GetHeight())));

        l_AsExpected = l_AsExpected && l_Texture->GetFormat() == l_Expected && l_Texture->GetMipLevels() == l_ExpectedLevels && l_Texture->GetTexture() && l_Texture->GetShaderResourceIndex() != Trinity::RHI::c_NoBindlessIndex;
        l_Loaded += std::format("{}{} as {} with {} mips{}", l_Loaded.empty() ? "" : ", ", l_Record != nullptr ? l_Record->Path : std::string("?"), Trinity::RHI::ToString(l_Texture->GetFormat()), l_Texture->GetMipLevels(), l_Texture->IsSrgb() ? " (sRGB)" : "");
    }

    const std::string_view l_PhaseName = m_Phase == Phase::LoadingBC7 ? "with BC7 allowed" : "with BC7 turned off";
    if (!l_AsExpected)
    {
        TR_ERROR("Import test: {}, a texture loaded in the wrong format, without its mips or without a GPU texture: {}", l_PhaseName, l_Loaded);
    }
    else
    {
        TR_INFO("Import test: {}, {} texture(s) loaded in {} frame(s): {}", l_PhaseName, m_Textures.size(), m_PhaseFrames, l_Loaded);
    }

    m_Textures.clear();
    if (m_Phase == Phase::LoadingBC7)
    {
        BeginLoads(Phase::LoadingRGBA8);

        return;
    }

    m_Phase = Phase::Idle;
    static_cast<void>(Trinity::ConsoleVariables::Set(c_BC7Variable, "true"));
    TR_INFO("Import test: finished, and {} asset(s) are still loaded", Trinity::AssetManager::GetEntryCount());
}