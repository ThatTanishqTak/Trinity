#pragma once

#include "CommandStack.hpp"

#include <Trinity.hpp>

#include <concepts>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

[[nodiscard]] std::string GetEntityLabel(Trinity::Entity entity);
[[nodiscard]] std::string_view GetComponentLabel(std::string_view name);
[[nodiscard]] std::string_view GetScriptLanguageLabel(Trinity::ScriptLanguage language);
// The script's class name after its namespace, with its language, such as Player (C#)
[[nodiscard]] std::string GetScriptLabel(const Trinity::ScriptEntry& entry);
[[nodiscard]] std::size_t GetScriptMemorySize(const Trinity::ScriptComponent& component);

// The model's nodes under one root with the given name, each with its transform, and a MeshRenderer where it has a mesh. The root's subtree holds the nodes in the model's order
Trinity::Entity CreateModelEntities(Trinity::Scene& scene, const Trinity::ModelData& model, std::string_view name, Trinity::Entity parent);

class CreateEntityCommand final : public EditorCommand
{
public:
    CreateEntityCommand(std::string name, Trinity::UUID parent, Trinity::UUID before);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    std::string m_Name;
    Trinity::UUID m_Parent;
    Trinity::UUID m_Before;
    Trinity::UUID m_Entity;
    std::string m_Text;
    std::string m_Label;
};

class CreateSpriteCommand final : public EditorCommand
{
public:
    CreateSpriteCommand(std::string name, Trinity::UUID texture, glm::vec3 position, glm::vec2 size);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    std::string m_Name;
    Trinity::UUID m_Texture;
    glm::vec3 m_Position{ 0.0f };
    glm::vec2 m_Size{ 1.0f };
    Trinity::UUID m_Entity;
    std::string m_Text;
    std::string m_Label;
};

class CreateModelCommand final : public EditorCommand
{
public:
    CreateModelCommand(Trinity::UUID model, std::string name, Trinity::UUID parent, Trinity::UUID before, glm::vec3 position);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Model;
    std::string m_Name;
    Trinity::UUID m_Parent;
    Trinity::UUID m_Before;
    glm::vec3 m_Position{ 0.0f };
    Trinity::UUID m_Entity;
    std::string m_Text;
    std::string m_Label;
};

// A built-in shape as one entity, with its MeshRenderer and the collider that fits it, static until it is given a RigidBody
class CreatePrimitiveCommand final : public EditorCommand
{
public:
    CreatePrimitiveCommand(Trinity::BuiltInMesh mesh, Trinity::UUID parent, glm::vec3 position);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::BuiltInMesh m_Mesh = Trinity::BuiltInMesh::Cube;
    Trinity::UUID m_Parent;
    glm::vec3 m_Position{ 0.0f };
    Trinity::UUID m_Entity;
    std::string m_Text;
    std::string m_Label;
};

class DeleteEntityCommand final : public EditorCommand
{
public:
    explicit DeleteEntityCommand(Trinity::UUID entity);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Entity;
    Trinity::UUID m_Parent;
    Trinity::UUID m_Before;
    std::string m_Text;
    std::string m_Label;
};

class DuplicateEntityCommand final : public EditorCommand
{
public:
    explicit DuplicateEntityCommand(Trinity::UUID source);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Copy; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Source;
    Trinity::UUID m_Copy;
    Trinity::UUID m_Parent;
    Trinity::UUID m_Before;
    std::string m_Text;
    std::string m_Label;
};

class MoveEntityCommand final : public EditorCommand
{
public:
    MoveEntityCommand(Trinity::UUID entity, Trinity::UUID parent, Trinity::UUID before, bool keepWorldTransform);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Entity;
    Trinity::UUID m_NewParent;
    Trinity::UUID m_NewBefore;
    Trinity::UUID m_OldParent;
    Trinity::UUID m_OldBefore;
    Trinity::TransformComponent m_OldTransform;
    Trinity::TransformComponent m_NewTransform;
    std::string m_Label;
    bool m_KeepWorldTransform = true;
    bool m_Executed = false;
};

class AddComponentCommand final : public EditorCommand
{
public:
    AddComponentCommand(Trinity::UUID entity, std::string component);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Entity;
    std::string m_Component;
    std::string m_Label;
};

class RemoveComponentCommand final : public EditorCommand
{
public:
    RemoveComponentCommand(Trinity::UUID entity, std::string component);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Entity;
    std::string m_Component;
    std::string m_Text;
    std::string m_Label;
};

// Adds a Script component first when the entity is without one, so adding the first script and undoing it are one step each
class AddScriptCommand final : public EditorCommand
{
public:
    AddScriptCommand(Trinity::UUID entity, Trinity::ScriptEntry entry);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Entity;
    Trinity::ScriptEntry m_Entry;
    std::string m_Label;
    bool m_AddedComponent = false;
};

// One field of a module's component, named by the component's name and the field's, so the edit still undoes once the module is built and loaded again. Edits of one field in a row become one
class SetNativeFieldCommand final : public EditorCommand
{
public:
    SetNativeFieldCommand(Trinity::UUID entity, std::string component, std::string field, Trinity::ScriptFieldValue value);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;
    [[nodiscard]] bool MergeWith(const EditorCommand& next) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Entity;
    std::string m_Component;
    std::string m_Field;
    Trinity::ScriptFieldValue m_Old;
    Trinity::ScriptFieldValue m_New;
    std::string m_Label;
    bool m_Executed = false;
};

// The field names what was edited, and edits of one field in a row become one. A label, when given, names the command in place of one made from the field
template<Trinity::Component T>
class SetComponentCommand final : public EditorCommand
{
public:
    SetComponentCommand(Trinity::UUID entity, T value, std::string field, std::string label = {}) : m_Entity(entity), m_New(std::move(value)), m_Field(std::move(field)), m_Label(std::move(label))
    {

    }

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override
    {
        Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
        if (!l_Entity || !l_Entity.Has<T>())
        {
            return false;
        }

        T& l_Component = l_Entity.Get<T>();
        if (!m_Executed)
        {
            if constexpr (std::equality_comparable<T>)
            {
                if (l_Component == m_New)
                {
                    return false;
                }
            }

            m_Old = l_Component;
            if (m_Label.empty())
            {
                m_Label = m_Field.empty() ? std::format("Edit {} of {}", GetComponentLabel(T::c_TypeName), GetEntityLabel(l_Entity)) : std::format("Edit {} {} of {}", GetComponentLabel(T::c_TypeName), m_Field, GetEntityLabel(l_Entity));
            }

            m_Executed = true;
        }

        l_Component = m_New;

        return true;
    }

    void Undo(Trinity::Scene& scene) override
    {
        scene.FindEntityByUUID(m_Entity).Get<T>() = m_Old;
    }

    [[nodiscard]] bool MergeWith(const EditorCommand& next) override
    {
        const SetComponentCommand* l_Next = dynamic_cast<const SetComponentCommand*>(&next);
        if (l_Next == nullptr || l_Next->m_Entity != m_Entity || l_Next->m_Field != m_Field)
        {
            return false;
        }

        m_New = l_Next->m_New;

        return true;
    }

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }

    [[nodiscard]] std::size_t GetMemorySize() const override
    {
        std::size_t l_Size = sizeof(*this) + m_Field.capacity() + m_Label.capacity();
        if constexpr (std::is_same_v<T, Trinity::TagComponent>)
        {
            l_Size += m_Old.Tag.capacity() + m_New.Tag.capacity();
        }
        else if constexpr (std::is_same_v<T, Trinity::PolygonCollider2DComponent>)
        {
            l_Size += (m_Old.Points.capacity() + m_New.Points.capacity()) * sizeof(glm::vec2);
        }
        else if constexpr (std::is_same_v<T, Trinity::ScriptComponent>)
        {
            l_Size += GetScriptMemorySize(m_Old) + GetScriptMemorySize(m_New);
        }

        return l_Size;
    }

private:
    Trinity::UUID m_Entity;
    T m_Old{};
    T m_New;
    std::string m_Field;
    std::string m_Label;
    bool m_Executed = false;
};


// An edit of the project's physics settings, which the writer saves with the project as it runs and as it is undone. Edits of one field in a row become one, as a drag does
class SetPhysicsSettingsCommand final : public EditorCommand
{
public:
    using Writer = std::function<bool(const Trinity::PhysicsSettings&)>;

    SetPhysicsSettingsCommand(Writer writer, Trinity::PhysicsSettings before, Trinity::PhysicsSettings after, std::string field);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;
    [[nodiscard]] bool MergeWith(const EditorCommand& next) override;
    [[nodiscard]] bool ChangesScene() const override { return false; }

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return {}; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Writer m_Writer;
    Trinity::PhysicsSettings m_Before;
    Trinity::PhysicsSettings m_After;
    std::string m_Field;
    std::string m_Label;
};

class SetMaterialCommand final : public EditorCommand
{
public:
    using Writer = std::function<bool(Trinity::UUID, const Trinity::MaterialData&)>;

    SetMaterialCommand(Writer writer, Trinity::UUID material, Trinity::MaterialData before, Trinity::MaterialData after, std::string field, std::string name);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;
    [[nodiscard]] bool MergeWith(const EditorCommand& next) override;
    [[nodiscard]] bool ChangesScene() const override { return false; }

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Material; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Writer m_Writer;
    Trinity::UUID m_Material;
    Trinity::MaterialData m_Before;
    Trinity::MaterialData m_After;
    std::string m_Field;
    std::string m_Label;
};