#include "Trinity/Asset/MaterialData.hpp"

#include "Trinity/Asset/CookedText.hpp"
#include "Trinity/Project/Project.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"

#include <yaml-cpp/yaml.h>

#include <array>
#include <format>
#include <span>

namespace Trinity
{
    namespace
    {
        struct TextureSlot
        {
            std::string_view Texture;
            std::string_view TexCoord;
            MaterialTexture MaterialData::* Member = nullptr;
        };

        constexpr std::array<TextureSlot, 5> c_TextureSlots{ {
            { "BaseColorTexture", "BaseColorTexCoord", &MaterialData::BaseColorTexture },
            { "MetallicRoughnessTexture", "MetallicRoughnessTexCoord", &MaterialData::MetallicRoughnessTexture },
            { "NormalTexture", "NormalTexCoord", &MaterialData::NormalTexture },
            { "OcclusionTexture", "OcclusionTexCoord", &MaterialData::OcclusionTexture },
            { "EmissiveTexture", "EmissiveTexCoord", &MaterialData::EmissiveTexture }
        } };
    }

    std::string GetCookedMaterialPath(UUID id)
    {
        return std::format("{}/Materials/{}.trmat", Project::c_CacheMount, id);
    }

    std::string WriteMaterialData(const MaterialData& material)
    {
        std::string l_Text = std::format("Format: {}", MaterialData::c_FormatVersion);
        ComponentWriter l_Writer(l_Text, 0);
        l_Writer.Write("BaseColorFactor", material.BaseColorFactor);
        l_Writer.Write("MetallicFactor", material.MetallicFactor);
        l_Writer.Write("RoughnessFactor", material.RoughnessFactor);
        l_Writer.Write("EmissiveFactor", material.EmissiveFactor);
        l_Writer.Write("EmissiveStrength", material.EmissiveStrength);
        l_Writer.Write("NormalScale", material.NormalScale);
        l_Writer.Write("OcclusionStrength", material.OcclusionStrength);
        l_Writer.Write("AlphaMode", ToString(material.AlphaMode));
        l_Writer.Write("AlphaCutoff", material.AlphaCutoff);
        l_Writer.Write("DoubleSided", material.DoubleSided);
        for (const TextureSlot& it_Slot : c_TextureSlots)
        {
            const MaterialTexture& l_Texture = material.*it_Slot.Member;
            if (l_Texture.Texture.IsValid())
            {
                l_Writer.Write(it_Slot.Texture, l_Texture.Texture);
                l_Writer.Write(it_Slot.TexCoord, l_Texture.TexCoord);
            }
        }

        return l_Text + "\n";
    }

    // A key that is missing keeps its default. A malformed one, or a newer format, refuses the file
    Expected<MaterialData, std::string> ParseMaterialData(std::string_view text)
    {
        const Expected<YAML::Node, std::string> l_Root = CookedText::LoadRoot(text, "a material", MaterialData::c_FormatVersion);
        if (!l_Root)
        {
            return Unexpected{ l_Root.GetError() };
        }

        const YAML::Node& l_Map = *l_Root;
        MaterialData l_Material;
        std::string l_Malformed;
        CookedText::ReadFloats(l_Map, "BaseColorFactor", std::span(&l_Material.BaseColorFactor.x, 4), l_Malformed);
        CookedText::ReadNumber(l_Map, "MetallicFactor", l_Material.MetallicFactor, l_Malformed);
        CookedText::ReadNumber(l_Map, "RoughnessFactor", l_Material.RoughnessFactor, l_Malformed);
        CookedText::ReadFloats(l_Map, "EmissiveFactor", std::span(&l_Material.EmissiveFactor.x, 3), l_Malformed);
        CookedText::ReadNumber(l_Map, "EmissiveStrength", l_Material.EmissiveStrength, l_Malformed);
        CookedText::ReadNumber(l_Map, "NormalScale", l_Material.NormalScale, l_Malformed);
        CookedText::ReadNumber(l_Map, "OcclusionStrength", l_Material.OcclusionStrength, l_Malformed);
        CookedText::ReadNumber(l_Map, "AlphaCutoff", l_Material.AlphaCutoff, l_Malformed);
        CookedText::ReadBool(l_Map, "DoubleSided", l_Material.DoubleSided, l_Malformed);

        std::string l_AlphaMode(ToString(l_Material.AlphaMode));
        CookedText::ReadString(l_Map, "AlphaMode", l_AlphaMode, l_Malformed);
        if (const std::optional<MaterialAlphaMode> l_Mode = ParseMaterialAlphaMode(l_AlphaMode))
        {
            l_Material.AlphaMode = *l_Mode;
        }
        else
        {
            l_Malformed = "AlphaMode";
        }

        for (const TextureSlot& it_Slot : c_TextureSlots)
        {
            MaterialTexture& l_Texture = l_Material.*it_Slot.Member;
            CookedText::ReadUUID(l_Map, it_Slot.Texture, l_Texture.Texture, l_Malformed);
            CookedText::ReadNumber(l_Map, it_Slot.TexCoord, l_Texture.TexCoord, l_Malformed);
        }

        if (!l_Malformed.empty())
        {
            return Unexpected{ std::format("{} is malformed", l_Malformed) };
        }

        return l_Material;
    }

    std::string_view ToString(MaterialAlphaMode mode)
    {
        switch (mode)
        {
            case MaterialAlphaMode::Opaque:
            {
                return "Opaque";
            }
            case MaterialAlphaMode::Mask:
            {
                return "Mask";
            }
            case MaterialAlphaMode::Blend:
            {
                return "Blend";
            }
        }

        return "Unknown";
    }

    std::optional<MaterialAlphaMode> ParseMaterialAlphaMode(std::string_view text)
    {
        for (const MaterialAlphaMode it_Mode : { MaterialAlphaMode::Opaque, MaterialAlphaMode::Mask, MaterialAlphaMode::Blend })
        {
            if (text == ToString(it_Mode))
            {
                return it_Mode;
            }
        }

        return std::nullopt;
    }
}