#include "ImportTest.hpp"

#include "EditorCommands.hpp"
#include "EditorSession.hpp"
#include "Importers/ModelSource.hpp"
#include "Importers/TextureImporter.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <ktx.h>
#include <stb_image.h>

#include <glm/gtc/packing.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    constexpr std::string_view c_TestMount = "/forge-tests";
    constexpr std::string_view c_TestFolder = "Textures";
    constexpr std::string_view c_ModelMount = "/forge-models";
    constexpr std::string_view c_ModelFolder = "Models";
    constexpr std::string_view c_InterchangeMount = "/forge-assimp";
    constexpr std::string_view c_InterchangeFolder = "Interchange";
    constexpr std::string_view c_BC7Variable = "renderer.texture_bc7";

    // UASTC at level 2 keeps the test images between 41 and 46 dB, so a drop below this means a broken encode or transcode, not a lossy setting
    constexpr double c_MinimumPsnr = 38.0;
    // Long enough for the test models' textures too, Sponza's among them
    constexpr std::uint64_t c_LoadTimeoutFrames = 1800;
    // How far an instantiated entity's world matrix may be from fastgltf's for its node, in any element
    constexpr float c_MaximumMatrixError = 1e-5f;
    // How far any vertex of one format may be from the nearest of another's, in metres once both are converted
    constexpr float c_MaximumPositionError = 1e-4f;
    // Frames of material edits, after the first hundred of which the renderer's memory must stay as it is
    constexpr std::uint64_t c_EditFrames = 1000;
    constexpr std::uint64_t c_EditWarmupFrames = 100;
    // The offscreen view of MetalRoughSpheres, and the random poses and scattered meshes culling is checked with
    constexpr std::uint32_t c_RenderSize = 256;
    constexpr std::uint32_t c_CullPoses = 1000;
    constexpr std::uint32_t c_CullEntities = 400;
    // Point and spot lights scattered through MetalRoughSpheres for the cluster checks, and the size of the clusters' counts and lights read back
    constexpr std::uint32_t c_TestLights = 1024;
    constexpr std::uint64_t c_ClusterCountsSize = std::uint64_t{ Trinity::ClusterGrid::c_Count } * sizeof(std::uint32_t);
    constexpr std::uint64_t c_ClusterListsSize = c_ClusterCountsSize * Trinity::ClusterGrid::c_MaxLights;
    constexpr std::array<float, 4> c_RenderClear{ 0.0f, 0.0f, 0.0f, 0.0f };

    // Khronos glTF sample models, as Scripts/FetchSamples checks them out under TR_FORGE_TEST_MODELS: two with images beside them and two that embed theirs
    struct TestModel
    {
        std::string_view Name;
        std::string_view Folder;
        std::string_view File;
    };

    constexpr std::array<TestModel, 4> c_TestModels{ {
        { "Sponza", "glTF-Sample-Assets/Models/Sponza/glTF", "Sponza.gltf" },
        { "DamagedHelmet", "glTF-Sample-Assets/Models/DamagedHelmet/glTF-Binary", "DamagedHelmet.glb" },
        { "MetalRoughSpheres", "glTF-Sample-Assets/Models/MetalRoughSpheres/glTF-Binary", "MetalRoughSpheres.glb" },
        { "Duck", "glTF-Sample-Assets/Models/Duck/glTF", "Duck.gltf" }
    } };

    // assimp's own test models under TR_FORGE_TEST_ASSIMP, the model first and then the files it names, each copied into a folder of its own. The binary duck borrows the ASCII one's texture, and the FBX spider names its textures by paths on another machine, so it finds them by name
    struct InterchangeModel
    {
        std::string_view Name;
        std::string_view Files;
        std::string_view Textures;
    };

    constexpr std::string_view c_DuckTexture = "models-nonbsd/FBX/2013_ASCII/duckCM.tga";
    constexpr std::string_view c_SpiderTextures = "models/OBJ/SpiderTex.jpg models/OBJ/drkwood2.jpg models/OBJ/engineflare1.jpg models/OBJ/wal67ar_small.jpg models/OBJ/wal69ar_small.jpg";
    constexpr std::array<InterchangeModel, 5> c_InterchangeModels{ {
        { "Duck-COLLADA", "models/Collada/duck.dae", "models/Collada/duckCM.tga" },
        { "Duck-FBX-ASCII", "models-nonbsd/FBX/2013_ASCII/duck.fbx", c_DuckTexture },
        { "Duck-FBX-Binary", "models-nonbsd/FBX/2013_BINARY/duck.fbx", c_DuckTexture },
        { "Spider-OBJ", "models/OBJ/spider.obj models/OBJ/spider.mtl", c_SpiderTextures },
        { "Spider-FBX", "models/FBX/spider.fbx", c_SpiderTextures }
    } };

    // One model and another exported from the same scene, which must come to the same vertices
    constexpr std::array<std::pair<std::string_view, std::string_view>, 4> c_FormatComparisons{ {
        { "Duck", "Duck-COLLADA" },
        { "Duck", "Duck-FBX-ASCII" },
        { "Duck", "Duck-FBX-Binary" },
        { "Spider-OBJ", "Spider-FBX" }
    } };

    std::vector<std::string_view> SplitFiles(std::string_view files)
    {
        std::vector<std::string_view> l_Files;
        while (!files.empty())
        {
            const std::size_t l_End = std::min(files.find(' '), files.size());
            l_Files.push_back(files.substr(0, l_End));
            files.remove_prefix(std::min(l_End + 1, files.size()));
        }

        return l_Files;
    }

    std::string GetTestModelPath(const TestModel& model)
    {
        return std::format("{}/{}/{}/{}", Trinity::Project::c_AssetsMount, c_ModelFolder, model.Name, model.File);
    }

    std::string GetInterchangePath(const InterchangeModel& model)
    {
        const std::string_view l_Source = SplitFiles(model.Files).front();

        return std::format("{}/{}/{}/{}", Trinity::Project::c_AssetsMount, c_InterchangeFolder, model.Name, l_Source.substr(l_Source.find_last_of('/') + 1));
    }

    // Where every test model is put in the project, glTF first
    std::vector<std::string> GetTestModelPaths()
    {
        std::vector<std::string> l_Paths;
        std::ranges::transform(c_TestModels, std::back_inserter(l_Paths), [](const TestModel& model) { return GetTestModelPath(model); });
        std::ranges::transform(c_InterchangeModels, std::back_inserter(l_Paths), GetInterchangePath);

        return l_Paths;
    }

    std::string FindTestModelPath(std::string_view name)
    {
        const auto a_Model = std::ranges::find(c_TestModels, name, &TestModel::Name);
        if (a_Model != c_TestModels.end())
        {
            return GetTestModelPath(*a_Model);
        }

        const auto a_Interchange = std::ranges::find(c_InterchangeModels, name, &InterchangeModel::Name);

        return a_Interchange != c_InterchangeModels.end() ? GetInterchangePath(*a_Interchange) : std::string();
    }

    bool IsGltf(std::string_view path)
    {
        return path.ends_with(".gltf") || path.ends_with(".glb");
    }

    // Every vertex of every mesh the cooked hierarchy places, in world space, or nothing when the model cannot be read
    std::optional<std::vector<glm::vec3>> GetWorldPositions(const Trinity::AssetRecord& record)
    {
        const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(Trinity::GetCookedModelPath(record.ID));
        const Trinity::Expected<Trinity::ModelData, std::string> l_Model = l_Text ? Trinity::ParseModelData(*l_Text) : Trinity::Expected<Trinity::ModelData, std::string>(Trinity::Unexpected{ std::string("it could not be read") });
        if (!l_Model)
        {
            return std::nullopt;
        }

        std::vector<glm::vec3> l_Positions;
        std::vector<glm::mat4> l_Worlds;
        for (const Trinity::ModelNode& it_Node : l_Model->Nodes)
        {
            const glm::mat4 l_Local = Trinity::TransformComponent{ it_Node.Translation, it_Node.Rotation, it_Node.Scale }.GetMatrix();
            l_Worlds.push_back(it_Node.Parent == Trinity::ModelNode::c_NoParent ? l_Local : l_Worlds[static_cast<std::size_t>(it_Node.Parent)] * l_Local);
            if (!it_Node.Mesh.IsValid())
            {
                continue;
            }

            const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_File = Trinity::FileSystem::ReadFile(Trinity::GetCookedMeshPath(it_Node.Mesh));
            const Trinity::Expected<Trinity::MeshFile, std::string> l_Mesh = l_File ? Trinity::ReadMeshFile(*l_File) : Trinity::Expected<Trinity::MeshFile, std::string>(Trinity::Unexpected{ std::string("it could not be read") });
            if (!l_Mesh)
            {
                return std::nullopt;
            }

            for (std::uint32_t it_Vertex = 0; it_Vertex < l_Mesh->Layout.VertexCount; ++it_Vertex)
            {
                glm::vec3 l_Position;
                std::memcpy(&l_Position, l_Mesh->Data.data() + l_Mesh->Layout.Positions + it_Vertex * sizeof(glm::vec3), sizeof(glm::vec3));
                l_Positions.push_back(glm::vec3(l_Worlds.back() * glm::vec4(l_Position, 1.0f)));
            }
        }

        return l_Positions;
    }

    // The farthest any point of one set is from its nearest in the other
    float GetFarthestNearest(const std::vector<glm::vec3>& from, const std::vector<glm::vec3>& to)
    {
        float l_Farthest = 0.0f;
        for (const glm::vec3& it_Point : from)
        {
            float l_Nearest = std::numeric_limits<float>::infinity();
            for (const glm::vec3& it_Other : to)
            {
                l_Nearest = std::min(l_Nearest, glm::dot(it_Point - it_Other, it_Point - it_Other));
            }

            l_Farthest = std::max(l_Farthest, l_Nearest);
        }

        return std::sqrt(l_Farthest);
    }

    // Only files that are missing or differ are written, so an unchanged set stays in the cache
    bool CopyIfChanged(const std::string& source, const std::string& destination)
    {
        const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Source = Trinity::FileSystem::ReadFile(source);
        const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Existing = Trinity::FileSystem::ReadFile(destination);
        if (!l_Source || (l_Existing && *l_Existing == *l_Source))
        {
            return false;
        }

        const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteFile(destination, *l_Source);
        if (!l_Written)
        {
            TR_ERROR("Import test: {} could not be written: {}", destination, Trinity::ToString(l_Written.GetError()));
        }

        return static_cast<bool>(l_Written);
    }

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
    const std::size_t l_Models = CopyTestModels();
    m_Session.ScanAssets();
    const EditorSession::ImportReport l_First = m_Session.WaitForImports();
    m_Session.ScanAssets();
    const EditorSession::ImportReport l_Second = m_Session.WaitForImports();

    const TextureImportReport& l_FirstTextures = l_First.Textures;
    const TextureImportReport& l_SecondTextures = l_Second.Textures;
    TR_INFO("Import test: {} image(s) copied in, {} texture(s): {} encoded and {} from the cache, then {} from the cache on a second pass", l_Copied, l_FirstTextures.Textures, l_FirstTextures.Encoded, l_FirstTextures.Cached, l_SecondTextures.Cached);
    if (l_FirstTextures.Textures == 0 || l_FirstTextures.Failed != 0 || l_SecondTextures.Encoded != 0 || l_SecondTextures.Failed != 0 || l_SecondTextures.Cached != l_SecondTextures.Textures)
    {
        TR_ERROR("Import test: every texture should import, and a second pass should find all of them in the cache");

        return;
    }

    if (l_Models != 0)
    {
        const ModelImportReport& l_FirstModels = l_First.Models;
        const ModelImportReport& l_SecondModels = l_Second.Models;
        TR_INFO("Import test: {} model(s): {} imported and {} from the cache, with {} embedded texture(s) encoded, then {} from the cache on a second pass", l_FirstModels.Models, l_FirstModels.Imported, l_FirstModels.Cached, l_FirstModels.TexturesEncoded, l_SecondModels.Cached);
        if (l_FirstModels.Models < l_Models || l_FirstModels.Failed != 0 || l_FirstModels.Stopped != 0 || l_SecondModels.Imported != 0 || l_SecondModels.Failed != 0 || l_SecondModels.Cached != l_SecondModels.Models)
        {
            TR_ERROR("Import test: every model should import, and a second pass should find all of them in the cache");

            return;
        }

        if (!CheckModels() || !ReimportModels() || !CheckInstances() || !CompareFormats() || !CheckMaterialMapping() || !CheckMaterialFiles())
        {
            return;
        }
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
    if (m_Phase != Phase::LoadingBC7 && m_Phase != Phase::LoadingRGBA8)
    {
        UpdateMaterials();

        return;
    }

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

// The Khronos samples, when Scripts/FetchSamples has fetched them, and assimp's test models, each into a folder of its own. Without the samples the test goes on with assimp's models alone
std::size_t ImportTest::CopyTestModels()
{
    std::size_t l_Found = 0;
    std::size_t l_Copied = 0;
    if (std::filesystem::is_directory(std::filesystem::path(TR_FORGE_TEST_ASSIMP)) && Trinity::FileSystem::MountDirectory(c_InterchangeMount, std::filesystem::path(TR_FORGE_TEST_ASSIMP)))
    {
        for (const InterchangeModel& it_Model : c_InterchangeModels)
        {
            std::vector<std::string_view> l_Files = SplitFiles(it_Model.Files);
            std::ranges::copy(SplitFiles(it_Model.Textures), std::back_inserter(l_Files));

            if (!Trinity::FileSystem::Exists(std::format("{}/{}", c_InterchangeMount, l_Files.front())))
            {
                TR_WARN("Import test: {} is missing from {}, so {} is not tested", l_Files.front(), TR_FORGE_TEST_ASSIMP, it_Model.Name);

                continue;
            }

            ++l_Found;
            for (const std::string_view it_File : l_Files)
            {
                const std::string_view l_Name = it_File.substr(it_File.find_last_of('/') + 1);
                if (CopyIfChanged(std::format("{}/{}", c_InterchangeMount, it_File), std::format("{}/{}/{}/{}", Trinity::Project::c_AssetsMount, c_InterchangeFolder, it_Model.Name, l_Name)))
                {
                    ++l_Copied;
                }
            }
        }

        static_cast<void>(Trinity::FileSystem::Unmount(c_InterchangeMount));
    }
    else
    {
        TR_WARN("Import test: assimp's test models are not in {}, so FBX, OBJ and COLLADA are not tested", TR_FORGE_TEST_ASSIMP);
    }

    if (!std::filesystem::is_directory(std::filesystem::path(TR_FORGE_TEST_MODELS)) || !Trinity::FileSystem::MountDirectory(c_ModelMount, std::filesystem::path(TR_FORGE_TEST_MODELS)))
    {
        TR_WARN("Import test: there are no glTF test models in {}, so glTF is not tested. Scripts/FetchSamples.sh or FetchSamples.ps1 fetches them", TR_FORGE_TEST_MODELS);
        TR_INFO("Import test: {} of {} test model(s) found, with {} file(s) copied in", l_Found, c_TestModels.size() + c_InterchangeModels.size(), l_Copied);

        return l_Found;
    }

    for (const TestModel& it_Model : c_TestModels)
    {
        const std::string l_Folder = std::format("{}/{}", c_ModelMount, it_Model.Folder);
        const Trinity::Expected<std::vector<Trinity::DirectoryEntry>, Trinity::FileError> l_Entries = Trinity::FileSystem::List(l_Folder);
        if (!l_Entries || !Trinity::FileSystem::Exists(std::format("{}/{}", l_Folder, it_Model.File)))
        {
            TR_WARN("Import test: {} is missing from {}, so it is not tested. Scripts/FetchSamples.sh or FetchSamples.ps1 fetches it", it_Model.File, TR_FORGE_TEST_MODELS);

            continue;
        }

        ++l_Found;
        for (const Trinity::DirectoryEntry& it_Entry : *l_Entries)
        {
            if (it_Entry.Type == Trinity::FileType::File && CopyIfChanged(std::format("{}/{}", l_Folder, it_Entry.Name), std::format("{}/{}/{}/{}", Trinity::Project::c_AssetsMount, c_ModelFolder, it_Model.Name, it_Entry.Name)))
            {
                ++l_Copied;
            }
        }
    }

    static_cast<void>(Trinity::FileSystem::Unmount(c_ModelMount));
    TR_INFO("Import test: {} of {} test model(s) found, with {} file(s) copied in", l_Found, c_TestModels.size() + c_InterchangeModels.size(), l_Copied);

    return l_Found;
}

// Every cooked mesh, material and node of each model reads back, and names only sub-assets of its model or textures in the project. The textures the materials use are kept, with how they are used, for the loads
bool ImportTest::CheckModels()
{
    const Trinity::AssetRegistry& l_Registry = *m_Session.GetRegistry();
    bool l_Passed = true;
    m_ModelTextures.clear();
    m_ModelTextures.clear();
    for (const std::string& it_Path : GetTestModelPaths())
    {
        const Trinity::AssetRecord* l_Record = l_Registry.FindByPath(it_Path);
        if (l_Record == nullptr)
        {
            continue;
        }

        const auto a_Fail = [&l_Passed, l_Record](std::string_view what)
        {
            TR_ERROR("Import test: {} {}", l_Record->Path, what);
            l_Passed = false;
        };

        const auto a_IsSubAsset = [l_Record](Trinity::UUID id, std::string_view importer)
        {
            return std::ranges::any_of(l_Record->SubAssets, [id, importer](const Trinity::SubAsset& subAsset) { return subAsset.ID == id && subAsset.Importer == importer; });
        };

        std::size_t l_Meshes = 0;
        std::size_t l_Submeshes = 0;
        std::uint64_t l_Vertices = 0;
        std::size_t l_Materials = 0;
        std::size_t l_Embedded = 0;
        std::size_t l_Hulls = 0;
        std::size_t l_CollisionMeshes = 0;
        for (const Trinity::SubAsset& it_SubAsset : l_Record->SubAssets)
        {
            if (it_SubAsset.Importer == Trinity::MeshAsset::c_AssetType)
            {
                const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_File = Trinity::FileSystem::ReadFile(Trinity::GetCookedMeshPath(it_SubAsset.ID));
                const Trinity::Expected<Trinity::MeshFile, std::string> l_Mesh = l_File ? Trinity::ReadMeshFile(*l_File) : Trinity::Expected<Trinity::MeshFile, std::string>(Trinity::Unexpected{ std::string("it could not be read") });
                if (!l_Mesh)
                {
                    a_Fail(std::format("has mesh {}, which does not read back: {}", it_SubAsset.Key, l_Mesh.GetError()));

                    continue;
                }

                ++l_Meshes;
                l_Submeshes += l_Mesh->Submeshes.size();
                l_Vertices += l_Mesh->Layout.VertexCount;
            }
            else if (it_SubAsset.Importer == ModelImporter::c_MaterialImporter)
            {
                const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(Trinity::GetCookedMaterialPath(it_SubAsset.ID));
                const Trinity::Expected<Trinity::MaterialData, std::string> l_Material = l_Text ? Trinity::ParseMaterialData(*l_Text) : Trinity::Expected<Trinity::MaterialData, std::string>(Trinity::Unexpected{ std::string("it could not be read") });
                if (!l_Material)
                {
                    a_Fail(std::format("has material {}, which does not read back: {}", it_SubAsset.Key, l_Material.GetError()));

                    continue;
                }

                ++l_Materials;
                const std::array<std::pair<Trinity::MaterialTexture, ModelImporter::TextureUsage>, 5> l_Slots{ { { l_Material->BaseColorTexture, ModelImporter::TextureUsage::Color }, { l_Material->MetallicRoughnessTexture, ModelImporter::TextureUsage::Data }, { l_Material->NormalTexture, ModelImporter::TextureUsage::Normal }, { l_Material->OcclusionTexture, ModelImporter::TextureUsage::Data }, { l_Material->EmissiveTexture, ModelImporter::TextureUsage::Color } } };
                for (const auto& [it_Texture, it_Usage] : l_Slots)
                {
                    const Trinity::AssetRecord* l_Texture = it_Texture.Texture.IsValid() ? l_Registry.Find(it_Texture.Texture) : nullptr;
                    if (it_Texture.Texture.IsValid() && (l_Texture == nullptr || l_Texture->Importer != Trinity::TextureAsset::c_AssetType))
                    {
                        a_Fail(std::format("has material {} using {}, which is not a texture in the project", it_SubAsset.Key, it_Texture.Texture));
                    }
                    else if (l_Texture != nullptr)
                    {
                        m_ModelTextures.insert({ it_Texture.Texture, it_Usage });
                    }
                }
            }
            else if (it_SubAsset.Importer == Trinity::TextureAsset::c_AssetType)
            {
                ++l_Embedded;
                if (!Trinity::FileSystem::Exists(Trinity::GetCookedTexturePath(it_SubAsset.ID)))
                {
                    a_Fail(std::format("has texture {} with nothing cooked", it_SubAsset.Key));
                }
            }
            else if (it_SubAsset.Importer == Trinity::ConvexHullAsset::c_AssetType || it_SubAsset.Importer == Trinity::CollisionMeshAsset::c_AssetType)
            {
                ++(it_SubAsset.Importer == Trinity::ConvexHullAsset::c_AssetType ? l_Hulls : l_CollisionMeshes);
                if (!Trinity::FileSystem::Exists(Trinity::GetCookedCollisionShapePath(it_SubAsset.ID)))
                {
                    a_Fail(std::format("has collision shape {} with nothing cooked", it_SubAsset.Key));
                }
            }
        }

        // Every mesh gets each kind of shape its settings ask for, both by default
        const ModelCollision l_Collision = ModelImporter::ReadSettings(*l_Record).Collision;
        const bool l_WantsHulls = l_Collision == ModelCollision::Both || l_Collision == ModelCollision::ConvexHulls;
        const bool l_WantsCollisionMeshes = l_Collision == ModelCollision::Both || l_Collision == ModelCollision::TriangleMeshes;
        if (l_Hulls != (l_WantsHulls ? l_Meshes : 0) || l_CollisionMeshes != (l_WantsCollisionMeshes ? l_Meshes : 0))
        {
            a_Fail(std::format("has {} convex hull(s) and {} collision mesh(es) for {} mesh(es)", l_Hulls, l_CollisionMeshes, l_Meshes));
        }

        const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(Trinity::GetCookedModelPath(l_Record->ID));
        const Trinity::Expected<Trinity::ModelData, std::string> l_Model = l_Text ? Trinity::ParseModelData(*l_Text) : Trinity::Expected<Trinity::ModelData, std::string>(Trinity::Unexpected{ std::string("it could not be read") });
        if (!l_Model || l_Model->Nodes.empty())
        {
            a_Fail(std::format("has a hierarchy that does not read back: {}", l_Model ? std::string("it has no nodes") : l_Model.GetError()));

            continue;
        }

        std::size_t l_MeshNodes = 0;
        for (const Trinity::ModelNode& it_Node : l_Model->Nodes)
        {
            const bool l_OwnMaterials = std::ranges::all_of(it_Node.Materials, [&a_IsSubAsset](Trinity::UUID id) { return !id.IsValid() || a_IsSubAsset(id, ModelImporter::c_MaterialImporter); });
            if ((it_Node.Mesh.IsValid() && !a_IsSubAsset(it_Node.Mesh, Trinity::MeshAsset::c_AssetType)) || !l_OwnMaterials)
            {
                a_Fail(std::format("has node {} naming a mesh or material that is not its own", it_Node.Name));
            }

            l_MeshNodes += it_Node.Mesh.IsValid() ? 1 : 0;
        }

        if (l_Meshes == 0 || l_Materials == 0 || l_MeshNodes == 0)
        {
            a_Fail("has no meshes, no materials or no node drawing a mesh");
        }

        TR_INFO("Import test: {} has {} mesh(es) with {} submesh(es) and {} vertices, {} convex hull(s), {} collision mesh(es), {} material(s), {} embedded texture(s), and {} node(s), {} with a mesh", l_Record->Path, l_Meshes, l_Submeshes, l_Vertices, l_Hulls, l_CollisionMeshes, l_Materials, l_Embedded, l_Model->Nodes.size(), l_MeshNodes);
    }

    return l_Passed;
}

// With every model's key removed, each is imported again, and must come back with the same sub-assets under the same UUIDs, and encode nothing its textures' own keys still hold
bool ImportTest::ReimportModels()
{
    std::vector<std::pair<Trinity::UUID, std::vector<Trinity::SubAsset>>> l_Before;
    for (const std::string& it_Path : GetTestModelPaths())
    {
        if (const Trinity::AssetRecord* l_Record = m_Session.GetRegistry()->FindByPath(it_Path))
        {
            l_Before.emplace_back(l_Record->ID, l_Record->SubAssets);
            static_cast<void>(Trinity::FileSystem::RemoveFile(ModelImporter::GetCacheKeyPath(l_Record->ID)));
        }
    }

    m_Session.ScanAssets();
    const EditorSession::ImportReport l_Report = m_Session.WaitForImports();

    std::size_t l_SubAssets = 0;
    bool l_Kept = true;
    for (const auto& [it_ID, it_SubAssets] : l_Before)
    {
        const Trinity::AssetRecord* l_Record = m_Session.GetRegistry()->Find(it_ID);
        if (l_Record == nullptr || l_Record->SubAssets != it_SubAssets)
        {
            TR_ERROR("Import test: {} came back from a reimport with different sub-assets or UUIDs", l_Record != nullptr ? l_Record->Path : it_ID.ToString());
            l_Kept = false;
        }

        l_SubAssets += it_SubAssets.size();
    }

    if (!l_Kept || l_Report.Models.Imported != l_Before.size() || l_Report.Models.TexturesEncoded != 0 || l_Report.Textures.Encoded != 0)
    {
        TR_ERROR("Import test: a forced reimport of {} model(s) imported {}, encoded {} embedded and {} other texture(s), and should have imported all and encoded none", l_Before.size(), l_Report.Models.Imported, l_Report.Models.TexturesEncoded, l_Report.Textures.Encoded);

        return false;
    }

    TR_INFO("Import test: a forced reimport of {} model(s) kept all {} sub-asset UUID(s), and encoded no texture again", l_Before.size(), l_SubAssets);

    return true;
}

// Each model is created in a scene of its own by the command a drop runs, which must undo to nothing and redo with the same UUIDs. For a glTF, every node's world matrix is then compared with the one fastgltf computes from the file, matrices and all
bool ImportTest::CheckInstances()
{
    bool l_Passed = true;
    for (const std::string& it_Path : GetTestModelPaths())
    {
        const Trinity::AssetRecord* l_Record = m_Session.GetRegistry()->FindByPath(it_Path);
        if (l_Record == nullptr)
        {
            continue;
        }

        const auto a_Fail = [&l_Passed, l_Record](std::string_view what)
        {
            TR_ERROR("Import test: {} {}", l_Record->Path, what);
            l_Passed = false;
        };

        Trinity::Scene l_Scene;
        CreateModelCommand l_Command(l_Record->ID, std::filesystem::path(it_Path).stem().string(), {}, {}, glm::vec3(0.0f));
        if (!l_Command.Execute(l_Scene))
        {
            a_Fail("could not be created in a scene");

            continue;
        }

        const std::size_t l_Created = l_Scene.GetEntityCount();
        const Trinity::UUID l_Root = l_Command.GetSubject();
        l_Command.Undo(l_Scene);
        const std::size_t l_AfterUndo = l_Scene.GetEntityCount();
        const bool l_Redone = l_Command.Execute(l_Scene);
        if (l_AfterUndo != 0 || !l_Redone || l_Scene.GetEntityCount() != l_Created || !l_Scene.FindEntityByUUID(l_Root))
        {
            a_Fail(std::format("made {} entities, left {} after an undo, and {} after a redo", l_Created, l_AfterUndo, l_Scene.GetEntityCount()));

            continue;
        }

        if (!IsGltf(it_Path))
        {
            TR_INFO("Import test: {} created {} entities in one command that undoes and redoes", l_Record->Path, l_Created);

            continue;
        }

        const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(Trinity::GetCookedModelPath(l_Record->ID));
        const Trinity::Expected<Trinity::ModelData, std::string> l_Model = l_Text ? Trinity::ParseModelData(*l_Text) : Trinity::Expected<Trinity::ModelData, std::string>(Trinity::Unexpected{ std::string("it could not be read") });
        const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Source = Trinity::FileSystem::ReadFile(l_Record->Path);
        fastgltf::Expected<fastgltf::GltfDataBuffer> l_Data = l_Source ? fastgltf::GltfDataBuffer::FromBytes(l_Source->data(), l_Source->size()) : fastgltf::Expected<fastgltf::GltfDataBuffer>(fastgltf::Error::InvalidPath);
        fastgltf::Parser l_Parser(fastgltf::Extensions::KHR_materials_emissive_strength | fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_transform);
        fastgltf::Expected<fastgltf::Asset> l_Asset = l_Data.error() == fastgltf::Error::None ? l_Parser.loadGltf(l_Data.get(), {}, fastgltf::Options::None, fastgltf::Category::Scenes | fastgltf::Category::Nodes) : fastgltf::Expected<fastgltf::Asset>(l_Data.error());
        if (!l_Model || l_Asset.error() != fastgltf::Error::None || l_Asset->scenes.empty())
        {
            a_Fail("has a hierarchy or a source that could not be read for the check");

            continue;
        }

        // Every node of the scene the importer took, by its index in the file
        std::vector<std::optional<glm::mat4>> l_Expected(l_Asset->nodes.size());
        const std::size_t l_SceneIndex = l_Asset->defaultScene.value_or(0) < l_Asset->scenes.size() ? l_Asset->defaultScene.value_or(0) : 0;
        fastgltf::iterateSceneNodes(l_Asset.get(), l_SceneIndex, fastgltf::math::fmat4x4(), [&](fastgltf::Node& node, const fastgltf::math::fmat4x4& matrix)
        {
            glm::mat4 l_Matrix(1.0f);
            for (glm::length_t it_Column = 0; it_Column < 4; ++it_Column)
            {
                for (glm::length_t it_Row = 0; it_Row < 4; ++it_Row)
                {
                    l_Matrix[it_Column][it_Row] = matrix[static_cast<std::size_t>(it_Column)][static_cast<std::size_t>(it_Row)];
                }
            }

            l_Expected[static_cast<std::size_t>(&node - l_Asset->nodes.data())] = l_Matrix;
        });

        // The root's subtree holds the nodes in the model's order
        l_Scene.UpdateWorldTransforms();
        const Trinity::Entity l_RootEntity = l_Scene.FindEntityByUUID(l_Root);
        std::size_t l_Node = 0;
        float l_Worst = 0.0f;
        for (Trinity::Entity it_Entity = l_Scene.GetNextInSubtree(l_RootEntity, l_RootEntity); it_Entity; it_Entity = l_Scene.GetNextInSubtree(it_Entity, l_RootEntity), ++l_Node)
        {
            const std::uint32_t l_SourceNode = l_Node < l_Model->Nodes.size() ? l_Model->Nodes[l_Node].SourceNode : UINT32_MAX;
            if (l_SourceNode >= l_Expected.size() || !l_Expected[l_SourceNode])
            {
                l_Worst = std::numeric_limits<float>::infinity();

                break;
            }

            const glm::mat4& l_World = it_Entity.Get<Trinity::WorldTransformComponent>().Matrix;
            for (glm::length_t it_Column = 0; it_Column < 4; ++it_Column)
            {
                for (glm::length_t it_Row = 0; it_Row < 4; ++it_Row)
                {
                    l_Worst = std::max(l_Worst, std::abs(l_World[it_Column][it_Row] - (*l_Expected[l_SourceNode])[it_Column][it_Row]));
                }
            }
        }

        if (l_Node != l_Model->Nodes.size() || !(l_Worst <= c_MaximumMatrixError))
        {
            a_Fail(std::format("has {} of {} node(s) matching fastgltf's world matrices, the worst by {}, and {} is allowed", l_Node, l_Model->Nodes.size(), l_Worst, c_MaximumMatrixError));

            continue;
        }

        TR_INFO("Import test: {} created {} entities in one command that undoes and redoes, with every world matrix within {:.2e} of fastgltf's", l_Record->Path, l_Created, l_Worst);
    }

    return l_Passed;
}

// The same model in two formats comes to the same vertices once each is in metres with Y up, which checks the units and axes each format is read in. A pair missing either model is left out
bool ImportTest::CompareFormats()
{
    bool l_Passed = true;
    for (const auto& [it_Reference, it_Other] : c_FormatComparisons)
    {
        const Trinity::AssetRecord* l_Reference = m_Session.GetRegistry()->FindByPath(FindTestModelPath(it_Reference));
        const Trinity::AssetRecord* l_Record = m_Session.GetRegistry()->FindByPath(FindTestModelPath(it_Other));
        if (l_Reference == nullptr || l_Record == nullptr)
        {
            TR_WARN("Import test: {} or {} is missing, so they are not compared", it_Reference, it_Other);

            continue;
        }

        const std::optional<std::vector<glm::vec3>> l_Expected = GetWorldPositions(*l_Reference);
        const std::optional<std::vector<glm::vec3>> l_Found = GetWorldPositions(*l_Record);
        if (!l_Expected || !l_Found || l_Expected->empty() || l_Found->empty())
        {
            TR_ERROR("Import test: {} or {} has a hierarchy or mesh that could not be read for the comparison", l_Reference->Path, l_Record->Path);
            l_Passed = false;

            continue;
        }

        const float l_Error = std::max(GetFarthestNearest(*l_Expected, *l_Found), GetFarthestNearest(*l_Found, *l_Expected));
        if (!(l_Error <= c_MaximumPositionError))
        {
            TR_ERROR("Import test: {} ({} vertices) and {} ({} vertices) are {} apart at their farthest, and {} is allowed", l_Reference->Path, l_Expected->size(), l_Record->Path, l_Found->size(), l_Error, c_MaximumPositionError);
            l_Passed = false;

            continue;
        }

        TR_INFO("Import test: {} ({} vertices) and {} ({} vertices) match within {:.2e}", l_Reference->Path, l_Expected->size(), l_Record->Path, l_Found->size(), l_Error);
    }

    return l_Passed;
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
        if (IsTestTexture(*it_Record) || m_ModelTextures.contains(it_Record->ID))
        {
            m_Textures.emplace_back(it_Record->ID);
        }
    }

    m_Phase = phase;
    m_PhaseFrames = 0;
    m_ReadyFrames = 0;
}

// BC7, or BC5 for a normal map, needs a device that samples it and a size of whole blocks. Anything else is RGBA8, an sRGB texture takes the sRGB format, and every texture keeps its full mip chain. A model's texture is sRGB as colour, linear as data, and a normal map in a normal slot
void ImportTest::FinishLoads()
{
    const Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const Trinity::RHI::TextureUsage l_Usage = Trinity::RHI::TextureUsage::ShaderResource | Trinity::RHI::TextureUsage::CopyDestination;
    const bool l_BlocksAllowed = m_Phase == Phase::LoadingBC7;
    const bool l_BC7Supported = l_Device.IsFormatSupported(Trinity::RHI::Format::BC7Unorm, l_Usage);
    const bool l_BC5Supported = l_Device.IsFormatSupported(Trinity::RHI::Format::BC5Unorm, l_Usage);

    std::string l_Loaded;
    std::string l_Wrong;
    std::size_t l_ModelTextures = 0;
    std::size_t l_NormalMaps = 0;
    for (const Trinity::AssetRef<Trinity::TextureAsset>& it_Texture : m_Textures)
    {
        const Trinity::TextureAsset* l_Texture = it_Texture.Get();
        const Trinity::AssetRecord* l_Record = m_Session.GetRegistry()->Find(it_Texture.GetID());
        const auto a_ModelUsage = m_ModelTextures.find(it_Texture.GetID());
        const bool l_FromModel = a_ModelUsage != m_ModelTextures.end();
        const bool l_NormalMap = l_FromModel && a_ModelUsage->second == ModelImporter::TextureUsage::Normal;
        const bool l_Srgb = l_FromModel ? a_ModelUsage->second == ModelImporter::TextureUsage::Color : l_Texture->IsSrgb();
        const bool l_WholeBlocks = l_Texture->GetWidth() % 4 == 0 && l_Texture->GetHeight() % 4 == 0;
        const bool l_Blocks = l_BlocksAllowed && l_WholeBlocks && (l_NormalMap ? l_BC5Supported : l_BC7Supported);

        Trinity::RHI::Format l_Expected = l_Srgb ? (l_Blocks ? Trinity::RHI::Format::BC7Srgb : Trinity::RHI::Format::RGBA8Srgb) : (l_Blocks ? Trinity::RHI::Format::BC7Unorm : Trinity::RHI::Format::RGBA8Unorm);
        l_Expected = l_NormalMap ? (l_Blocks ? Trinity::RHI::Format::BC5Unorm : Trinity::RHI::Format::RGBA8Unorm) : l_Expected;
        const std::uint32_t l_ExpectedLevels = static_cast<std::uint32_t>(std::bit_width(std::max(l_Texture->GetWidth(), l_Texture->GetHeight())));

        const std::string l_Description = std::format("{} as {} with {} mips{}{}", l_Record != nullptr ? l_Record->Path : std::string("?"), Trinity::RHI::ToString(l_Texture->GetFormat()), l_Texture->GetMipLevels(), l_Texture->IsSrgb() ? " (sRGB)" : "", l_Texture->IsNormalMap() ? " (normal map)" : "");
        const bool l_AsExpected = l_Texture->GetFormat() == l_Expected && l_Texture->IsSrgb() == l_Srgb && l_Texture->IsNormalMap() == l_NormalMap && l_Texture->GetMipLevels() == l_ExpectedLevels && l_Texture->GetTexture() && l_Texture->GetShaderResourceIndex() != Trinity::RHI::c_NoBindlessIndex;
        if (!l_AsExpected)
        {
            l_Wrong += std::format("{}{}, expected {}", l_Wrong.empty() ? "" : "; ", l_Description, Trinity::RHI::ToString(l_Expected));
        }

        if (l_FromModel)
        {
            ++l_ModelTextures;
            l_NormalMaps += l_NormalMap ? 1 : 0;
        }
        else
        {
            l_Loaded += std::format("{}{}", l_Loaded.empty() ? "" : ", ", l_Description);
        }
    }

    const std::string_view l_PhaseName = m_Phase == Phase::LoadingBC7 ? "with BC7 and BC5 allowed" : "with BC7 and BC5 turned off";
    if (!l_Wrong.empty())
    {
        TR_ERROR("Import test: {}, textures loaded in the wrong format, without their mips or without a GPU texture: {}", l_PhaseName, l_Wrong);
    }
    else
    {
        TR_INFO("Import test: {}, {} texture(s) loaded in {} frame(s): {}, and {} used by the test models, {} of them normal maps", l_PhaseName, m_Textures.size(), m_PhaseFrames, l_Loaded, l_ModelTextures, l_NormalMaps);
    }

    m_Textures.clear();
    if (m_Phase == Phase::LoadingBC7)
    {
        BeginLoads(Phase::LoadingRGBA8);

        return;
    }

    static_cast<void>(Trinity::ConsoleVariables::Set(c_BC7Variable, "true"));
    BeginMaterials();
}


ImportTest::~ImportTest()
{
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    l_Device.DestroyBuffer(m_TableReadback);
    l_Device.DestroyBuffer(m_ColorReadback);
    l_Device.DestroyBuffer(m_DepthReadback);
    l_Device.DestroyBuffer(m_AllLightsReadback);
    l_Device.DestroyBuffer(m_ClusterCountsReadback);
    l_Device.DestroyBuffer(m_ClusterLightsReadback);
    l_Device.DestroyTexture(m_RenderTarget);
    l_Device.DestroyTexture(m_AllLightsTarget);
}

// The material table copied whole, on a frame a check asks for it, and MetalRoughSpheres drawn offscreen on the frame the render check asks for it
void ImportTest::OnBuildFrameGraph(Trinity::FrameGraph& graph)
{
    if (m_RenderWanted)
    {
        AddRenderPasses(graph);
    }

    if (!m_ReadbackWanted)
    {
        return;
    }

    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const Trinity::MaterialLoader& l_Loader = Trinity::Application::Get().GetRenderer().GetMaterialLoader();
    const std::uint64_t l_Size = static_cast<std::uint64_t>(l_Loader.GetCapacity()) * Trinity::MaterialLoader::c_RecordSize;
    if (!l_Loader.GetTable() || l_Size == 0)
    {
        return;
    }

    if (m_TableReadbackSize != l_Size)
    {
        l_Device.DestroyBuffer(m_TableReadback);

        Trinity::RHI::BufferDescription l_Description;
        l_Description.Size = l_Size;
        l_Description.Usage = Trinity::RHI::BufferUsage::CopyDestination;
        l_Description.Memory = Trinity::RHI::MemoryType::Readback;
        l_Description.DebugName = "Import test material table readback";
        m_TableReadback = l_Device.CreateBuffer(l_Description);
        m_TableReadbackSize = m_TableReadback ? l_Size : 0;
    }

    m_ReadbackWanted = false;
    m_ReadbackAdded = true;
    if (!m_TableReadback)
    {
        return;
    }

    const Trinity::FrameGraphBuffer l_Table = graph.ImportBuffer("Material table", l_Loader.GetTable(), l_Size, Trinity::RHI::ResourceState::ShaderResource, Trinity::RHI::ResourceState::ShaderResource);
    const Trinity::FrameGraphBuffer l_Readback = graph.ImportBuffer("Import test material table readback", m_TableReadback, l_Size, Trinity::RHI::ResourceState::CopyDestination, Trinity::RHI::ResourceState::CopyDestination);
    graph.AddPass("Import test material readback", Trinity::FrameGraphPassType::Copy, [l_Table, l_Readback](Trinity::FrameGraphPassBuilder& builder)
    {
        builder.Read(l_Table, Trinity::RHI::ResourceState::CopySource);
        builder.Write(l_Readback, Trinity::RHI::ResourceState::CopyDestination);
    }, [l_Table, l_Readback, l_Size](const Trinity::FrameGraphContext& context)
    {
        context.GetCommands().CopyBuffer(context.GetBuffer(l_Table), 0, context.GetBuffer(l_Readback), 0, l_Size);
    });
}

// Each glTF material as fastgltf reads it, with its texture slots naming the texture the importer made for that glTF texture and use, or the texture file its image names, and the same UV sets. Compared with the material as imported, before any overrides
bool ImportTest::CheckMaterialMapping()
{
    const Trinity::AssetRegistry& l_Registry = *m_Session.GetRegistry();
    bool l_Passed = true;
    std::size_t l_Materials = 0;
    std::size_t l_Textures = 0;
    for (const TestModel& it_Model : c_TestModels)
    {
        const Trinity::AssetRecord* l_Record = l_Registry.FindByPath(GetTestModelPath(it_Model));
        if (l_Record == nullptr)
        {
            continue;
        }

        const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Source = Trinity::FileSystem::ReadFile(l_Record->Path);
        fastgltf::Expected<fastgltf::GltfDataBuffer> l_Data = l_Source ? fastgltf::GltfDataBuffer::FromBytes(l_Source->data(), l_Source->size()) : fastgltf::Expected<fastgltf::GltfDataBuffer>(fastgltf::Error::InvalidPath);
        fastgltf::Parser l_Parser(fastgltf::Extensions::KHR_materials_emissive_strength | fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_transform);
        fastgltf::Expected<fastgltf::Asset> l_Asset = l_Data.error() == fastgltf::Error::None ? l_Parser.loadGltf(l_Data.get(), {}, fastgltf::Options::None, fastgltf::Category::Materials | fastgltf::Category::Textures | fastgltf::Category::Images) : fastgltf::Expected<fastgltf::Asset>(l_Data.error());
        if (l_Asset.error() != fastgltf::Error::None)
        {
            TR_ERROR("Import test: {} could not be read with fastgltf for the material check", l_Record->Path);
            l_Passed = false;

            continue;
        }

        const auto a_SubAsset = [l_Record](std::string_view prefix) -> Trinity::UUID
        {
            const auto a_Found = std::ranges::find_if(l_Record->SubAssets, [prefix](const Trinity::SubAsset& subAsset) { return subAsset.Key == prefix || (subAsset.Key.starts_with(prefix) && subAsset.Key.size() > prefix.size() && subAsset.Key[prefix.size()] == '.'); });

            return a_Found != l_Record->SubAssets.end() ? a_Found->ID : Trinity::UUID();
        };

        // The texture the slot should name: the model's own for an image it holds, or the project's texture at the path its image names
        const auto a_Texture = [&](const auto& info, ModelImporter::TextureUsage usage) -> Trinity::MaterialTexture
        {
            if (!info)
            {
                return {};
            }

            const fastgltf::Texture& l_Texture = l_Asset->textures[info->textureIndex];
            const fastgltf::Image& l_Image = l_Asset->images[l_Texture.imageIndex.value_or(0)];
            Trinity::UUID l_ID;
            if (const auto* l_URI = std::get_if<fastgltf::sources::URI>(&l_Image.data))
            {
                const std::optional<std::string> l_Path = ModelReading::ResolvePath(l_Record->Path, l_URI->uri.path());
                const Trinity::AssetRecord* l_File = l_Path ? l_Registry.FindByPath(*l_Path) : nullptr;
                l_ID = l_File != nullptr ? l_File->ID : Trinity::UUID();
            }
            else
            {
                l_ID = a_SubAsset(std::format("Texture.{}.{}", info->textureIndex, ModelImporter::ToString(usage)));
            }

            return { l_ID, static_cast<std::uint32_t>(info->texCoordIndex) };
        };

        for (std::size_t it_Material = 0; it_Material < l_Asset->materials.size(); ++it_Material)
        {
            const fastgltf::Material& l_Gltf = l_Asset->materials[it_Material];
            Trinity::MaterialData l_Expected;
            const auto& l_Factor = l_Gltf.pbrData.baseColorFactor;
            l_Expected.BaseColorFactor = glm::vec4(l_Factor[0], l_Factor[1], l_Factor[2], l_Factor[3]);
            l_Expected.MetallicFactor = l_Gltf.pbrData.metallicFactor;
            l_Expected.RoughnessFactor = l_Gltf.pbrData.roughnessFactor;
            l_Expected.EmissiveFactor = glm::vec3(l_Gltf.emissiveFactor[0], l_Gltf.emissiveFactor[1], l_Gltf.emissiveFactor[2]);
            l_Expected.EmissiveStrength = l_Gltf.emissiveStrength;
            l_Expected.NormalScale = l_Gltf.normalTexture ? l_Gltf.normalTexture->scale : 1.0f;
            l_Expected.OcclusionStrength = l_Gltf.occlusionTexture ? l_Gltf.occlusionTexture->strength : 1.0f;
            l_Expected.AlphaMode = l_Gltf.alphaMode == fastgltf::AlphaMode::Mask ? Trinity::MaterialAlphaMode::Mask : (l_Gltf.alphaMode == fastgltf::AlphaMode::Blend ? Trinity::MaterialAlphaMode::Blend : Trinity::MaterialAlphaMode::Opaque);
            l_Expected.AlphaCutoff = l_Gltf.alphaCutoff;
            l_Expected.DoubleSided = l_Gltf.doubleSided;
            l_Expected.BaseColorTexture = a_Texture(l_Gltf.pbrData.baseColorTexture, ModelImporter::TextureUsage::Color);
            l_Expected.MetallicRoughnessTexture = a_Texture(l_Gltf.pbrData.metallicRoughnessTexture, ModelImporter::TextureUsage::Data);
            l_Expected.NormalTexture = a_Texture(l_Gltf.normalTexture, ModelImporter::TextureUsage::Normal);
            l_Expected.OcclusionTexture = a_Texture(l_Gltf.occlusionTexture, ModelImporter::TextureUsage::Data);
            l_Expected.EmissiveTexture = a_Texture(l_Gltf.emissiveTexture, ModelImporter::TextureUsage::Color);

            const Trinity::UUID l_ID = a_SubAsset(std::format("Material.{}", it_Material));
            const Trinity::Expected<std::string, Trinity::FileError> l_Text = l_ID.IsValid() ? Trinity::FileSystem::ReadText(ModelImporter::GetImportedMaterialPath(l_ID)) : Trinity::Expected<std::string, Trinity::FileError>(Trinity::Unexpected{ Trinity::FileError::NotFound });
            const Trinity::Expected<Trinity::MaterialData, std::string> l_Imported = l_Text ? Trinity::ParseMaterialData(*l_Text) : Trinity::Expected<Trinity::MaterialData, std::string>(Trinity::Unexpected{ std::string("it has no imported file") });
            if (!l_Imported)
            {
                TR_ERROR("Import test: material {} of {} does not read back: {}", it_Material, l_Record->Path, l_Imported.GetError());
                l_Passed = false;

                continue;
            }

            std::string l_Wrong;
            const std::vector<Trinity::MaterialField> l_Found = Trinity::GetMaterialFields(*l_Imported);
            const std::vector<Trinity::MaterialField> l_Wanted = Trinity::GetMaterialFields(l_Expected);
            for (std::size_t it_Field = 0; it_Field < l_Found.size(); ++it_Field)
            {
                if (l_Found[it_Field] != l_Wanted[it_Field])
                {
                    l_Wrong += std::format("{}{} is {} and glTF gives {}", l_Wrong.empty() ? "" : "; ", l_Found[it_Field].Name, l_Found[it_Field].Value, l_Wanted[it_Field].Value);
                }
            }

            const std::array<Trinity::MaterialTexture, 5> l_Slots{ l_Expected.BaseColorTexture, l_Expected.MetallicRoughnessTexture, l_Expected.NormalTexture, l_Expected.OcclusionTexture, l_Expected.EmissiveTexture };
            const std::size_t l_Named = static_cast<std::size_t>(std::ranges::count_if(l_Slots, [](const Trinity::MaterialTexture& texture) { return texture.Texture.IsValid(); }));
            const bool l_AllFound = l_Named == static_cast<std::size_t>((l_Gltf.pbrData.baseColorTexture ? 1 : 0) + (l_Gltf.pbrData.metallicRoughnessTexture ? 1 : 0) + (l_Gltf.normalTexture ? 1 : 0) + (l_Gltf.occlusionTexture ? 1 : 0) + (l_Gltf.emissiveTexture ? 1 : 0));
            if (!l_Wrong.empty() || !l_AllFound || *l_Imported != l_Expected)
            {
                TR_ERROR("Import test: material {} of {} does not map onto its material: {}", it_Material, l_Record->Path, l_Wrong.empty() ? std::string("a texture glTF names has no texture in the project") : l_Wrong);
                l_Passed = false;

                continue;
            }

            ++l_Materials;
            l_Textures += l_Named;
        }
    }

    if (l_Passed)
    {
        TR_INFO("Import test: {} glTF material(s) map onto their materials in every factor, and in {} texture slot(s) with their UV sets", l_Materials, l_Textures);
    }

    return l_Passed;
}

// Every cooked and imported material of every test model writes back as the same file, and a model whose .meta overrides none has the two the same
bool ImportTest::CheckMaterialFiles()
{
    bool l_Passed = true;
    std::size_t l_Files = 0;
    for (const std::string& it_Path : GetTestModelPaths())
    {
        const Trinity::AssetRecord* l_Record = m_Session.GetRegistry()->FindByPath(it_Path);
        if (l_Record == nullptr)
        {
            continue;
        }

        for (const Trinity::SubAsset& it_SubAsset : l_Record->SubAssets)
        {
            if (it_SubAsset.Importer != ModelImporter::c_MaterialImporter)
            {
                continue;
            }

            const Trinity::Expected<std::string, Trinity::FileError> l_Cooked = Trinity::FileSystem::ReadText(Trinity::GetCookedMaterialPath(it_SubAsset.ID));
            const Trinity::Expected<std::string, Trinity::FileError> l_Imported = Trinity::FileSystem::ReadText(ModelImporter::GetImportedMaterialPath(it_SubAsset.ID));
            const auto a_RoundTrips = [](const Trinity::Expected<std::string, Trinity::FileError>& text)
            {
                const Trinity::Expected<Trinity::MaterialData, std::string> l_Material = text ? Trinity::ParseMaterialData(*text) : Trinity::Expected<Trinity::MaterialData, std::string>(Trinity::Unexpected{ std::string() });

                return l_Material && Trinity::WriteMaterialData(*l_Material) == *text;
            };

            const bool l_Same = ModelImporter::ReadMaterialOverrides(*l_Record, it_SubAsset.Key).empty() ? l_Cooked && l_Imported && *l_Cooked == *l_Imported : true;
            if (!a_RoundTrips(l_Cooked) || !a_RoundTrips(l_Imported) || !l_Same)
            {
                TR_ERROR("Import test: {} of {} does not write back as the same file, or differs from its import with nothing overridden", it_SubAsset.Key, l_Record->Path);
                l_Passed = false;

                continue;
            }

            l_Files += 2;
        }
    }

    if (l_Passed)
    {
        TR_INFO("Import test: {} material file(s) of the test models write back byte for byte", l_Files);
    }

    return l_Passed;
}

// Every material of the glTF test models is loaded, with its textures, then the table is read back
void ImportTest::BeginMaterials()
{
    m_Materials.clear();
    m_MaterialIDs.clear();
    for (const TestModel& it_Model : c_TestModels)
    {
        const Trinity::AssetRecord* l_Record = m_Session.GetRegistry()->FindByPath(GetTestModelPath(it_Model));
        for (const Trinity::SubAsset& it_SubAsset : l_Record != nullptr ? l_Record->SubAssets : std::vector<Trinity::SubAsset>())
        {
            if (it_SubAsset.Importer == ModelImporter::c_MaterialImporter)
            {
                m_MaterialIDs.push_back(it_SubAsset.ID);
                m_Materials.emplace_back(it_SubAsset.ID);
            }
        }
    }

    m_MaterialsPassed = true;
    m_PhaseFrames = 0;
    m_Phase = Phase::LoadingMaterials;
    if (m_MaterialIDs.empty())
    {
        TR_WARN("Import test: there are no glTF test models, so only a material of its own is edited");
        BeginStandalone();
    }
}

bool ImportTest::AreTexturesReady(const Trinity::MaterialData& material) const
{
    for (const Trinity::MaterialTexture& it_Texture : { material.BaseColorTexture, material.MetallicRoughnessTexture, material.NormalTexture, material.OcclusionTexture, material.EmissiveTexture })
    {
        if (it_Texture.Texture.IsValid() && Trinity::AssetManager::GetState(it_Texture.Texture) != Trinity::AssetState::Ready)
        {
            return false;
        }
    }

    return true;
}

// The null device reads back nothing, so there only the table's own copy is compared
std::string ImportTest::CheckRecords(std::span<const Trinity::UUID> materials, bool readBack) const
{
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const Trinity::MaterialLoader& l_Loader = Trinity::Application::Get().GetRenderer().GetMaterialLoader();
    const bool l_Gpu = readBack && l_Device.GetInfo().API != Trinity::GraphicsAPI::None;
    const std::span<const std::byte> l_ReadBack = m_TableReadback ? l_Device.GetMappedData(m_TableReadback) : std::span<std::byte>();
    std::vector<std::uint32_t> l_Indices;
    for (const Trinity::UUID it_ID : materials)
    {
        const Trinity::Asset* l_Asset = Trinity::AssetManager::GetState(it_ID) == Trinity::AssetState::Ready ? Trinity::AssetManager::GetAsset(it_ID) : nullptr;
        if (l_Asset == nullptr || l_Asset->GetAssetType() != Trinity::MaterialAsset::c_AssetType)
        {
            return std::format("material {} is not loaded", it_ID);
        }

        const Trinity::MaterialAsset& l_Material = static_cast<const Trinity::MaterialAsset&>(*l_Asset);
        const std::uint32_t l_Index = l_Material.GetTableIndex();
        const std::size_t l_Offset = static_cast<std::size_t>(l_Index) * Trinity::MaterialLoader::c_RecordSize;
        const Trinity::MaterialLoader::Record l_Expected = Trinity::MaterialLoader::Pack(l_Material.GetData(), Trinity::MaterialLoader::ResolveTextures(l_Material.GetData()));
        if (l_Index == Trinity::MaterialLoader::c_DefaultIndex || std::ranges::find(l_Indices, l_Index) != l_Indices.end() || l_Offset + l_Expected.size() > l_Loader.GetRecords().size())
        {
            return std::format("material {} has record {}, which is the default's, another material's or past the table", it_ID, l_Index);
        }

        l_Indices.push_back(l_Index);
        if (std::memcmp(l_Loader.GetRecords().data() + l_Offset, l_Expected.data(), l_Expected.size()) != 0)
        {
            return std::format("material {}'s record {} in the table's copy is not its data and textures", it_ID, l_Index);
        }

        if (l_Gpu && (l_Offset + l_Expected.size() > l_ReadBack.size() || std::memcmp(l_ReadBack.data() + l_Offset, l_Expected.data(), l_Expected.size()) != 0))
        {
            return std::format("material {}'s record {} on the GPU is not its data and textures", it_ID, l_Index);
        }
    }

    return {};
}

void ImportTest::UpdateMaterials()
{
    switch (m_Phase)
    {
        case Phase::LoadingMaterials:
        {
            const bool l_Failed = std::ranges::any_of(m_Materials, [](const auto& material) { return material.GetState() == Trinity::AssetState::Failed; });
            const bool l_Ready = std::ranges::all_of(m_Materials, [this](const auto& material) { return material.IsReady() && AreTexturesReady(material.Get()->GetData()); });
            if (l_Failed || (!l_Ready && m_PhaseFrames > c_LoadTimeoutFrames))
            {
                TR_ERROR("Import test: the test models' materials {} after {} frame(s)", l_Failed ? "failed to load" : "or their textures were still loading", m_PhaseFrames);
                FinishMaterials(false);
            }
            else if (l_Ready)
            {
                m_ReadbackWanted = true;
                m_Phase = Phase::ReadingMaterials;
            }

            break;
        }
        case Phase::ReadingMaterials:
        {
            if (!std::exchange(m_ReadbackAdded, false))
            {
                break;
            }

            Trinity::Application::Get().GetDevice().WaitIdle();
            const std::string l_Wrong = CheckRecords(m_MaterialIDs, true);
            if (!l_Wrong.empty())
            {
                TR_ERROR("Import test: {}", l_Wrong);
                FinishMaterials(false);

                break;
            }

            TR_INFO("Import test: {} material(s) of the glTF test models hold records of their own in the material table, naming their loaded textures, on the GPU as in the table's copy", m_MaterialIDs.size());

            // A material with a normal map, so clearing a texture slot is edited too
            const auto a_Edited = std::ranges::find_if(m_MaterialIDs, [](Trinity::UUID id) { return static_cast<const Trinity::MaterialAsset*>(Trinity::AssetManager::GetAsset(id))->GetData().NormalTexture.Texture.IsValid(); });
            m_Edited = a_Edited != m_MaterialIDs.end() ? *a_Edited : m_MaterialIDs.front();
            m_EditBefore = static_cast<const Trinity::MaterialAsset*>(Trinity::AssetManager::GetAsset(m_Edited))->GetData();
            const Trinity::AssetRecord* l_Model = m_Session.GetRegistry()->Find(m_Session.GetRegistry()->Find(m_Edited)->Parent);
            const Trinity::Expected<std::string, Trinity::FileError> l_Cooked = Trinity::FileSystem::ReadText(Trinity::GetCookedMaterialPath(m_Edited));
            const Trinity::Expected<std::string, Trinity::FileError> l_Meta = Trinity::FileSystem::ReadText(l_Model->Path + std::string(Trinity::AssetRegistry::c_MetaExtension));
            m_EditBeforeCooked = l_Cooked ? *l_Cooked : std::string();
            m_EditBeforeMeta = l_Meta ? *l_Meta : std::string();
            m_EditFrames = 0;
            m_EditLate = 0;
            m_Phase = Phase::EditingMaterial;

            break;
        }
        case Phase::EditingMaterial:
        {
            EditMaterial();

            break;
        }
        case Phase::ReadingEdit:
        {
            if (std::exchange(m_ReadbackAdded, false))
            {
                FinishEdit();
            }

            break;
        }
        case Phase::ReadingUndo:
        {
            if (std::exchange(m_ReadbackAdded, false))
            {
                FinishUndo();
            }

            break;
        }
        case Phase::LoadingRender:
        case Phase::Rendering:
        {
            UpdateRendering();

            break;
        }
        case Phase::LoadingStandalone:
        {
            if (m_Standalone.GetState() == Trinity::AssetState::Failed || m_PhaseFrames > c_LoadTimeoutFrames)
            {
                TR_ERROR("Import test: the material of its own {} after {} frame(s)", m_Standalone.GetState() == Trinity::AssetState::Failed ? "failed to load" : "was still loading", m_PhaseFrames);
                FinishMaterials(false);
            }
            else if (m_Standalone.IsReady() && AreTexturesReady(m_Standalone.Get()->GetData()))
            {
                FinishStandalone();
            }

            break;
        }
        default:
        {
            break;
        }
    }
}

// A thousand frames of edits, each written where the material lives as a drag's are. Each must be in the table by the frame after it, and the renderer's memory must not change after the first hundred frames
void ImportTest::EditMaterial()
{
    const Trinity::MaterialLoader& l_Loader = Trinity::Application::Get().GetRenderer().GetMaterialLoader();
    const Trinity::MaterialAsset* l_Material = static_cast<const Trinity::MaterialAsset*>(Trinity::AssetManager::GetAsset(m_Edited));
    if (m_EditFrames > 0)
    {
        const Trinity::MaterialLoader::Record l_Expected = Trinity::MaterialLoader::Pack(m_EditLast, Trinity::MaterialLoader::ResolveTextures(m_EditLast));
        const bool l_Shown = l_Material->GetData() == m_EditLast && std::memcmp(l_Loader.GetRecords().data() + static_cast<std::size_t>(l_Material->GetTableIndex()) * Trinity::MaterialLoader::c_RecordSize, l_Expected.data(), l_Expected.size()) == 0;
        m_EditLate += l_Shown ? 0 : 1;
    }

    if (m_EditFrames == c_EditWarmupFrames)
    {
        m_EditMemory = Trinity::Memory::GetStats(Trinity::MemoryTag::Renderer).CurrentBytes;
    }

    if (m_EditFrames == c_EditFrames)
    {
        const std::uint64_t l_Memory = Trinity::Memory::GetStats(Trinity::MemoryTag::Renderer).CurrentBytes;
        if (m_EditLate != 0 || l_Memory != m_EditMemory)
        {
            TR_ERROR("Import test: over {} frames of material edits, {} were not in the table the frame after, and the renderer's memory went from {} to {}", c_EditFrames, m_EditLate, Trinity::Memory::FormatBytes(m_EditMemory), Trinity::Memory::FormatBytes(l_Memory));
            m_MaterialsPassed = false;
        }
        else
        {
            TR_INFO("Import test: {} frames of material edits each showed in the table the frame after, with the renderer's memory at {} from frame {} on", c_EditFrames, Trinity::Memory::FormatBytes(l_Memory), c_EditWarmupFrames);
        }

        // Two edits of one field in a row, as a drag makes, become one command
        static_cast<void>(m_Session.WriteMaterial(m_Edited, m_EditBefore));
        m_Session.GetHistory().EndMerge();
        m_EditHistory = m_Session.GetHistory().GetCount();
        Trinity::MaterialData l_First = m_EditBefore;
        l_First.RoughnessFactor = 0.125f;
        Trinity::MaterialData l_Second = l_First;
        l_Second.RoughnessFactor = 0.875f;
        l_Second.NormalTexture = {};
        const bool l_Edited = m_Session.EditMaterial(m_Edited, m_EditBefore, l_First, "Import test") && m_Session.EditMaterial(m_Edited, l_First, l_Second, "Import test");
        if (!l_Edited || m_Session.GetHistory().GetCount() != m_EditHistory + 1)
        {
            TR_ERROR("Import test: two edits of one material field made {} command(s), and should have made one", m_Session.GetHistory().GetCount() - m_EditHistory);
            m_MaterialsPassed = false;
        }

        m_EditLast = l_Second;
        m_ReadbackWanted = true;
        m_Phase = Phase::ReadingEdit;

        return;
    }

    Trinity::MaterialData l_Edit = m_EditBefore;
    l_Edit.RoughnessFactor = static_cast<float>(m_EditFrames % 100) / 100.0f;
    l_Edit.BaseColorFactor = glm::vec4(static_cast<float>(m_EditFrames % 7) / 7.0f, 0.5f, 0.25f, 1.0f);
    l_Edit.NormalTexture = {};
    if (!m_Session.WriteMaterial(m_Edited, l_Edit))
    {
        TR_ERROR("Import test: material {} could not be edited", m_Edited);
        FinishMaterials(false);

        return;
    }

    m_EditLast = l_Edit;
    ++m_EditFrames;
}

// The edit is on the GPU, in the cooked file and as overrides in the model's .meta. One undo then puts back the material, its file and the .meta byte for byte
void ImportTest::FinishEdit()
{
    Trinity::Application::Get().GetDevice().WaitIdle();
    const Trinity::AssetRecord* l_Model = m_Session.GetRegistry()->Find(m_Session.GetRegistry()->Find(m_Edited)->Parent);
    const std::string l_Key = m_Session.GetMaterialKey(m_Edited).value_or(std::string());
    const std::string l_Wrong = CheckRecords(std::span(&m_Edited, 1), true);
    const Trinity::Expected<std::string, Trinity::FileError> l_Cooked = Trinity::FileSystem::ReadText(Trinity::GetCookedMaterialPath(m_Edited));
    const std::vector<Trinity::MaterialField> l_Overrides = ModelImporter::ReadMaterialOverrides(*l_Model, l_Key);
    if (!l_Wrong.empty() || !l_Cooked || *l_Cooked != Trinity::WriteMaterialData(m_EditLast) || l_Overrides.size() != 2)
    {
        TR_ERROR("Import test: an edit of {} is not on the GPU, in its cooked file, or as its 2 overrides in {}'s .meta, which has {}{}", l_Key, l_Model->Path, l_Overrides.size(), l_Wrong.empty() ? std::string() : std::format(": {}", l_Wrong));
        m_MaterialsPassed = false;
    }

    m_Session.GetHistory().Undo();
    m_ReadbackWanted = true;
    m_Phase = Phase::ReadingUndo;
}

// The material, its cooked file and the .meta are as before the edit. A reimport then keeps an override, which an edit back to the import takes out again
void ImportTest::FinishUndo()
{
    Trinity::Application::Get().GetDevice().WaitIdle();
    const Trinity::AssetRecord* l_Model = m_Session.GetRegistry()->Find(m_Session.GetRegistry()->Find(m_Edited)->Parent);
    const std::string l_MetaPath = l_Model->Path + std::string(Trinity::AssetRegistry::c_MetaExtension);
    const std::string l_Wrong = CheckRecords(std::span(&m_Edited, 1), true);
    const Trinity::Expected<std::string, Trinity::FileError> l_Cooked = Trinity::FileSystem::ReadText(Trinity::GetCookedMaterialPath(m_Edited));
    const Trinity::Expected<std::string, Trinity::FileError> l_Meta = Trinity::FileSystem::ReadText(l_MetaPath);
    const Trinity::MaterialAsset* l_Material = static_cast<const Trinity::MaterialAsset*>(Trinity::AssetManager::GetAsset(m_Edited));
    if (!l_Wrong.empty() || l_Material->GetData() != m_EditBefore || !l_Cooked || *l_Cooked != m_EditBeforeCooked || !l_Meta || *l_Meta != m_EditBeforeMeta)
    {
        TR_ERROR("Import test: one undo did not put back the material, its cooked file and {} as they were{}", l_MetaPath, l_Wrong.empty() ? std::string() : std::format(": {}", l_Wrong));
        m_MaterialsPassed = false;
    }
    else
    {
        TR_INFO("Import test: an edit of a material showed on the GPU the next frame, and one undo put back the GPU record, the cooked file and the .meta byte for byte");
    }

    Trinity::MaterialData l_Overridden = m_EditBefore;
    l_Overridden.EmissiveStrength = 3.5f;
    static_cast<void>(m_Session.WriteMaterial(m_Edited, l_Overridden));
    static_cast<void>(Trinity::FileSystem::RemoveFile(ModelImporter::GetCacheKeyPath(l_Model->ID)));
    m_Session.ScanAssets();
    const EditorSession::ImportReport l_Report = m_Session.WaitForImports();
    const Trinity::Expected<std::string, Trinity::FileError> l_Reimported = Trinity::FileSystem::ReadText(Trinity::GetCookedMaterialPath(m_Edited));
    const bool l_Kept = l_Report.Models.Imported == 1 && l_Reimported && *l_Reimported == Trinity::WriteMaterialData(l_Overridden);

    static_cast<void>(m_Session.WriteMaterial(m_Edited, m_EditBefore));
    const Trinity::Expected<std::string, Trinity::FileError> l_Restored = Trinity::FileSystem::ReadText(l_MetaPath);
    if (!l_Kept || !l_Restored || *l_Restored != m_EditBeforeMeta)
    {
        TR_ERROR("Import test: a reimport of {} lost a material override, or an edit back to the import left the .meta different", l_Model->Path);
        m_MaterialsPassed = false;
    }
    else
    {
        TR_INFO("Import test: a reimport of {} kept its material override, and an edit back to the import left the .meta as it was", l_Model->Path);
    }

    BeginStandalone();
}

// A material of its own, made as the Content Browser makes one, then given colours, a mask, both sides and a texture on the second UV set
void ImportTest::BeginStandalone()
{
    const std::optional<std::string> l_Path = m_Session.CreateMaterial(Trinity::Project::c_AssetsMount);
    const Trinity::AssetRecord* l_Record = l_Path ? m_Session.GetRegistry()->FindByPath(*l_Path) : nullptr;
    if (l_Record == nullptr || l_Record->Importer != Trinity::MaterialAsset::c_AssetType)
    {
        TR_ERROR("Import test: a material of its own could not be created");
        FinishMaterials(false);

        return;
    }

    m_StandalonePath = *l_Path;
    m_StandaloneData = *m_Session.ReadMaterial(l_Record->ID);
    m_StandaloneData.BaseColorFactor = glm::vec4(0.2f, 0.4f, 0.6f, 0.75f);
    m_StandaloneData.EmissiveFactor = glm::vec3(0.1f, 0.0f, 0.3f);
    m_StandaloneData.AlphaMode = Trinity::MaterialAlphaMode::Mask;
    m_StandaloneData.AlphaCutoff = 0.4f;
    m_StandaloneData.DoubleSided = true;
    for (const Trinity::AssetRecord* it_Record : m_Session.GetRegistry()->GetRecords())
    {
        if (IsTestTexture(*it_Record))
        {
            m_StandaloneData.BaseColorTexture = { it_Record->ID, 1 };

            break;
        }
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_Text = m_Session.WriteMaterial(l_Record->ID, m_StandaloneData) ? Trinity::FileSystem::ReadText(m_StandalonePath) : Trinity::Expected<std::string, Trinity::FileError>(Trinity::Unexpected{ Trinity::FileError::NotFound });
    const Trinity::Expected<Trinity::MaterialData, std::string> l_Read = l_Text ? Trinity::ParseMaterialData(*l_Text) : Trinity::Expected<Trinity::MaterialData, std::string>(Trinity::Unexpected{ std::string() });
    if (!l_Read || *l_Read != m_StandaloneData || Trinity::WriteMaterialData(*l_Read) != *l_Text)
    {
        TR_ERROR("Import test: {} does not read back as written, or does not write back as the same file", m_StandalonePath);
        FinishMaterials(false);

        return;
    }

    m_Standalone = Trinity::AssetRef<Trinity::MaterialAsset>(l_Record->ID);
    m_PhaseFrames = 0;
    m_Phase = Phase::LoadingStandalone;
}

// Loaded as written, then written again into the same bytes, and deleted
void ImportTest::FinishStandalone()
{
    const Trinity::UUID l_ID = m_Standalone.GetID();
    const Trinity::Expected<std::string, Trinity::FileError> l_Before = Trinity::FileSystem::ReadText(m_StandalonePath);
    const bool l_Loaded = m_Standalone.Get()->GetData() == m_StandaloneData && CheckRecords(std::span(&l_ID, 1), false).empty();
    const bool l_Written = m_Session.WriteMaterial(l_ID, *m_Session.ReadMaterial(l_ID));
    const Trinity::Expected<std::string, Trinity::FileError> l_After = Trinity::FileSystem::ReadText(m_StandalonePath);
    if (!l_Loaded || !l_Written || !l_Before || !l_After || *l_Before != *l_After)
    {
        TR_ERROR("Import test: {} did not load as written, or did not save and reload into the same file", m_StandalonePath);
        m_MaterialsPassed = false;
    }
    else
    {
        TR_INFO("Import test: {} loaded as written, with its texture, and saved and reloaded into the same file", m_StandalonePath);
    }

    m_Standalone = {};
    static_cast<void>(m_Session.DeleteAsset(m_StandalonePath));
    FinishMaterials(m_MaterialsPassed);
}

void ImportTest::FinishMaterials(bool passed)
{
    m_Materials.clear();
    m_Standalone = {};
    if (passed)
    {
        TR_INFO("Import test: materials passed");
    }

    BeginRendering();
}

void ImportTest::Finish(bool passed)
{
    m_RenderScene.reset();
    m_CullScene.reset();
    m_RenderDraws.Clear();
    m_CullDraws.Clear();
    m_Phase = Phase::Idle;
    if (passed)
    {
        TR_INFO("Import test: rendering passed");
    }

    TR_INFO("Import test: finished, and {} asset(s) are still loaded", Trinity::AssetManager::GetEntryCount());
}

// MetalRoughSpheres in a scene of its own, with a perspective camera framing it and a directional light from over the camera's shoulder, and its meshes scattered at random through another scene for the culling check. Both are collected every frame until every mesh, material and texture they use has loaded
void ImportTest::BeginRendering()
{
    const auto a_Model = std::ranges::find(c_TestModels, std::string_view("MetalRoughSpheres"), &TestModel::Name);
    const Trinity::AssetRecord* l_Record = m_Session.GetRegistry()->FindByPath(GetTestModelPath(*a_Model));
    const Trinity::Expected<std::string, Trinity::FileError> l_Text = l_Record != nullptr ? Trinity::FileSystem::ReadText(Trinity::GetCookedModelPath(l_Record->ID)) : Trinity::Expected<std::string, Trinity::FileError>(Trinity::Unexpected{ Trinity::FileError::NotFound });
    const Trinity::Expected<Trinity::ModelData, std::string> l_Model = l_Text ? Trinity::ParseModelData(*l_Text) : Trinity::Expected<Trinity::ModelData, std::string>(Trinity::Unexpected{ std::string() });
    if (!l_Model)
    {
        TR_WARN("Import test: MetalRoughSpheres is not in the project, so rendering is not tested");
        Finish(true);

        return;
    }

    m_RenderScene = std::make_unique<Trinity::Scene>();
    static_cast<void>(CreateModelEntities(*m_RenderScene, *l_Model, "MetalRoughSpheres", {}));

    std::vector<const Trinity::ModelNode*> l_MeshNodes;
    m_RenderMaterials.clear();
    for (const Trinity::ModelNode& it_Node : l_Model->Nodes)
    {
        if (it_Node.Mesh.IsValid())
        {
            l_MeshNodes.push_back(&it_Node);
            std::ranges::copy_if(it_Node.Materials, std::back_inserter(m_RenderMaterials), [](Trinity::UUID id) { return id.IsValid(); });
        }
    }

    // Random places, turns and scales, some of them mirroring, of the model's own meshes and materials
    m_CullScene = std::make_unique<Trinity::Scene>();
    std::mt19937 l_Random(9);
    std::uniform_real_distribution<float> l_Unit(-1.0f, 1.0f);
    for (std::uint32_t it_Entity = 0; it_Entity < c_CullEntities && !l_MeshNodes.empty(); ++it_Entity)
    {
        const Trinity::ModelNode& l_Node = *l_MeshNodes[l_Random() % l_MeshNodes.size()];
        Trinity::Entity l_Entity = m_CullScene->CreateEntity(std::format("Scattered {}", it_Entity));
        Trinity::TransformComponent& l_Transform = l_Entity.Get<Trinity::TransformComponent>();
        l_Transform.Position = glm::vec3(l_Unit(l_Random), l_Unit(l_Random), l_Unit(l_Random)) * 20.0f;
        l_Transform.Rotation = glm::normalize(glm::quat(l_Unit(l_Random), l_Unit(l_Random), l_Unit(l_Random), l_Unit(l_Random)) + glm::quat(0.001f, 0.0f, 0.0f, 0.0f));
        l_Transform.Scale = glm::vec3(0.2f + std::abs(l_Unit(l_Random)) * 2.8f, 0.2f + std::abs(l_Unit(l_Random)) * 2.8f, 0.2f + std::abs(l_Unit(l_Random)) * 2.8f) * (it_Entity % 5 == 0 ? -1.0f : 1.0f);
        Trinity::MeshRendererComponent& l_Renderer = l_Entity.Add<Trinity::MeshRendererComponent>();
        l_Renderer.Mesh = l_Node.Mesh;
        l_Renderer.Materials.assign(l_Node.Materials.begin(), l_Node.Materials.end());
    }

    m_RenderScene->UpdateWorldTransforms();
    m_CullScene->UpdateWorldTransforms();
    m_PhaseFrames = 0;
    m_Phase = Phase::LoadingRender;
}

void ImportTest::UpdateRendering()
{
    Trinity::Renderer3D& l_Renderer = Trinity::Application::Get().GetRenderer().GetRenderer3D();
    if (m_Phase == Phase::Rendering)
    {
        if (std::exchange(m_RenderAdded, false))
        {
            FinishRendering();
        }

        return;
    }

    // Collecting keeps the meshes and materials loaded, and the camera is only placed once their bounds are known
    l_Renderer.Collect(*m_RenderScene, Trinity::RenderView(), m_RenderDraws);
    l_Renderer.Collect(*m_CullScene, Trinity::RenderView(), m_CullDraws);
    const bool l_Materials = std::ranges::all_of(m_RenderMaterials, [this](Trinity::UUID id)
    {
        const Trinity::Asset* l_Asset = Trinity::AssetManager::GetState(id) == Trinity::AssetState::Ready ? Trinity::AssetManager::GetAsset(id) : nullptr;

        return l_Asset != nullptr && AreTexturesReady(static_cast<const Trinity::MaterialAsset*>(l_Asset)->GetData());
    });

    if (m_RenderDraws.Pending != 0 || m_CullDraws.Pending != 0 || !l_Materials)
    {
        if (m_PhaseFrames > c_LoadTimeoutFrames)
        {
            TR_ERROR("Import test: MetalRoughSpheres' meshes, materials or textures were still loading after {} frame(s)", m_PhaseFrames);
            Finish(false);
        }

        return;
    }

    if (!CheckCulling())
    {
        Finish(false);

        return;
    }

    // Framed from +Z, the side the spheres face, with the whole model inside a 45 degree field of view, and lit from above and behind the camera
    glm::vec3 l_Minimum(std::numeric_limits<float>::max());
    glm::vec3 l_Maximum(std::numeric_limits<float>::lowest());
    Trinity::SceneRegistry& l_Registry = m_RenderScene->GetRegistry();
    for (Trinity::Entity it_Entity = m_RenderScene->GetFirstRoot(); it_Entity; it_Entity = m_RenderScene->GetNextInHierarchyOrder(it_Entity))
    {
        const Trinity::MeshRendererComponent* l_Mesh = l_Registry.try_get<Trinity::MeshRendererComponent>(it_Entity.GetHandle());
        const Trinity::Asset* l_Asset = l_Mesh != nullptr ? Trinity::AssetManager::GetAsset(l_Mesh->Mesh) : nullptr;
        if (l_Asset == nullptr)
        {
            continue;
        }

        glm::vec3 l_Center;
        glm::vec3 l_Extents;
        Trinity::GetWorldBounds(static_cast<const Trinity::MeshAsset*>(l_Asset)->GetBounds(), l_Registry.get<Trinity::WorldTransformComponent>(it_Entity.GetHandle()).Matrix, l_Center, l_Extents);
        l_Minimum = glm::min(l_Minimum, l_Center - l_Extents);
        l_Maximum = glm::max(l_Maximum, l_Center + l_Extents);
    }

    const glm::vec3 l_Center = (l_Minimum + l_Maximum) * 0.5f;
    const float l_Radius = glm::length(l_Maximum - l_Minimum) * 0.5f;
    Trinity::Entity l_Camera = m_RenderScene->CreateEntity("Camera");
    Trinity::CameraComponent& l_CameraComponent = l_Camera.Add<Trinity::CameraComponent>();
    l_CameraComponent.Projection = Trinity::CameraProjection::Perspective;
    l_CameraComponent.FieldOfView = 45.0f;
    l_CameraComponent.PerspectiveNear = l_Radius * 0.01f;
    l_Camera.Get<Trinity::TransformComponent>().Position = l_Center + glm::vec3(0.0f, 0.0f, l_Radius / std::sin(glm::radians(22.5f)) + l_Radius * 0.1f);

    Trinity::Entity l_Light = m_RenderScene->CreateEntity("Sun");
    l_Light.Add<Trinity::LightComponent>();
    l_Light.Get<Trinity::TransformComponent>().Rotation = glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f), glm::normalize(glm::vec3(0.3f, 0.6f, 1.0f)));
    AddTestLights(l_Minimum, l_Maximum, l_Radius);
    m_RenderScene->UpdateWorldTransforms();
    m_RenderView = Trinity::RenderView::FromCamera(l_Camera.Get<Trinity::CameraComponent>(), l_Camera.Get<Trinity::WorldTransformComponent>().Matrix, 1.0f);

    m_RenderWanted = true;
    m_Phase = Phase::Rendering;
}

// Point and spot lights scattered through and around the model, each reaching a small part of it. Half have a range of their own and half work it out from an intensity chosen to give the same range, and spot lights point every way with cones from narrow to wide
void ImportTest::AddTestLights(const glm::vec3& minimum, const glm::vec3& maximum, float radius)
{
    std::mt19937 l_Random(23);
    std::uniform_real_distribution<float> l_Unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> l_Fraction(0.0f, 1.0f);
    const glm::vec3 l_Center = (minimum + maximum) * 0.5f;
    const glm::vec3 l_HalfSize = (maximum - minimum) * 0.6f + glm::vec3(radius * 0.05f);
    for (std::uint32_t it_Light = 0; it_Light < c_TestLights; ++it_Light)
    {
        Trinity::Entity l_Entity = m_RenderScene->CreateEntity(std::format("Light {}", it_Light));
        Trinity::TransformComponent& l_Transform = l_Entity.Get<Trinity::TransformComponent>();
        l_Transform.Position = l_Center + glm::vec3(l_Unit(l_Random), l_Unit(l_Random), l_Unit(l_Random)) * l_HalfSize;
        l_Transform.Rotation = glm::normalize(glm::quat(l_Unit(l_Random), l_Unit(l_Random), l_Unit(l_Random), l_Unit(l_Random)) + glm::quat(0.001f, 0.0f, 0.0f, 0.0f));

        Trinity::LightComponent& l_Light = l_Entity.Add<Trinity::LightComponent>();
        l_Light.Type = l_Fraction(l_Random) < 0.4f ? Trinity::LightType::Spot : Trinity::LightType::Point;
        l_Light.Color = glm::vec3(0.25f + l_Fraction(l_Random) * 0.75f, 0.25f + l_Fraction(l_Random) * 0.75f, 0.25f + l_Fraction(l_Random) * 0.75f);
        l_Light.OuterConeAngle = 10.0f + l_Fraction(l_Random) * 70.0f;
        l_Light.InnerConeAngle = l_Light.OuterConeAngle * l_Fraction(l_Random) * 0.9f;

        const float l_Range = radius * (0.03f + l_Fraction(l_Random) * 0.17f);
        if (it_Light % 2 == 0)
        {
            l_Light.Range = l_Range;
            l_Light.Intensity = (0.5f + l_Fraction(l_Random)) * l_Range * l_Range * 0.25f;
        }
        else
        {
            l_Light.Range = 0.0f;
            l_Light.Intensity = Trinity::LightComponent::c_AutomaticRangeCutoff * l_Range * l_Range / std::max({ l_Light.Color.r, l_Light.Color.g, l_Light.Color.b });
        }
    }
}

// Over random poses, perspective and orthographic, the culled and sorted list must equal the one tested a submesh at a time, and nothing culled may reach into the view: every corner of each culled submesh's box lies outside one side of the frustum
bool ImportTest::CheckCulling()
{
    Trinity::Renderer3D& l_Renderer = Trinity::Application::Get().GetRenderer().GetRenderer3D();
    Trinity::SceneDrawList l_Reference;
    std::mt19937 l_Random(17);
    std::uniform_real_distribution<float> l_Unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> l_Fraction(0.0f, 1.0f);
    std::uint32_t l_Mismatches = 0;
    std::uint32_t l_Unsafe = 0;
    std::uint64_t l_Drawn = 0;
    std::uint64_t l_Culled = 0;
    Trinity::SceneRegistry& l_Registry = m_CullScene->GetRegistry();
    for (std::uint32_t it_Pose = 0; it_Pose < c_CullPoses; ++it_Pose)
    {
        Trinity::CameraComponent l_Camera;
        l_Camera.Projection = it_Pose % 5 == 0 ? Trinity::CameraProjection::Orthographic : Trinity::CameraProjection::Perspective;
        l_Camera.FieldOfView = 20.0f + l_Fraction(l_Random) * 100.0f;
        l_Camera.PerspectiveNear = 0.01f + l_Fraction(l_Random);
        l_Camera.OrthographicSize = 1.0f + l_Fraction(l_Random) * 40.0f;
        l_Camera.Near = -50.0f;
        l_Camera.Far = 50.0f;
        Trinity::TransformComponent l_Pose;
        l_Pose.Position = glm::vec3(l_Unit(l_Random), l_Unit(l_Random), l_Unit(l_Random)) * 40.0f;
        l_Pose.Rotation = glm::normalize(glm::quat(l_Unit(l_Random), l_Unit(l_Random), l_Unit(l_Random), l_Unit(l_Random)) + glm::quat(0.001f, 0.0f, 0.0f, 0.0f));
        const Trinity::RenderView l_View = Trinity::RenderView::FromCamera(l_Camera, l_Pose.GetMatrix(), 0.5f + l_Fraction(l_Random) * 1.5f);

        l_Renderer.Collect(*m_CullScene, l_View, m_CullDraws);
        Trinity::Renderer3D::CollectReference(*m_CullScene, l_View, l_Reference);
        const bool l_Same = m_CullDraws.Draws.size() == l_Reference.Draws.size() && std::ranges::equal(m_CullDraws.Draws, l_Reference.Draws) && m_CullDraws.Submeshes == l_Reference.Submeshes && m_CullDraws.Culled == l_Reference.Culled;
        l_Mismatches += l_Same ? 0 : 1;
        l_Drawn += m_CullDraws.Draws.size();
        l_Culled += m_CullDraws.Culled;

        // A box is culled only when wholly outside one plane, so its eight corners must all be on the outside of one side of clip space
        for (Trinity::Entity it_Entity = m_CullScene->GetFirstRoot(); it_Entity; it_Entity = m_CullScene->GetNextInHierarchyOrder(it_Entity))
        {
            const Trinity::MeshRendererComponent& l_Mesh = it_Entity.Get<Trinity::MeshRendererComponent>();
            const Trinity::MeshAsset* l_Asset = static_cast<const Trinity::MeshAsset*>(Trinity::AssetManager::GetAsset(l_Mesh.Mesh));
            const glm::mat4 l_Clip = l_View.ViewProjection * l_Registry.get<Trinity::WorldTransformComponent>(it_Entity.GetHandle()).Matrix;
            for (std::uint32_t it_Submesh = 0; it_Submesh < l_Asset->GetSubmeshes().size(); ++it_Submesh)
            {
                const bool l_IsDrawn = std::ranges::any_of(m_CullDraws.Draws, [&](const Trinity::MeshDraw& draw) { return draw.Entity == it_Entity.GetUUID() && draw.Submesh == it_Submesh; });
                if (l_IsDrawn)
                {
                    continue;
                }

                const Trinity::MeshBounds& l_Bounds = l_Asset->GetSubmeshes()[it_Submesh].Bounds;
                std::array<int, 6> l_Outside{};
                for (std::uint32_t it_Corner = 0; it_Corner < 8; ++it_Corner)
                {
                    const glm::vec4 l_Point = l_Clip * glm::vec4((it_Corner & 1) != 0 ? l_Bounds.Max.x : l_Bounds.Min.x, (it_Corner & 2) != 0 ? l_Bounds.Max.y : l_Bounds.Min.y, (it_Corner & 4) != 0 ? l_Bounds.Max.z : l_Bounds.Min.z, 1.0f);
                    const float l_Slack = 1e-4f * std::max(std::abs(l_Point.w), 1.0f);
                    l_Outside[0] += l_Point.x < -l_Point.w + l_Slack ? 1 : 0;
                    l_Outside[1] += l_Point.x > l_Point.w - l_Slack ? 1 : 0;
                    l_Outside[2] += l_Point.y < -l_Point.w + l_Slack ? 1 : 0;
                    l_Outside[3] += l_Point.y > l_Point.w - l_Slack ? 1 : 0;
                    l_Outside[4] += l_Point.z > l_Point.w - l_Slack ? 1 : 0;
                    l_Outside[5] += l_Point.z < l_Slack ? 1 : 0;
                }

                l_Unsafe += std::ranges::find(l_Outside, 8) != l_Outside.end() ? 0 : 1;
            }
        }
    }

    if (l_Mismatches != 0 || l_Unsafe != 0)
    {
        TR_ERROR("Import test: over {} random poses, {} culled list(s) differed from the reference, and {} submesh(es) culled while reaching into the view", c_CullPoses, l_Mismatches, l_Unsafe);

        return false;
    }

    TR_INFO("Import test: over {} random poses of {} scattered meshes, every culled and sorted list equalled the reference, {} draws kept and {} culled, and nothing culled reached into the view", c_CullPoses, c_CullEntities, l_Drawn, l_Culled);

    return true;
}

// Into a target of the test's own, with its depth, both then copied for reading back
void ImportTest::AddRenderPasses(Trinity::FrameGraph& graph)
{
    m_RenderWanted = false;
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    Trinity::RHI::TextureDescription l_Description;
    l_Description.Width = c_RenderSize;
    l_Description.Height = c_RenderSize;
    l_Description.TextureFormat = Trinity::RHI::Format::RGBA16Float;
    l_Description.Usage = Trinity::RHI::TextureUsage::RenderTarget | Trinity::RHI::TextureUsage::ShaderResource | Trinity::RHI::TextureUsage::CopySource;
    l_Description.ClearColor = c_RenderClear;
    l_Description.DebugName = "Import test render target";
    const std::uint64_t l_ColorSize = Trinity::RHI::GetTextureCopyRowPitch(Trinity::RHI::Format::RGBA16Float, c_RenderSize) * c_RenderSize;
    const std::uint64_t l_DepthSize = Trinity::RHI::GetTextureCopyRowPitch(Trinity::Renderer3D::c_DepthFormat, c_RenderSize) * c_RenderSize;
    if (!m_RenderTarget)
    {
        m_RenderTarget = l_Device.CreateTexture(l_Description);
        l_Description.DebugName = "Import test render target, every light";
        m_AllLightsTarget = l_Device.CreateTexture(l_Description);
        l_Description.DebugName = "Import test render target";

        const auto a_Readback = [&l_Device](std::uint64_t size, std::string_view name)
        {
            Trinity::RHI::BufferDescription l_Readback;
            l_Readback.Usage = Trinity::RHI::BufferUsage::CopyDestination;
            l_Readback.Memory = Trinity::RHI::MemoryType::Readback;
            l_Readback.Size = size;
            l_Readback.DebugName = name;

            return l_Device.CreateBuffer(l_Readback);
        };

        m_ColorReadback = a_Readback(l_ColorSize, "Import test color readback");
        m_DepthReadback = a_Readback(l_DepthSize, "Import test depth readback");
        m_AllLightsReadback = a_Readback(l_ColorSize, "Import test every-light color readback");
        m_ClusterCountsReadback = a_Readback(c_ClusterCountsSize, "Import test cluster counts readback");
        m_ClusterLightsReadback = a_Readback(c_ClusterListsSize, "Import test cluster lights readback");
    }

    m_RenderAdded = true;
    m_ClustersBuilt = false;
    if (!m_RenderTarget || !m_AllLightsTarget || !m_ColorReadback || !m_DepthReadback || !m_AllLightsReadback || !m_ClusterCountsReadback || !m_ClusterLightsReadback)
    {
        return;
    }

    // The same draws twice: by each pixel's cluster, then by every light
    Trinity::Renderer3D& l_Renderer = Trinity::Application::Get().GetRenderer().GetRenderer3D();
    l_Renderer.Collect(*m_RenderScene, m_RenderView, m_RenderDraws);
    const Trinity::FrameGraphTexture l_Target = graph.ImportTexture("Import test render target", m_RenderTarget, l_Description, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::CopySource);
    const Trinity::FrameGraphTexture l_AllLightsTarget = graph.ImportTexture("Import test render target, every light", m_AllLightsTarget, l_Description, Trinity::RHI::ResourceState::Undefined, Trinity::RHI::ResourceState::CopySource);
    const Trinity::Renderer3D::Passes l_Clustered = l_Renderer.AddPasses(graph, m_RenderDraws, l_Target, l_Description, c_RenderClear);
    Trinity::SceneOptions l_EveryLight;
    l_EveryLight.ShadeAllLights = true;
    static_cast<void>(l_Renderer.AddPasses(graph, m_RenderDraws, l_AllLightsTarget, l_Description, c_RenderClear, l_EveryLight));
    m_ClustersBuilt = l_Clustered.ClusterCounts.IsValid();

    const auto a_Import = [&graph](std::string_view name, Trinity::RHI::BufferHandle buffer, std::uint64_t size) { return graph.ImportBuffer(name, buffer, size, Trinity::RHI::ResourceState::CopyDestination, Trinity::RHI::ResourceState::CopyDestination); };
    const Trinity::FrameGraphBuffer l_Color = a_Import("Import test color readback", m_ColorReadback, l_ColorSize);
    const Trinity::FrameGraphBuffer l_DepthCopy = a_Import("Import test depth readback", m_DepthReadback, l_DepthSize);
    const Trinity::FrameGraphBuffer l_AllLightsColor = a_Import("Import test every-light color readback", m_AllLightsReadback, l_ColorSize);
    const Trinity::FrameGraphTexture l_Depth = l_Clustered.Depth;
    graph.AddPass("Import test render readback", Trinity::FrameGraphPassType::Copy, [l_Target, l_Depth, l_AllLightsTarget, l_Color, l_DepthCopy, l_AllLightsColor](Trinity::FrameGraphPassBuilder& builder)
    {
        builder.Read(l_Target, Trinity::RHI::ResourceState::CopySource);
        builder.Read(l_Depth, Trinity::RHI::ResourceState::CopySource);
        builder.Read(l_AllLightsTarget, Trinity::RHI::ResourceState::CopySource);
        builder.Write(l_Color, Trinity::RHI::ResourceState::CopyDestination);
        builder.Write(l_DepthCopy, Trinity::RHI::ResourceState::CopyDestination);
        builder.Write(l_AllLightsColor, Trinity::RHI::ResourceState::CopyDestination);
    }, [l_Target, l_Depth, l_AllLightsTarget, l_Color, l_DepthCopy, l_AllLightsColor](const Trinity::FrameGraphContext& context)
    {
        context.GetCommands().CopyTextureToBuffer(context.GetTexture(l_Target), 0, 0, context.GetBuffer(l_Color), 0);
        context.GetCommands().CopyTextureToBuffer(context.GetTexture(l_Depth), 0, 0, context.GetBuffer(l_DepthCopy), 0);
        context.GetCommands().CopyTextureToBuffer(context.GetTexture(l_AllLightsTarget), 0, 0, context.GetBuffer(l_AllLightsColor), 0);
    });

    if (!m_ClustersBuilt)
    {
        return;
    }

    const Trinity::FrameGraphBuffer l_Counts = l_Clustered.ClusterCounts;
    const Trinity::FrameGraphBuffer l_Lists = l_Clustered.ClusterLights;
    const Trinity::FrameGraphBuffer l_CountsCopy = a_Import("Import test cluster counts readback", m_ClusterCountsReadback, c_ClusterCountsSize);
    const Trinity::FrameGraphBuffer l_ListsCopy = a_Import("Import test cluster lights readback", m_ClusterLightsReadback, c_ClusterListsSize);
    graph.AddPass("Import test cluster readback", Trinity::FrameGraphPassType::Copy, [l_Counts, l_Lists, l_CountsCopy, l_ListsCopy](Trinity::FrameGraphPassBuilder& builder)
    {
        builder.Read(l_Counts, Trinity::RHI::ResourceState::CopySource);
        builder.Read(l_Lists, Trinity::RHI::ResourceState::CopySource);
        builder.Write(l_CountsCopy, Trinity::RHI::ResourceState::CopyDestination);
        builder.Write(l_ListsCopy, Trinity::RHI::ResourceState::CopyDestination);
    }, [l_Counts, l_Lists, l_CountsCopy, l_ListsCopy](const Trinity::FrameGraphContext& context)
    {
        context.GetCommands().CopyBuffer(context.GetBuffer(l_Counts), 0, context.GetBuffer(l_CountsCopy), 0, c_ClusterCountsSize);
        context.GetCommands().CopyBuffer(context.GetBuffer(l_Lists), 0, context.GetBuffer(l_ListsCopy), 0, c_ClusterListsSize);
    });
}

// Every pixel the pre-pass wrote depth to was shaded by the opaque pass's equal test, every other kept the clear colour, the spheres cover a good part of the view, and the lit side is brighter than the ambient light alone could make it
void ImportTest::FinishRendering()
{
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    l_Device.WaitIdle();
    const Trinity::Renderer3D::Statistics& l_Statistics = Trinity::Application::Get().GetRenderer().GetRenderer3D().GetStatistics();
    if (l_Device.GetInfo().API == Trinity::GraphicsAPI::None || !m_RenderTarget)
    {
        TR_WARN("Import test: the null device draws nothing, so MetalRoughSpheres is not checked on screen");
        Finish(true);

        return;
    }

    const std::uint64_t l_ColorPitch = Trinity::RHI::GetTextureCopyRowPitch(Trinity::RHI::Format::RGBA16Float, c_RenderSize);
    const std::uint64_t l_DepthPitch = Trinity::RHI::GetTextureCopyRowPitch(Trinity::Renderer3D::c_DepthFormat, c_RenderSize);
    const std::span<const std::byte> l_Color = l_Device.GetMappedData(m_ColorReadback);
    const std::span<const std::byte> l_Depth = l_Device.GetMappedData(m_DepthReadback);
    std::uint32_t l_Covered = 0;
    std::uint32_t l_Holes = 0;
    std::uint32_t l_Overdrawn = 0;
    std::uint32_t l_NotFinite = 0;
    float l_Brightest = 0.0f;
    for (std::uint32_t it_Y = 0; it_Y < c_RenderSize; ++it_Y)
    {
        for (std::uint32_t it_X = 0; it_X < c_RenderSize; ++it_X)
        {
            std::array<std::uint16_t, 4> l_Half{};
            float l_DepthValue = 0.0f;
            std::memcpy(l_Half.data(), l_Color.data() + it_Y * l_ColorPitch + it_X * 8, 8);
            std::memcpy(&l_DepthValue, l_Depth.data() + it_Y * l_DepthPitch + it_X * 4, 4);
            const glm::vec4 l_Pixel(glm::unpackHalf1x16(l_Half[0]), glm::unpackHalf1x16(l_Half[1]), glm::unpackHalf1x16(l_Half[2]), glm::unpackHalf1x16(l_Half[3]));
            const bool l_Cleared = l_Pixel == glm::vec4(c_RenderClear[0], c_RenderClear[1], c_RenderClear[2], c_RenderClear[3]);
            if (l_DepthValue > 0.0f)
            {
                ++l_Covered;
                l_Holes += l_Cleared ? 1 : 0;
                l_NotFinite += std::isfinite(l_Pixel.r) && std::isfinite(l_Pixel.g) && std::isfinite(l_Pixel.b) && std::isfinite(l_Pixel.a) ? 0 : 1;
                l_Brightest = std::max(l_Brightest, std::max({ l_Pixel.r, l_Pixel.g, l_Pixel.b }));
            }
            else
            {
                l_Overdrawn += l_Cleared ? 0 : 1;
            }
        }
    }

    const std::uint32_t l_Pixels = c_RenderSize * c_RenderSize;
    const float l_Ambient = Trinity::Renderer3D::c_DefaultSunIntensity * Trinity::Renderer3D::c_AmbientFraction;
    const bool l_Passed = l_Covered > l_Pixels / 10 && l_Holes == 0 && l_Overdrawn == 0 && l_NotFinite == 0 && l_Brightest > l_Ambient && l_Statistics.PrePassDraws == l_Statistics.OpaqueDraws && m_RenderDraws.Draws.size() > 0;
    if (!l_Passed)
    {
        TR_ERROR("Import test: MetalRoughSpheres covered {} of {} pixels, with {} the opaque pass left unshaded, {} shaded outside the pre-pass's depth, {} not finite and the brightest at {}, in {} draws with {} in the pre-pass and {} in the opaque pass", l_Covered, l_Pixels, l_Holes, l_Overdrawn, l_NotFinite, l_Brightest, m_RenderDraws.Draws.size(), l_Statistics.PrePassDraws, l_Statistics.OpaqueDraws);
        Finish(false);

        return;
    }

    TR_INFO("Import test: MetalRoughSpheres drew through a perspective camera in {} draws, covering {} of {} pixels, every one the pre-pass reached shaded by the opaque pass's equal depth test and the brightest at {:.3f}", m_RenderDraws.Draws.size(), l_Covered, l_Pixels, l_Brightest);
    const bool l_Clusters = CheckClusters();
    const bool l_Shading = CompareShading();
    Finish(l_Clusters && l_Shading);
}

// Every cluster's count and lights read back must equal the CPU's. The GPU's floating point may differ from the CPU's in the last bits, so a light whose sphere just touches a cluster's box, within a hair of its radius, may be in one list and not the other. A cluster past the cap is checked by its count, since which lights it keeps then depends on every light before them
bool ImportTest::CheckClusters()
{
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    if (!m_ClustersBuilt)
    {
        TR_ERROR("Import test: no light clusters were built for {} point and spot lights", m_RenderDraws.Lights.size());

        return false;
    }

    const std::span<const std::byte> l_GpuCounts = l_Device.GetMappedData(m_ClusterCountsReadback);
    const std::span<const std::byte> l_GpuLists = l_Device.GetMappedData(m_ClusterLightsReadback);
    std::vector<std::uint32_t> l_Counts;
    std::vector<std::uint32_t> l_Lists;
    Trinity::Renderer3D::BuildClustersReference(m_RenderDraws, l_Counts, l_Lists);
    if (l_GpuCounts.size() < l_Counts.size() * sizeof(std::uint32_t) || l_GpuLists.size() < l_Lists.size() * sizeof(std::uint32_t))
    {
        TR_ERROR("Import test: the cluster readbacks are smaller than the clusters");

        return false;
    }

    const Trinity::ClusterGrid& l_Grid = m_RenderDraws.Clusters;
    std::uint32_t l_Exact = 0;
    std::uint32_t l_Grazing = 0;
    std::uint32_t l_Mismatched = 0;
    std::uint32_t l_Overfull = 0;
    std::uint32_t l_Most = 0;
    std::uint64_t l_Entries = 0;
    std::vector<std::uint32_t> l_GpuList;
    std::vector<std::uint32_t> l_Difference;
    for (std::uint32_t it_Cluster = 0; it_Cluster < Trinity::ClusterGrid::c_Count; ++it_Cluster)
    {
        std::uint32_t l_GpuCount = 0;
        std::memcpy(&l_GpuCount, l_GpuCounts.data() + std::size_t{ it_Cluster } * sizeof(std::uint32_t), sizeof(l_GpuCount));
        const std::uint32_t l_Count = l_Counts[it_Cluster];
        l_Most = std::max(l_Most, l_Count);
        l_Entries += l_Count;

        const std::size_t l_First = std::size_t{ it_Cluster } * Trinity::ClusterGrid::c_MaxLights;
        l_GpuList.resize(std::min(l_GpuCount, Trinity::ClusterGrid::c_MaxLights));
        std::memcpy(l_GpuList.data(), l_GpuLists.data() + l_First * sizeof(std::uint32_t), l_GpuList.size() * sizeof(std::uint32_t));
        const std::span<const std::uint32_t> l_List(l_Lists.data() + l_First, std::min(l_Count, Trinity::ClusterGrid::c_MaxLights));
        if (l_GpuCount == l_Count && std::ranges::equal(l_GpuList, l_List))
        {
            ++l_Exact;

            continue;
        }

        glm::vec3 l_Minimum;
        glm::vec3 l_Maximum;
        l_Grid.GetBounds(it_Cluster % Trinity::ClusterGrid::c_TilesX, (it_Cluster / Trinity::ClusterGrid::c_TilesX) % Trinity::ClusterGrid::c_TilesY, it_Cluster / (Trinity::ClusterGrid::c_TilesX * Trinity::ClusterGrid::c_TilesY), l_Minimum, l_Maximum);
        const auto a_Grazes = [&](std::uint32_t light)
        {
            if (light >= m_RenderDraws.LightBounds.size())
            {
                return false;
            }

            const glm::vec4& l_Sphere = m_RenderDraws.LightBounds[light];
            const float l_Distance = std::sqrt(Trinity::GetSquaredDistance(glm::vec3(l_Sphere), l_Minimum, l_Maximum));

            return std::abs(l_Distance - l_Sphere.w) <= 1e-4f * std::max({ 1.0f, l_Sphere.w, l_Grid.Far });
        };

        bool l_Explained = false;
        if (l_GpuCount > Trinity::ClusterGrid::c_MaxLights || l_Count > Trinity::ClusterGrid::c_MaxLights)
        {
            ++l_Overfull;
            std::uint32_t l_Grazers = 0;
            for (std::uint32_t it_Light = 0; it_Light < m_RenderDraws.LightBounds.size(); ++it_Light)
            {
                l_Grazers += a_Grazes(it_Light) ? 1 : 0;
            }

            l_Explained = (l_GpuCount > l_Count ? l_GpuCount - l_Count : l_Count - l_GpuCount) <= l_Grazers;
        }
        else
        {
            l_Difference.clear();
            std::ranges::set_symmetric_difference(l_GpuList, l_List, std::back_inserter(l_Difference));
            l_Explained = std::ranges::is_sorted(l_GpuList) && std::ranges::all_of(l_Difference, a_Grazes);
        }

        ++(l_Explained ? l_Grazing : l_Mismatched);
    }

    if (l_Mismatched != 0)
    {
        TR_ERROR("Import test: {} of {} clusters' light lists differed from the CPU's beyond lights grazing their edges, with {} point and spot lights", l_Mismatched, Trinity::ClusterGrid::c_Count, m_RenderDraws.Lights.size());

        return false;
    }

    TR_INFO("Import test: all {} clusters' light lists equalled the CPU's for {} point and spot lights, {} exactly and {} but for lights grazing their edges, with {} entries, at most {} lights in a cluster and {} cluster(s) over the cap of {}", Trinity::ClusterGrid::c_Count, m_RenderDraws.Lights.size(), l_Exact, l_Grazing, l_Entries, l_Most, l_Overfull, Trinity::ClusterGrid::c_MaxLights);

    return true;
}

// Each pixel shaded by its cluster's lights against the same pixel shaded by every light: within 1/255 of the brighter of the two, or 1/255 itself below 1
bool ImportTest::CompareShading()
{
    Trinity::RHI::Device& l_Device = Trinity::Application::Get().GetDevice();
    const std::uint64_t l_Pitch = Trinity::RHI::GetTextureCopyRowPitch(Trinity::RHI::Format::RGBA16Float, c_RenderSize);
    const std::span<const std::byte> l_Clustered = l_Device.GetMappedData(m_ColorReadback);
    const std::span<const std::byte> l_EveryLight = l_Device.GetMappedData(m_AllLightsReadback);
    if (l_Clustered.size() < l_Pitch * c_RenderSize || l_EveryLight.size() < l_Pitch * c_RenderSize)
    {
        TR_ERROR("Import test: the colour readbacks are smaller than the render target");

        return false;
    }

    std::uint32_t l_Different = 0;
    float l_Largest = 0.0f;
    for (std::uint32_t it_Y = 0; it_Y < c_RenderSize; ++it_Y)
    {
        for (std::uint32_t it_X = 0; it_X < c_RenderSize; ++it_X)
        {
            std::array<std::uint16_t, 4> l_Ours{};
            std::array<std::uint16_t, 4> l_Theirs{};
            std::memcpy(l_Ours.data(), l_Clustered.data() + it_Y * l_Pitch + it_X * 8, 8);
            std::memcpy(l_Theirs.data(), l_EveryLight.data() + it_Y * l_Pitch + it_X * 8, 8);
            bool l_Same = true;
            for (std::size_t it_Channel = 0; it_Channel < 4; ++it_Channel)
            {
                const float l_A = glm::unpackHalf1x16(l_Ours[it_Channel]);
                const float l_B = glm::unpackHalf1x16(l_Theirs[it_Channel]);
                const float l_Difference = std::abs(l_A - l_B);
                l_Largest = std::isfinite(l_Difference) ? std::max(l_Largest, l_Difference) : l_Largest;
                l_Same = l_Same && std::isfinite(l_Difference) && l_Difference <= std::max({ 1.0f, std::abs(l_A), std::abs(l_B) }) / 255.0f;
            }

            l_Different += l_Same ? 0 : 1;
        }
    }

    if (l_Different != 0)
    {
        TR_ERROR("Import test: {} pixel(s) shaded by their clusters' lights differed from shading by every light by more than 1/255, the largest by {}", l_Different, l_Largest);

        return false;
    }

    TR_INFO("Import test: every pixel shaded by its cluster's lights matched shading by all {} point and spot lights within 1/255, the largest difference {}", m_RenderDraws.Lights.size(), l_Largest);

    return true;
}