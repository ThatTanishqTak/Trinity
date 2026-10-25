#include "EditorCommands.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <utility>
#include <vector>

namespace
{
    Trinity::UUID GetID(Trinity::Entity entity)
    {
        return entity ? entity.GetUUID() : Trinity::UUID();
    }

    // A parent that exists when one is named, and a sibling to go before that is that parent's child
    bool IsPlaceValid(Trinity::Scene& scene, Trinity::UUID parent, Trinity::UUID before)
    {
        const Trinity::Entity l_Parent = scene.FindEntityByUUID(parent);
        const Trinity::Entity l_Before = scene.FindEntityByUUID(before);
        if (parent.IsValid() && !l_Parent)
        {
            return false;
        }

        return !before.IsValid() || (l_Before && l_Before.GetParent().GetHandle() == l_Parent.GetHandle());
    }

    // Before the sibling when there is one, otherwise last under the parent or among the roots. The transform is left alone, since the commands set it exactly
    bool Place(Trinity::Scene& scene, Trinity::Entity entity, Trinity::UUID parent, Trinity::UUID before)
    {
        if (const Trinity::Entity l_Before = scene.FindEntityByUUID(before))
        {
            return scene.MoveBefore(entity, l_Before, false);
        }

        return scene.SetParent(entity, scene.FindEntityByUUID(parent), false);
    }

    // The defaults of the box, sphere, capsule and cylinder colliders fit the shapes of the same name exactly, and the flat shapes get a box 10 cm thick behind their face, so what lands on one rests on its surface
    void AddFittingCollider(Trinity::Entity entity, Trinity::BuiltInMesh mesh)
    {
        switch (mesh)
        {
            case Trinity::BuiltInMesh::Cube:
            {
                entity.Add<Trinity::BoxColliderComponent>();

                break;
            }
            case Trinity::BuiltInMesh::Sphere:
            {
                entity.Add<Trinity::SphereColliderComponent>();

                break;
            }
            case Trinity::BuiltInMesh::Capsule:
            {
                entity.Add<Trinity::CapsuleColliderComponent>();

                break;
            }
            case Trinity::BuiltInMesh::Cylinder:
            {
                entity.Add<Trinity::CylinderColliderComponent>();

                break;
            }
            case Trinity::BuiltInMesh::Plane:
            {
                Trinity::BoxColliderComponent& l_Box = entity.Add<Trinity::BoxColliderComponent>();
                l_Box.HalfExtents = glm::vec3(5.0f, 0.05f, 5.0f);
                l_Box.Offset = glm::vec3(0.0f, -0.05f, 0.0f);

                break;
            }
            case Trinity::BuiltInMesh::Quad:
            {
                Trinity::BoxColliderComponent& l_Box = entity.Add<Trinity::BoxColliderComponent>();
                l_Box.HalfExtents = glm::vec3(0.5f, 0.5f, 0.05f);
                l_Box.Offset = glm::vec3(0.0f, 0.0f, -0.05f);

                break;
            }
        }
    }

    // The text came from this scene in the same state, so this only fails when the history no longer matches the scene
    bool Restore(Trinity::Scene& scene, std::string_view text, Trinity::UUID parent, Trinity::UUID before, std::string_view label)
    {
        const Trinity::Expected<Trinity::Entity, std::string> l_Restored = Trinity::SceneSerializer::LoadEntityFromText(scene, text, scene.FindEntityByUUID(parent), scene.FindEntityByUUID(before));
        if (!l_Restored)
        {
            TR_ERROR("Forge: {} could not bring its entities back: {}", label, l_Restored.GetError());

            return false;
        }

        return true;
    }

    // An instance as its root's text where it sits, which RestoreInstances makes again there
    Trinity::SceneSerializer::PrefabInstanceText SaveInstance(Trinity::Scene& scene, Trinity::Entity root)
    {
        return { .Root = root.GetUUID(), .Parent = GetID(root.GetParent()), .Before = GetID(root.GetNextSibling()), .Text = Trinity::SceneSerializer::SaveEntityToText(scene, root) };
    }

    // As Restore, for instances made again from their text in place of what they are now
    bool RestoreInstances(Trinity::Scene& scene, std::span<const Trinity::SceneSerializer::PrefabInstanceText> instances, std::string_view label)
    {
        if (const Trinity::Expected<void, std::string> l_Restored = Trinity::SceneSerializer::RestorePrefabInstances(scene, instances); !l_Restored)
        {
            TR_ERROR("Forge: {} could not bring its entities back: {}", label, l_Restored.GetError());

            return false;
        }

        return true;
    }

    std::size_t GetInstancesMemorySize(const std::vector<Trinity::SceneSerializer::PrefabInstanceText>& instances)
    {
        std::size_t l_Size = instances.capacity() * sizeof(Trinity::SceneSerializer::PrefabInstanceText);
        for (const Trinity::SceneSerializer::PrefabInstanceText& it_Instance : instances)
        {
            l_Size += it_Instance.Text.capacity();
        }

        return l_Size;
    }

    std::size_t GetTextsMemorySize(const std::vector<Trinity::SceneSerializer::PrefabText>& texts)
    {
        std::size_t l_Size = texts.capacity() * sizeof(Trinity::SceneSerializer::PrefabText);
        for (const Trinity::SceneSerializer::PrefabText& it_Text : texts)
        {
            l_Size += it_Text.Text.capacity();
        }

        return l_Size;
    }

    std::size_t GetLinksMemorySize(const std::vector<PrefabLink>& links)
    {
        std::size_t l_Size = links.capacity() * sizeof(PrefabLink);
        for (const PrefabLink& it_Link : links)
        {
            l_Size += it_Link.Instance ? it_Link.Instance->Kept.capacity() : 0;
        }

        return l_Size;
    }

    // Each entity's root component as the link has it, replacing the one it had
    void SetLinks(Trinity::Scene& scene, std::span<const PrefabLink> links)
    {
        for (const PrefabLink& it_Link : links)
        {
            Trinity::Entity l_Entity = scene.FindEntityByUUID(it_Link.Entity);
            if (l_Entity.Has<Trinity::PrefabInstanceComponent>())
            {
                l_Entity.Remove<Trinity::PrefabInstanceComponent>();
            }

            if (it_Link.Instance)
            {
                l_Entity.Add<Trinity::PrefabInstanceComponent>(*it_Link.Instance);
            }
        }
    }

    // The first in the subtree, the entity itself or under it, that stands in for an instance whose prefab is missing
    Trinity::Entity FindMissingPrefab(Trinity::Entity entity)
    {
        Trinity::Scene& l_Scene = *entity.GetScene();
        for (Trinity::Entity it_Entity = entity; it_Entity; it_Entity = l_Scene.GetNextInSubtree(it_Entity, entity))
        {
            if (IsMissingPrefab(it_Entity))
            {
                return it_Entity;
            }
        }

        return {};
    }

    std::size_t GetOverridesMemorySize(const std::vector<Trinity::SceneSerializer::PrefabOverride>& overrides)
    {
        std::size_t l_Size = overrides.capacity() * sizeof(Trinity::SceneSerializer::PrefabOverride);
        for (const Trinity::SceneSerializer::PrefabOverride& it_Override : overrides)
        {
            l_Size += it_Override.Component.capacity() + it_Override.Path.capacity();
        }

        return l_Size;
    }

    // A string's text, an input action's path, or a list's elements with what each holds, the parts of a value held outside it
    std::size_t GetScriptValueMemorySize(const Trinity::ScriptFieldValue& value)
    {
        if (value.GetType() == Trinity::ScriptFieldType::List)
        {
            const auto& l_Elements = value.Get<Trinity::ScriptList>().Elements;
            std::size_t l_Size = l_Elements.capacity() * sizeof(Trinity::ScriptFieldValue);
            for (const Trinity::ScriptFieldValue& it_Element : l_Elements)
            {
                l_Size += GetScriptValueMemorySize(it_Element);
            }

            return l_Size;
        }

        return value.GetType() == Trinity::ScriptFieldType::String || value.GetType() == Trinity::ScriptFieldType::InputAction ? value.Get<Trinity::ScriptString>().capacity() : 0;
    }

    // A rename's fields at their values after it, or back at their values before it, where one that was its class's default goes again
    void SetInputActionFields(Trinity::Scene& scene, const std::vector<InputActionFieldRewrite>& rewrites, bool undo)
    {
        for (const InputActionFieldRewrite& it_Rewrite : rewrites)
        {
            Trinity::Entity l_Entity = scene.FindEntityByUUID(it_Rewrite.Entity);
            Trinity::ScriptComponent* l_Scripts = l_Entity && l_Entity.Has<Trinity::ScriptComponent>() ? &l_Entity.Get<Trinity::ScriptComponent>() : nullptr;
            if (l_Scripts == nullptr || it_Rewrite.Script >= l_Scripts->Scripts.size())
            {
                continue;
            }

            Trinity::ScriptEntry& l_Entry = l_Scripts->Scripts[it_Rewrite.Script];
            if (undo && it_Rewrite.Added)
            {
                std::erase_if(l_Entry.Fields, [&it_Rewrite](const Trinity::ScriptEntry::Field& field) { return std::string_view(field.Name) == it_Rewrite.Field; });
            }
            else
            {
                l_Entry.SetField(it_Rewrite.Field, undo ? it_Rewrite.Before : it_Rewrite.After);
            }
        }
    }

    // What an entry holds beyond its own size
    std::size_t GetScriptEntryMemorySize(const Trinity::ScriptEntry& entry)
    {
        std::size_t l_Size = entry.ClassName.capacity() + entry.Fields.capacity() * sizeof(Trinity::ScriptEntry::Field);
        for (const Trinity::ScriptEntry::Field& it_Field : entry.Fields)
        {
            l_Size += it_Field.Name.capacity() + GetScriptValueMemorySize(it_Field.Value);
        }

        return l_Size;
    }
}

std::string GetEntityLabel(Trinity::Entity entity)
{
    if (entity.Has<Trinity::TagComponent>() && !entity.Get<Trinity::TagComponent>().Tag.empty())
    {
        return std::string(std::string_view(entity.Get<Trinity::TagComponent>().Tag));
    }

    return std::format("entity {}", entity.GetUUID());
}

// The part after the last dot, so Trinity.SpriteRenderer reads as SpriteRenderer
std::string_view GetComponentLabel(std::string_view name)
{
    const std::size_t l_Dot = name.rfind('.');

    return l_Dot == std::string_view::npos ? name : name.substr(l_Dot + 1);
}

std::string_view GetScriptLanguageLabel(Trinity::ScriptLanguage language)
{
    return language == Trinity::ScriptLanguage::Cpp ? "C++" : "C#";
}

std::string_view GetLightLabel(Trinity::LightType type)
{
    constexpr std::array<std::string_view, 3> c_Labels{ "Directional Light", "Point Light", "Spot Light" };

    return c_Labels[static_cast<std::size_t>(type)];
}

std::string GetScriptLabel(const Trinity::ScriptEntry& entry)
{
    const std::string_view l_Name(entry.ClassName);
    const std::size_t l_Separator = l_Name.find_last_of(".:");

    return std::format("{} ({})", l_Separator == std::string_view::npos ? l_Name : l_Name.substr(l_Separator + 1), GetScriptLanguageLabel(entry.Language));
}

void ForEachSlotReference(Trinity::Entity entity, const std::function<void(const SlotReference&)>& visit)
{
    if (entity.Has<Trinity::PrefabInstanceComponent>())
    {
        visit({ .Component = Trinity::PrefabInstanceComponent::c_TypeName, .Script = nullptr, .Slot = "Prefab", .Index = std::nullopt, .AssetType = Trinity::PrefabAsset::c_AssetType, .Value = entity.Get<Trinity::PrefabInstanceComponent>().Prefab, .IsEntity = false });
    }

    if (entity.Has<Trinity::SpriteRendererComponent>())
    {
        visit({ .Component = Trinity::SpriteRendererComponent::c_TypeName, .Script = nullptr, .Slot = "Texture", .Index = std::nullopt, .AssetType = Trinity::TextureAsset::c_AssetType, .Value = entity.Get<Trinity::SpriteRendererComponent>().Texture, .IsEntity = false });
    }

    if (entity.Has<Trinity::MeshRendererComponent>())
    {
        const Trinity::MeshRendererComponent& l_Renderer = entity.Get<Trinity::MeshRendererComponent>();
        visit({ .Component = Trinity::MeshRendererComponent::c_TypeName, .Script = nullptr, .Slot = "Mesh", .Index = std::nullopt, .AssetType = Trinity::MeshAsset::c_AssetType, .Value = l_Renderer.Mesh, .IsEntity = false });
        for (std::size_t it_Slot = 0; it_Slot < l_Renderer.Materials.size(); ++it_Slot)
        {
            visit({ .Component = Trinity::MeshRendererComponent::c_TypeName, .Script = nullptr, .Slot = "Material", .Index = it_Slot, .AssetType = Trinity::MaterialAsset::c_AssetType, .Value = l_Renderer.Materials[it_Slot], .IsEntity = false });
        }
    }

    if (entity.Has<Trinity::EnvironmentComponent>())
    {
        visit({ .Component = Trinity::EnvironmentComponent::c_TypeName, .Script = nullptr, .Slot = "Environment", .Index = std::nullopt, .AssetType = Trinity::EnvironmentAsset::c_AssetType, .Value = entity.Get<Trinity::EnvironmentComponent>().Environment, .IsEntity = false });
    }

    if (entity.Has<Trinity::ConvexHullColliderComponent>())
    {
        visit({ .Component = Trinity::ConvexHullColliderComponent::c_TypeName, .Script = nullptr, .Slot = "Shape", .Index = std::nullopt, .AssetType = Trinity::ConvexHullAsset::c_AssetType, .Value = entity.Get<Trinity::ConvexHullColliderComponent>().Shape, .IsEntity = false });
    }

    if (entity.Has<Trinity::MeshColliderComponent>())
    {
        visit({ .Component = Trinity::MeshColliderComponent::c_TypeName, .Script = nullptr, .Slot = "Shape", .Index = std::nullopt, .AssetType = Trinity::CollisionMeshAsset::c_AssetType, .Value = entity.Get<Trinity::MeshColliderComponent>().Shape, .IsEntity = false });
    }

    if (entity.Has<Trinity::JointComponent>())
    {
        visit({ .Component = Trinity::JointComponent::c_TypeName, .Script = nullptr, .Slot = "Connected Body", .Index = std::nullopt, .AssetType = {}, .Value = entity.Get<Trinity::JointComponent>().ConnectedBody, .IsEntity = true });
    }

    if (entity.Has<Trinity::Joint2DComponent>())
    {
        visit({ .Component = Trinity::Joint2DComponent::c_TypeName, .Script = nullptr, .Slot = "Connected Body", .Index = std::nullopt, .AssetType = {}, .Value = entity.Get<Trinity::Joint2DComponent>().ConnectedBody, .IsEntity = true });
    }

    if (!entity.Has<Trinity::ScriptComponent>())
    {
        return;
    }

    for (const Trinity::ScriptEntry& it_Script : entity.Get<Trinity::ScriptComponent>().Scripts)
    {
        const Trinity::ScriptClassDescription* l_Class = Trinity::ScriptClassRegistry::Find(it_Script.Language, it_Script.ClassName);
        for (const Trinity::ScriptEntry::Field& it_Field : it_Script.Fields)
        {
            const Trinity::ScriptFieldType l_Type = it_Field.Value.GetElementType();
            if (l_Type != Trinity::ScriptFieldType::Entity && l_Type != Trinity::ScriptFieldType::Asset)
            {
                continue;
            }

            const Trinity::ScriptFieldDescription* l_Field = l_Class != nullptr ? l_Class->FindField(it_Field.Name) : nullptr;
            const std::string_view l_AssetType = l_Type == Trinity::ScriptFieldType::Asset && l_Field != nullptr ? std::string_view(l_Field->AssetType) : std::string_view();
            const bool l_IsList = it_Field.Value.GetType() == Trinity::ScriptFieldType::List;
            std::size_t l_Index = 0;
            it_Field.Value.ForEachValue<Trinity::UUID>(l_Type, [&](Trinity::UUID value)
            {
                visit({ .Component = {}, .Script = &it_Script, .Slot = it_Field.Name, .Index = l_IsList ? std::optional<std::size_t>(l_Index++) : std::nullopt, .AssetType = l_AssetType, .Value = value, .IsEntity = l_Type == Trinity::ScriptFieldType::Entity });
            });
        }
    }
}

std::optional<std::pair<std::size_t, std::size_t>> FindInputAction(const Trinity::InputActionsData& data, std::string_view path)
{
    const std::size_t l_Separator = path.find(Trinity::InputActionsData::c_PathSeparator);
    const std::string_view l_Map = l_Separator != std::string_view::npos ? path.substr(0, l_Separator) : std::string_view();
    const std::string_view l_Action = l_Separator != std::string_view::npos ? path.substr(l_Separator + 1) : path;
    std::optional<std::pair<std::size_t, std::size_t>> l_Found;
    for (std::size_t it_Map = 0; it_Map < data.Maps.size(); ++it_Map)
    {
        if (l_Separator != std::string_view::npos && std::string_view(data.Maps[it_Map].Name) != l_Map)
        {
            continue;
        }

        const auto& l_Actions = data.Maps[it_Map].Actions;
        const auto a_Action = std::ranges::find_if(l_Actions, [l_Action](const Trinity::InputActionDefinition& action) { return std::string_view(action.Name) == l_Action; });
        if (a_Action == l_Actions.end())
        {
            continue;
        }

        if (l_Found)
        {
            return std::nullopt;
        }

        l_Found = std::pair(it_Map, static_cast<std::size_t>(a_Action - l_Actions.begin()));
    }

    return l_Found;
}

std::vector<InputActionFieldRewrite> FindInputActionRewrites(Trinity::Scene& scene, const Trinity::InputActionsData& before, const Trinity::InputActionsData& after)
{
    std::vector<InputActionFieldRewrite> l_Rewrites;
    const auto a_Rewrite = [&before, &after](const Trinity::ScriptFieldValue& value) -> std::optional<Trinity::ScriptFieldValue>
    {
        if (value.GetElementType() != Trinity::ScriptFieldType::InputAction)
        {
            return std::nullopt;
        }

        Trinity::ScriptFieldValue l_After = value;
        bool l_Changed = false;
        l_After.ForEachValue<Trinity::ScriptString>(Trinity::ScriptFieldType::InputAction, [&before, &after, &l_Changed](Trinity::ScriptString& path)
        {
            const std::optional<std::pair<std::size_t, std::size_t>> l_Before = path.empty() ? std::nullopt : FindInputAction(before, std::string_view(path));
            if (!l_Before || FindInputAction(after, std::string_view(path)) == l_Before)
            {
                return;
            }

            const Trinity::InputActionMap& l_Map = after.Maps[l_Before->first];
            path = std::format("{}{}{}", std::string_view(l_Map.Name), Trinity::InputActionsData::c_PathSeparator, std::string_view(l_Map.Actions[l_Before->second].Name));
            l_Changed = true;
        });

        return l_Changed ? std::optional(std::move(l_After)) : std::nullopt;
    };

    for (const auto [it_Handle, it_Scripts] : scene.GetRegistry().view<Trinity::ScriptComponent>().each())
    {
        const Trinity::UUID l_Entity = Trinity::Entity(it_Handle, &scene).GetUUID();
        for (std::size_t it_Script = 0; it_Script < it_Scripts.Scripts.size(); ++it_Script)
        {
            const Trinity::ScriptEntry& l_Entry = it_Scripts.Scripts[it_Script];
            for (const Trinity::ScriptEntry::Field& it_Field : l_Entry.Fields)
            {
                if (std::optional<Trinity::ScriptFieldValue> l_After = a_Rewrite(it_Field.Value))
                {
                    l_Rewrites.push_back({ l_Entity, it_Script, std::string(std::string_view(it_Field.Name)), it_Field.Value, std::move(*l_After), false });
                }
            }

            const Trinity::ScriptClassDescription* l_Class = Trinity::ScriptClassRegistry::Find(l_Entry.Language, l_Entry.ClassName);
            if (l_Class == nullptr)
            {
                continue;
            }

            for (const Trinity::ScriptFieldDescription& it_Field : l_Class->Fields)
            {
                if (l_Entry.FindField(it_Field.Name) != nullptr)
                {
                    continue;
                }

                if (std::optional<Trinity::ScriptFieldValue> l_After = a_Rewrite(it_Field.Default))
                {
                    l_Rewrites.push_back({ l_Entity, it_Script, std::string(std::string_view(it_Field.Name)), it_Field.Default, std::move(*l_After), true });
                }
            }
        }
    }

    return l_Rewrites;
}

std::size_t GetScriptMemorySize(const Trinity::ScriptComponent& component)
{
    std::size_t l_Size = component.Scripts.capacity() * sizeof(Trinity::ScriptEntry);
    for (const Trinity::ScriptEntry& it_Entry : component.Scripts)
    {
        l_Size += GetScriptEntryMemorySize(it_Entry);
    }

    return l_Size;
}

bool IsMissingPrefab(Trinity::Entity entity)
{
    return entity.Has<Trinity::PrefabInstanceComponent>() && entity.Get<Trinity::PrefabInstanceComponent>().IsMissing();
}

bool IsPrefabPart(Trinity::Entity entity)
{
    const Trinity::Entity l_Root = Trinity::Prefab::FindInstanceRoot(entity);

    return l_Root && l_Root != entity;
}

// The innermost level with a root, and of the levels sharing that root the outermost, which is the variant's. An instance whose prefab is missing has no levels, so its root is the nearest
NearestInstance FindNearestInstance(Trinity::Entity entity)
{
    const std::vector<Trinity::Prefab::Level> l_Levels = Trinity::Prefab::GetLevels(entity);
    const auto a_Inner = std::ranges::find_if(l_Levels.rbegin(), l_Levels.rend(), [](const Trinity::Prefab::Level& level) { return static_cast<bool>(level.Root); });
    if (a_Inner == l_Levels.rend())
    {
        const Trinity::Entity l_Root = Trinity::Prefab::FindInstanceRoot(entity);

        return { .Root = l_Root, .Prefab = l_Root ? l_Root.Get<Trinity::PrefabInstanceComponent>().Prefab : Trinity::UUID() };
    }

    const auto a_Outer = std::ranges::find(l_Levels, a_Inner->Root, &Trinity::Prefab::Level::Root);

    return { .Root = a_Outer->Root, .Prefab = a_Outer->Prefab };
}

bool IsDefaultOverride(Trinity::Entity root, const Trinity::SceneSerializer::PrefabOverride& change)
{
    return change.Type == Trinity::SceneSerializer::PrefabOverride::Kind::Value && change.Entity == root.GetUUID() && change.Component == Trinity::TransformComponent::c_TypeName && (change.Path == "Position" || change.Path == "Rotation");
}

Trinity::UUID FindAddedPlace(Trinity::Scene& scene, Trinity::UUID before)
{
    Trinity::Entity l_Before = scene.FindEntityByUUID(before);
    while (l_Before && IsPrefabPart(l_Before))
    {
        l_Before = l_Before.GetNextSibling();
    }

    return GetID(l_Before);
}

std::optional<std::string> FindChildProblem(Trinity::Scene& scene, Trinity::UUID parent)
{
    const Trinity::Entity l_Parent = scene.FindEntityByUUID(parent);
    if (!l_Parent || !IsMissingPrefab(l_Parent))
    {
        return std::nullopt;
    }

    return std::format("{} stands in for a prefab instance whose prefab is missing, so nothing goes under it until the prefab is back", GetEntityLabel(l_Parent));
}

std::optional<std::string> FindMoveProblem(Trinity::Scene& scene, Trinity::UUID entity, Trinity::UUID parent)
{
    const Trinity::Entity l_Entity = scene.FindEntityByUUID(entity);
    if (l_Entity && IsPrefabPart(l_Entity))
    {
        return std::format("{} is part of the prefab instance {}, whose prefab's entities keep the places the prefab gives them, as in Unity. Entities added to the instance can move", GetEntityLabel(l_Entity), GetEntityLabel(Trinity::Prefab::FindInstanceRoot(l_Entity)));
    }

    return FindChildProblem(scene, parent);
}

std::optional<std::string> FindPrefabProblem(Trinity::Entity entity)
{
    if (IsPrefabPart(entity))
    {
        return std::format("{} is part of the prefab instance {}, whose prefab's entities are that prefab's, so a prefab is made from the instance's root, or from what is added to it", GetEntityLabel(entity), GetEntityLabel(Trinity::Prefab::FindInstanceRoot(entity)));
    }

    const Trinity::Entity l_Missing = FindMissingPrefab(entity);
    if (!l_Missing)
    {
        return std::nullopt;
    }

    if (l_Missing == entity)
    {
        return std::format("{} stands in for a prefab instance whose prefab is missing, so it becomes no prefab until the prefab is back", GetEntityLabel(entity));
    }

    return std::format("{} holds {}, which stands in for a prefab instance whose prefab is missing, so it becomes no prefab until the prefab is back", GetEntityLabel(entity), GetEntityLabel(l_Missing));
}

std::optional<std::string> FindCopyProblem(Trinity::Entity entity)
{
    const Trinity::Entity l_Missing = FindMissingPrefab(entity);
    if (!l_Missing)
    {
        return std::nullopt;
    }

    if (l_Missing == entity)
    {
        return std::format("{} stands in for a prefab instance whose prefab is missing, which only it can keep, so it is not copied until the prefab is back", GetEntityLabel(entity));
    }

    return std::format("{} holds {}, which stands in for a prefab instance whose prefab is missing and only it can keep, so it is not copied until the prefab is back", GetEntityLabel(entity), GetEntityLabel(l_Missing));
}

// Never hidden, as a name starting with a dot would be
std::string GetPrefabFileName(std::string_view name)
{
    std::string l_Name(name);
    std::ranges::replace_if(l_Name, [](char character) { return std::string_view("/\\:*?\"<>|").contains(character) || static_cast<unsigned char>(character) < 0x20; }, '_');
    const std::size_t l_Start = l_Name.find_first_not_of(". ");
    if (l_Start == std::string::npos)
    {
        return "Prefab";
    }

    return l_Name.substr(l_Start, l_Name.find_last_not_of(' ') - l_Start + 1);
}

CreateEntityCommand::CreateEntityCommand(std::string name, Trinity::UUID parent, Trinity::UUID before, glm::vec3 position) : m_Name(std::move(name)), m_Parent(parent), m_Before(before), m_Position(position)
{

}

// The first run creates the entity at the position in the parent's space and keeps it as text, so a redo brings back the same UUID
bool CreateEntityCommand::Execute(Trinity::Scene& scene)
{
    if (!m_Text.empty())
    {
        return Restore(scene, m_Text, m_Parent, m_Before, m_Label);
    }

    if (!IsPlaceValid(scene, m_Parent, m_Before))
    {
        return false;
    }

    Trinity::Entity l_Entity = scene.CreateEntity(m_Name, scene.FindEntityByUUID(m_Parent));
    if (const Trinity::Entity l_Before = scene.FindEntityByUUID(m_Before))
    {
        scene.MoveBefore(l_Entity, l_Before, false);
    }

    l_Entity.Get<Trinity::TransformComponent>().Position = m_Position;
    m_Entity = l_Entity.GetUUID();
    m_Text = Trinity::SceneSerializer::SaveEntityToText(scene, l_Entity);
    m_Label = std::format("Create {}", GetEntityLabel(l_Entity));

    return true;
}

void CreateEntityCommand::Undo(Trinity::Scene& scene)
{
    scene.DestroyEntity(scene.FindEntityByUUID(m_Entity));
}

std::size_t CreateEntityCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Name.capacity() + m_Text.capacity() + m_Label.capacity();
}

CreateSpriteCommand::CreateSpriteCommand(std::string name, Trinity::UUID texture, Trinity::UUID parent, glm::vec3 position, glm::vec2 size) : m_Name(std::move(name)), m_Texture(texture), m_Parent(parent), m_Position(position), m_Size(size)
{

}

// A root entity with its transform, SpriteRenderer and texture set in one step, so a single undo takes it away. Kept as text after the first run, as CreateEntityCommand does
bool CreateSpriteCommand::Execute(Trinity::Scene& scene)
{
    if (!m_Text.empty())
    {
        return Restore(scene, m_Text, m_Parent, {}, m_Label);
    }

    if (!IsPlaceValid(scene, m_Parent, {}))
    {
        return false;
    }

    Trinity::Entity l_Entity = scene.CreateEntity(m_Name, scene.FindEntityByUUID(m_Parent));
    Trinity::TransformComponent& l_Transform = l_Entity.Get<Trinity::TransformComponent>();
    l_Transform.Position = m_Position;
    l_Transform.Scale = glm::vec3(m_Size, 1.0f);
    l_Entity.Add<Trinity::SpriteRendererComponent>().Texture = m_Texture;

    m_Entity = l_Entity.GetUUID();
    m_Text = Trinity::SceneSerializer::SaveEntityToText(scene, l_Entity);
    m_Label = std::format("Create sprite {}", GetEntityLabel(l_Entity));

    return true;
}

void CreateSpriteCommand::Undo(Trinity::Scene& scene)
{
    scene.DestroyEntity(scene.FindEntityByUUID(m_Entity));
}

std::size_t CreateSpriteCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Name.capacity() + m_Text.capacity() + m_Label.capacity();
}

InstantiatePrefabCommand::InstantiatePrefabCommand(Trinity::UUID prefab, std::string name, Trinity::UUID parent, Trinity::UUID before, std::optional<glm::vec3> position) : m_Prefab(prefab), m_Name(std::move(name)), m_Parent(parent), m_Before(before), m_Position(position)
{

}

// The first run makes the instance under a new instance UUID and keeps it as text, so a redo brings back the same UUIDs, with the position it was given as what it changes of its prefab
bool InstantiatePrefabCommand::Execute(Trinity::Scene& scene)
{
    if (!m_Text.empty())
    {
        return Restore(scene, m_Text, m_Parent, m_Before, m_Label);
    }

    if (!IsPlaceValid(scene, m_Parent, m_Before))
    {
        return false;
    }

    const Trinity::Expected<Trinity::Entity, std::string> l_Root = Trinity::SceneSerializer::InstantiatePrefab(scene, m_Prefab, Trinity::UUID::Generate(), scene.FindEntityByUUID(m_Parent), scene.FindEntityByUUID(m_Before));
    if (!l_Root)
    {
        TR_ERROR("Forge: {} cannot be instanced: {}", m_Name, l_Root.GetError());

        return false;
    }

    Trinity::Entity l_Entity = *l_Root;
    if (m_Position)
    {
        l_Entity.Get<Trinity::TransformComponent>().Position = *m_Position;
    }

    m_Entity = l_Entity.GetUUID();
    m_Text = Trinity::SceneSerializer::SaveEntityToText(scene, l_Entity);
    m_Label = std::format("Create prefab instance {}", GetEntityLabel(l_Entity));

    return true;
}

void InstantiatePrefabCommand::Undo(Trinity::Scene& scene)
{
    scene.DestroyEntity(scene.FindEntityByUUID(m_Entity));
}

std::size_t InstantiatePrefabCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Name.capacity() + m_Text.capacity() + m_Label.capacity();
}

LinkPrefabCommand::LinkPrefabCommand(std::vector<PrefabLink> links, Trinity::UUID subject, std::string label) : m_Links(std::move(links)), m_Subject(subject), m_Label(std::move(label))
{

}

// Each entity's link before is taken as it runs, so nothing changes while one of them is not there
bool LinkPrefabCommand::Execute(Trinity::Scene& scene)
{
    std::vector<PrefabLink> l_Before;
    for (const PrefabLink& it_Link : m_Links)
    {
        const Trinity::Entity l_Entity = scene.FindEntityByUUID(it_Link.Entity);
        if (!l_Entity)
        {
            return false;
        }

        l_Before.push_back({ .Entity = it_Link.Entity, .Instance = l_Entity.Has<Trinity::PrefabInstanceComponent>() ? std::optional(l_Entity.Get<Trinity::PrefabInstanceComponent>()) : std::nullopt });
    }

    SetLinks(scene, m_Links);
    m_Before = std::move(l_Before);

    return true;
}

void LinkPrefabCommand::Undo(Trinity::Scene& scene)
{
    SetLinks(scene, m_Before);
}

std::size_t LinkPrefabCommand::GetMemorySize() const
{
    return sizeof(*this) + GetLinksMemorySize(m_Links) + GetLinksMemorySize(m_Before) + m_Label.capacity();
}

RevertPrefabCommand::RevertPrefabCommand(Trinity::UUID root, std::vector<Trinity::SceneSerializer::PrefabOverride> overrides, Trinity::UUID subject, std::string label) : m_Root(root), m_Overrides(std::move(overrides)), m_Subject(subject), m_Label(std::move(label))
{

}

// The first run undoes the overrides where the instance is, and keeps it as text before and after, so a redo makes it again as it became
bool RevertPrefabCommand::Execute(Trinity::Scene& scene)
{
    if (!m_After.Text.empty())
    {
        return RestoreInstances(scene, std::span(&m_After, 1), m_Label);
    }

    const Trinity::Entity l_Root = scene.FindEntityByUUID(m_Root);
    if (!l_Root)
    {
        return false;
    }

    Trinity::SceneSerializer::PrefabInstanceText l_Before = SaveInstance(scene, l_Root);
    if (const Trinity::Expected<void, std::string> l_Reverted = Trinity::SceneSerializer::RevertPrefabOverrides(l_Root, m_Overrides); !l_Reverted)
    {
        TR_ERROR("Forge: {} could not be done: {}", m_Label, l_Reverted.GetError());

        return false;
    }

    m_Before = std::move(l_Before);
    m_After = SaveInstance(scene, l_Root);

    return true;
}

void RevertPrefabCommand::Undo(Trinity::Scene& scene)
{
    static_cast<void>(RestoreInstances(scene, std::span(&m_Before, 1), m_Label));
}

std::size_t RevertPrefabCommand::GetMemorySize() const
{
    return sizeof(*this) + GetOverridesMemorySize(m_Overrides) + m_Label.capacity() + m_Before.Text.capacity() + m_After.Text.capacity();
}

ApplyPrefabCommand::ApplyPrefabCommand(Writer writer, Trinity::UUID root, std::vector<Trinity::SceneSerializer::PrefabOverride> overrides, std::size_t level, std::vector<Trinity::SceneSerializer::PrefabText> oldTexts, Trinity::UUID subject, std::string label) : m_Writer(std::move(writer)), m_Root(root), m_Overrides(std::move(overrides)), m_Level(level), m_OldTexts(std::move(oldTexts)), m_Subject(subject), m_Label(std::move(label))
{

}

// Each instance is saved against the prefabs it was made from, the one applied from once it changes none of what is applied, so made again from the new prefabs each keeps the rest of what it changes. Every prefab written holds the innermost, so its instances are all those of the innermost and of each prefab holding it. Of the old texts, those of the prefabs written are kept
bool ApplyPrefabCommand::Execute(Trinity::Scene& scene)
{
    if (!m_After.empty())
    {
        return m_Writer(m_NewTexts, scene) && RestoreInstances(scene, m_After, m_Label);
    }

    const Trinity::Entity l_Root = scene.FindEntityByUUID(m_Root);
    if (!l_Root || !l_Root.Has<Trinity::PrefabInstanceComponent>())
    {
        return false;
    }

    Trinity::Expected<std::vector<Trinity::SceneSerializer::PrefabText>, std::string> l_New = Trinity::SceneSerializer::ApplyPrefabOverrides(l_Root, m_Overrides, m_Level);
    if (!l_New)
    {
        TR_ERROR("Forge: {} could not be done: {}", m_Label, l_New.GetError());

        return false;
    }

    std::vector<Trinity::SceneSerializer::PrefabText> l_Old;
    for (const Trinity::SceneSerializer::PrefabText& it_New : *l_New)
    {
        const auto a_Old = std::ranges::find(m_OldTexts, it_New.Prefab, &Trinity::SceneSerializer::PrefabText::Prefab);
        if (a_Old == m_OldTexts.end())
        {
            TR_ERROR("Forge: {} could not be done, since prefab {} had no text to go back to", m_Label, it_New.Prefab);

            return false;
        }

        l_Old.push_back(*a_Old);
    }

    const Trinity::UUID l_Innermost = l_New->front().Prefab;
    std::vector<Trinity::SceneSerializer::PrefabInstanceText> l_Before = Trinity::SceneSerializer::SavePrefabInstances(scene, l_Innermost);
    if (const Trinity::Expected<void, std::string> l_Reverted = Trinity::SceneSerializer::RevertPrefabOverrides(l_Root, m_Overrides); !l_Reverted)
    {
        TR_ERROR("Forge: {} could not be done: {}", m_Label, l_Reverted.GetError());

        return false;
    }

    const std::vector<Trinity::SceneSerializer::PrefabInstanceText> l_Reverted = Trinity::SceneSerializer::SavePrefabInstances(scene, l_Innermost);
    if (!m_Writer(*l_New, scene))
    {
        static_cast<void>(RestoreInstances(scene, l_Before, m_Label));

        return false;
    }

    static_cast<void>(RestoreInstances(scene, l_Reverted, m_Label));
    m_NewTexts = std::move(*l_New);
    m_OldTexts = std::move(l_Old);
    m_Before = std::move(l_Before);
    m_After = Trinity::SceneSerializer::SavePrefabInstances(scene, l_Innermost);

    return true;
}

// The prefabs first, so the instances are made again from those they were saved against
void ApplyPrefabCommand::Undo(Trinity::Scene& scene)
{
    if (!m_Writer(m_OldTexts, scene))
    {
        TR_ERROR("Forge: {} could not be undone, since the prefabs could not be written back, so they and their instances stay as they are", m_Label);

        return;
    }

    static_cast<void>(RestoreInstances(scene, m_Before, m_Label));
}

std::size_t ApplyPrefabCommand::GetMemorySize() const
{
    return sizeof(*this) + GetOverridesMemorySize(m_Overrides) + GetTextsMemorySize(m_OldTexts) + m_Label.capacity() + GetTextsMemorySize(m_NewTexts) + GetInstancesMemorySize(m_Before) + GetInstancesMemorySize(m_After);
}

CreatePrimitiveCommand::CreatePrimitiveCommand(Trinity::BuiltInMesh mesh, Trinity::UUID parent, glm::vec3 position) : m_Mesh(mesh), m_Parent(parent), m_Position(position)
{

}

// Last under the parent or among the roots, at the position in the parent's space. Kept as text after the first run, as CreateEntityCommand does
bool CreatePrimitiveCommand::Execute(Trinity::Scene& scene)
{
    if (!m_Text.empty())
    {
        return Restore(scene, m_Text, m_Parent, {}, m_Label);
    }

    if (!IsPlaceValid(scene, m_Parent, {}))
    {
        return false;
    }

    Trinity::Entity l_Entity = scene.CreateEntity(Trinity::ToString(m_Mesh), scene.FindEntityByUUID(m_Parent));
    l_Entity.Get<Trinity::TransformComponent>().Position = m_Position;
    l_Entity.Add<Trinity::MeshRendererComponent>().Mesh = Trinity::GetBuiltInMeshID(m_Mesh);
    AddFittingCollider(l_Entity, m_Mesh);

    m_Entity = l_Entity.GetUUID();
    m_Text = Trinity::SceneSerializer::SaveEntityToText(scene, l_Entity);
    m_Label = std::format("Create {}", GetEntityLabel(l_Entity));

    return true;
}

void CreatePrimitiveCommand::Undo(Trinity::Scene& scene)
{
    scene.DestroyEntity(scene.FindEntityByUUID(m_Entity));
}

std::size_t CreatePrimitiveCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Text.capacity() + m_Label.capacity();
}

CreateLightCommand::CreateLightCommand(Trinity::LightType type, Trinity::UUID parent, glm::vec3 position) : m_Type(type), m_Parent(parent), m_Position(position)
{

}

// Last under the parent or among the roots, at the position in the parent's space. Unity's (50, -30, 0) and (90, 0, 0) are (-50, 30, 0) and (-90, 0, 0) here, since a light here shines along -Z where Unity's shines along +Z. Kept as text after the first run, as CreateEntityCommand does
bool CreateLightCommand::Execute(Trinity::Scene& scene)
{
    if (!m_Text.empty())
    {
        return Restore(scene, m_Text, m_Parent, {}, m_Label);
    }

    if (!IsPlaceValid(scene, m_Parent, {}))
    {
        return false;
    }

    constexpr std::array<glm::vec3, 3> c_Degrees{ glm::vec3(-50.0f, 30.0f, 0.0f), glm::vec3(0.0f), glm::vec3(-90.0f, 0.0f, 0.0f) };
    Trinity::Entity l_Entity = scene.CreateEntity(GetLightLabel(m_Type), scene.FindEntityByUUID(m_Parent));
    Trinity::TransformComponent& l_Transform = l_Entity.Get<Trinity::TransformComponent>();
    l_Transform.Position = m_Position;
    l_Transform.Rotation = glm::quat(glm::radians(c_Degrees[static_cast<std::size_t>(m_Type)]));
    l_Entity.Add<Trinity::LightComponent>().Type = m_Type;

    m_Entity = l_Entity.GetUUID();
    m_Text = Trinity::SceneSerializer::SaveEntityToText(scene, l_Entity);
    m_Label = std::format("Create {}", GetEntityLabel(l_Entity));

    return true;
}

void CreateLightCommand::Undo(Trinity::Scene& scene)
{
    scene.DestroyEntity(scene.FindEntityByUUID(m_Entity));
}

std::size_t CreateLightCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Text.capacity() + m_Label.capacity();
}

CreateCameraCommand::CreateCameraCommand(Trinity::CameraProjection projection, Trinity::UUID parent, glm::vec3 position) : m_Projection(projection), m_Parent(parent), m_Position(position)
{

}

// Last under the parent or among the roots, at the position in the parent's space, looking along -Z as every camera here does. Kept as text after the first run, as CreateEntityCommand does
bool CreateCameraCommand::Execute(Trinity::Scene& scene)
{
    if (!m_Text.empty())
    {
        return Restore(scene, m_Text, m_Parent, {}, m_Label);
    }

    if (!IsPlaceValid(scene, m_Parent, {}))
    {
        return false;
    }

    const auto l_Cameras = scene.GetRegistry().view<Trinity::CameraComponent>();
    bool l_HasPrimary = false;
    for (const entt::entity it_Camera : l_Cameras)
    {
        l_HasPrimary = l_HasPrimary || l_Cameras.get<Trinity::CameraComponent>(it_Camera).Primary;
    }

    Trinity::Entity l_Entity = scene.CreateEntity("Camera", scene.FindEntityByUUID(m_Parent));
    l_Entity.Get<Trinity::TransformComponent>().Position = m_Position;
    Trinity::CameraComponent& l_Camera = l_Entity.Add<Trinity::CameraComponent>();
    l_Camera.Projection = m_Projection;
    l_Camera.Primary = !l_HasPrimary;

    m_Entity = l_Entity.GetUUID();
    m_Text = Trinity::SceneSerializer::SaveEntityToText(scene, l_Entity);
    m_Label = std::format("Create {}", GetEntityLabel(l_Entity));

    return true;
}

void CreateCameraCommand::Undo(Trinity::Scene& scene)
{
    scene.DestroyEntity(scene.FindEntityByUUID(m_Entity));
}

std::size_t CreateCameraCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Text.capacity() + m_Label.capacity();
}

DeleteEntityCommand::DeleteEntityCommand(Trinity::UUID entity) : m_Entity(entity)
{

}

// The entity and its subtree are kept as scene text, with the parent and the sibling after them, so undo puts them back with their UUIDs in the same place
bool DeleteEntityCommand::Execute(Trinity::Scene& scene)
{
    const Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
    if (!l_Entity)
    {
        return false;
    }

    if (m_Text.empty())
    {
        m_Parent = GetID(l_Entity.GetParent());
        m_Before = GetID(l_Entity.GetNextSibling());
        m_Text = Trinity::SceneSerializer::SaveEntityToText(scene, l_Entity);
        m_Label = std::format("Delete {}", GetEntityLabel(l_Entity));
    }

    scene.DestroyEntity(l_Entity);

    return true;
}

void DeleteEntityCommand::Undo(Trinity::Scene& scene)
{
    static_cast<void>(Restore(scene, m_Text, m_Parent, m_Before, m_Label));
}

std::size_t DeleteEntityCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Text.capacity() + m_Label.capacity();
}

DuplicateEntityCommand::DuplicateEntityCommand(Trinity::UUID source) : m_Source(source)
{

}

// The copy and its subtree get new UUIDs once, on the first run, and go just after the source. They are kept as text, so a redo brings back the same UUIDs
bool DuplicateEntityCommand::Execute(Trinity::Scene& scene)
{
    if (!m_Text.empty())
    {
        return Restore(scene, m_Text, m_Parent, m_Before, m_Label);
    }

    const Trinity::Entity l_Source = scene.FindEntityByUUID(m_Source);
    if (!l_Source)
    {
        return false;
    }

    const Trinity::Entity l_Copy = scene.DuplicateEntity(l_Source);
    // A copy of one of a prefab's entities in an instance is the instance's addition, so it goes after the prefab's own children there
    if (const Trinity::Entity l_Next = l_Copy.GetNextSibling(); l_Next && IsPrefabPart(l_Next))
    {
        static_cast<void>(Place(scene, l_Copy, GetID(l_Copy.GetParent()), FindAddedPlace(scene, l_Next.GetUUID())));
    }

    m_Copy = l_Copy.GetUUID();
    m_Parent = GetID(l_Copy.GetParent());
    m_Before = GetID(l_Copy.GetNextSibling());
    m_Text = Trinity::SceneSerializer::SaveEntityToText(scene, l_Copy);
    m_Label = std::format("Duplicate {}", GetEntityLabel(l_Source));

    return true;
}

void DuplicateEntityCommand::Undo(Trinity::Scene& scene)
{
    scene.DestroyEntity(scene.FindEntityByUUID(m_Copy));
}

std::size_t DuplicateEntityCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Text.capacity() + m_Label.capacity();
}

PasteEntityCommand::PasteEntityCommand(std::string text, Trinity::UUID parent, Trinity::UUID before, std::optional<Trinity::TransformComponent> world) : m_Text(std::move(text)), m_Parent(parent), m_Before(before), m_World(world)
{

}

// The copied text goes into a scene of its own and is duplicated there, so the copy has new UUIDs and its links name it, which is then loaded here with them
bool PasteEntityCommand::Execute(Trinity::Scene& scene)
{
    if (m_Entity.IsValid())
    {
        return Restore(scene, m_Text, m_Parent, m_Before, m_Label);
    }

    if (!IsPlaceValid(scene, m_Parent, m_Before))
    {
        return false;
    }

    Trinity::Scene l_Clipboard;
    const Trinity::Expected<Trinity::Entity, std::string> l_Copied = Trinity::SceneSerializer::LoadEntityFromText(l_Clipboard, m_Text, {}, {});
    if (!l_Copied)
    {
        TR_ERROR("Forge: the copied entity could not be read to paste: {}", l_Copied.GetError());

        return false;
    }

    const Trinity::Entity l_Parent = scene.FindEntityByUUID(m_Parent);
    const std::string l_Text = Trinity::SceneSerializer::SaveEntityToText(l_Clipboard, l_Clipboard.DuplicateEntity(*l_Copied));
    const Trinity::Expected<Trinity::Entity, std::string> l_Pasted = Trinity::SceneSerializer::LoadEntityFromText(scene, l_Text, l_Parent, scene.FindEntityByUUID(m_Before));
    if (!l_Pasted)
    {
        TR_ERROR("Forge: the copied entity could not be pasted: {}", l_Pasted.GetError());

        return false;
    }

    if (m_World)
    {
        scene.SetWorldTransform(*l_Pasted, *m_World);
    }

    m_Entity = l_Pasted->GetUUID();
    m_Text = Trinity::SceneSerializer::SaveEntityToText(scene, *l_Pasted);
    m_Label = l_Parent ? std::format("Paste {} under {}", GetEntityLabel(*l_Pasted), GetEntityLabel(l_Parent)) : std::format("Paste {}", GetEntityLabel(*l_Pasted));

    return true;
}

void PasteEntityCommand::Undo(Trinity::Scene& scene)
{
    scene.DestroyEntity(scene.FindEntityByUUID(m_Entity));
}

std::size_t PasteEntityCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Text.capacity() + m_Label.capacity();
}

MoveEntityCommand::MoveEntityCommand(Trinity::UUID entity, Trinity::UUID parent, Trinity::UUID before, bool keepWorldTransform) : m_Entity(entity), m_NewParent(parent), m_NewBefore(before), m_KeepWorldTransform(keepWorldTransform)
{

}

// Reparents, or reorders when the parent stays. The transform before and after the first run is kept, so undo and redo land on exactly the same values rather than recomputing them
bool MoveEntityCommand::Execute(Trinity::Scene& scene)
{
    Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
    if (!l_Entity)
    {
        return false;
    }

    if (m_Executed)
    {
        static_cast<void>(Place(scene, l_Entity, m_NewParent, m_NewBefore));
        l_Entity.Get<Trinity::TransformComponent>() = m_NewTransform;

        return true;
    }

    if (!IsPlaceValid(scene, m_NewParent, m_NewBefore) || m_NewBefore == m_Entity)
    {
        return false;
    }

    // Already there, so nothing would change
    const Trinity::Entity l_OldParent = l_Entity.GetParent();
    const Trinity::Entity l_OldNext = l_Entity.GetNextSibling();
    const Trinity::Entity l_NewParent = scene.FindEntityByUUID(m_NewParent);
    const Trinity::Entity l_NewBefore = scene.FindEntityByUUID(m_NewBefore);
    if (l_OldParent.GetHandle() == l_NewParent.GetHandle() && l_OldNext.GetHandle() == l_NewBefore.GetHandle())
    {
        return false;
    }

    const Trinity::TransformComponent l_OldTransform = l_Entity.Get<Trinity::TransformComponent>();
    const bool l_Moved = l_NewBefore ? scene.MoveBefore(l_Entity, l_NewBefore, m_KeepWorldTransform) : scene.SetParent(l_Entity, l_NewParent, m_KeepWorldTransform);
    if (!l_Moved)
    {
        return false;
    }

    m_OldParent = GetID(l_OldParent);
    m_OldBefore = GetID(l_OldNext);
    m_OldTransform = l_OldTransform;
    m_NewTransform = l_Entity.Get<Trinity::TransformComponent>();
    m_Label = std::format("{} {}", l_OldParent.GetHandle() == l_NewParent.GetHandle() ? "Reorder" : "Reparent", GetEntityLabel(l_Entity));
    m_Executed = true;

    return true;
}

void MoveEntityCommand::Undo(Trinity::Scene& scene)
{
    Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
    static_cast<void>(Place(scene, l_Entity, m_OldParent, m_OldBefore));
    l_Entity.Get<Trinity::TransformComponent>() = m_OldTransform;
}

std::size_t MoveEntityCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Label.capacity();
}

AddComponentCommand::AddComponentCommand(Trinity::UUID entity, std::string component) : m_Entity(entity), m_Component(std::move(component))
{

}

// Default constructed, which is the same on every redo
bool AddComponentCommand::Execute(Trinity::Scene& scene)
{
    const Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
    if (!l_Entity || !Trinity::SceneSerializer::AddComponent(l_Entity, m_Component))
    {
        return false;
    }

    if (m_Label.empty())
    {
        m_Label = std::format("Add {} to {}", GetComponentLabel(m_Component), GetEntityLabel(l_Entity));
    }

    return true;
}

void AddComponentCommand::Undo(Trinity::Scene& scene)
{
    static_cast<void>(Trinity::SceneSerializer::RemoveComponent(scene.FindEntityByUUID(m_Entity), m_Component));
}

std::size_t AddComponentCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Component.capacity() + m_Label.capacity();
}

AddScriptCommand::AddScriptCommand(Trinity::UUID entity, Trinity::ScriptEntry entry) : m_Entity(entity), m_Entry(std::move(entry))
{

}

bool AddScriptCommand::Execute(Trinity::Scene& scene)
{
    Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
    if (!l_Entity)
    {
        return false;
    }

    m_AddedComponent = !l_Entity.Has<Trinity::ScriptComponent>();
    if (m_AddedComponent)
    {
        l_Entity.Add<Trinity::ScriptComponent>();
    }

    l_Entity.Get<Trinity::ScriptComponent>().Scripts.push_back(m_Entry);
    if (m_Label.empty())
    {
        m_Label = std::format("Add Script {} to {}", GetScriptLabel(m_Entry), GetEntityLabel(l_Entity));
    }

    return true;
}

// The script went last, and every command after this one has been undone, so it is still last
void AddScriptCommand::Undo(Trinity::Scene& scene)
{
    Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
    if (m_AddedComponent)
    {
        l_Entity.Remove<Trinity::ScriptComponent>();

        return;
    }

    l_Entity.Get<Trinity::ScriptComponent>().Scripts.pop_back();
}

std::size_t AddScriptCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Label.capacity() + GetScriptEntryMemorySize(m_Entry);
}

RemoveComponentCommand::RemoveComponentCommand(Trinity::UUID entity, std::string component) : m_Entity(entity), m_Component(std::move(component))
{

}

// The component is kept as scene text, so undo brings back its values through its own serializer, for a module's component as well as the engine's
bool RemoveComponentCommand::Execute(Trinity::Scene& scene)
{
    const Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
    if (!l_Entity)
    {
        return false;
    }

    std::string l_Text = Trinity::SceneSerializer::SaveComponentToText(l_Entity, m_Component);
    if (m_Label.empty())
    {
        m_Label = std::format("Remove {} from {}", GetComponentLabel(m_Component), GetEntityLabel(l_Entity));
    }

    if (l_Text.empty() || !Trinity::SceneSerializer::RemoveComponent(l_Entity, m_Component))
    {
        return false;
    }

    m_Text = std::move(l_Text);

    return true;
}

void RemoveComponentCommand::Undo(Trinity::Scene& scene)
{
    const Trinity::Expected<void, std::string> l_Loaded = Trinity::SceneSerializer::LoadComponentFromText(scene.FindEntityByUUID(m_Entity), m_Component, m_Text);
    if (!l_Loaded)
    {
        TR_ERROR("Forge: {} could not be undone: {}", m_Label, l_Loaded.GetError());
    }
}

std::size_t RemoveComponentCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Component.capacity() + m_Text.capacity() + m_Label.capacity();
}

SetComponentTextCommand::SetComponentTextCommand(Trinity::UUID entity, std::string component, std::string text, std::string label) : m_Entity(entity), m_Component(std::move(component)), m_New(std::move(text)), m_Label(std::move(label))
{

}

// The text the component had is kept, so undo puts back its values through its own serializer, for a module's component as well as the engine's
bool SetComponentTextCommand::Execute(Trinity::Scene& scene)
{
    const Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
    if (!l_Entity)
    {
        return false;
    }

    if (!m_Executed)
    {
        m_Old = Trinity::SceneSerializer::SaveComponentToText(l_Entity, m_Component);
        if (m_Old == m_New)
        {
            return false;
        }

        m_Added = m_Old.empty();
        m_Executed = true;
    }

    const Trinity::Expected<void, std::string> l_Loaded = Trinity::SceneSerializer::LoadComponentFromText(l_Entity, m_Component, m_New);
    if (!l_Loaded)
    {
        TR_ERROR("Forge: {} failed: {}", m_Label, l_Loaded.GetError());

        return false;
    }

    return true;
}

void SetComponentTextCommand::Undo(Trinity::Scene& scene)
{
    const Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
    if (m_Added)
    {
        static_cast<void>(Trinity::SceneSerializer::RemoveComponent(l_Entity, m_Component));

        return;
    }

    const Trinity::Expected<void, std::string> l_Loaded = Trinity::SceneSerializer::LoadComponentFromText(l_Entity, m_Component, m_Old);
    if (!l_Loaded)
    {
        TR_ERROR("Forge: {} could not be undone: {}", m_Label, l_Loaded.GetError());
    }
}

std::size_t SetComponentTextCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Component.capacity() + m_New.capacity() + m_Old.capacity() + m_Label.capacity();
}

SetNativeFieldCommand::SetNativeFieldCommand(Trinity::UUID entity, std::string component, std::string field, Trinity::ScriptFieldValue value) : m_Entity(entity), m_Component(std::move(component)), m_Field(std::move(field)), m_New(std::move(value))
{

}

// Refused, so nothing is recorded, when the value is the one the field has, and on a redo once no loaded code knows the component
bool SetNativeFieldCommand::Execute(Trinity::Scene& scene)
{
    const Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
    if (!l_Entity)
    {
        return false;
    }

    if (!m_Executed)
    {
        const std::optional<Trinity::ScriptFieldValue> l_Old = Trinity::NativeComponents::GetField(l_Entity, m_Component, m_Field);
        if (!l_Old || *l_Old == m_New)
        {
            return false;
        }

        m_Old = *l_Old;
        m_Label = std::format("Edit {} {} of {}", GetComponentLabel(m_Component), m_Field, GetEntityLabel(l_Entity));
        m_Executed = true;
    }

    return Trinity::NativeComponents::SetField(l_Entity, m_Component, m_Field, m_New);
}

void SetNativeFieldCommand::Undo(Trinity::Scene& scene)
{
    if (!Trinity::NativeComponents::SetField(scene.FindEntityByUUID(m_Entity), m_Component, m_Field, m_Old))
    {
        TR_ERROR("Forge: {} could not be undone, since no loaded code has {} with that field", m_Label, m_Component);
    }
}

bool SetNativeFieldCommand::MergeWith(const EditorCommand& next)
{
    const SetNativeFieldCommand* l_Next = dynamic_cast<const SetNativeFieldCommand*>(&next);
    if (l_Next == nullptr || l_Next->m_Entity != m_Entity || l_Next->m_Component != m_Component || l_Next->m_Field != m_Field)
    {
        return false;
    }

    m_New = l_Next->m_New;

    return true;
}

std::size_t SetNativeFieldCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Component.capacity() + m_Field.capacity() + m_Label.capacity() + GetScriptValueMemorySize(m_Old) + GetScriptValueMemorySize(m_New);
}

SetPhysicsSettingsCommand::SetPhysicsSettingsCommand(Writer writer, Trinity::PhysicsSettings before, Trinity::PhysicsSettings after, std::string field) : m_Writer(std::move(writer)), m_Before(std::move(before)), m_After(std::move(after)), m_Field(std::move(field)), m_Label(std::format("Edit project {}", m_Field))
{

}

// An edit that changes nothing is no command
bool SetPhysicsSettingsCommand::Execute([[maybe_unused]] Trinity::Scene& scene)
{
    return m_Before != m_After && m_Writer(m_After);
}

void SetPhysicsSettingsCommand::Undo([[maybe_unused]] Trinity::Scene& scene)
{
    static_cast<void>(m_Writer(m_Before));
}

bool SetPhysicsSettingsCommand::MergeWith(const EditorCommand& next)
{
    const SetPhysicsSettingsCommand* l_Next = dynamic_cast<const SetPhysicsSettingsCommand*>(&next);
    if (l_Next == nullptr || l_Next->m_Field != m_Field)
    {
        return false;
    }

    m_After = l_Next->m_After;

    return true;
}

// The layers' names are the only part that grows
std::size_t SetPhysicsSettingsCommand::GetMemorySize() const
{
    std::size_t l_Size = sizeof(*this) + m_Field.capacity() + m_Label.capacity();
    for (const Trinity::PhysicsSettings* it_Settings : { &m_Before, &m_After })
    {
        for (const std::string& it_Name : it_Settings->Layers.Names)
        {
            l_Size += it_Name.capacity();
        }
    }

    return l_Size;
}

SetMaterialCommand::SetMaterialCommand(Writer writer, Trinity::UUID material, Trinity::MaterialData before, Trinity::MaterialData after, std::string field, std::string name) : m_Writer(std::move(writer)), m_Material(material), m_Before(std::move(before)), m_After(std::move(after)), m_Field(std::move(field)), m_Label(std::format("Edit {} of {}", m_Field, name))
{

}

// An edit that changes nothing is no command
bool SetMaterialCommand::Execute([[maybe_unused]] Trinity::Scene& scene)
{
    return m_Before != m_After && m_Writer(m_Material, m_After);
}

void SetMaterialCommand::Undo([[maybe_unused]] Trinity::Scene& scene)
{
    static_cast<void>(m_Writer(m_Material, m_Before));
}

bool SetMaterialCommand::MergeWith(const EditorCommand& next)
{
    const SetMaterialCommand* l_Next = dynamic_cast<const SetMaterialCommand*>(&next);
    if (l_Next == nullptr || l_Next->m_Material != m_Material || l_Next->m_Field != m_Field)
    {
        return false;
    }

    m_After = l_Next->m_After;

    return true;
}

std::size_t SetMaterialCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Field.capacity() + m_Label.capacity();
}

SetInputActionsCommand::SetInputActionsCommand(Writer writer, Trinity::UUID asset, Trinity::InputActionsData before, Trinity::InputActionsData after, std::string field, std::string label, std::vector<InputActionFieldRewrite> rewrites) : m_Writer(std::move(writer)), m_Asset(asset), m_Before(std::move(before)), m_After(std::move(after)), m_Field(std::move(field)), m_Label(std::move(label)), m_Rewrites(std::move(rewrites))
{

}

// An edit that changes nothing is no command. A rename's fields follow once its file is written
bool SetInputActionsCommand::Execute(Trinity::Scene& scene)
{
    if (m_Before == m_After || !m_Writer(m_Asset, m_After))
    {
        return false;
    }

    SetInputActionFields(scene, m_Rewrites, false);

    return true;
}

void SetInputActionsCommand::Undo(Trinity::Scene& scene)
{
    SetInputActionFields(scene, m_Rewrites, true);
    static_cast<void>(m_Writer(m_Asset, m_Before));
}

// Only edits named by a field merge, so adding, removing and moving each stay a step of their own
bool SetInputActionsCommand::MergeWith(const EditorCommand& next)
{
    const SetInputActionsCommand* l_Next = dynamic_cast<const SetInputActionsCommand*>(&next);
    if (l_Next == nullptr || l_Next->m_Asset != m_Asset || m_Field.empty() || l_Next->m_Field != m_Field)
    {
        return false;
    }

    m_After = l_Next->m_After;

    return true;
}

std::size_t SetInputActionsCommand::GetMemorySize() const
{
    std::size_t l_Size = sizeof(*this) + m_Field.capacity() + m_Label.capacity() + m_Rewrites.capacity() * sizeof(InputActionFieldRewrite);
    for (const InputActionFieldRewrite& it_Rewrite : m_Rewrites)
    {
        l_Size += it_Rewrite.Field.capacity() + GetScriptValueMemorySize(it_Rewrite.Before) + GetScriptValueMemorySize(it_Rewrite.After);
    }

    for (const Trinity::InputActionsData* it_Data : { &m_Before, &m_After })
    {
        l_Size += it_Data->Maps.capacity() * sizeof(Trinity::InputActionMap);
        for (const Trinity::InputActionMap& it_Map : it_Data->Maps)
        {
            l_Size += it_Map.Name.capacity() + it_Map.Actions.capacity() * sizeof(Trinity::InputActionDefinition);
            for (const Trinity::InputActionDefinition& it_Action : it_Map.Actions)
            {
                l_Size += it_Action.Name.capacity() + it_Action.Bindings.capacity() * sizeof(Trinity::InputBinding);
                for (const Trinity::InputBinding& it_Binding : it_Action.Bindings)
                {
                    l_Size += it_Binding.Processors.capacity() * sizeof(Trinity::InputProcessor);
                }
            }
        }
    }

    return l_Size;
}