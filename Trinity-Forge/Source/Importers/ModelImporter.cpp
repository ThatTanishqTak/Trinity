#include "Importers/ModelImporter.hpp"

#include "Importers/ModelSource.hpp"
#include "Importers/TextureImporter.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <format>
#include <iterator>
#include <system_error>

namespace
{
    constexpr std::string_view c_UnitScaleSetting = "UnitScale";
    constexpr std::string_view c_UpAxisSetting = "UpAxis";
    constexpr std::string_view c_Auto = "Auto";
    constexpr std::array c_UpAxes{ ModelUpAxis::Auto, ModelUpAxis::Y, ModelUpAxis::Z };

    std::string_view ToString(ModelUpAxis axis)
    {
        switch (axis)
        {
            case ModelUpAxis::Y:
            {
                return "Y";
            }
            case ModelUpAxis::Z:
            {
                return "Z";
            }
            default:
            {
                return c_Auto;
            }
        }
    }

    std::string FormatUnitScale(const std::optional<float>& scale)
    {
        return scale ? std::format("{}", *scale) : std::string(c_Auto);
    }

    bool IsOverride(const Trinity::AssetSetting& setting)
    {
        return setting.Key.find(ModelImporter::c_OverrideSeparator) != std::string::npos;
    }

    bool IsOverrideOf(const Trinity::AssetSetting& setting, std::string_view material)
    {
        return setting.Key.size() > material.size() && setting.Key.starts_with(material) && setting.Key[material.size()] == ModelImporter::c_OverrideSeparator;
    }

    bool IsGltf(std::string_view path)
    {
        std::string l_Extension(path.substr(std::min(path.find_last_of('.'), path.size())));
        std::ranges::transform(l_Extension, l_Extension.begin(), [](char character) { return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character; });

        return l_Extension == ".gltf" || l_Extension == ".glb";
    }

    // Metres and Y up from what the file says of itself, or from the settings where they say otherwise. Z up turns (x, y, z) into (x, z, -y)
    glm::mat4 GetConversion(const ModelSource& source, const ModelImportSettings& settings)
    {
        const glm::mat3 l_ZUp(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);
        const glm::mat3 l_Axes = settings.UpAxis == ModelUpAxis::Auto ? source.Axes : (settings.UpAxis == ModelUpAxis::Z ? l_ZUp : glm::mat3(1.0f));

        return glm::mat4(l_Axes * settings.UnitScale.value_or(source.UnitScale));
    }
}

Trinity::AssetSettings ModelImporter::GetDefaultSettings()
{
    return MakeSettings({});
}

Trinity::AssetSettings ModelImporter::MakeSettings(const ModelImportSettings& settings)
{
    return { { std::string(c_UnitScaleSetting), FormatUnitScale(settings.UnitScale) }, { std::string(c_UpAxisSetting), std::string(::ToString(settings.UpAxis)) } };
}

Trinity::AssetSettings ModelImporter::MakeSettings(const ModelImportSettings& settings, const Trinity::AssetRecord& record)
{
    Trinity::AssetSettings l_Settings = MakeSettings(settings);
    std::ranges::copy_if(record.Settings, std::back_inserter(l_Settings), IsOverride);

    return l_Settings;
}

// A missing setting takes its default. One that cannot be read does too, with a warning naming the file
ModelImportSettings ModelImporter::ReadSettings(const Trinity::AssetRecord& record)
{
    ModelImportSettings l_Settings;
    if (const std::string* l_Scale = record.FindSetting(c_UnitScaleSetting); l_Scale != nullptr && *l_Scale != c_Auto)
    {
        float l_Value = 0.0f;
        const std::from_chars_result l_Parsed = std::from_chars(l_Scale->data(), l_Scale->data() + l_Scale->size(), l_Value);
        if (l_Parsed.ec != std::errc() || l_Parsed.ptr != l_Scale->data() + l_Scale->size() || !(l_Value > 0.0f))
        {
            TR_WARN("Models: {} has {}: {}, which is neither {} nor a positive number, so the file's own units are used", record.Path, c_UnitScaleSetting, *l_Scale, c_Auto);
        }
        else
        {
            l_Settings.UnitScale = l_Value;
        }
    }

    if (const std::string* l_Axis = record.FindSetting(c_UpAxisSetting))
    {
        const auto a_Axis = std::ranges::find_if(c_UpAxes, [l_Axis](ModelUpAxis axis) { return ::ToString(axis) == *l_Axis; });
        if (a_Axis == c_UpAxes.end())
        {
            TR_WARN("Models: {} has {}: {}, which is not Auto, Y or Z, so the file's own axes are used", record.Path, c_UpAxisSetting, *l_Axis);
        }
        else
        {
            l_Settings.UpAxis = *a_Axis;
        }
    }

    return l_Settings;
}

std::string ModelImporter::GetImportedMaterialPath(Trinity::UUID id)
{
    return std::format("{}/Materials/{}.imported", Trinity::Project::c_CacheMount, id);
}

std::vector<Trinity::MaterialField> ModelImporter::ReadMaterialOverrides(const Trinity::AssetRecord& model, std::string_view material)
{
    std::vector<Trinity::MaterialField> l_Fields;
    for (const Trinity::AssetSetting& it_Setting : model.Settings)
    {
        if (IsOverrideOf(it_Setting, material))
        {
            l_Fields.push_back({ it_Setting.Key.substr(material.size() + 1), it_Setting.Value });
        }
    }

    return l_Fields;
}

// Overrides that do not read leave the material as it was imported, with a warning naming them
Trinity::MaterialData ModelImporter::ApplyMaterialOverrides(const Trinity::AssetRecord& model, std::string_view material, const Trinity::MaterialData& imported)
{
    const std::vector<Trinity::MaterialField> l_Fields = ReadMaterialOverrides(model, material);
    if (l_Fields.empty())
    {
        return imported;
    }

    const Trinity::Expected<Trinity::MaterialData, std::string> l_Applied = Trinity::ApplyMaterialFields(imported, l_Fields);
    if (!l_Applied)
    {
        TR_WARN("Models: the overrides of {} in {}'s .meta cannot be applied, since {}, so it stays as imported", material, model.Path, l_Applied.GetError());

        return imported;
    }

    return *l_Applied;
}

// Kept in the order the fields are written, after the settings that are not this material's overrides
Trinity::AssetSettings ModelImporter::MakeMaterialOverrides(const Trinity::AssetRecord& model, std::string_view material, const Trinity::MaterialData& imported, const Trinity::MaterialData& edited)
{
    Trinity::AssetSettings l_Settings;
    std::ranges::copy_if(model.Settings, std::back_inserter(l_Settings), [material](const Trinity::AssetSetting& setting) { return !IsOverrideOf(setting, material); });

    const std::vector<Trinity::MaterialField> l_Imported = Trinity::GetMaterialFields(imported);
    const std::vector<Trinity::MaterialField> l_Edited = Trinity::GetMaterialFields(edited);
    for (std::size_t it_Field = 0; it_Field < l_Edited.size(); ++it_Field)
    {
        if (l_Edited[it_Field] != l_Imported[it_Field])
        {
            l_Settings.push_back({ std::format("{}{}{}", material, c_OverrideSeparator, l_Edited[it_Field].Name), l_Edited[it_Field].Value });
        }
    }

    return l_Settings;
}

// A material whose imported file is missing is left to the next import, which a missing file brings about
std::vector<Trinity::UUID> ModelImporter::RefreshMaterials(const Trinity::AssetRecord& model)
{
    std::vector<Trinity::UUID> l_Written;
    for (const Trinity::SubAsset& it_SubAsset : model.SubAssets)
    {
        if (it_SubAsset.Importer != c_MaterialImporter)
        {
            continue;
        }

        const Trinity::Expected<std::string, Trinity::FileError> l_ImportedText = Trinity::FileSystem::ReadText(GetImportedMaterialPath(it_SubAsset.ID));
        const Trinity::Expected<Trinity::MaterialData, std::string> l_Imported = l_ImportedText ? Trinity::ParseMaterialData(*l_ImportedText) : Trinity::Expected<Trinity::MaterialData, std::string>(Trinity::Unexpected{ std::string() });
        if (!l_Imported)
        {
            continue;
        }

        const std::string l_Text = Trinity::WriteMaterialData(ApplyMaterialOverrides(model, it_SubAsset.Key, *l_Imported));
        const std::string l_Path = Trinity::GetCookedMaterialPath(it_SubAsset.ID);
        const Trinity::Expected<std::string, Trinity::FileError> l_Cooked = Trinity::FileSystem::ReadText(l_Path);
        if (l_Cooked && *l_Cooked == l_Text)
        {
            continue;
        }

        if (const Trinity::Expected<void, Trinity::FileError> l_Result = Trinity::FileSystem::WriteText(l_Path, l_Text); !l_Result)
        {
            TR_ERROR("Models: {} could not be written for {}: {}", l_Path, model.Path, Trinity::ToString(l_Result.GetError()));

            continue;
        }

        TR_INFO("Models: {} of {} now has the overrides its .meta gives", it_SubAsset.Key, model.Path);
        l_Written.push_back(it_SubAsset.ID);
    }

    return l_Written;
}

std::optional<Trinity::AssetSettings> ModelImporter::PruneMaterialOverrides(const Trinity::AssetRecord& model)
{
    Trinity::AssetSettings l_Settings;
    for (const Trinity::AssetSetting& it_Setting : model.Settings)
    {
        const bool l_Kept = !IsOverride(it_Setting) || std::ranges::any_of(model.SubAssets, [&it_Setting](const Trinity::SubAsset& subAsset) { return subAsset.Importer == c_MaterialImporter && IsOverrideOf(it_Setting, subAsset.Key); });
        if (l_Kept)
        {
            l_Settings.push_back(it_Setting);
        }
        else
        {
            TR_WARN("Models: {} no longer has the material {} overrides, so the override is dropped from its .meta", model.Path, it_Setting.Key);
        }
    }

    return l_Settings.size() != model.Settings.size() ? std::optional(std::move(l_Settings)) : std::nullopt;
}

std::string ModelImporter::GetCacheKeyPath(Trinity::UUID id)
{
    return std::format("{}/Models/{}.key", Trinity::Project::c_CacheMount, id);
}

// The model's files, its settings, its sub-assets' keys and UUIDs, the textures beside it, and both importers' versions, since a texture it embeds is encoded as the texture importer encodes
std::string ModelImporter::GetCacheKey(const Trinity::AssetRecord& record, const Plan& plan, std::span<const Trinity::UUID> externalTextures)
{
    std::uint64_t l_SubAssets = ModelReading::c_HashSeed;
    for (const Trinity::SubAsset& it_SubAsset : record.SubAssets)
    {
        l_SubAssets = ModelReading::Hash(std::format("{}/{}/{};", it_SubAsset.Key, it_SubAsset.Importer, it_SubAsset.ID), l_SubAssets);
    }

    std::uint64_t l_Textures = ModelReading::c_HashSeed;
    for (const Trinity::UUID it_Texture : externalTextures)
    {
        l_Textures = ModelReading::Hash(std::format("{};", it_Texture), l_Textures);
    }

    const ModelImportSettings l_Settings = ReadSettings(record);

    return std::format("content {:016x}, unit scale {}, up axis {}, sub-assets {:016x}, textures {:016x}, importer {}, texture importer {}", plan.ContentHash, FormatUnitScale(l_Settings.UnitScale), ::ToString(l_Settings.UpAxis), l_SubAssets, l_Textures, c_Version, TextureImporter::c_Version);
}

std::string_view ModelImporter::ToString(TextureUsage usage)
{
    switch (usage)
    {
        case TextureUsage::Color:
        {
            return "Color";
        }
        case TextureUsage::Data:
        {
            return "Data";
        }
        case TextureUsage::Normal:
        {
            return "Normal";
        }
    }

    return "Unknown";
}

std::string ModelImporter::GetCookedPath(const Trinity::SubAsset& subAsset)
{
    if (subAsset.Importer == Trinity::MeshAsset::c_AssetType)
    {
        return Trinity::GetCookedMeshPath(subAsset.ID);
    }

    if (subAsset.Importer == c_MaterialImporter)
    {
        return Trinity::GetCookedMaterialPath(subAsset.ID);
    }

    return subAsset.Importer == Trinity::TextureAsset::c_AssetType ? Trinity::GetCookedTexturePath(subAsset.ID) : std::string();
}

// glTF through fastgltf and everything else through assimp. The sub-assets come in one order for every format: meshes, materials, then the textures the file holds
ModelImporter::Plan ModelImporter::PlanImport(const Trinity::AssetRecord& record)
{
    TR_PROFILE_FUNCTION();

    Plan l_Plan;
    const std::shared_ptr<ModelSource> l_Source = IsGltf(record.Path) ? ModelReading::ReadGltf(record, l_Plan) : ModelReading::ReadWithAssimp(record, l_Plan);
    if (!l_Source)
    {
        return l_Plan;
    }

    for (const std::string& it_Key : l_Source->MeshKeys)
    {
        if (!it_Key.empty())
        {
            l_Plan.SubAssets.push_back({ it_Key, std::string(Trinity::MeshAsset::c_AssetType), {} });
        }
    }

    for (const std::string& it_Key : l_Source->MaterialKeys)
    {
        l_Plan.SubAssets.push_back({ it_Key, std::string(c_MaterialImporter), {} });
    }

    for (const ModelSource::EmbeddedTexture& it_Texture : l_Source->EmbeddedTextures)
    {
        l_Plan.SubAssets.push_back({ it_Texture.Key, std::string(Trinity::TextureAsset::c_AssetType), {} });
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_CachedKey = Trinity::FileSystem::ReadText(GetCacheKeyPath(record.ID));
    l_Plan.CachedKey = l_CachedKey ? *l_CachedKey : std::string();
    l_Plan.CookedFilesExist = Trinity::FileSystem::Exists(Trinity::GetCookedModelPath(record.ID)) && std::ranges::all_of(record.SubAssets, [](const Trinity::SubAsset& subAsset)
    {
        const std::string l_Path = GetCookedPath(subAsset);

        return !l_Path.empty() && Trinity::FileSystem::Exists(l_Path) && (subAsset.Importer != c_MaterialImporter || Trinity::FileSystem::Exists(GetImportedMaterialPath(subAsset.ID)));
    });

    l_Plan.Model = l_Source;
    l_Plan.Read = true;

    return l_Plan;
}

// Embedded textures first, since they take the time, each skipped when its own key says the cache already holds it. The key is written last, so a cook cut short is cooked again
ModelImporter::Cooked ModelImporter::Cook(const Trinity::AssetRecord& record, const Plan& plan, std::span<const Trinity::UUID> externalTextures, const std::atomic<bool>& stop)
{
    TR_PROFILE_FUNCTION();

    const auto l_Start = std::chrono::steady_clock::now();
    const ModelSource& l_Source = *plan.Model;

    Cooked l_Cooked;
    bool l_Failed = false;
    const auto a_Find = [&record](std::string_view key)
    {
        const auto a_Found = std::ranges::find(record.SubAssets, key, &Trinity::SubAsset::Key);

        return a_Found != record.SubAssets.end() ? a_Found->ID : Trinity::UUID();
    };

    const auto a_Write = [&](Trinity::UUID id, const std::string& path, std::span<const std::byte> bytes)
    {
        const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteFile(path, bytes);
        if (!l_Written)
        {
            TR_ERROR("Models: {} could not be written for {}: {}", path, record.Path, Trinity::ToString(l_Written.GetError()));
            l_Failed = true;

            return false;
        }

        if (id.IsValid())
        {
            l_Cooked.Written.push_back(id);
        }

        return true;
    };

    for (const ModelSource::EmbeddedTexture& it_Texture : l_Source.EmbeddedTextures)
    {
        if (stop.load(std::memory_order_relaxed))
        {
            l_Cooked.Outcome = Result::Stopped;

            return l_Cooked;
        }

        const Trinity::UUID l_ID = a_Find(it_Texture.Key);
        if (!l_ID.IsValid())
        {
            continue;
        }

        const std::string l_Key = TextureImporter::GetCacheKey(it_Texture.Bytes, it_Texture.Settings);
        if (TextureImporter::IsCached(l_ID, l_Key))
        {
            ++l_Cooked.TexturesCached;

            continue;
        }

        const Trinity::Expected<TextureImporter::EncodedTexture, std::string> l_Encoded = TextureImporter::Encode(it_Texture.Bytes, it_Texture.Settings);
        if (!l_Encoded)
        {
            TR_ERROR("Models: {} of {} was not imported, since {}", it_Texture.Key, record.Path, l_Encoded.GetError());
            l_Failed = true;

            continue;
        }

        if (a_Write(l_ID, Trinity::GetCookedTexturePath(l_ID), l_Encoded->File) && a_Write({}, TextureImporter::GetCacheKeyPath(l_ID), std::as_bytes(std::span(l_Key))))
        {
            ++l_Cooked.TexturesEncoded;
        }
    }

    std::vector<Trinity::UUID> l_Materials(l_Source.MaterialKeys.size());
    std::ranges::transform(l_Source.MaterialKeys, l_Materials.begin(), a_Find);

    std::vector<Trinity::UUID> l_Meshes(l_Source.MeshKeys.size());
    std::vector<std::vector<Trinity::UUID>> l_MeshMaterials(l_Source.MeshKeys.size());
    for (std::size_t it_Mesh = 0; it_Mesh < l_Source.MeshKeys.size(); ++it_Mesh)
    {
        const Trinity::UUID l_ID = a_Find(l_Source.MeshKeys[it_Mesh]);
        if (!l_ID.IsValid())
        {
            continue;
        }

        const Trinity::Expected<ModelSource::BuiltMesh, std::string> l_Mesh = l_Source.BuildMesh(it_Mesh);
        const Trinity::Expected<std::vector<std::byte>, std::string> l_File = l_Mesh ? Trinity::CookMesh(l_Mesh->Data) : Trinity::Expected<std::vector<std::byte>, std::string>(Trinity::Unexpected{ l_Mesh.GetError() });
        if (!l_File)
        {
            TR_ERROR("Models: {} of {} was not imported, since {}", l_Source.MeshKeys[it_Mesh], record.Path, l_File.GetError());
            l_Failed = true;

            continue;
        }

        if (a_Write(l_ID, Trinity::GetCookedMeshPath(l_ID), *l_File))
        {
            l_Meshes[it_Mesh] = l_ID;
            for (const std::optional<std::size_t>& it_Material : l_Mesh->SlotMaterials)
            {
                l_MeshMaterials[it_Mesh].push_back(it_Material && *it_Material < l_Materials.size() ? l_Materials[*it_Material] : Trinity::UUID());
            }
        }
    }

    const auto a_Resolve = [&](const ModelSource::TextureUse& use) { return use.Key.empty() ? (use.External < externalTextures.size() ? externalTextures[use.External] : Trinity::UUID()) : a_Find(use.Key); };
    // Each material as imported, which edits are compared with, then with the overrides its .meta gives
    for (std::size_t it_Material = 0; it_Material < l_Source.MaterialKeys.size(); ++it_Material)
    {
        const Trinity::MaterialData l_Imported = l_Source.BuildMaterial(it_Material, a_Resolve);
        const std::string l_ImportedText = Trinity::WriteMaterialData(l_Imported);
        const std::string l_Text = Trinity::WriteMaterialData(ApplyMaterialOverrides(record, l_Source.MaterialKeys[it_Material], l_Imported));
        if (a_Write({}, GetImportedMaterialPath(l_Materials[it_Material]), std::as_bytes(std::span(l_ImportedText))))
        {
            static_cast<void>(a_Write(l_Materials[it_Material], Trinity::GetCookedMaterialPath(l_Materials[it_Material]), std::as_bytes(std::span(l_Text))));
        }
    }

    const Trinity::ModelData l_Model = l_Source.BuildHierarchy(GetConversion(l_Source, ReadSettings(record)), l_Meshes, l_MeshMaterials);
    const std::string l_ModelText = Trinity::WriteModelData(l_Model);
    static_cast<void>(a_Write({}, Trinity::GetCookedModelPath(record.ID), std::as_bytes(std::span(l_ModelText))));

    if (l_Failed)
    {
        TR_ERROR("Models: {} was not fully imported, and is imported again at the next refresh", record.Path);

        return l_Cooked;
    }

    const std::string l_Key = GetCacheKey(record, plan, externalTextures);
    l_Cooked.Outcome = a_Write({}, GetCacheKeyPath(record.ID), std::as_bytes(std::span(l_Key))) ? Result::Imported : Result::Failed;

    const auto l_Milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - l_Start).count();
    TR_INFO("Models: imported {} into {} mesh(es), {} material(s) and {} node(s), with {} texture(s) encoded, {} from the cache and {} beside it, in {} ms", record.Path, std::ranges::count_if(l_Meshes, [](Trinity::UUID id) { return id.IsValid(); }), l_Materials.size(), l_Model.Nodes.size(), l_Cooked.TexturesEncoded, l_Cooked.TexturesCached, externalTextures.size(), l_Milliseconds);

    return l_Cooked;
}