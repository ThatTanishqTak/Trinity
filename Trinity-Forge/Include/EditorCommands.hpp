#pragma once

#include "CommandStack.hpp"

#include <Trinity.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

[[nodiscard]] std::string GetEntityLabel(Trinity::Entity entity);
[[nodiscard]] std::string_view GetComponentLabel(std::string_view name);
[[nodiscard]] std::string_view GetScriptLanguageLabel(Trinity::ScriptLanguage language);
// Every type of light, in the order of Unity's Light menu, and a new one's name
inline constexpr std::array<Trinity::LightType, 3> c_LightTypes{ Trinity::LightType::Directional, Trinity::LightType::Point, Trinity::LightType::Spot };
[[nodiscard]] std::string_view GetLightLabel(Trinity::LightType type);
// The script's class name after its namespace, with its language, such as Player (C#)
[[nodiscard]] std::string GetScriptLabel(const Trinity::ScriptEntry& entry);
[[nodiscard]] std::size_t GetScriptMemorySize(const Trinity::ScriptComponent& component);

// What an AudioSource's Resource slot takes, as Unity 6's Audio Resource: a clip or a Sound Event
inline constexpr std::string_view c_AudioResourceType = "Audio Resource";

// A slot of a component that names an asset or another entity, as Properties edits it
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

// Every such slot, empty ones too. A component with a new asset or entity slot needs adding here
void ForEachSlotReference(Trinity::Entity entity, const std::function<void(const SlotReference&)>& visit);

// Map/Action, or a name alone while only one map has it. Nothing for an unknown or ambiguous path
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> FindInputAction(const Trinity::InputActionsData& data, std::string_view path);

// A script's input action field that a rename changes. Added marks one left at its class's default
struct InputActionFieldRewrite
{
    Trinity::UUID Entity;
    std::size_t Script = 0;
    std::string Field;
    Trinity::ScriptFieldValue Before;
    Trinity::ScriptFieldValue After;
    bool Added = false;
};

// Each field path the rename moves, rewritten to its action's new path. Dead paths are left alone
[[nodiscard]] std::vector<InputActionFieldRewrite> FindInputActionRewrites(Trinity::Scene& scene, const Trinity::InputActionsData& before, const Trinity::InputActionsData& after);

// Whether the entity stands in for an instance whose prefab is missing, kept as the file had it
[[nodiscard]] bool IsMissingPrefab(Trinity::Entity entity);
// Whether the entity is one of a prefab's entities in an instance, other than its root
[[nodiscard]] bool IsPrefabPart(Trinity::Entity entity);

// The root of the innermost instance the entity is part of, and that instance's prefab, or null
struct NearestInstance
{
    Trinity::Entity Root;
    Trinity::UUID Prefab;
};

[[nodiscard]] NearestInstance FindNearestInstance(Trinity::Entity entity);
// Whether the change is the instance root's Position or Rotation, which are never marked overrides
[[nodiscard]] bool IsDefaultOverride(Trinity::Entity root, const Trinity::SceneSerializer::PrefabOverride& change);
// An entity added under an instance's entity goes after the prefab's own children there
[[nodiscard]] Trinity::UUID FindAddedPlace(Trinity::Scene& scene, Trinity::UUID before);
// Why nothing can go under the parent, or nothing when it can. No parent is the roots
[[nodiscard]] std::optional<std::string> FindChildProblem(Trinity::Scene& scene, Trinity::UUID parent);
// Why the entity cannot move under the parent, or nothing when it can
[[nodiscard]] std::optional<std::string> FindMoveProblem(Trinity::Scene& scene, Trinity::UUID entity, Trinity::UUID parent);
// Why the entity and its subtree cannot become a prefab, or nothing when they can
[[nodiscard]] std::optional<std::string> FindPrefabProblem(Trinity::Entity entity);
// Why the entity and its subtree cannot be copied or duplicated, or nothing when they can
[[nodiscard]] std::optional<std::string> FindCopyProblem(Trinity::Entity entity);
// The file name for an entity's prefab, without extension: bad characters made underscores
[[nodiscard]] std::string GetPrefabFileName(std::string_view name);

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

// As SetPhysicsSettingsCommand, for what the project exports, which the Build panel edits
class SetExportSettingsCommand final : public EditorCommand
{
public:
    using Writer = std::function<bool(const Trinity::ExportSettings&)>;

    SetExportSettingsCommand(Writer writer, Trinity::ExportSettings before, Trinity::ExportSettings after, std::string field);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;
    [[nodiscard]] bool MergeWith(const EditorCommand& next) override;
    [[nodiscard]] bool ChangesScene() const override { return false; }

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return {}; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Writer m_Writer;
    Trinity::ExportSettings m_Before;
    Trinity::ExportSettings m_After;
    std::string m_Field;
    std::string m_Label;
};

class CreateSpriteCommand final : public EditorCommand
{
public:
    CreateSpriteCommand(std::string name, Trinity::UUID texture, Trinity::UUID parent, glm::vec3 position, glm::vec2 size);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    std::string m_Name;
    Trinity::UUID m_Texture;
    Trinity::UUID m_Parent;
    glm::vec3 m_Position{ 0.0f };
    glm::vec2 m_Size{ 1.0f };
    Trinity::UUID m_Entity;
    std::string m_Text;
    std::string m_Label;
};

// An entity with an AudioSource that plays the clip or event as it wakes, made in one step
class CreateAudioSourceCommand final : public EditorCommand
{
public:
    CreateAudioSourceCommand(std::string name, Trinity::UUID resource, Trinity::UUID parent, Trinity::UUID before, glm::vec3 position, bool spatialize);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    std::string m_Name;
    Trinity::UUID m_Resource;
    Trinity::UUID m_Parent;
    Trinity::UUID m_Before;
    glm::vec3 m_Position{ 0.0f };
    bool m_Spatialize = false;
    Trinity::UUID m_Entity;
    std::string m_Text;
    std::string m_Label;
};

// A new instance of the prefab, its root at the position if given. Last, or before the sibling
class InstantiatePrefabCommand final : public EditorCommand
{
public:
    InstantiatePrefabCommand(Trinity::UUID prefab, std::string name, Trinity::UUID parent, Trinity::UUID before, std::optional<glm::vec3> position);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Prefab;
    std::string m_Name;
    Trinity::UUID m_Parent;
    Trinity::UUID m_Before;
    std::optional<glm::vec3> m_Position;
    Trinity::UUID m_Entity;
    std::string m_Text;
    std::string m_Label;
};

// The prefab and instance an entity's root component names, or none for no instance's root
struct PrefabLink
{
    Trinity::UUID Entity;
    std::optional<Trinity::PrefabInstanceComponent> Instance;
};

// Links entities to prefabs as given, keeping every UUID. Undo links each as it was
class LinkPrefabCommand final : public EditorCommand
{
public:
    LinkPrefabCommand(std::vector<PrefabLink> links, Trinity::UUID subject, std::string label);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Subject; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    std::vector<PrefabLink> m_Links;
    std::vector<PrefabLink> m_Before;
    Trinity::UUID m_Subject;
    std::string m_Label;
};

// Kept as the instance's text before and after, so undo and redo keep the same UUIDs
class RevertPrefabCommand final : public EditorCommand
{
public:
    RevertPrefabCommand(Trinity::UUID root, std::vector<Trinity::SceneSerializer::PrefabOverride> overrides, Trinity::UUID subject, std::string label);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Subject; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Root;
    std::vector<Trinity::SceneSerializer::PrefabOverride> m_Overrides;
    Trinity::UUID m_Subject;
    std::string m_Label;
    Trinity::SceneSerializer::PrefabInstanceText m_Before;
    Trinity::SceneSerializer::PrefabInstanceText m_After;
};

// Writes the prefab's file at the level, 0 the instance's own, then remakes every instance of it
class ApplyPrefabCommand final : public EditorCommand
{
public:
    using Writer = std::function<bool(std::span<const Trinity::SceneSerializer::PrefabText>, const Trinity::Scene&)>;

    // The old texts are each level's down to the one applied to, as their files held them
    ApplyPrefabCommand(Writer writer, Trinity::UUID root, std::vector<Trinity::SceneSerializer::PrefabOverride> overrides, std::size_t level, std::vector<Trinity::SceneSerializer::PrefabText> oldTexts, Trinity::UUID subject, std::string label);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Subject; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Writer m_Writer;
    Trinity::UUID m_Root;
    std::vector<Trinity::SceneSerializer::PrefabOverride> m_Overrides;
    std::size_t m_Level = 0;
    std::vector<Trinity::SceneSerializer::PrefabText> m_OldTexts;
    Trinity::UUID m_Subject;
    std::string m_Label;
    std::vector<Trinity::SceneSerializer::PrefabText> m_NewTexts;
    std::vector<Trinity::SceneSerializer::PrefabInstanceText> m_Before;
    std::vector<Trinity::SceneSerializer::PrefabInstanceText> m_After;
};

// A built-in shape as one entity, with its MeshRenderer and the collider that fits it
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

// Named after its type and turned as Unity turns a new one: directional tilted, spot straight down
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

// A camera as one entity. It is the primary camera while the scene has none
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

// From the text Copy took, with new UUIDs, links inside the copy naming the copy, as Duplicate
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
    // What Copy took. Once pasted, the copy as it went in, so a redo keeps the same UUIDs
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

// As Reset and Paste Component set them. Adds the component if missing. Refused if nothing changes
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

// Adds a Script component first when the entity is without one, so the first script is one step
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

// Named by component and field, so the edit still undoes after the module reloads. Edits merge
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

// The field names what was edited, and edits of one field in a row become one
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


// The writer saves the project as it runs and as it is undone. Edits of one field in a row merge
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

// As SetPhysicsSettingsCommand, for the project's audio settings
class SetAudioSettingsCommand final : public EditorCommand
{
public:
    using Writer = std::function<bool(const Trinity::AudioSettings&)>;

    SetAudioSettingsCommand(Writer writer, Trinity::AudioSettings before, Trinity::AudioSettings after, std::string field);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;
    [[nodiscard]] bool MergeWith(const EditorCommand& next) override;
    [[nodiscard]] bool ChangesScene() const override { return false; }

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return {}; }
    [[nodiscard]] std::size_t GetMemorySize() const override { return sizeof(*this) + m_Field.capacity() + m_Label.capacity(); }

private:
    Writer m_Writer;
    Trinity::AudioSettings m_Before;
    Trinity::AudioSettings m_After;
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

// Saved and reloaded as it runs and undoes. A rename takes the scene's fields that named it along
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

// Saved and reloaded as it runs and undoes, so the mix is heard as edited at once. Edits merge
class SetAudioMixerCommand final : public EditorCommand
{
public:
    using Writer = std::function<bool(Trinity::UUID, const Trinity::AudioMixerData&)>;

    SetAudioMixerCommand(Writer writer, Trinity::UUID asset, Trinity::AudioMixerData before, Trinity::AudioMixerData after, std::string field, std::string label);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;
    [[nodiscard]] bool MergeWith(const EditorCommand& next) override;
    [[nodiscard]] bool ChangesScene() const override { return false; }

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Asset; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Writer m_Writer;
    Trinity::UUID m_Asset;
    Trinity::AudioMixerData m_Before;
    Trinity::AudioMixerData m_After;
    std::string m_Field;
    std::string m_Label;
};

// Saved and reloaded as it runs and undoes. An instance playing moves to it as containers trigger
class SetSoundEventCommand final : public EditorCommand
{
public:
    using Writer = std::function<bool(Trinity::UUID, const Trinity::SoundEventData&)>;

    SetSoundEventCommand(Writer writer, Trinity::UUID asset, Trinity::SoundEventData before, Trinity::SoundEventData after, std::string field, std::string label);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;
    [[nodiscard]] bool MergeWith(const EditorCommand& next) override;
    [[nodiscard]] bool ChangesScene() const override { return false; }

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Asset; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Writer m_Writer;
    Trinity::UUID m_Asset;
    Trinity::SoundEventData m_Before;
    Trinity::SoundEventData m_After;
    std::string m_Field;
    std::string m_Label;
};