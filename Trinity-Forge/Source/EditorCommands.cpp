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