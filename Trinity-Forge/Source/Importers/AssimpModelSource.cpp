#include "Importers/ModelSource.hpp"

#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <initializer_list>
#include <map>
#include <optional>
#include <utility>

namespace
{
    // A file the IO system has read whole, which assimp reads as it would a file on disk
    class TrinityIOStream final : public Assimp::IOStream
    {
    public:
        explicit TrinityIOStream(Trinity::FileBuffer bytes) : m_Bytes(std::move(bytes))
        {

        }

        size_t Read(void* buffer, size_t size, size_t count) override
        {
            if (buffer == nullptr || size == 0)
            {
                return 0;
            }

            const std::size_t l_Count = std::min(count, (m_Bytes.size() - m_Position) / size);
            std::memcpy(buffer, m_Bytes.data() + m_Position, l_Count * size);
            m_Position += l_Count * size;

            return l_Count;
        }

        size_t Write(const void*, size_t, size_t) override
        {
            return 0;
        }

        aiReturn Seek(size_t offset, aiOrigin origin) override
        {
            const std::size_t l_Base = origin == aiOrigin_CUR ? m_Position : 0;
            if (offset > m_Bytes.size() - l_Base)
            {
                return aiReturn_FAILURE;
            }

            m_Position = origin == aiOrigin_END ? m_Bytes.size() - offset : l_Base + offset;

            return aiReturn_SUCCESS;
        }

        size_t Tell() const override
        {
            return m_Position;
        }

        size_t FileSize() const override
        {
            return m_Bytes.size();
        }

        void Flush() override
        {

        }

    private:
        Trinity::FileBuffer m_Bytes;
        std::size_t m_Position = 0;
    };

    // Assimp reads the model, and whatever it names, through Trinity's file system, so a model is read from wherever its mount is. Every file read goes into the content hash, so a changed .mtl imports the model again
    class TrinityIOSystem final : public Assimp::IOSystem
    {
    public:
        explicit TrinityIOSystem(std::string modelPath) : m_ModelPath(std::move(modelPath))
        {

        }

        bool Exists(const char* path) const override
        {
            const std::optional<std::string> l_Path = Resolve(path);

            return l_Path && Trinity::FileSystem::Exists(*l_Path);
        }

        char getOsSeparator() const override
        {
            return '/';
        }

        Assimp::IOStream* Open(const char* path, const char* mode) override
        {
            const std::optional<std::string> l_Path = Resolve(path);
            if (!l_Path || mode == nullptr || std::strchr(mode, 'w') != nullptr || std::strchr(mode, 'a') != nullptr)
            {
                return nullptr;
            }

            Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Bytes = Trinity::FileSystem::ReadFile(*l_Path);
            if (!l_Bytes)
            {
                return nullptr;
            }

            m_Hash = ModelReading::Hash(*l_Path, m_Hash);
            m_Hash = ModelReading::Hash(*l_Bytes, m_Hash);
            if (*l_Path == m_ModelPath && m_ModelText.empty())
            {
                m_ModelText.assign(reinterpret_cast<const char*>(l_Bytes->data()), l_Bytes->size());
            }

            return new TrinityIOStream(std::move(*l_Bytes));
        }

        void Close(Assimp::IOStream* stream) override
        {
            delete stream;
        }

        [[nodiscard]] std::uint64_t GetHash() const
        {
            return m_Hash;
        }

        [[nodiscard]] const std::string& GetModelText() const
        {
            return m_ModelText;
        }

    private:
        [[nodiscard]] std::optional<std::string> Resolve(const char* path) const
        {
            if (path == nullptr)
            {
                return std::nullopt;
            }

            const std::string_view l_Path(path);

            return l_Path.starts_with('/') || l_Path.starts_with('\\') ? ModelReading::NormalizePath(l_Path) : ModelReading::ResolvePath(m_ModelPath, l_Path);
        }

        std::string m_ModelPath;
        std::uint64_t m_Hash = ModelReading::c_HashSeed;
        std::string m_ModelText;
    };

    // The textures a material samples, found while planning, by the material slot they fill
    struct MaterialTextures
    {
        struct Slot
        {
            std::optional<ModelSource::TextureUse> Use;
            std::uint32_t TexCoord = 0;
        };

        Slot BaseColor;
        Slot MetallicRoughness;
        Slot Normal;
        Slot Occlusion;
        Slot Emissive;
    };

    // The scene assimp read, kept from planning to cooking with the importer that owns it
    class AssimpSource final : public ModelSource
    {
    public:
        [[nodiscard]] Trinity::Expected<BuiltMesh, std::string> BuildMesh(std::size_t mesh) const override;
        [[nodiscard]] Trinity::MaterialData BuildMaterial(std::size_t material, const std::function<Trinity::UUID(const TextureUse&)>& resolve) const override;
        [[nodiscard]] Trinity::ModelData BuildHierarchy(const glm::mat4& conversion, std::span<const Trinity::UUID> meshes, std::span<const std::vector<Trinity::UUID>> meshMaterials) const override;

        std::unique_ptr<Assimp::Importer> Importer;
        const aiScene* Scene = nullptr;
        // Assimp splits a mesh by material, so a mesh asset is the list of assimp meshes a node holds, and nodes holding the same list share it
        std::vector<std::vector<unsigned int>> MeshGroups;
        std::map<const aiNode*, std::size_t> NodeGroups;
        std::vector<MaterialTextures> Materials;
    };

    glm::mat4 ToMatrix(const aiMatrix4x4& matrix)
    {
        glm::mat4 l_Matrix(1.0f);
        for (glm::length_t it_Column = 0; it_Column < 4; ++it_Column)
        {
            for (glm::length_t it_Row = 0; it_Row < 4; ++it_Row)
            {
                l_Matrix[it_Column][it_Row] = matrix[static_cast<unsigned int>(it_Row)][static_cast<unsigned int>(it_Column)];
            }
        }

        return l_Matrix;
    }

    std::string GetExtension(std::string_view path)
    {
        std::string l_Extension(path.substr(std::min(path.find_last_of('.'), path.size())));
        std::ranges::transform(l_Extension, l_Extension.begin(), [](char character) { return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character; });

        return l_Extension;
    }

    // Nodes parents first, as BuildHierarchy walks them, so a node's place in the list is its source index
    std::vector<const aiNode*> GetNodes(const aiNode* root)
    {
        std::vector<const aiNode*> l_Nodes;
        std::vector<const aiNode*> l_Stack{ root };
        while (!l_Stack.empty())
        {
            const aiNode* l_Node = l_Stack.back();
            l_Stack.pop_back();
            l_Nodes.push_back(l_Node);
            for (unsigned int it_Child = l_Node->mNumChildren; it_Child > 0; --it_Child)
            {
                l_Stack.push_back(l_Node->mChildren[it_Child - 1]);
            }
        }

        return l_Nodes;
    }

    // FBX names which of its axes is right, up and toward the viewer, and its unit in centimetres
    void ReadFbxUnits(const aiScene& scene, ModelSource& source, std::string_view path)
    {
        std::int32_t l_Up = 1;
        std::int32_t l_UpSign = 1;
        std::int32_t l_Front = 2;
        std::int32_t l_FrontSign = 1;
        std::int32_t l_Coord = 0;
        std::int32_t l_CoordSign = 1;
        float l_Unit = 1.0f;
        if (scene.mMetaData != nullptr)
        {
            scene.mMetaData->Get("UpAxis", l_Up);
            scene.mMetaData->Get("UpAxisSign", l_UpSign);
            scene.mMetaData->Get("FrontAxis", l_Front);
            scene.mMetaData->Get("FrontAxisSign", l_FrontSign);
            scene.mMetaData->Get("CoordAxis", l_Coord);
            scene.mMetaData->Get("CoordAxisSign", l_CoordSign);
            if (double l_Double = 1.0; !scene.mMetaData->Get("UnitScaleFactor", l_Unit) && scene.mMetaData->Get("UnitScaleFactor", l_Double))
            {
                l_Unit = static_cast<float>(l_Double);
            }
        }

        const std::array<std::int32_t, 3> l_Axes{ l_Coord, l_Up, l_Front };
        const bool l_Valid = std::ranges::all_of(l_Axes, [](std::int32_t axis) { return axis >= 0 && axis < 3; }) && l_Coord != l_Up && l_Up != l_Front && l_Coord != l_Front;
        if (!l_Valid)
        {
            TR_WARN("Models: {} names its axes as right {}, up {} and front {}, which are not three different axes, so they are taken as they are", path, l_Coord, l_Up, l_Front);
        }
        else
        {
            const std::array<float, 3> l_Signs{ l_CoordSign < 0 ? -1.0f : 1.0f, l_UpSign < 0 ? -1.0f : 1.0f, l_FrontSign < 0 ? -1.0f : 1.0f };
            source.Axes = glm::mat3(0.0f);
            for (glm::length_t it_Row = 0; it_Row < 3; ++it_Row)
            {
                source.Axes[l_Axes[static_cast<std::size_t>(it_Row)]][it_Row] = l_Signs[static_cast<std::size_t>(it_Row)];
            }
        }

        if (l_Unit > 0.0f && std::isfinite(l_Unit))
        {
            source.UnitScale = l_Unit * 0.01f;
        }
        else
        {
            TR_WARN("Models: {} gives its unit as {} centimetres, so it is taken as one", path, l_Unit);
            source.UnitScale = 0.01f;
        }
    }

    // COLLADA gives its unit in metres and its up axis in the first <asset>, which assimp leaves unread when told not to apply them
    void ReadColladaUnits(std::string_view text, ModelSource& source, std::string_view path)
    {
        const std::size_t l_Unit = text.find("<unit");
        const std::size_t l_UnitEnd = l_Unit != std::string_view::npos ? text.find('>', l_Unit) : std::string_view::npos;
        const std::size_t l_Meter = l_UnitEnd != std::string_view::npos ? text.substr(l_Unit, l_UnitEnd - l_Unit).find("meter=") : std::string_view::npos;
        if (l_Meter != std::string_view::npos && l_Unit + l_Meter + 7 < l_UnitEnd)
        {
            const char* l_First = text.data() + l_Unit + l_Meter + 7;
            float l_Value = 0.0f;
            const std::from_chars_result l_Parsed = std::from_chars(l_First, text.data() + l_UnitEnd, l_Value);
            if (l_Parsed.ec == std::errc() && l_Value > 0.0f && std::isfinite(l_Value))
            {
                source.UnitScale = l_Value;
            }
            else
            {
                TR_WARN("Models: {} has a <unit> whose metres cannot be read, so it is taken as metres", path);
            }
        }

        const std::size_t l_Up = text.find("<up_axis>");
        const std::size_t l_UpEnd = l_Up != std::string_view::npos ? text.find("</up_axis>", l_Up) : std::string_view::npos;
        std::string_view l_Axis = l_UpEnd != std::string_view::npos ? text.substr(l_Up + 9, l_UpEnd - l_Up - 9) : std::string_view("Y_UP");
        l_Axis = l_Axis.substr(std::min(l_Axis.find_first_not_of(" \t\r\n"), l_Axis.size()));
        l_Axis = l_Axis.substr(0, l_Axis.find_last_not_of(" \t\r\n") + 1);
        if (l_Axis == "Z_UP")
        {
            source.Axes = glm::mat3(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);
        }
        else if (l_Axis == "X_UP")
        {
            source.Axes = glm::mat3(0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        }
        else if (l_Axis != "Y_UP")
        {
            TR_WARN("Models: {} has <up_axis> {}, which is not X_UP, Y_UP or Z_UP, so it is taken as Y up", path, l_Axis);
        }
    }

    // A colour from a file without physically based materials, which tools pick as it looks on screen
    glm::vec3 ToLinear(const aiColor3D& color)
    {
        return glm::vec3(Trinity::SrgbToLinear(color.r), Trinity::SrgbToLinear(color.g), Trinity::SrgbToLinear(color.b));
    }
}

// Each assimp mesh of the group a submesh, in the order the node lists them
Trinity::Expected<ModelSource::BuiltMesh, std::string> AssimpSource::BuildMesh(std::size_t mesh) const
{
    std::vector<ModelReading::Primitive> l_Primitives;
    for (const unsigned int it_Mesh : MeshGroups[mesh])
    {
        const aiMesh& l_Mesh = *Scene->mMeshes[it_Mesh];
        if ((l_Mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0 || l_Mesh.mNumVertices == 0)
        {
            continue;
        }

        ModelReading::Primitive l_Data;
        l_Data.Material = l_Mesh.mMaterialIndex < Materials.size() ? std::optional<std::size_t>(l_Mesh.mMaterialIndex) : std::nullopt;
        l_Data.NormalTexCoord = l_Data.Material ? Materials[*l_Data.Material].Normal.TexCoord : 0;
        l_Data.Positions.reserve(l_Mesh.mNumVertices);
        for (unsigned int it_Vertex = 0; it_Vertex < l_Mesh.mNumVertices; ++it_Vertex)
        {
            const aiVector3D& l_Position = l_Mesh.mVertices[it_Vertex];
            l_Data.Positions.emplace_back(l_Position.x, l_Position.y, l_Position.z);
            if (l_Mesh.mNormals != nullptr)
            {
                l_Data.Normals.emplace_back(l_Mesh.mNormals[it_Vertex].x, l_Mesh.mNormals[it_Vertex].y, l_Mesh.mNormals[it_Vertex].z);
            }

            if (l_Mesh.mTextureCoords[0] != nullptr)
            {
                l_Data.TexCoords0.emplace_back(l_Mesh.mTextureCoords[0][it_Vertex].x, l_Mesh.mTextureCoords[0][it_Vertex].y);
            }

            if (l_Mesh.mTextureCoords[1] != nullptr)
            {
                l_Data.TexCoords1.emplace_back(l_Mesh.mTextureCoords[1][it_Vertex].x, l_Mesh.mTextureCoords[1][it_Vertex].y);
            }

            if (l_Mesh.mColors[0] != nullptr)
            {
                const aiColor4D& l_Color = l_Mesh.mColors[0][it_Vertex];
                l_Data.Colors.emplace_back(l_Color.r, l_Color.g, l_Color.b, l_Color.a);
            }
        }

        for (unsigned int it_Face = 0; it_Face < l_Mesh.mNumFaces; ++it_Face)
        {
            const aiFace& l_Face = l_Mesh.mFaces[it_Face];
            if (l_Face.mNumIndices == 3 && std::all_of(l_Face.mIndices, l_Face.mIndices + 3, [&l_Mesh](unsigned int index) { return index < l_Mesh.mNumVertices; }))
            {
                l_Data.Indices.insert(l_Data.Indices.end(), l_Face.mIndices, l_Face.mIndices + 3);
            }
        }

        if (l_Data.Indices.empty())
        {
            continue;
        }

        ModelReading::FinishPrimitive(l_Data);
        l_Primitives.push_back(std::move(l_Data));
    }

    return ModelReading::MergePrimitives(std::move(l_Primitives));
}

// The file's physically based values where it has them, and otherwise what its Phong values come to: the diffuse colour as base colour, roughness from the specular exponent as Blinn-Phong's lobe matches GGX's, no metal, and an opacity below one blended. A diffuse texture is not tinted by the diffuse colour, as most tools draw it
Trinity::MaterialData AssimpSource::BuildMaterial(std::size_t material, const std::function<Trinity::UUID(const TextureUse&)>& resolve) const
{
    const aiMaterial& l_Source = *Scene->mMaterials[material];
    const MaterialTextures& l_Textures = Materials[material];
    const auto a_Texture = [&resolve](const MaterialTextures::Slot& slot) { return slot.Use ? Trinity::MaterialTexture{ resolve(*slot.Use), slot.TexCoord } : Trinity::MaterialTexture(); };

    Trinity::MaterialData l_Material;
    l_Material.BaseColorTexture = a_Texture(l_Textures.BaseColor);
    l_Material.MetallicRoughnessTexture = a_Texture(l_Textures.MetallicRoughness);
    l_Material.NormalTexture = a_Texture(l_Textures.Normal);
    l_Material.OcclusionTexture = a_Texture(l_Textures.Occlusion);
    l_Material.EmissiveTexture = a_Texture(l_Textures.Emissive);

    aiColor4D l_Base;
    aiColor3D l_Diffuse;
    if (l_Source.Get(AI_MATKEY_BASE_COLOR, l_Base) == aiReturn_SUCCESS)
    {
        l_Material.BaseColorFactor = glm::vec4(l_Base.r, l_Base.g, l_Base.b, l_Base.a);
    }
    else if (!l_Textures.BaseColor.Use && l_Source.Get(AI_MATKEY_COLOR_DIFFUSE, l_Diffuse) == aiReturn_SUCCESS)
    {
        l_Material.BaseColorFactor = glm::vec4(ToLinear(l_Diffuse), 1.0f);
    }

    float l_Value = 0.0f;
    l_Material.MetallicFactor = l_Source.Get(AI_MATKEY_METALLIC_FACTOR, l_Value) == aiReturn_SUCCESS ? std::clamp(l_Value, 0.0f, 1.0f) : (l_Textures.MetallicRoughness.Use ? 1.0f : 0.0f);
    if (l_Source.Get(AI_MATKEY_ROUGHNESS_FACTOR, l_Value) == aiReturn_SUCCESS)
    {
        l_Material.RoughnessFactor = std::clamp(l_Value, 0.0f, 1.0f);
    }
    else if (!l_Textures.MetallicRoughness.Use)
    {
        const float l_Shininess = l_Source.Get(AI_MATKEY_SHININESS, l_Value) == aiReturn_SUCCESS && l_Value > 0.0f ? l_Value : 0.0f;
        l_Material.RoughnessFactor = std::clamp(std::sqrt(2.0f / (l_Shininess + 2.0f)), 0.0f, 1.0f);
    }

    aiColor3D l_Emissive;
    if (l_Source.Get(AI_MATKEY_COLOR_EMISSIVE, l_Emissive) == aiReturn_SUCCESS)
    {
        l_Material.EmissiveFactor = ToLinear(l_Emissive);
    }

    if (l_Textures.Emissive.Use && l_Material.EmissiveFactor == glm::vec3(0.0f))
    {
        l_Material.EmissiveFactor = glm::vec3(1.0f);
    }

    if (l_Source.Get(AI_MATKEY_EMISSIVE_INTENSITY, l_Value) == aiReturn_SUCCESS && l_Value >= 0.0f)
    {
        l_Material.EmissiveStrength = l_Value;
    }

    if (l_Source.Get(AI_MATKEY_BUMPSCALING, l_Value) == aiReturn_SUCCESS && l_Value > 0.0f)
    {
        l_Material.NormalScale = l_Value;
    }

    if (l_Source.Get(AI_MATKEY_OPACITY, l_Value) == aiReturn_SUCCESS && l_Value >= 0.0f && l_Value < 1.0f)
    {
        l_Material.BaseColorFactor.a *= l_Value;
    }

    l_Material.AlphaMode = l_Material.BaseColorFactor.a < 1.0f ? Trinity::MaterialAlphaMode::Blend : Trinity::MaterialAlphaMode::Opaque;

    int l_TwoSided = 0;
    l_Material.DoubleSided = l_Source.Get(AI_MATKEY_TWOSIDED, l_TwoSided) == aiReturn_SUCCESS && l_TwoSided != 0;

    return l_Material;
}

// Assimp's root is the file's scene, and has no place of its own when it holds no mesh, so its children are the model's roots, with its transform folded into theirs. Each root goes through the conversion. A node's source index is its place among all of assimp's nodes, parents first, the root included
Trinity::ModelData AssimpSource::BuildHierarchy(const glm::mat4& conversion, std::span<const Trinity::UUID> meshes, std::span<const std::vector<Trinity::UUID>> meshMaterials) const
{
    const std::vector<const aiNode*> l_Nodes = GetNodes(Scene->mRootNode);
    const aiNode* l_Root = Scene->mRootNode;
    const bool l_SkipRoot = l_Root->mNumMeshes == 0 && l_Root->mNumChildren > 0;

    Trinity::ModelData l_Model;
    std::vector<std::int32_t> l_Indices(l_Nodes.size(), Trinity::ModelNode::c_NoParent);
    std::map<const aiNode*, std::size_t> l_Places;
    for (std::size_t it_Node = 0; it_Node < l_Nodes.size(); ++it_Node)
    {
        l_Places[l_Nodes[it_Node]] = it_Node;
    }

    for (std::size_t it_Node = l_SkipRoot ? 1 : 0; it_Node < l_Nodes.size(); ++it_Node)
    {
        const aiNode& l_Source = *l_Nodes[it_Node];
        const bool l_IsRoot = &l_Source == l_Root || (l_SkipRoot && l_Source.mParent == l_Root);

        Trinity::ModelNode l_Node;
        l_Node.Name = l_Source.mName.length > 0 ? std::string(l_Source.mName.C_Str()) : std::format("Node {}", it_Node);
        l_Node.Parent = l_IsRoot ? Trinity::ModelNode::c_NoParent : l_Indices[l_Places.at(l_Source.mParent)];
        l_Node.SourceNode = static_cast<std::uint32_t>(it_Node);

        const glm::mat4 l_Local = ToMatrix(l_Source.mTransformation);
        const glm::mat4 l_Folded = l_IsRoot && &l_Source != l_Root ? ToMatrix(l_Root->mTransformation) * l_Local : l_Local;
        ModelReading::SetTransform(l_Node, l_IsRoot ? conversion * l_Folded : l_Local);

        if (const auto a_Group = NodeGroups.find(&l_Source); a_Group != NodeGroups.end() && a_Group->second < meshes.size() && meshes[a_Group->second].IsValid())
        {
            l_Node.Mesh = meshes[a_Group->second];
            l_Node.Materials = meshMaterials[a_Group->second];
        }

        l_Indices[it_Node] = static_cast<std::int32_t>(l_Model.Nodes.size());
        l_Model.Nodes.push_back(std::move(l_Node));
    }

    return l_Model;
}

namespace ModelReading
{
    // FBX, OBJ and COLLADA through assimp, told to leave units and axes as the file has them, so the conversion is the importer's to make and the settings can override it. The textures a material names are found in the file, beside the model by their relative path, or beside it by their file name, as exporters write absolute paths from the machine they ran on
    std::shared_ptr<ModelSource> ReadWithAssimp(const Trinity::AssetRecord& record, ModelImporter::Plan& plan)
    {
        const std::shared_ptr<AssimpSource> l_Source = std::make_shared<AssimpSource>();
        l_Source->Importer = std::make_unique<Assimp::Importer>();
        Assimp::Importer& l_Importer = *l_Source->Importer;

        TrinityIOSystem* l_IO = new TrinityIOSystem(record.Path);
        l_Importer.SetIOHandler(l_IO);
        l_Importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        l_Importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS, false);
        l_Importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_IGNORE_UP_DIRECTION, true);
        l_Importer.SetPropertyBool(AI_CONFIG_IMPORT_COLLADA_IGNORE_UP_DIRECTION, true);
        l_Importer.SetPropertyBool(AI_CONFIG_IMPORT_COLLADA_IGNORE_UNIT_SIZE, true);
        l_Importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);

        const unsigned int l_Steps = aiProcess_Triangulate | aiProcess_SortByPType | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_ValidateDataStructure;
        l_Source->Scene = l_Importer.ReadFile(record.Path, l_Steps);
        if (l_Source->Scene == nullptr || l_Source->Scene->mRootNode == nullptr)
        {
            TR_ERROR("Models: {} could not be read: {}", record.Path, l_Importer.GetErrorString());

            return nullptr;
        }

        const aiScene& l_Scene = *l_Source->Scene;
        const std::string l_Extension = GetExtension(record.Path);
        if (l_Extension == ".fbx")
        {
            ReadFbxUnits(l_Scene, *l_Source, record.Path);
        }
        else if (l_Extension == ".dae")
        {
            ReadColladaUnits(l_IO->GetModelText(), *l_Source, record.Path);
        }

        bool l_Skinned = false;
        std::map<std::vector<unsigned int>, std::size_t> l_Groups;
        for (const aiNode* it_Node : GetNodes(l_Scene.mRootNode))
        {
            std::vector<unsigned int> l_Meshes;
            for (unsigned int it_Mesh = 0; it_Mesh < it_Node->mNumMeshes; ++it_Mesh)
            {
                const unsigned int l_Mesh = it_Node->mMeshes[it_Mesh];
                if (l_Mesh < l_Scene.mNumMeshes && (l_Scene.mMeshes[l_Mesh]->mPrimitiveTypes& aiPrimitiveType_TRIANGLE) != 0)
                {
                    l_Meshes.push_back(l_Mesh);
                    l_Skinned = l_Skinned || l_Scene.mMeshes[l_Mesh]->mNumBones > 0;
                }
            }

            if (l_Meshes.empty())
            {
                continue;
            }

            const auto [a_Group, a_Added] = l_Groups.try_emplace(l_Meshes, l_Source->MeshGroups.size());
            if (a_Added)
            {
                const std::string_view l_Name = l_Scene.mMeshes[l_Meshes.front()]->mName.length > 0 ? l_Scene.mMeshes[l_Meshes.front()]->mName.C_Str() : it_Node->mName.C_Str();
                l_Source->MeshKeys.push_back(MakeKey("Mesh", l_Source->MeshGroups.size(), {}, l_Name));
                l_Source->MeshGroups.push_back(std::move(l_Meshes));
            }

            l_Source->NodeGroups[it_Node] = a_Group->second;
        }

        if (l_Skinned)
        {
            TR_WARN("Models: {} has skinned meshes, which are imported in their bind pose until skinning is", record.Path);
        }

        std::map<std::pair<int, ModelImporter::TextureUsage>, std::string> l_EmbeddedKeys;
        l_Source->Materials.resize(l_Scene.mNumMaterials);
        l_Source->MaterialKeys.resize(l_Scene.mNumMaterials);
        for (unsigned int it_Material = 0; it_Material < l_Scene.mNumMaterials; ++it_Material)
        {
            const aiMaterial& l_Material = *l_Scene.mMaterials[it_Material];
            l_Source->MaterialKeys[it_Material] = MakeKey("Material", it_Material, {}, l_Material.GetName().C_Str());

            // The first of the types the material has a texture of, found in the file or beside the model
            const auto a_Slot = [&](std::initializer_list<aiTextureType> types, ModelImporter::TextureUsage usage)
            {
                MaterialTextures::Slot l_Slot;
                const auto a_Type = std::ranges::find_if(types, [&l_Material](aiTextureType type) { return l_Material.GetTextureCount(type) > 0; });
                aiString l_Path;
                unsigned int l_TexCoord = 0;
                if (a_Type == types.end() || l_Material.GetTexture(*a_Type, 0, &l_Path, nullptr, &l_TexCoord) != aiReturn_SUCCESS || l_Path.length == 0)
                {
                    return l_Slot;
                }

                if (l_TexCoord > 1)
                {
                    TR_WARN("Models: material {} of {} samples {} with UV set {}, and only two are imported, so it uses the first", it_Material, record.Path, l_Path.C_Str(), l_TexCoord);
                    l_TexCoord = 0;
                }

                l_Slot.TexCoord = l_TexCoord;
                if (const auto [l_Embedded, l_Index] = l_Scene.GetEmbeddedTextureAndIndex(l_Path.C_Str()); l_Embedded != nullptr && l_Index >= 0)
                {
                    if (l_Embedded->mHeight != 0 || l_Embedded->pcData == nullptr || l_Embedded->mWidth == 0)
                    {
                        TR_WARN("Models: {} holds {} as raw texels, which are not imported yet, so material {} goes without", record.Path, l_Path.C_Str(), it_Material);

                        return l_Slot;
                    }

                    std::string& l_Key = l_EmbeddedKeys[{ l_Index, usage }];
                    if (l_Key.empty())
                    {
                        const std::string_view l_File = l_Embedded->mFilename.C_Str();
                        l_Key = MakeKey("Texture", static_cast<std::size_t>(l_Index), ModelImporter::ToString(usage), l_File.substr(std::min(l_File.find_last_of("/\\") + 1, l_File.size())));

                        TextureImportSettings l_Settings;
                        l_Settings.Srgb = usage == ModelImporter::TextureUsage::Color;
                        l_Settings.NormalMap = usage == ModelImporter::TextureUsage::Normal;
                        l_Source->EmbeddedTextures.push_back({ l_Key, std::span(reinterpret_cast<const std::byte*>(l_Embedded->pcData), l_Embedded->mWidth), l_Settings });
                    }

                    l_Slot.Use = ModelSource::TextureUse{ l_Key, 0 };

                    return l_Slot;
                }

                std::string_view l_Name(l_Path.C_Str());
                if (l_Name.starts_with("file://"))
                {
                    l_Name.remove_prefix(7);
                }

                std::optional<std::string> l_File = ResolvePath(record.Path, l_Name);
                if (!l_File || !Trinity::FileSystem::Exists(*l_File))
                {
                    l_File = ResolvePath(record.Path, l_Name.substr(std::min(l_Name.find_last_of("/\\") + 1, l_Name.size())));
                }

                if (!l_File || !Trinity::FileSystem::Exists(*l_File))
                {
                    TR_WARN("Models: material {} of {} names {}, which is neither in the file nor beside it, so it goes without", it_Material, record.Path, l_Path.C_Str());

                    return l_Slot;
                }

                l_Slot.Use = ModelSource::TextureUse{ {}, AddExternalTexture(plan, record.Path, *l_File, usage) };

                return l_Slot;
            };

            MaterialTextures& l_Textures = l_Source->Materials[it_Material];
            l_Textures.BaseColor = a_Slot({ aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, ModelImporter::TextureUsage::Color);
            l_Textures.MetallicRoughness = a_Slot({ aiTextureType_GLTF_METALLIC_ROUGHNESS }, ModelImporter::TextureUsage::Data);
            l_Textures.Normal = a_Slot({ aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT }, ModelImporter::TextureUsage::Normal);
            l_Textures.Occlusion = a_Slot({ aiTextureType_AMBIENT_OCCLUSION }, ModelImporter::TextureUsage::Data);
            l_Textures.Emissive = a_Slot({ aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE }, ModelImporter::TextureUsage::Color);
            if (!l_Textures.MetallicRoughness.Use && (l_Material.GetTextureCount(aiTextureType_METALNESS) > 0 || l_Material.GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0))
            {
                TR_WARN("Models: material {} of {} has separate metalness and roughness textures, which are not packed into one yet, so it goes without them", it_Material, record.Path);
            }
        }

        plan.ContentHash = l_IO->GetHash();

        return l_Source;
    }
}