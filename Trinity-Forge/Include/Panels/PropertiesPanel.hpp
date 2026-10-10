#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

class PropertiesPanel final : public Panel
{
public:
    explicit PropertiesPanel(EditorSession& session);
    ~PropertiesPanel() override;

protected:
    void OnImGuiRender() override;

private:
    void DrawName(Trinity::Entity entity);
    void CommitName(Trinity::Entity entity);
    void DrawTransform(Trinity::Entity entity);
    void DrawCamera(Trinity::Entity entity);
    void DrawSpriteRenderer(Trinity::Entity entity);
    void DrawMeshRenderer(Trinity::Entity entity);
    void DrawLight(Trinity::Entity entity);
    void DrawTextureSlot(Trinity::Entity entity);
    void DrawEnvironment(Trinity::Entity entity);
    void DrawRigidBody(Trinity::Entity entity);
    void DrawColliders(Trinity::Entity entity);
    template<typename T>
    void DrawShapeSlot(Trinity::Entity entity, Trinity::CollisionShapeKind kind, const char* popup, const char* hint);
    void DrawAssetPicker(const char* popup, std::string_view assetType, Trinity::UUID current, const std::function<void(Trinity::UUID)>& pick);
    void DrawAssetSlot(const char* label, const char* popup, std::string_view assetType, Trinity::UUID current, const std::function<void(Trinity::UUID)>& set, const char* hint);
    void DrawOtherComponents(Trinity::Entity entity);
    void DrawAddComponent(Trinity::Entity entity);

    void DrawAsset(Trinity::UUID id);
    void DrawTexturePreview();
    void DrawTextureSettings(const Trinity::AssetRecord& record);
    void DrawModelSettings(const Trinity::AssetRecord& record);
    void DrawModelMaterials(const Trinity::AssetRecord& record);
    void DrawMaterial(const Trinity::AssetRecord& record);
    void DrawMaterialTextures(const Trinity::AssetRecord& record, const Trinity::MaterialData& current, const std::function<void(const char*, std::initializer_list<std::string_view>)>& label);

    [[nodiscard]] bool BeginComponent(std::string_view name, const char* icon, bool removable);
    void SetTexture(Trinity::Entity entity, Trinity::UUID texture);

    EditorSession& m_Session;
    std::uint64_t m_CloseListener = 0;
    Trinity::UUID m_LastSelection;
    Trinity::AssetRef<Trinity::TextureAsset> m_Preview;
    Trinity::UUID m_SettingsAsset;
    TextureImportSettings m_TextureSettings;
    ModelImportSettings m_ModelSettings;
    Trinity::AssetRef<Trinity::MaterialAsset> m_Material;
    // A model's material as imported, read again after each model import, which the fields that differ from are marked against
    std::optional<Trinity::MaterialData> m_ImportedMaterial;
    std::uint64_t m_ImportedMaterialImports = UINT64_MAX;
    std::string m_PendingRemoval;
    std::string m_NameText;
    Trinity::UUID m_NameEntity;
    bool m_NameActive = false;
    std::string m_AssetFilter;
    Trinity::UUID m_EulerEntity;
    glm::quat m_EulerRotation = glm::identity<glm::quat>();
    glm::vec3 m_EulerDegrees{ 0.0f };
};