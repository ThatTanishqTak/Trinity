#pragma once

#include "EditorSession.hpp"
#include "Panels/AudioMixerInspector.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

struct ImGuiInputTextCallbackData;

class PropertiesPanel final : public Panel
{
public:
    explicit PropertiesPanel(EditorSession& session);
    ~PropertiesPanel() override;

    // The question about import settings not applied, after every panel is drawn, shown or not
    void DrawPopups();
    // Brings the Console forward, as a stopped script's badge does when clicked
    void SetShowConsole(std::function<void()> showConsole) { m_ShowConsole = std::move(showConsole); }
    // Where an asset slot's Locate in Content Browser shows what the slot holds
    void SetLocateAsset(std::function<void(Trinity::UUID asset)> locate) { m_LocateAsset = std::move(locate); }

protected:
    void OnImGuiRender() override;

private:
    // Import settings edited and not applied, asked about once the asset is no longer shown
    struct UnappliedSettings
    {
        Trinity::UUID Asset;
        std::string Path;
        Trinity::AssetSettings Settings;
    };

    // A component as Copy Component took it, as scene text, kept until the project closes
    struct CopiedComponent
    {
        std::string Name;
        std::string Text;
    };

    [[nodiscard]] std::optional<UnappliedSettings> GetUnappliedSettings() const;
    void KeepUnappliedSettings(Trinity::UUID shown);
    void AskUnapplied(std::move_only_function<void()> then);

    void DrawName(Trinity::Entity entity);
    void DrawOverrides(Trinity::Entity entity);
    void CommitName(Trinity::Entity entity);
    void DrawTransform(Trinity::Entity entity);
    void DrawCamera(Trinity::Entity entity);
    void DrawSpriteRenderer(Trinity::Entity entity);
    void DrawMeshRenderer(Trinity::Entity entity);
    void DrawLight(Trinity::Entity entity);
    void DrawEnvironment(Trinity::Entity entity);
    void DrawRigidBody(Trinity::Entity entity);
    void DrawColliders(Trinity::Entity entity);
    void DrawCharacterController(Trinity::Entity entity);
    void DrawJoint(Trinity::Entity entity);
    void DrawRigidBody2D(Trinity::Entity entity);
    void DrawColliders2D(Trinity::Entity entity);
    void DrawCharacterController2D(Trinity::Entity entity);
    void DrawJoint2D(Trinity::Entity entity);
    void DrawAudioSource(Trinity::Entity entity);
    void DrawAudioListener(Trinity::Entity entity);
    void DrawUIDocument(Trinity::Entity entity);
    void DrawUIWorldSettings(Trinity::Entity entity);
    void DrawScripts(Trinity::Entity entity);
    void DrawScriptFields(Trinity::ScriptEntry& entry, std::string& change);
    [[nodiscard]] bool DrawScriptValue(const Trinity::ScriptFieldDescription& field, Trinity::ScriptFieldValue& value);
    [[nodiscard]] bool DrawFieldValue(const Trinity::ScriptFieldDescription& field, const char* label, Trinity::ScriptFieldValue& value, float reserve = 0.0f);
    [[nodiscard]] bool DrawListValue(const Trinity::ScriptFieldDescription& field, Trinity::ScriptFieldValue& value);
    [[nodiscard]] bool DrawInputActionSlot(Trinity::ScriptString& path);
    template<typename T>
    void DrawShapeSlot(Trinity::Entity entity, Trinity::CollisionShapeKind kind, const char* popup, const char* hint);
    void DrawAssetPicker(const char* popup, std::string_view assetType, Trinity::UUID current, const std::function<void(Trinity::UUID)>& pick);
    void DrawSlotLocate(const char* id, Trinity::UUID current, float width);
    void DrawAssetSlot(const char* label, const char* popup, std::string_view assetType, Trinity::UUID current, const std::function<void(Trinity::UUID)>& set, const char* hint, float reserve = 0.0f);
    void DrawOtherComponents(Trinity::Entity entity);
    void DrawAddComponent(Trinity::Entity entity);
    static int OnComponentFilterEdit(ImGuiInputTextCallbackData* data);

    void DrawAsset(Trinity::UUID id);
    void DrawTexturePreview();
    void DrawTextureSettings(const Trinity::AssetRecord& record);
    void DrawModelSettings(const Trinity::AssetRecord& record);
    void DrawAudioClip(const Trinity::AssetRecord& record);
    void DrawAudioClipSettings(const Trinity::AssetRecord& record);
    void DrawModelMaterials(const Trinity::AssetRecord& record);
    void DrawMaterial(const Trinity::AssetRecord& record);
    void DrawMaterialTextures(const Trinity::AssetRecord& record, const Trinity::MaterialData& current, const std::function<void(const char*, std::initializer_list<std::string_view>)>& label);

    [[nodiscard]] bool BeginComponent(Trinity::Entity entity, std::string_view name, const char* icon);
    void SetTexture(Trinity::Entity entity, Trinity::UUID texture);

    EditorSession& m_Session;
    std::uint64_t m_CloseListener = 0;
    Trinity::UUID m_LastSelection;
    Trinity::AssetRef<Trinity::TextureAsset> m_Preview;
    Trinity::UUID m_SettingsAsset;
    TextureImportSettings m_TextureSettings;
    ModelImportSettings m_ModelSettings;
    AudioClipImportSettings m_AudioClipSettings;
    std::optional<UnappliedSettings> m_Unapplied;
    // What waits for the question, such as Forge closing
    std::move_only_function<void()> m_AfterUnapplied;
    bool m_OpenUnappliedPopup = false;
    Trinity::AssetRef<Trinity::MaterialAsset> m_Material;
    // The Audio Mixer panel's group, with its effects, shown while the panel has one chosen
    AudioMixerInspector m_MixerInspector{ m_Session };
    // A model's material as imported, read again after each model import, to mark differing fields
    std::optional<Trinity::MaterialData> m_ImportedMaterial;
    std::uint64_t m_ImportedMaterialImports = UINT64_MAX;
    // A change from a component's menu, which waits until every section is drawn, as they read it
    Trinity::Scope<EditorCommand> m_PendingCommand;
    // The instance Unpack was clicked on, unpacked once the sections are drawn, as they read it
    Trinity::UUID m_PendingUnpack;
    std::optional<CopiedComponent> m_CopiedComponent;
    std::string m_NameText;
    Trinity::UUID m_NameEntity;
    bool m_NameActive = false;
    std::string m_AssetFilter;
    std::string m_ActionFilter;
    // Add Component's search, the highlighted choice, which Enter adds, and whether to scroll to it
    std::string m_ComponentFilter;
    int m_ComponentHighlight = 0;
    bool m_ScrollToHighlight = false;
    std::function<void()> m_ShowConsole;
    std::function<void(Trinity::UUID asset)> m_LocateAsset;
    Trinity::UUID m_EulerEntity;
    glm::quat m_EulerRotation = glm::identity<glm::quat>();
    glm::vec3 m_EulerDegrees{ 0.0f };
    // The list, by its ImGui ID, and the element last edited, so another element is its own command
    std::uint32_t m_EditedList = 0;
    std::size_t m_EditedElement = 0;
};