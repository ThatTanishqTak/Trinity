#include "Trinity/Asset/ModelData.hpp"

#include "Trinity/Asset/CookedText.hpp"
#include "Trinity/Project/Project.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"

#include <yaml-cpp/yaml.h>

#include <format>
#include <span>

namespace Trinity
{
    std::string GetCookedModelPath(UUID id)
    {
        return std::format("{}/Models/{}.trmodel", Project::c_CacheMount, id);
    }

    // Each node a map in a block sequence. A node without a mesh leaves out the mesh and its materials
    std::string WriteModelData(const ModelData& model)
    {
        std::string l_Text = std::format("Format: {}\nNodes:{}", ModelData::c_FormatVersion, model.Nodes.empty() ? " []" : "");
        for (const ModelNode& it_Node : model.Nodes)
        {
            l_Text += "\n  -";
            ComponentWriter l_Writer(l_Text, 4);
            l_Writer.Write("Name", it_Node.Name);
            l_Writer.Write("Parent", it_Node.Parent);
            l_Writer.Write("Translation", it_Node.Translation);
            l_Writer.Write("Rotation", it_Node.Rotation);
            l_Writer.Write("Scale", it_Node.Scale);
            if (it_Node.Mesh.IsValid())
            {
                l_Writer.Write("Mesh", it_Node.Mesh);
                l_Writer.Write("Materials", std::span<const UUID>(it_Node.Materials));
            }
        }

        return l_Text + "\n";
    }

    // Refuses a node whose parent is not an earlier node, so the hierarchy has no cycles
    Expected<ModelData, std::string> ParseModelData(std::string_view text)
    {
        const Expected<YAML::Node, std::string> l_Root = CookedText::LoadRoot(text, "a model", ModelData::c_FormatVersion);
        if (!l_Root)
        {
            return Unexpected{ l_Root.GetError() };
        }

        const YAML::Node& l_Map = *l_Root;
        const YAML::Node l_Nodes = l_Map["Nodes"];
        if (!l_Nodes || !l_Nodes.IsSequence())
        {
            return Unexpected{ std::string("Nodes is missing or not a list") };
        }

        ModelData l_Model;
        l_Model.Nodes.reserve(l_Nodes.size());
        for (const YAML::Node it_Node : l_Nodes)
        {
            if (!it_Node.IsMap())
            {
                return Unexpected{ std::format("node {} is not a map", l_Model.Nodes.size()) };
            }

            ModelNode l_Node;
            std::string l_Malformed;
            CookedText::ReadString(it_Node, "Name", l_Node.Name, l_Malformed);
            CookedText::ReadNumber(it_Node, "Parent", l_Node.Parent, l_Malformed);
            CookedText::ReadFloats(it_Node, "Translation", std::span(&l_Node.Translation.x, 3), l_Malformed);
            CookedText::ReadFloats(it_Node, "Rotation", std::span(&l_Node.Rotation.x, 4), l_Malformed);
            CookedText::ReadFloats(it_Node, "Scale", std::span(&l_Node.Scale.x, 3), l_Malformed);
            CookedText::ReadUUID(it_Node, "Mesh", l_Node.Mesh, l_Malformed);

            if (const YAML::Node l_Materials = it_Node["Materials"])
            {
                for (std::size_t it_Index = 0; l_Materials.IsSequence() && it_Index < l_Materials.size(); ++it_Index)
                {
                    const std::optional<UUID> l_Material = CookedText::ParseUUID(l_Materials[it_Index]);
                    l_Malformed = l_Material ? l_Malformed : std::string("Materials");
                    l_Node.Materials.push_back(l_Material.value_or(UUID()));
                }

                l_Malformed = l_Materials.IsSequence() ? l_Malformed : std::string("Materials");
            }

            const std::int32_t l_Index = static_cast<std::int32_t>(l_Model.Nodes.size());
            if (l_Malformed.empty() && (l_Node.Parent < ModelNode::c_NoParent || l_Node.Parent >= l_Index))
            {
                l_Malformed = "Parent";
            }

            if (!l_Malformed.empty())
            {
                return Unexpected{ std::format("node {} has a malformed {}", l_Index, l_Malformed) };
            }

            l_Model.Nodes.push_back(std::move(l_Node));
        }

        return l_Model;
    }
}