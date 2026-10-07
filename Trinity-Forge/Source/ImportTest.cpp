#include "ImportTest.hpp"

#include "EditorCommands.hpp"
#include "EditorSession.hpp"
#include "Importers/TextureImporter.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <ktx.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    constexpr std::string_view c_TestMount = "/forge-tests";
    constexpr std::string_view c_TestFolder = "Textures";
    constexpr std::string_view c_ModelMount = "/forge-models";
    constexpr std::string_view c_ModelFolder = "Models";
    constexpr std::string_view c_BC7Variable = "renderer.texture_bc7";

    // UASTC at level 2 keeps the test images between 41 and 46 dB, so a drop below this means a broken encode or transcode, not a lossy setting
    constexpr double c_MinimumPsnr = 38.0;
    // Long enough for the test models' textures too, Sponza's among them
    constexpr std::uint64_t c_LoadTimeoutFrames = 1800;
    // How far an instantiated entity's world matrix may be from fastgltf's for its node, in any element
    constexpr float c_MaximumMatrixError = 1e-5f;

    // Khronos glTF sample models, as Scripts/FetchSamples checks them out under TR_FORGE_TEST_MODELS: one with images beside it and two that embed theirs
    struct TestModel
    {
        std::string_view Name;
        std::string_view Folder;
        std::string_view File;
    };

    constexpr std::array<TestModel, 3> c_TestModels{ {
        { "Sponza", "glTF-Sample-Assets/Models/Sponza/glTF", "Sponza.gltf" },
        { "DamagedHelmet", "glTF-Sample-Assets/Models/DamagedHelmet/glTF-Binary", "DamagedHelmet.glb" },
        { "MetalRoughSpheres", "glTF-Sample-Assets/Models/MetalRoughSpheres/glTF-Binary", "MetalRoughSpheres.glb" }
    } };

    std::string GetTestModelPath(const TestModel& model)
    {
        return std::format("{}/{}/{}/{}", Trinity::Project::c_AssetsMount, c_ModelFolder, model.Name, model.File);
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

        if (!CheckModels() || !ReimportModels() || !CheckInstances())
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

// The Khronos samples, when Scripts/FetchSamples has fetched them, each into a folder of its own. Without them the test goes on with the images alone
std::size_t ImportTest::CopyTestModels()
{
    if (!std::filesystem::is_directory(std::filesystem::path(TR_FORGE_TEST_MODELS)) || !Trinity::FileSystem::MountDirectory(c_ModelMount, std::filesystem::path(TR_FORGE_TEST_MODELS)))
    {
        TR_WARN("Import test: there are no test models in {}, so only images are tested. Scripts/FetchSamples.sh or FetchSamples.ps1 fetches them", TR_FORGE_TEST_MODELS);

        return 0;
    }

    std::size_t l_Found = 0;
    std::size_t l_Copied = 0;
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
    TR_INFO("Import test: {} of {} test model(s) found, with {} file(s) copied in", l_Found, c_TestModels.size(), l_Copied);

    return l_Found;
}

// Every cooked mesh, material and node of each model reads back, and names only sub-assets of its model or textures in the project. The textures the materials use are kept, with how they are used, for the loads
bool ImportTest::CheckModels()
{
    const Trinity::AssetRegistry& l_Registry = *m_Session.GetRegistry();
    bool l_Passed = true;
    m_ModelTextures.clear();
    for (const TestModel& it_Model : c_TestModels)
    {
        const Trinity::AssetRecord* l_Record = l_Registry.FindByPath(GetTestModelPath(it_Model));
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

        TR_INFO("Import test: {} has {} mesh(es) with {} submesh(es) and {} vertices, {} material(s), {} embedded texture(s), and {} node(s), {} with a mesh", l_Record->Path, l_Meshes, l_Submeshes, l_Vertices, l_Materials, l_Embedded, l_Model->Nodes.size(), l_MeshNodes);
    }

    return l_Passed;
}

// With every model's key removed, each is imported again, and must come back with the same sub-assets under the same UUIDs, and encode nothing its textures' own keys still hold
bool ImportTest::ReimportModels()
{
    std::vector<std::pair<Trinity::UUID, std::vector<Trinity::SubAsset>>> l_Before;
    for (const TestModel& it_Model : c_TestModels)
    {
        if (const Trinity::AssetRecord* l_Record = m_Session.GetRegistry()->FindByPath(GetTestModelPath(it_Model)))
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

// Each model is created in a scene of its own by the command a drop runs, which must undo to nothing and redo with the same UUIDs. Every node's world matrix is then compared with the one fastgltf computes from the file, matrices and all
bool ImportTest::CheckInstances()
{
    bool l_Passed = true;
    for (const TestModel& it_Model : c_TestModels)
    {
        const Trinity::AssetRecord* l_Record = m_Session.GetRegistry()->FindByPath(GetTestModelPath(it_Model));
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
        CreateModelCommand l_Command(l_Record->ID, std::string(it_Model.Name), {}, {}, glm::vec3(0.0f));
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

    m_Phase = Phase::Idle;
    static_cast<void>(Trinity::ConsoleVariables::Set(c_BC7Variable, "true"));
    TR_INFO("Import test: finished, and {} asset(s) are still loaded", Trinity::AssetManager::GetEntryCount());
}