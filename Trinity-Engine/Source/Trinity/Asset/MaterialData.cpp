#include "Trinity/Asset/MaterialData.hpp"

#include "Trinity/Asset/CookedText.hpp"
#include "Trinity/Project/Project.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
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

        // Every key but the textures', which follow in slot order
        constexpr std::array<std::string_view, 10> c_FactorKeys{ "BaseColorFactor", "MetallicFactor", "RoughnessFactor", "EmissiveFactor", "EmissiveStrength", "NormalScale", "OcclusionStrength", "AlphaMode", "AlphaCutoff", "DoubleSided" };

        bool IsMaterialKey(std::string_view name)
        {
            return std::ranges::find(c_FactorKeys, name) != c_FactorKeys.end() || std::ranges::any_of(c_TextureSlots, [name](const TextureSlot& slot) { return slot.Texture == name || slot.TexCoord == name; });
        }

        // Every factor, and each texture slot when writeEmpty is set or it holds a texture
        void WriteFields(ComponentWriter& writer, const MaterialData& material, bool writeEmpty)
        {
            writer.Write("BaseColorFactor", material.BaseColorFactor);
            writer.Write("MetallicFactor", material.MetallicFactor);
            writer.Write("RoughnessFactor", material.RoughnessFactor);
            writer.Write("EmissiveFactor", material.EmissiveFactor);
            writer.Write("EmissiveStrength", material.EmissiveStrength);
            writer.Write("NormalScale", material.NormalScale);
            writer.Write("OcclusionStrength", material.OcclusionStrength);
            writer.Write("AlphaMode", ToString(material.AlphaMode));
            writer.Write("AlphaCutoff", material.AlphaCutoff);
            writer.Write("DoubleSided", material.DoubleSided);
            for (const TextureSlot& it_Slot : c_TextureSlots)
            {
                const MaterialTexture& l_Texture = material.*it_Slot.Member;
                if (writeEmpty || l_Texture.Texture.IsValid())
                {
                    writer.Write(it_Slot.Texture, l_Texture.Texture);
                    writer.Write(it_Slot.TexCoord, l_Texture.TexCoord);
                }
            }
        }

        // Onto the material as it is, so a key the map lacks keeps its value. Gives back the key that did not read, or nothing
        std::string ReadFields(const YAML::Node& map, MaterialData& material)
        {
            std::string l_Malformed;
            CookedText::ReadFloats(map, "BaseColorFactor", std::span(&material.BaseColorFactor.x, 4), l_Malformed);
            CookedText::ReadNumber(map, "MetallicFactor", material.MetallicFactor, l_Malformed);
            CookedText::ReadNumber(map, "RoughnessFactor", material.RoughnessFactor, l_Malformed);
            CookedText::ReadFloats(map, "EmissiveFactor", std::span(&material.EmissiveFactor.x, 3), l_Malformed);
            CookedText::ReadNumber(map, "EmissiveStrength", material.EmissiveStrength, l_Malformed);
            CookedText::ReadNumber(map, "NormalScale", material.NormalScale, l_Malformed);
            CookedText::ReadNumber(map, "OcclusionStrength", material.OcclusionStrength, l_Malformed);
            CookedText::ReadNumber(map, "AlphaCutoff", material.AlphaCutoff, l_Malformed);
            CookedText::ReadBool(map, "DoubleSided", material.DoubleSided, l_Malformed);

            std::string l_AlphaMode(ToString(material.AlphaMode));
            CookedText::ReadString(map, "AlphaMode", l_AlphaMode, l_Malformed);
            if (const std::optional<MaterialAlphaMode> l_Mode = ParseMaterialAlphaMode(l_AlphaMode))
            {
                material.AlphaMode = *l_Mode;
            }
            else
            {
                l_Malformed = "AlphaMode";
            }

            for (const TextureSlot& it_Slot : c_TextureSlots)
            {
                MaterialTexture& l_Texture = material.*it_Slot.Member;
                CookedText::ReadUUID(map, it_Slot.Texture, l_Texture.Texture, l_Malformed);
                CookedText::ReadNumber(map, it_Slot.TexCoord, l_Texture.TexCoord, l_Malformed);
            }

            return l_Malformed;
        }
    }

    std::string GetCookedMaterialPath(UUID id)
    {
        return std::format("{}/Materials/{}.trmat", Project::c_CacheMount, id);
    }

    std::string WriteMaterialData(const MaterialData& material)
    {
        std::string l_Text = std::format("Format: {}", MaterialData::c_FormatVersion);
        ComponentWriter l_Writer(l_Text, 0);
        WriteFields(l_Writer, material, false);

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

        MaterialData l_Material;
        const std::string l_Malformed = ReadFields(*l_Root, l_Material);
        if (!l_Malformed.empty())
        {
            return Unexpected{ std::format("{} is malformed", l_Malformed) };
        }

        return l_Material;
    }

    // Each key written alone, then read back without its key
    std::vector<MaterialField> GetMaterialFields(const MaterialData& material)
    {
        std::string l_Text;
        ComponentWriter l_Writer(l_Text, 0);
        WriteFields(l_Writer, material, true);

        std::vector<MaterialField> l_Fields;
        std::size_t l_Start = 0;
        while (l_Start < l_Text.size())
        {
            const std::size_t l_Line = l_Start + 1;
            const std::size_t l_End = std::min(l_Text.find('\n', l_Line), l_Text.size());
            const std::string_view l_Entry = std::string_view(l_Text).substr(l_Line, l_End - l_Line);
            const std::size_t l_Colon = l_Entry.find(": ");
            l_Fields.push_back({ std::string(l_Entry.substr(0, l_Colon)), std::string(l_Entry.substr(l_Colon + 2)) });
            l_Start = l_End;
        }

        return l_Fields;
    }

    // As one file of only those keys, read over the material, so the values read exactly as a file's do
    Expected<MaterialData, std::string> ApplyMaterialFields(MaterialData material, std::span<const MaterialField> fields)
    {
        std::string l_Text = std::format("Format: {}\n", MaterialData::c_FormatVersion);
        for (const MaterialField& it_Field : fields)
        {
            if (!IsMaterialKey(it_Field.Name) || it_Field.Value.find('\n') != std::string::npos || std::ranges::count(fields, it_Field.Name, &MaterialField::Name) != 1)
            {
                return Unexpected{ std::format("{} is not a material key, or is given more than once", it_Field.Name) };
            }

            l_Text += std::format("{}: {}\n", it_Field.Name, it_Field.Value);
        }

        const Expected<YAML::Node, std::string> l_Root = CookedText::LoadRoot(l_Text, "a material", MaterialData::c_FormatVersion);
        if (!l_Root)
        {
            return Unexpected{ l_Root.GetError() };
        }

        const std::string l_Malformed = ReadFields(*l_Root, material);
        if (!l_Malformed.empty())
        {
            return Unexpected{ std::format("{} is malformed", l_Malformed) };
        }

        return material;
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