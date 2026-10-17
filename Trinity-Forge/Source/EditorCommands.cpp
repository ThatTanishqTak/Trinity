#include "EditorCommands.hpp"

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

    // A string's text, the one part of a value held outside it
    std::size_t GetScriptValueMemorySize(const Trinity::ScriptFieldValue& value)
    {
        return value.GetType() == Trinity::ScriptFieldType::String ? value.Get<Trinity::ScriptString>().capacity() : 0;
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

std::string GetScriptLabel(const Trinity::ScriptEntry& entry)
{
    const std::string_view l_Name(entry.ClassName);
    const std::size_t l_Separator = l_Name.find_last_of(".:");

    return std::format("{} ({})", l_Separator == std::string_view::npos ? l_Name : l_Name.substr(l_Separator + 1), GetScriptLanguageLabel(entry.Language));
}

void ForEachSlotReference(Trinity::Entity entity, const std::function<void(const SlotReference&)>& visit)
{
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
            const Trinity::ScriptFieldType l_Type = it_Field.Value.GetType();
            if (l_Type != Trinity::ScriptFieldType::Entity && l_Type != Trinity::ScriptFieldType::Asset)
            {
                continue;
            }

            const Trinity::ScriptFieldDescription* l_Field = l_Class != nullptr ? l_Class->FindField(it_Field.Name) : nullptr;
            const std::string_view l_AssetType = l_Type == Trinity::ScriptFieldType::Asset && l_Field != nullptr ? std::string_view(l_Field->AssetType) : std::string_view();
            visit({ .Component = {}, .Script = &it_Script, .Slot = it_Field.Name, .Index = std::nullopt, .AssetType = l_AssetType, .Value = it_Field.Value.Get<Trinity::UUID>(), .IsEntity = l_Type == Trinity::ScriptFieldType::Entity });
        }
    }
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

// Parents come before their children in the model, so each node's parent entity already exists, and children are added last, in the model's order
Trinity::Entity CreateModelEntities(Trinity::Scene& scene, const Trinity::ModelData& model, std::string_view name, Trinity::Entity parent)
{
    const Trinity::Entity l_Root = scene.CreateEntity(name, parent);
    std::vector<Trinity::Entity> l_Entities;
    l_Entities.reserve(model.Nodes.size());
    for (const Trinity::ModelNode& it_Node : model.Nodes)
    {
        Trinity::Entity l_Entity = scene.CreateEntity(it_Node.Name, it_Node.Parent == Trinity::ModelNode::c_NoParent ? l_Root : l_Entities[static_cast<std::size_t>(it_Node.Parent)]);
        Trinity::TransformComponent& l_Transform = l_Entity.Get<Trinity::TransformComponent>();
        l_Transform.Position = it_Node.Translation;
        l_Transform.Rotation = it_Node.Rotation;
        l_Transform.Scale = it_Node.Scale;
        if (it_Node.Mesh.IsValid())
        {
            Trinity::MeshRendererComponent& l_Renderer = l_Entity.Add<Trinity::MeshRendererComponent>();
            l_Renderer.Mesh = it_Node.Mesh;
            l_Renderer.Materials.assign(it_Node.Materials.begin(), it_Node.Materials.end());
        }

        l_Entities.push_back(l_Entity);
    }

    return l_Root;
}

CreateEntityCommand::CreateEntityCommand(std::string name, Trinity::UUID parent, Trinity::UUID before) : m_Name(std::move(name)), m_Parent(parent), m_Before(before)
{

}

// The first run creates the entity and keeps it as text, so a redo brings back the same UUID
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

    const Trinity::Entity l_Entity = scene.CreateEntity(m_Name, scene.FindEntityByUUID(m_Parent));
    if (const Trinity::Entity l_Before = scene.FindEntityByUUID(m_Before))
    {
        scene.MoveBefore(l_Entity, l_Before, false);
    }

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

CreateSpriteCommand::CreateSpriteCommand(std::string name, Trinity::UUID texture, glm::vec3 position, glm::vec2 size) : m_Name(std::move(name)), m_Texture(texture), m_Position(position), m_Size(size)
{

}

// A root entity with its transform, SpriteRenderer and texture set in one step, so a single undo takes it away. Kept as text after the first run, as CreateEntityCommand does
bool CreateSpriteCommand::Execute(Trinity::Scene& scene)
{
    if (!m_Text.empty())
    {
        return Restore(scene, m_Text, {}, {}, m_Label);
    }

    Trinity::Entity l_Entity = scene.CreateEntity(m_Name);
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

CreateModelCommand::CreateModelCommand(Trinity::UUID model, std::string name, Trinity::UUID parent, Trinity::UUID before, glm::vec3 position) : m_Model(model), m_Name(std::move(name)), m_Parent(parent), m_Before(before), m_Position(position)
{

}

// The first run reads the hierarchy the model's import cooked and keeps what it made as text, so a redo brings back the same UUIDs even after a reimport
bool CreateModelCommand::Execute(Trinity::Scene& scene)
{
    if (!m_Text.empty())
    {
        return Restore(scene, m_Text, m_Parent, m_Before, m_Label);
    }

    if (!IsPlaceValid(scene, m_Parent, m_Before))
    {
        return false;
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(Trinity::GetCookedModelPath(m_Model));
    const Trinity::Expected<Trinity::ModelData, std::string> l_Model = l_Text ? Trinity::ParseModelData(*l_Text) : Trinity::Expected<Trinity::ModelData, std::string>(Trinity::Unexpected{ std::string("it has not been imported yet, or its import failed") });
    if (!l_Model)
    {
        TR_ERROR("Forge: {} cannot be created: {}", m_Name, l_Model.GetError());

        return false;
    }

    Trinity::Entity l_Root = CreateModelEntities(scene, *l_Model, m_Name, scene.FindEntityByUUID(m_Parent));
    if (const Trinity::Entity l_Before = scene.FindEntityByUUID(m_Before))
    {
        scene.MoveBefore(l_Root, l_Before, false);
    }

    l_Root.Get<Trinity::TransformComponent>().Position = m_Position;
    m_Entity = l_Root.GetUUID();
    m_Text = Trinity::SceneSerializer::SaveEntityToText(scene, l_Root);
    m_Label = std::format("Create model {}", GetEntityLabel(l_Root));

    return true;
}

void CreateModelCommand::Undo(Trinity::Scene& scene)
{
    scene.DestroyEntity(scene.FindEntityByUUID(m_Entity));
}

std::size_t CreateModelCommand::GetMemorySize() const
{
    return sizeof(*this) + m_Name.capacity() + m_Text.capacity() + m_Label.capacity();
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