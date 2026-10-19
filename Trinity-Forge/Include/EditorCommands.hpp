#pragma once

#include "CommandStack.hpp"

#include <Trinity.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

[[nodiscard]] std::string GetEntityLabel(Trinity::Entity entity);
[[nodiscard]] std::string_view GetComponentLabel(std::string_view name);
[[nodiscard]] std::string_view GetScriptLanguageLabel(Trinity::ScriptLanguage language);
// Every type of light, in the order of Unity's Light menu, and a new one's name, such as Directional Light
inline constexpr std::array<Trinity::LightType, 3> c_LightTypes{ Trinity::LightType::Directional, Trinity::LightType::Point, Trinity::LightType::Spot };
[[nodiscard]] std::string_view GetLightLabel(Trinity::LightType type);
// The script's class name after its namespace, with its language, such as Player (C#)
[[nodiscard]] std::string GetScriptLabel(const Trinity::ScriptEntry& entry);
[[nodiscard]] std::size_t GetScriptMemorySize(const Trinity::ScriptComponent& component);

// A slot of an entity's component that names an asset or another entity, as Properties edits it: the component's type name, or the script whose field it is, the slot's name with its index among the mesh's materials, the asset type it takes, empty for any or for an entity, and what it holds
struct SlotReference
{
    std::string_view Component;
    const Trinity::ScriptEntry* Script = nullptr;
    std::string_view Slot;
    std::optional<std::size_t> Index;
    std::string_view AssetType;
    Trinity::UUID Value;
    bool IsEntity = false;
};

// Every such slot of the entity, empty ones too. A script's asset field takes its type from its class while that is loaded. Find References In Scene and the missing references a scene opens with both go through it, so a component with a new slot that names an asset or an entity needs it here
void ForEachSlotReference(Trinity::Entity entity, const std::function<void(const SlotReference&)>& visit);

// The map and action a path names, Map/Action, or an action's name alone while only one map has it, as the running scene finds it. Nothing for a path the actions lack, or a name more than one map has
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> FindInputAction(const Trinity::InputActionsData& data, std::string_view path);

// An input action field of a script in the scene that a rename in the Input Actions panel changes: its entity, the script's index there, the field's name, and its path before and after. Added is for a field the entry left at its class's default, which an undo leaves at the default again
struct InputActionFieldRewrite
{
    Trinity::UUID Entity;
    std::size_t Script = 0;
    std::string Field;
    std::string Before;
    std::string After;
    bool Added = false;
};

// Each input action field of the scene, as its entry or its class's default has it, whose path led to an action before the rename and leads elsewhere or nowhere after it, rewritten to that action's path after, Map/Action. Before and after have the same maps and actions in the same places, renamed. A path that led nowhere before is left as it is
[[nodiscard]] std::vector<InputActionFieldRewrite> FindInputActionRewrites(Trinity::Scene& scene, const Trinity::InputActionsData& before, const Trinity::InputActionsData& after);

// The model's nodes under one root with the given name, each with its transform, and a MeshRenderer where it has a mesh. The root's subtree holds the nodes in the model's order
Trinity::Entity CreateModelEntities(Trinity::Scene& scene, const Trinity::ModelData& model, std::string_view name, Trinity::Entity parent);

class CreateEntityCommand final : public EditorCommand
{
public:
    CreateEntityCommand(std::string name, Trinity::UUID parent, Trinity::UUID before, glm::vec3 position);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    std::string m_Name;
    Trinity::UUID m_Parent;
    Trinity::UUID m_Before;
    glm::vec3 m_Position{ 0.0f };
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

// A light as one entity, named after its type and turned as Unity turns a new one: a directional light tilted down across the scene, and a spot light pointing straight down
class CreateLightCommand final : public EditorCommand
{
public:
    CreateLightCommand(Trinity::LightType type, Trinity::UUID parent, glm::vec3 position);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::LightType m_Type = Trinity::LightType::Directional;
    Trinity::UUID m_Parent;
    glm::vec3 m_Position{ 0.0f };
    Trinity::UUID m_Entity;
    std::string m_Text;
    std::string m_Label;
};

// A camera as one entity, perspective or orthographic, and the primary camera while the scene has none
class CreateCameraCommand final : public EditorCommand
{
public:
    CreateCameraCommand(Trinity::CameraProjection projection, Trinity::UUID parent, glm::vec3 position);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::CameraProjection m_Projection = Trinity::CameraProjection::Perspective;
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

// An entity and its subtree from the text Copy took, with new UUIDs, and the links inside the copy naming the copy, as Duplicate makes them. Last under the parent or among the roots, or before the sibling when one is given, and at the place in the world it was copied at when given one
class PasteEntityCommand final : public EditorCommand
{
public:
    PasteEntityCommand(std::string text, Trinity::UUID parent, Trinity::UUID before, std::optional<Trinity::TransformComponent> world);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    // What Copy took, and once pasted, the copy as it went in, which a redo puts back with the same UUIDs
    std::string m_Text;
    Trinity::UUID m_Parent;
    Trinity::UUID m_Before;
    std::optional<Trinity::TransformComponent> m_World;
    Trinity::UUID m_Entity;
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

// The component's fields set from scene text, as Reset and Paste Component put them, with the component added first when the entity is without it, so undo takes it off again. Refused, so nothing is recorded, when the component already holds the text
class SetComponentTextCommand final : public EditorCommand
{
public:
    SetComponentTextCommand(Trinity::UUID entity, std::string component, std::string text, std::string label);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Entity;
    std::string m_Component;
    std::string m_New;
    std::string m_Old;
    std::string m_Label;
    bool m_Added = false;
    bool m_Executed = false;
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

// An edit of an input actions asset, which the writer saves to its file and loads again as it runs and as it is undone, so a scene playing reads it at once. Edits of one field in a row become one, as a drag does. A rename takes the scene's input action fields that named what it renamed along, so they undo with it, and is then an edit of the scene too, which leaves the selection as it is
class SetInputActionsCommand final : public EditorCommand
{
public:
    using Writer = std::function<bool(Trinity::UUID, const Trinity::InputActionsData&)>;

    SetInputActionsCommand(Writer writer, Trinity::UUID asset, Trinity::InputActionsData before, Trinity::InputActionsData after, std::string field, std::string label, std::vector<InputActionFieldRewrite> rewrites = {});

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;
    [[nodiscard]] bool MergeWith(const EditorCommand& next) override;
    [[nodiscard]] bool ChangesScene() const override { return !m_Rewrites.empty(); }
    [[nodiscard]] bool SelectsSubject() const override { return false; }

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Asset; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Writer m_Writer;
    Trinity::UUID m_Asset;
    Trinity::InputActionsData m_Before;
    Trinity::InputActionsData m_After;
    std::string m_Field;
    std::string m_Label;
    std::vector<InputActionFieldRewrite> m_Rewrites;
};