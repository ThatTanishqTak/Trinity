#pragma once

#include "EditorSession.hpp"
#include "Panels/Panel.hpp"

#include <Trinity.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
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
    void DrawTextureSlot(Trinity::Entity entity);
    void DrawTexturePicker(Trinity::Entity entity);
    void DrawOtherComponents(Trinity::Entity entity);
    void DrawAddComponent(Trinity::Entity entity);

    void DrawAsset(Trinity::UUID id);
    void DrawTexturePreview();
    void DrawTextureSettings(const Trinity::AssetRecord& record);
    void DrawModelSettings(const Trinity::AssetRecord& record);

    [[nodiscard]] bool BeginComponent(std::string_view name, const char* icon, bool removable);
    void SetTexture(Trinity::Entity entity, Trinity::UUID texture);

    EditorSession& m_Session;
    std::uint64_t m_CloseListener = 0;
    Trinity::UUID m_LastSelection;
    Trinity::AssetRef<Trinity::TextureAsset> m_Preview;
    Trinity::UUID m_SettingsAsset;
    TextureImportSettings m_TextureSettings;
    ModelImportSettings m_ModelSettings;
    std::string m_PendingRemoval;
    std::string m_NameText;
    Trinity::UUID m_NameEntity;
    bool m_NameActive = false;
    std::string m_TextureFilter;
    Trinity::UUID m_EulerEntity;
    glm::quat m_EulerRotation = glm::identity<glm::quat>();
    glm::vec3 m_EulerDegrees{ 0.0f };
};