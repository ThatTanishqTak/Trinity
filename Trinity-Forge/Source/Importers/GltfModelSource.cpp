#include "Importers/ModelSource.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <format>
#include <initializer_list>
#include <map>
#include <numeric>
#include <optional>
#include <utility>
#include <variant>

namespace
{
    // The parsed glTF with its buffers and embedded images, kept from planning to cooking
    class GltfSource final : public ModelSource
    {
    public:
        [[nodiscard]] Trinity::Expected<BuiltMesh, std::string> BuildMesh(std::size_t mesh) const override;
        [[nodiscard]] Trinity::MaterialData BuildMaterial(std::size_t material, const std::function<Trinity::UUID(const TextureUse&)>& resolve) const override;
        [[nodiscard]] Trinity::ModelData BuildHierarchy(const glm::mat4& conversion, std::span<const Trinity::UUID> meshes, std::span<const std::vector<Trinity::UUID>> meshMaterials) const override;

        fastgltf::Asset Asset;
        // By glTF image, the bytes of each the model holds itself, and nothing for one beside it
        std::vector<std::span<const std::byte>> EmbeddedImages;
        // By glTF texture and how it is used
        std::map<std::pair<std::size_t, ModelImporter::TextureUsage>, TextureUse> Textures;
    };

    std::span<const std::byte> GetBytes(const fastgltf::DataSource& data)
    {
        if (const auto* l_Array = std::get_if<fastgltf::sources::Array>(&data))
        {
            return { l_Array->bytes.data(), l_Array->bytes.size_bytes() };
        }

        if (const auto* l_Vector = std::get_if<fastgltf::sources::Vector>(&data))
        {
            return l_Vector->bytes;
        }

        if (const auto* l_View = std::get_if<fastgltf::sources::ByteView>(&data))
        {
            return { l_View->bytes.data(), l_View->bytes.size() };
        }

        return {};
    }

    // Every element inside its buffer view, which PlanImport has checked lies inside its buffer, as fastgltf reads without checking
    bool IsReadable(const fastgltf::Asset& asset, std::size_t accessorIndex, std::initializer_list<fastgltf::AccessorType> types)
    {
        if (accessorIndex >= asset.accessors.size())
        {
            return false;
        }

        const fastgltf::Accessor& l_Accessor = asset.accessors[accessorIndex];
        if (std::ranges::find(types, l_Accessor.type) == types.end() || (l_Accessor.sparse && l_Accessor.sparse->count > 0))
        {
            return false;
        }

        if (!l_Accessor.bufferViewIndex.has_value() || l_Accessor.count == 0)
        {
            return true;
        }

        if (l_Accessor.bufferViewIndex.value() >= asset.bufferViews.size())
        {
            return false;
        }

        const fastgltf::BufferView& l_View = asset.bufferViews[l_Accessor.bufferViewIndex.value()];
        const std::size_t l_ElementSize = fastgltf::getElementByteSize(l_Accessor.type, l_Accessor.componentType);
        const std::size_t l_Stride = l_View.byteStride.value_or(l_ElementSize);
        if (l_ElementSize == 0 || l_Stride == 0 || l_View.meshoptCompression != nullptr || l_Accessor.byteOffset > l_View.byteLength || l_ElementSize > l_View.byteLength - l_Accessor.byteOffset)
        {
            return false;
        }

        return l_Accessor.count - 1 <= (l_View.byteLength - l_Accessor.byteOffset - l_ElementSize) / l_Stride;
    }

    template<typename T>
    std::vector<T> ReadAccessor(const fastgltf::Asset& asset, std::size_t accessorIndex)
    {
        const fastgltf::Accessor& l_Accessor = asset.accessors[accessorIndex];
        std::vector<T> l_Values;
        l_Values.reserve(l_Accessor.count);
        fastgltf::iterateAccessor<T>(asset, l_Accessor, [&l_Values](T value) { l_Values.push_back(value); });

        return l_Values;
    }

    bool IsImported(const fastgltf::Primitive& primitive)
    {
        const bool l_Triangles = primitive.type == fastgltf::PrimitiveType::Triangles || primitive.type == fastgltf::PrimitiveType::TriangleStrip || primitive.type == fastgltf::PrimitiveType::TriangleFan;

        return l_Triangles && primitive.findAttribute("POSITION") != primitive.attributes.end();
    }

    // Whole triangles in a list, finished as every reader finishes them
    Trinity::Expected<ModelReading::Primitive, std::string> ReadPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive, std::optional<std::size_t> material)
    {
        std::string l_Error;
        const auto a_Find = [&](std::string_view name, std::initializer_list<fastgltf::AccessorType> types) -> std::optional<std::size_t>
        {
            const auto* l_Attribute = primitive.findAttribute(name);
            if (l_Attribute == primitive.attributes.end())
            {
                return std::nullopt;
            }

            if (!IsReadable(asset, l_Attribute->accessorIndex, types))
            {
                l_Error = std::format("its {} cannot be read", name);

                return std::nullopt;
            }

            return l_Attribute->accessorIndex;
        };

        const std::optional<std::size_t> l_Positions = a_Find("POSITION", { fastgltf::AccessorType::Vec3 });
        const std::optional<std::size_t> l_Normals = a_Find("NORMAL", { fastgltf::AccessorType::Vec3 });
        const std::optional<std::size_t> l_Tangents = a_Find("TANGENT", { fastgltf::AccessorType::Vec4 });
        const std::optional<std::size_t> l_TexCoords0 = a_Find("TEXCOORD_0", { fastgltf::AccessorType::Vec2 });
        const std::optional<std::size_t> l_TexCoords1 = a_Find("TEXCOORD_1", { fastgltf::AccessorType::Vec2 });
        const std::optional<std::size_t> l_Colors = a_Find("COLOR_0", { fastgltf::AccessorType::Vec3, fastgltf::AccessorType::Vec4 });
        if (!l_Error.empty() || !l_Positions)
        {
            return Trinity::Unexpected{ l_Error.empty() ? std::string("it has no positions") : l_Error };
        }

        const std::size_t l_Count = asset.accessors[*l_Positions].count;
        for (const std::optional<std::size_t>& it_Accessor : { l_Normals, l_Tangents, l_TexCoords0, l_TexCoords1, l_Colors })
        {
            if (it_Accessor && asset.accessors[*it_Accessor].count != l_Count)
            {
                return Trinity::Unexpected{ std::string("its attributes hold different numbers of vertices") };
            }
        }

        ModelReading::Primitive l_Data;
        l_Data.Material = material;
        l_Data.NormalTexCoord = material && asset.materials[*material].normalTexture ? asset.materials[*material].normalTexture->texCoordIndex : 0;
        l_Data.Positions = ReadAccessor<glm::vec3>(asset, *l_Positions);
        l_Data.Normals = l_Normals ? ReadAccessor<glm::vec3>(asset, *l_Normals) : std::vector<glm::vec3>();
        l_Data.Tangents = l_Tangents ? ReadAccessor<glm::vec4>(asset, *l_Tangents) : std::vector<glm::vec4>();
        l_Data.TexCoords0 = l_TexCoords0 ? ReadAccessor<glm::vec2>(asset, *l_TexCoords0) : std::vector<glm::vec2>();
        l_Data.TexCoords1 = l_TexCoords1 ? ReadAccessor<glm::vec2>(asset, *l_TexCoords1) : std::vector<glm::vec2>();
        if (l_Colors && asset.accessors[*l_Colors].type == fastgltf::AccessorType::Vec3)
        {
            for (const glm::vec3& it_Color : ReadAccessor<glm::vec3>(asset, *l_Colors))
            {
                l_Data.Colors.emplace_back(it_Color, 1.0f);
            }
        }
        else if (l_Colors)
        {
            l_Data.Colors = ReadAccessor<glm::vec4>(asset, *l_Colors);
        }

        std::vector<std::uint32_t> l_Indices(l_Count);
        if (primitive.indicesAccessor.has_value())
        {
            const std::size_t l_Accessor = primitive.indicesAccessor.value();
            const bool l_Unsigned = l_Accessor < asset.accessors.size() && (asset.accessors[l_Accessor].componentType == fastgltf::ComponentType::UnsignedByte || asset.accessors[l_Accessor].componentType == fastgltf::ComponentType::UnsignedShort || asset.accessors[l_Accessor].componentType == fastgltf::ComponentType::UnsignedInt);
            if (!l_Unsigned || !IsReadable(asset, l_Accessor, { fastgltf::AccessorType::Scalar }))
            {
                return Trinity::Unexpected{ std::string("its indices cannot be read") };
            }

            l_Indices = ReadAccessor<std::uint32_t>(asset, l_Accessor);
        }
        else
        {
            std::iota(l_Indices.begin(), l_Indices.end(), 0u);
        }

        if (std::ranges::any_of(l_Indices, [l_Count](std::uint32_t index) { return index >= l_Count; }))
        {
            return Trinity::Unexpected{ std::format("an index names a vertex past its {}", l_Count) };
        }

        switch (primitive.type)
        {
            case fastgltf::PrimitiveType::TriangleStrip:
            {
                for (std::size_t it_Index = 0; it_Index + 2 < l_Indices.size(); ++it_Index)
                {
                    const std::size_t l_Odd = it_Index % 2;
                    l_Data.Indices.insert(l_Data.Indices.end(), { l_Indices[it_Index], l_Indices[it_Index + 1 + l_Odd], l_Indices[it_Index + 2 - l_Odd] });
                }

                break;
            }
            case fastgltf::PrimitiveType::TriangleFan:
            {
                for (std::size_t it_Index = 0; it_Index + 2 < l_Indices.size(); ++it_Index)
                {
                    l_Data.Indices.insert(l_Data.Indices.end(), { l_Indices[it_Index + 1], l_Indices[it_Index + 2], l_Indices[0] });
                }

                break;
            }
            default:
            {
                if (l_Indices.size() % 3 != 0)
                {
                    return Trinity::Unexpected{ std::format("its {} indices are not whole triangles", l_Indices.size()) };
                }

                l_Data.Indices = std::move(l_Indices);
                break;
            }
        }

        if (l_Data.Indices.empty())
        {
            return Trinity::Unexpected{ std::string("it has no triangles") };
        }

        ModelReading::FinishPrimitive(l_Data);

        return l_Data;
    }

    Trinity::MaterialAlphaMode ToAlphaMode(fastgltf::AlphaMode mode)
    {
        switch (mode)
        {
            case fastgltf::AlphaMode::Mask:
            {
                return Trinity::MaterialAlphaMode::Mask;
            }
            case fastgltf::AlphaMode::Blend:
            {
                return Trinity::MaterialAlphaMode::Blend;
            }
            default:
            {
                return Trinity::MaterialAlphaMode::Opaque;
            }
        }
    }

    TextureImportSettings GetTextureSettings(const fastgltf::Asset& asset, const fastgltf::Texture& texture, ModelImporter::TextureUsage usage)
    {
        TextureImportSettings l_Settings;
        l_Settings.Srgb = usage == ModelImporter::TextureUsage::Color;
        l_Settings.NormalMap = usage == ModelImporter::TextureUsage::Normal;
        if (texture.samplerIndex.has_value() && texture.samplerIndex.value() < asset.samplers.size() && asset.samplers[texture.samplerIndex.value()].magFilter.value_or(fastgltf::Filter::Linear) == fastgltf::Filter::Nearest)
        {
            l_Settings.Filter = Trinity::RHI::Filter::Nearest;
        }

        return l_Settings;
    }
}

// A glTF mesh as one mesh asset with a submesh for each primitive
Trinity::Expected<ModelSource::BuiltMesh, std::string> GltfSource::BuildMesh(std::size_t mesh) const
{
    std::vector<ModelReading::Primitive> l_Primitives;
    const fastgltf::Mesh& l_Mesh = Asset.meshes[mesh];
    for (std::size_t it_Primitive = 0; it_Primitive < l_Mesh.primitives.size(); ++it_Primitive)
    {
        const fastgltf::Primitive& l_Primitive = l_Mesh.primitives[it_Primitive];
        if (!IsImported(l_Primitive))
        {
            continue;
        }

        const std::optional<std::size_t> l_Material = l_Primitive.materialIndex.has_value() && l_Primitive.materialIndex.value() < Asset.materials.size() ? std::optional(l_Primitive.materialIndex.value()) : std::nullopt;
        Trinity::Expected<ModelReading::Primitive, std::string> l_Data = ReadPrimitive(Asset, l_Primitive, l_Material);
        if (!l_Data)
        {
            return Trinity::Unexpected{ std::format("primitive {} cannot be imported, since {}", it_Primitive, l_Data.GetError()) };
        }

        l_Primitives.push_back(std::move(*l_Data));
    }

    return ModelReading::MergePrimitives(std::move(l_Primitives));
}

Trinity::MaterialData GltfSource::BuildMaterial(std::size_t material, const std::function<Trinity::UUID(const TextureUse&)>& resolve) const
{
    const fastgltf::Material& l_Gltf = Asset.materials[material];
    const auto a_Texture = [&](const auto& info, ModelImporter::TextureUsage usage)
    {
        const auto a_Use = info ? Textures.find({ info->textureIndex, usage }) : Textures.end();

        return a_Use != Textures.end() ? Trinity::MaterialTexture{ resolve(a_Use->second), static_cast<std::uint32_t>(info->texCoordIndex) } : Trinity::MaterialTexture();
    };

    Trinity::MaterialData l_Material;
    const auto& l_Factor = l_Gltf.pbrData.baseColorFactor;
    l_Material.BaseColorFactor = glm::vec4(l_Factor[0], l_Factor[1], l_Factor[2], l_Factor[3]);
    l_Material.MetallicFactor = l_Gltf.pbrData.metallicFactor;
    l_Material.RoughnessFactor = l_Gltf.pbrData.roughnessFactor;
    l_Material.EmissiveFactor = glm::vec3(l_Gltf.emissiveFactor[0], l_Gltf.emissiveFactor[1], l_Gltf.emissiveFactor[2]);
    l_Material.EmissiveStrength = l_Gltf.emissiveStrength;
    l_Material.NormalScale = l_Gltf.normalTexture ? l_Gltf.normalTexture->scale : 1.0f;
    l_Material.OcclusionStrength = l_Gltf.occlusionTexture ? l_Gltf.occlusionTexture->strength : 1.0f;
    l_Material.AlphaMode = ToAlphaMode(l_Gltf.alphaMode);
    l_Material.AlphaCutoff = l_Gltf.alphaCutoff;
    l_Material.DoubleSided = l_Gltf.doubleSided;
    l_Material.BaseColorTexture = a_Texture(l_Gltf.pbrData.baseColorTexture, ModelImporter::TextureUsage::Color);
    l_Material.MetallicRoughnessTexture = a_Texture(l_Gltf.pbrData.metallicRoughnessTexture, ModelImporter::TextureUsage::Data);
    l_Material.NormalTexture = a_Texture(l_Gltf.normalTexture, ModelImporter::TextureUsage::Normal);
    l_Material.OcclusionTexture = a_Texture(l_Gltf.occlusionTexture, ModelImporter::TextureUsage::Data);
    l_Material.EmissiveTexture = a_Texture(l_Gltf.emissiveTexture, ModelImporter::TextureUsage::Color);

    return l_Material;
}

// Parents before children from the default scene's roots, or every node no other node holds when the model has no scenes. A node reached twice is created once. A root goes through the conversion, and keeps its own translation, rotation and scale exactly when there is none to make
Trinity::ModelData GltfSource::BuildHierarchy(const glm::mat4& conversion, std::span<const Trinity::UUID> meshes, std::span<const std::vector<Trinity::UUID>> meshMaterials) const
{
    std::vector<std::size_t> l_Roots;
    if (!Asset.scenes.empty())
    {
        const std::size_t l_Scene = Asset.defaultScene.value_or(0) < Asset.scenes.size() ? Asset.defaultScene.value_or(0) : 0;
        l_Roots.assign(Asset.scenes[l_Scene].nodeIndices.begin(), Asset.scenes[l_Scene].nodeIndices.end());
    }
    else
    {
        std::vector<bool> l_IsChild(Asset.nodes.size(), false);
        for (const fastgltf::Node& it_Node : Asset.nodes)
        {
            for (const std::size_t it_Child : it_Node.children)
            {
                if (it_Child < l_IsChild.size())
                {
                    l_IsChild[it_Child] = true;
                }
            }
        }

        for (std::size_t it_Node = 0; it_Node < Asset.nodes.size(); ++it_Node)
        {
            if (!l_IsChild[it_Node])
            {
                l_Roots.push_back(it_Node);
            }
        }
    }

    Trinity::ModelData l_Model;
    std::vector<bool> l_Visited(Asset.nodes.size(), false);
    std::vector<std::pair<std::size_t, std::int32_t>> l_Stack;
    for (auto it_Root = l_Roots.rbegin(); it_Root != l_Roots.rend(); ++it_Root)
    {
        l_Stack.emplace_back(*it_Root, Trinity::ModelNode::c_NoParent);
    }

    while (!l_Stack.empty())
    {
        const auto [l_NodeIndex, l_Parent] = l_Stack.back();
        l_Stack.pop_back();
        if (l_NodeIndex >= Asset.nodes.size() || l_Visited[l_NodeIndex])
        {
            continue;
        }

        l_Visited[l_NodeIndex] = true;
        const fastgltf::Node& l_Source = Asset.nodes[l_NodeIndex];
        const std::optional<std::size_t> l_Mesh = l_Source.meshIndex.has_value() && l_Source.meshIndex.value() < meshes.size() ? std::optional(l_Source.meshIndex.value()) : std::nullopt;

        Trinity::ModelNode l_Node;
        l_Node.Name = !l_Source.name.empty() ? std::string(std::string_view(l_Source.name)) : (l_Mesh && !Asset.meshes[*l_Mesh].name.empty() ? std::string(std::string_view(Asset.meshes[*l_Mesh].name)) : std::format("Node {}", l_NodeIndex));
        l_Node.Parent = l_Parent;
        l_Node.SourceNode = static_cast<std::uint32_t>(l_NodeIndex);
        if (const auto* l_TRS = std::get_if<fastgltf::TRS>(&l_Source.transform))
        {
            l_Node.Translation = glm::vec3(l_TRS->translation[0], l_TRS->translation[1], l_TRS->translation[2]);
            l_Node.Rotation = glm::quat(l_TRS->rotation[3], l_TRS->rotation[0], l_TRS->rotation[1], l_TRS->rotation[2]);
            l_Node.Scale = glm::vec3(l_TRS->scale[0], l_TRS->scale[1], l_TRS->scale[2]);
        }
        else if (const auto* l_Matrix = std::get_if<fastgltf::math::fmat4x4>(&l_Source.transform))
        {
            glm::mat4 l_Value(1.0f);
            for (glm::length_t it_Column = 0; it_Column < 4; ++it_Column)
            {
                for (glm::length_t it_Row = 0; it_Row < 4; ++it_Row)
                {
                    l_Value[it_Column][it_Row] = (*l_Matrix)[static_cast<std::size_t>(it_Column)][static_cast<std::size_t>(it_Row)];
                }
            }

            ModelReading::SetTransform(l_Node, l_Value);
        }

        if (l_Parent == Trinity::ModelNode::c_NoParent && conversion != glm::mat4(1.0f))
        {
            ModelReading::SetTransform(l_Node, conversion * Trinity::TransformComponent{ l_Node.Translation, l_Node.Rotation, l_Node.Scale }.GetMatrix());
        }

        if (l_Mesh && meshes[*l_Mesh].IsValid())
        {
            l_Node.Mesh = meshes[*l_Mesh];
            l_Node.Materials = meshMaterials[*l_Mesh];
        }

        const std::int32_t l_Index = static_cast<std::int32_t>(l_Model.Nodes.size());
        l_Model.Nodes.push_back(std::move(l_Node));
        for (std::size_t it_Child = l_Source.children.size(); it_Child > 0; --it_Child)
        {
            l_Stack.emplace_back(l_Source.children[it_Child - 1], l_Index);
        }
    }

    return l_Model;
}

namespace ModelReading
{
    // The file and every buffer beside it are read, and checked as far as fastgltf trusts them, so cooking reads nothing it has not checked. Images beside it are only named, since they are textures of their own
    std::shared_ptr<ModelSource> ReadGltf(const Trinity::AssetRecord& record, ModelImporter::Plan& plan)
    {
        const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_File = Trinity::FileSystem::ReadFile(record.Path);
        if (!l_File)
        {
            TR_ERROR("Models: {} could not be read: {}", record.Path, Trinity::ToString(l_File.GetError()));

            return nullptr;
        }

        fastgltf::Expected<fastgltf::GltfDataBuffer> l_Data = fastgltf::GltfDataBuffer::FromBytes(l_File->data(), l_File->size());
        if (l_Data.error() != fastgltf::Error::None)
        {
            TR_ERROR("Models: {} could not be read: {}", record.Path, fastgltf::getErrorMessage(l_Data.error()));

            return nullptr;
        }

        const fastgltf::Extensions l_Extensions = fastgltf::Extensions::KHR_materials_emissive_strength | fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_transform;
        fastgltf::Parser l_Parser(l_Extensions);
        fastgltf::Expected<fastgltf::Asset> l_Parsed = l_Parser.loadGltf(l_Data.get(), {}, fastgltf::Options::DecomposeNodeMatrices);
        if (l_Parsed.error() != fastgltf::Error::None)
        {
            TR_ERROR("Models: {} is not a glTF that can be imported: {} ({})", record.Path, fastgltf::getErrorMessage(l_Parsed.error()), fastgltf::getErrorName(l_Parsed.error()));

            return nullptr;
        }

        const std::shared_ptr<GltfSource> l_Source = std::make_shared<GltfSource>();
        l_Source->Asset = std::move(l_Parsed.get());
        fastgltf::Asset& l_Asset = l_Source->Asset;

        std::uint64_t l_Hash = Hash(*l_File);
        for (std::size_t it_Buffer = 0; it_Buffer < l_Asset.buffers.size(); ++it_Buffer)
        {
            fastgltf::Buffer& l_Buffer = l_Asset.buffers[it_Buffer];
            if (const auto* l_URI = std::get_if<fastgltf::sources::URI>(&l_Buffer.data))
            {
                const std::optional<std::string> l_Path = l_URI->uri.isLocalPath() ? ResolvePath(record.Path, l_URI->uri.path()) : std::nullopt;
                const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Bytes = l_Path ? Trinity::FileSystem::ReadFile(*l_Path) : Trinity::Expected<Trinity::FileBuffer, Trinity::FileError>(Trinity::Unexpected{ Trinity::FileError::NotFound });
                if (!l_Bytes || l_Bytes->size() < l_URI->fileByteOffset || l_Bytes->size() - l_URI->fileByteOffset < l_Buffer.byteLength)
                {
                    TR_ERROR("Models: {} names {} for buffer {}, which is not a file of at least {} bytes beside it", record.Path, l_URI->uri.string(), it_Buffer, l_Buffer.byteLength);

                    return nullptr;
                }

                const auto a_First = l_Bytes->begin() + static_cast<std::ptrdiff_t>(l_URI->fileByteOffset);
                std::vector<std::byte> l_Contents(a_First, a_First + static_cast<std::ptrdiff_t>(l_Buffer.byteLength));
                l_Hash = Hash(l_Contents, l_Hash);
                l_Buffer.data = fastgltf::sources::Vector{ std::move(l_Contents), fastgltf::MimeType::GltfBuffer };
            }

            if (GetBytes(l_Buffer.data).size() < l_Buffer.byteLength)
            {
                TR_ERROR("Models: buffer {} of {} holds fewer bytes than it says, or none", it_Buffer, record.Path);

                return nullptr;
            }
        }

        for (std::size_t it_View = 0; it_View < l_Asset.bufferViews.size(); ++it_View)
        {
            const fastgltf::BufferView& l_View = l_Asset.bufferViews[it_View];
            const std::size_t l_Size = l_View.bufferIndex < l_Asset.buffers.size() ? l_Asset.buffers[l_View.bufferIndex].byteLength : 0;
            if (l_View.byteOffset > l_Size || l_View.byteLength > l_Size - l_View.byteOffset)
            {
                TR_ERROR("Models: buffer view {} of {} reaches past its buffer", it_View, record.Path);

                return nullptr;
            }
        }

        l_Source->EmbeddedImages.resize(l_Asset.images.size());
        for (std::size_t it_Image = 0; it_Image < l_Asset.images.size(); ++it_Image)
        {
            const fastgltf::DataSource& l_Image = l_Asset.images[it_Image].data;
            if (const auto* l_View = std::get_if<fastgltf::sources::BufferView>(&l_Image); l_View != nullptr && l_View->bufferViewIndex < l_Asset.bufferViews.size())
            {
                const fastgltf::BufferView& l_BufferView = l_Asset.bufferViews[l_View->bufferViewIndex];
                l_Source->EmbeddedImages[it_Image] = GetBytes(l_Asset.buffers[l_BufferView.bufferIndex].data).subspan(l_BufferView.byteOffset, l_BufferView.byteLength);
            }
            else
            {
                l_Source->EmbeddedImages[it_Image] = GetBytes(l_Image);
            }
        }

        l_Source->MeshKeys.resize(l_Asset.meshes.size());
        for (std::size_t it_Mesh = 0; it_Mesh < l_Asset.meshes.size(); ++it_Mesh)
        {
            const fastgltf::Mesh& l_Mesh = l_Asset.meshes[it_Mesh];
            if (std::ranges::any_of(l_Mesh.primitives, IsImported))
            {
                l_Source->MeshKeys[it_Mesh] = MakeKey("Mesh", it_Mesh, {}, l_Mesh.name);
            }
            else
            {
                TR_WARN("Models: mesh {} of {} has no triangles, so it is left out", it_Mesh, record.Path);
            }
        }

        bool l_Transformed = false;
        const auto a_Use = [&](const fastgltf::TextureInfo& info, ModelImporter::TextureUsage usage)
        {
            l_Transformed = l_Transformed || info.transform != nullptr;
            const std::pair<std::size_t, ModelImporter::TextureUsage> l_Use{ info.textureIndex, usage };
            if (l_Source->Textures.contains(l_Use))
            {
                return;
            }

            const fastgltf::Texture* l_Texture = info.textureIndex < l_Asset.textures.size() ? &l_Asset.textures[info.textureIndex] : nullptr;
            const std::size_t l_ImageIndex = l_Texture != nullptr ? l_Texture->imageIndex.value_or(l_Asset.images.size()) : l_Asset.images.size();
            if (l_ImageIndex >= l_Asset.images.size())
            {
                TR_WARN("Models: texture {} of {} has no PNG or JPEG image, so materials using it go without", info.textureIndex, record.Path);

                return;
            }

            const fastgltf::Image& l_Image = l_Asset.images[l_ImageIndex];
            if (const auto* l_URI = std::get_if<fastgltf::sources::URI>(&l_Image.data))
            {
                const std::optional<std::string> l_Path = l_URI->uri.isLocalPath() ? ResolvePath(record.Path, l_URI->uri.path()) : std::nullopt;
                if (!l_Path)
                {
                    TR_WARN("Models: {} names {} for image {}, which is not a file inside the project's Assets, so materials using it go without", record.Path, l_URI->uri.string(), l_ImageIndex);

                    return;
                }

                l_Source->Textures[l_Use] = { {}, AddExternalTexture(plan, record.Path, *l_Path, usage) };

                return;
            }

            if (l_Source->EmbeddedImages[l_ImageIndex].empty())
            {
                TR_WARN("Models: image {} of {} could not be found in the file, so materials using it go without", l_ImageIndex, record.Path);

                return;
            }

            const std::string_view l_Name = !l_Texture->name.empty() ? std::string_view(l_Texture->name) : std::string_view(l_Image.name);
            l_Source->Textures[l_Use] = { MakeKey("Texture", info.textureIndex, ModelImporter::ToString(usage), l_Name), 0 };
        };

        l_Source->MaterialKeys.resize(l_Asset.materials.size());
        for (std::size_t it_Material = 0; it_Material < l_Asset.materials.size(); ++it_Material)
        {
            const fastgltf::Material& l_Material = l_Asset.materials[it_Material];
            l_Source->MaterialKeys[it_Material] = MakeKey("Material", it_Material, {}, l_Material.name);

            if (l_Material.pbrData.baseColorTexture)
            {
                a_Use(*l_Material.pbrData.baseColorTexture, ModelImporter::TextureUsage::Color);
            }

            if (l_Material.pbrData.metallicRoughnessTexture)
            {
                a_Use(*l_Material.pbrData.metallicRoughnessTexture, ModelImporter::TextureUsage::Data);
            }

            if (l_Material.normalTexture)
            {
                a_Use(*l_Material.normalTexture, ModelImporter::TextureUsage::Normal);
            }

            if (l_Material.occlusionTexture)
            {
                a_Use(*l_Material.occlusionTexture, ModelImporter::TextureUsage::Data);
            }

            if (l_Material.emissiveTexture)
            {
                a_Use(*l_Material.emissiveTexture, ModelImporter::TextureUsage::Color);
            }
        }

        for (const auto& [it_Use, it_Texture] : l_Source->Textures)
        {
            if (!it_Texture.Key.empty())
            {
                const fastgltf::Texture& l_Texture = l_Asset.textures[it_Use.first];
                l_Source->EmbeddedTextures.push_back({ it_Texture.Key, l_Source->EmbeddedImages[l_Texture.imageIndex.value()], GetTextureSettings(l_Asset, l_Texture, it_Use.second) });
            }
        }

        if (l_Transformed)
        {
            TR_WARN("Models: {} moves or scales some textures with KHR_texture_transform, which is not imported yet, so they are sampled as they are", record.Path);
        }

        plan.ContentHash = l_Hash;

        return l_Source;
    }
}