#include "Panels/PropertiesPanel.hpp"

#include "EditorCommands.hpp"
#include "EditorPayloads.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <format>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr float c_LabelWidthInFonts = 8.0f;
    constexpr const char* c_TexturePickerPopup = "##TexturePicker";
    constexpr const char* c_EnvironmentPickerPopup = "##EnvironmentPicker";
    constexpr const char* c_AddComponentPopup = "##AddComponent";

    // Components with an editor of their own here. Transform is never removed, and Tag is the name at the top
    constexpr std::array<std::string_view, 6> c_EditedComponents{ Trinity::TagComponent::c_TypeName, Trinity::TransformComponent::c_TypeName, Trinity::CameraComponent::c_TypeName, Trinity::SpriteRendererComponent::c_TypeName, Trinity::LightComponent::c_TypeName, Trinity::EnvironmentComponent::c_TypeName };

    // A label on the left and the widget filling the rest of the row
    void Label(const char* label)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(ImGui::GetFontSize() * c_LabelWidthInFonts);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // The widget edits a copy, and a change becomes a command. While a widget stays active, as during a drag, its commands merge into one
    template<Trinity::Component T, typename F>
    void EditField(CommandStack& history, Trinity::Entity entity, std::string_view field, F widget)
    {
        T l_Value = entity.Get<T>();
        if (widget(l_Value))
        {
            history.Execute(Trinity::CreateScope<SetComponentCommand<T>>(entity.GetUUID(), std::move(l_Value), std::string(field)));
        }
    }

    std::string GetAssetName(const Trinity::AssetRecord& record)
    {
        return std::filesystem::path(record.Path).filename().string();
    }
}

PropertiesPanel::PropertiesPanel(EditorSession& session) : Panel("Properties", Trinity::Icons::c_Sliders, DockSlot::Right), m_Session(session)
{
    m_CloseListener = m_Session.AddCloseListener([this]
    {
        m_Preview = {};
        m_Material = {};
        m_SettingsAsset = {};
    });
}

PropertiesPanel::~PropertiesPanel()
{
    m_Session.RemoveCloseListener(m_CloseListener);
}

// The selected entity's components, each in its own section, under IDs of that entity's own. A removal waits until every section is drawn, since the sections read the components
void PropertiesPanel::OnImGuiRender()
{
    Trinity::Scene& l_Scene = m_Session.GetScene();
    const Trinity::Entity l_Entity = l_Scene.FindEntityByUUID(m_Session.GetSelection());

    // An entity selected since an asset was, here or by an undo, takes Properties back from the asset
    if (m_Session.GetSelection() != m_LastSelection)
    {
        m_LastSelection = m_Session.GetSelection();
        if (m_LastSelection.IsValid())
        {
            m_Session.SetInspectedAsset({});
        }
    }

    // A name still being typed when the selection moved on is kept for the entity it was typed for
    if (m_NameActive && (!l_Entity || l_Entity.GetUUID() != m_NameEntity || m_Session.GetInspectedAsset()))
    {
        CommitName(l_Scene.FindEntityByUUID(m_NameEntity));
        m_NameActive = false;
    }

    if (const Trinity::UUID l_Asset = m_Session.GetInspectedAsset())
    {
        ImGui::PushID(l_Asset.ToString().c_str());
        DrawAsset(l_Asset);
        ImGui::PopID();

        return;
    }

    m_Preview = {};
    m_Material = {};
    m_SettingsAsset = {};

    if (!l_Entity)
    {
        ImGui::TextDisabled("Select an entity in the Hierarchy or the Viewport");

        return;
    }

    ImGui::PushID(l_Entity.GetUUID().ToString().c_str());
    DrawName(l_Entity);
    DrawTransform(l_Entity);
    DrawCamera(l_Entity);
    DrawSpriteRenderer(l_Entity);
    DrawLight(l_Entity);
    DrawEnvironment(l_Entity);
    DrawOtherComponents(l_Entity);
    DrawAddComponent(l_Entity);
    ImGui::PopID();

    if (!m_PendingRemoval.empty())
    {
        m_Session.GetHistory().Execute(Trinity::CreateScope<RemoveComponentCommand>(l_Entity.GetUUID(), std::exchange(m_PendingRemoval, {})));
    }

    // Nothing is being dragged or typed into, so the next change is a command of its own
    if (!ImGui::IsAnyItemActive())
    {
        m_Session.GetHistory().EndMerge();
    }
}

// The name follows the Tag while it is not being typed into, and becomes one command when the field is left, however it is left. Escape puts the text back, so nothing changes
void PropertiesPanel::DrawName(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::TagComponent>())
    {
        ImGui::TextDisabled("%s", std::format("{} No name, since the entity has no Tag", Trinity::Icons::c_CubeOutline).c_str());
    }
    else
    {
        if (!m_NameActive)
        {
            m_NameText = std::string(std::string_view(entity.Get<Trinity::TagComponent>().Tag));
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Trinity::Icons::c_CubeOutline);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##Name", &m_NameText);
        m_NameActive = ImGui::IsItemActive();
        m_NameEntity = entity.GetUUID();
        if (ImGui::IsItemDeactivated())
        {
            CommitName(entity);
        }
    }

    ImGui::TextDisabled("UUID %s", entity.GetUUID().ToString().c_str());
    ImGui::Spacing();
}

void PropertiesPanel::CommitName(Trinity::Entity entity)
{
    if (!entity || !entity.Has<Trinity::TagComponent>() || std::string_view(entity.Get<Trinity::TagComponent>().Tag) == m_NameText)
    {
        return;
    }

    Trinity::TagComponent l_Value = entity.Get<Trinity::TagComponent>();
    l_Value.Tag = m_NameText;

    // The command selects what it renamed, which is not what was clicked when the field was left by selecting something else
    const Trinity::UUID l_Selection = m_Session.GetSelection();
    CommandStack& l_History = m_Session.GetHistory();
    l_History.Execute(Trinity::CreateScope<SetComponentCommand<Trinity::TagComponent>>(entity.GetUUID(), std::move(l_Value), "Name"));
    l_History.EndMerge();
    m_Session.SetSelection(l_Selection);
}

// Rotation shows as X, Y and Z degrees, kept while the entity stays selected and its rotation is only changed here, so a drag never jumps between equivalent angles
void PropertiesPanel::DrawTransform(Trinity::Entity entity)
{
    if (!BeginComponent(Trinity::TransformComponent::c_TypeName, Trinity::Icons::c_WindowRestore, false))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();
    const Trinity::TransformComponent& l_Transform = entity.Get<Trinity::TransformComponent>();
    if (m_EulerEntity != entity.GetUUID() || m_EulerRotation != l_Transform.Rotation)
    {
        m_EulerEntity = entity.GetUUID();
        m_EulerRotation = l_Transform.Rotation;
        m_EulerDegrees = glm::degrees(glm::eulerAngles(l_Transform.Rotation));
    }

    Label("Position");
    EditField<Trinity::TransformComponent>(l_History, entity, "Position", [](Trinity::TransformComponent& transform) { return ImGui::DragFloat3("##Position", &transform.Position.x, 0.05f, 0.0f, 0.0f, "%.3f"); });

    Label("Rotation");
    glm::vec3 l_Degrees = m_EulerDegrees;
    if (ImGui::DragFloat3("##Rotation", &l_Degrees.x, 0.5f, 0.0f, 0.0f, "%.2f\xC2\xB0"))
    {
        Trinity::TransformComponent l_Value = l_Transform;
        l_Value.Rotation = glm::quat(glm::radians(l_Degrees));
        m_EulerDegrees = l_Degrees;
        m_EulerRotation = l_Value.Rotation;
        l_History.Execute(Trinity::CreateScope<SetComponentCommand<Trinity::TransformComponent>>(entity.GetUUID(), std::move(l_Value), "Rotation"));
    }

    Label("Scale");
    EditField<Trinity::TransformComponent>(l_History, entity, "Scale", [](Trinity::TransformComponent& transform) { return ImGui::DragFloat3("##Scale", &transform.Scale.x, 0.01f, 0.0f, 0.0f, "%.3f"); });
}

void PropertiesPanel::DrawCamera(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::CameraComponent>() || !BeginComponent(Trinity::CameraComponent::c_TypeName, Trinity::Icons::c_Monitor, true))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();

    Label("Projection");
    EditField<Trinity::CameraComponent>(l_History, entity, "Projection", [](Trinity::CameraComponent& camera)
    {
        int l_Projection = static_cast<int>(camera.Projection);
        const bool l_Changed = ImGui::Combo("##Projection", &l_Projection, "Orthographic\0Perspective\0");
        camera.Projection = static_cast<Trinity::CameraProjection>(l_Projection);

        return l_Changed;
    });

    if (entity.Get<Trinity::CameraComponent>().Projection == Trinity::CameraProjection::Perspective)
    {
        Label("Field of View");
        EditField<Trinity::CameraComponent>(l_History, entity, "FieldOfView", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##FieldOfView", &camera.FieldOfView, 0.25f, 1.0f, 179.0f, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp); });

        Label("Near");
        EditField<Trinity::CameraComponent>(l_History, entity, "PerspectiveNear", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##PerspectiveNear", &camera.PerspectiveNear, 0.001f, 0.0001f, 1000.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });
    }
    else
    {
        Label("Size");
        EditField<Trinity::CameraComponent>(l_History, entity, "OrthographicSize", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##Size", &camera.OrthographicSize, 0.05f, 0.01f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

        Label("Near");
        EditField<Trinity::CameraComponent>(l_History, entity, "Near", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##Near", &camera.Near, 0.05f, 0.0f, 0.0f, "%.3f"); });

        Label("Far");
        EditField<Trinity::CameraComponent>(l_History, entity, "Far", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##Far", &camera.Far, 0.05f, 0.0f, 0.0f, "%.3f"); });
    }

    Label("Primary");
    EditField<Trinity::CameraComponent>(l_History, entity, "Primary", [](Trinity::CameraComponent& camera) { return ImGui::Checkbox("##Primary", &camera.Primary); });

    Label("Exposure");
    EditField<Trinity::CameraComponent>(l_History, entity, "ExposureEV100", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##Exposure", &camera.ExposureEV100, 0.05f, -10.0f, 24.0f, "%.2f EV100", ImGuiSliderFlags_AlwaysClamp); });

    Label("Tonemapper");
    EditField<Trinity::CameraComponent>(l_History, entity, "Tonemap", [](Trinity::CameraComponent& camera)
    {
        constexpr std::array<std::pair<Trinity::Tonemapper, const char*>, 2> c_Tonemappers{ { { Trinity::Tonemapper::None, "None" }, { Trinity::Tonemapper::PBRNeutral, "PBR Neutral" } } };

        bool l_Changed = false;
        const auto a_Current = std::ranges::find(c_Tonemappers, camera.Tonemap, &std::pair<Trinity::Tonemapper, const char*>::first);
        if (ImGui::BeginCombo("##Tonemapper", a_Current != c_Tonemappers.end() ? a_Current->second : "?"))
        {
            for (const auto& [it_Tonemapper, it_Name] : c_Tonemappers)
            {
                if (ImGui::Selectable(it_Name, it_Tonemapper == camera.Tonemap) && it_Tonemapper != camera.Tonemap)
                {
                    camera.Tonemap = it_Tonemapper;
                    l_Changed = true;
                }
            }

            ImGui::EndCombo();
        }

        return l_Changed;
    });

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Exposure 1 is EV100 %.3f. PBR Neutral matches glTF Sample Viewer, and None only clamps, which keeps unlit 2D colours as they are", Trinity::c_NeutralEV100);
    }
}

// Directional, point or spot, in glTF's units: lux for a directional light, candela for the others. Directional and spot lights shine along the entity's -Z, so turning the entity aims them, and can cast shadows. Colour is edited as it looks, in sRGB, and kept linear. A range of 0 is worked out from the intensity, and the cone's inner angle stays inside its outer one
void PropertiesPanel::DrawLight(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::LightComponent>() || !BeginComponent(Trinity::LightComponent::c_TypeName, Trinity::Icons::c_Globe, true))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();

    Label("Type");
    EditField<Trinity::LightComponent>(l_History, entity, "Type", [](Trinity::LightComponent& light)
    {
        int l_Type = static_cast<int>(light.Type);
        const bool l_Changed = ImGui::Combo("##Type", &l_Type, "Directional\0Point\0Spot\0");
        light.Type = static_cast<Trinity::LightType>(l_Type);

        return l_Changed;
    });

    Label("Color");
    EditField<Trinity::LightComponent>(l_History, entity, "Color", [](Trinity::LightComponent& light)
    {
        glm::vec3 l_Color(Trinity::LinearToSrgb(light.Color.r), Trinity::LinearToSrgb(light.Color.g), Trinity::LinearToSrgb(light.Color.b));
        const bool l_Changed = ImGui::ColorEdit3("##Color", &l_Color.x);
        light.Color = glm::vec3(Trinity::SrgbToLinear(l_Color.r), Trinity::SrgbToLinear(l_Color.g), Trinity::SrgbToLinear(l_Color.b));

        return l_Changed;
    });

    const Trinity::LightComponent& l_Light = entity.Get<Trinity::LightComponent>();
    const bool l_Directional = l_Light.Type == Trinity::LightType::Directional;
    Label("Intensity");
    EditField<Trinity::LightComponent>(l_History, entity, "Intensity", [l_Directional](Trinity::LightComponent& light) { return ImGui::DragFloat("##Intensity", &light.Intensity, 0.05f, 0.0f, 200000.0f, l_Directional ? "%.3f lux" : "%.3f cd", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });

    if (l_Light.Type != Trinity::LightType::Point)
    {
        Label("Cast Shadows");
        EditField<Trinity::LightComponent>(l_History, entity, "CastShadows", [](Trinity::LightComponent& light) { return ImGui::Checkbox("##CastShadows", &light.CastShadows); });
        if (ImGui::IsItemHovered() && l_Directional)
        {
            ImGui::SetTooltip("The first directional light that casts shadows is the sun, whose shadows reach %.0f m from the camera", static_cast<double>(Trinity::ShadowAtlas::c_Distance));
        }
        else if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("The first %u spot lights in view that cast shadows, in hierarchy order, each get a shadow map", Trinity::ShadowAtlas::c_MaxSpotShadows);
        }
    }

    if (l_Directional)
    {
        return;
    }

    Label("Range");
    EditField<Trinity::LightComponent>(l_History, entity, "Range", [](Trinity::LightComponent& light)
    {
        const std::string l_Format = light.Range > 0.0f ? std::string("%.3f m") : std::format("Automatic, {:.2f} m", light.GetRange());

        return ImGui::DragFloat("##Range", &light.Range, 0.05f, 0.0f, 100000.0f, l_Format.c_str(), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
    });

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Where the light has faded to nothing. At 0 it is where the light falls to %g lux on a surface facing it", Trinity::LightComponent::c_AutomaticRangeCutoff);
    }

    if (l_Light.Type != Trinity::LightType::Spot)
    {
        return;
    }

    Label("Inner Angle");
    EditField<Trinity::LightComponent>(l_History, entity, "InnerConeAngle", [](Trinity::LightComponent& light) { return ImGui::DragFloat("##InnerConeAngle", &light.InnerConeAngle, 0.25f, 0.0f, light.OuterConeAngle, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp); });

    Label("Outer Angle");
    EditField<Trinity::LightComponent>(l_History, entity, "OuterConeAngle", [](Trinity::LightComponent& light)
    {
        const bool l_Changed = ImGui::DragFloat("##OuterConeAngle", &light.OuterConeAngle, 0.25f, 0.1f, 90.0f, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp);
        light.InnerConeAngle = std::min(light.InnerConeAngle, light.OuterConeAngle);

        return l_Changed;
    });
}

// Image-based lighting from an HDR environment in the project, which takes one dropped from the Content Browser. Rotation turns it about +Y, and 90 degrees lights a model as glTF Sample Viewer does by default
void PropertiesPanel::DrawEnvironment(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::EnvironmentComponent>() || !BeginComponent(Trinity::EnvironmentComponent::c_TypeName, Trinity::Icons::c_Globe, true))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();
    const auto a_SetEnvironment = [&l_History, entity](Trinity::UUID environment)
    {
        Trinity::EnvironmentComponent l_Value = entity.Get<Trinity::EnvironmentComponent>();
        l_Value.Environment = environment;
        l_History.Execute(Trinity::CreateScope<SetComponentCommand<Trinity::EnvironmentComponent>>(entity.GetUUID(), std::move(l_Value), "Environment"));
        l_History.EndMerge();
    };

    const Trinity::UUID l_Environment = entity.Get<Trinity::EnvironmentComponent>().Environment;
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = l_Registry != nullptr && l_Environment ? l_Registry->Find(l_Environment) : nullptr;
    const std::string l_Name = !l_Environment ? std::string("None") : l_Record != nullptr ? GetAssetName(*l_Record) : std::format("Missing {}", l_Environment);

    Label("Environment");
    const float l_ClearWidth = ImGui::GetFrameHeight();
    const float l_Width = std::max(ImGui::GetContentRegionAvail().x - l_ClearWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f);
    if (ImGui::Button(std::format("{}###Environment", l_Name).c_str(), ImVec2(l_Width, 0.0f)))
    {
        m_AssetFilter.clear();
        ImGui::OpenPopup(c_EnvironmentPickerPopup);
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", l_Record != nullptr ? l_Record->Path.c_str() : "An equirectangular .hdr in the project. Without one the scene keeps its ambient light");
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_AssetPayload))
        {
            std::uint64_t l_Value = 0;
            std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));
            const Trinity::AssetRecord* l_Dropped = l_Registry != nullptr ? l_Registry->Find(Trinity::UUID(l_Value)) : nullptr;
            if (l_Dropped != nullptr && l_Dropped->Importer == Trinity::EnvironmentAsset::c_AssetType)
            {
                a_SetEnvironment(l_Dropped->ID);
            }
            else
            {
                TR_WARN("Forge: only an environment, an .hdr file, can go in an Environment's slot");
            }
        }

        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!l_Environment);
    if (ImGui::Button(std::format("{}##ClearEnvironment", Trinity::Icons::c_TimesCircle).c_str(), ImVec2(l_ClearWidth, 0.0f)))
    {
        a_SetEnvironment({});
    }

    ImGui::EndDisabled();

    DrawAssetPicker(c_EnvironmentPickerPopup, Trinity::EnvironmentAsset::c_AssetType, l_Environment, a_SetEnvironment);

    Label("Intensity");
    EditField<Trinity::EnvironmentComponent>(l_History, entity, "Intensity", [](Trinity::EnvironmentComponent& environment) { return ImGui::DragFloat("##Intensity", &environment.Intensity, 0.01f, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });

    Label("Rotation");
    EditField<Trinity::EnvironmentComponent>(l_History, entity, "Rotation", [](Trinity::EnvironmentComponent& environment) { return ImGui::DragFloat("##Rotation", &environment.Rotation, 0.5f, -360.0f, 360.0f, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp); });
}

void PropertiesPanel::DrawSpriteRenderer(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::SpriteRendererComponent>() || !BeginComponent(Trinity::SpriteRendererComponent::c_TypeName, Trinity::Icons::c_File, true))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();

    DrawTextureSlot(entity);

    Label("Tint");
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "Tint", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::ColorEdit4("##Tint", &sprite.Tint.x, ImGuiColorEditFlags_AlphaPreviewHalf); });

    Label("Flip");
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "FlipX", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::Checkbox("X##FlipX", &sprite.FlipX); });
    ImGui::SameLine();
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "FlipY", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::Checkbox("Y##FlipY", &sprite.FlipY); });

    Label("UV Rect");
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "UVRect", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::DragFloat4("##UVRect", &sprite.UVRect.x, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    Label("Sorting Layer");
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "SortingLayer", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::DragInt("##SortingLayer", &sprite.SortingLayer, 0.1f); });

    Label("Order in Layer");
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "OrderInLayer", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::DragInt("##OrderInLayer", &sprite.OrderInLayer, 0.1f); });
}

// Takes a texture dropped from elsewhere in Forge, or one picked from the project's textures, and the cross clears it
void PropertiesPanel::DrawTextureSlot(Trinity::Entity entity)
{
    const Trinity::UUID l_Texture = entity.Get<Trinity::SpriteRendererComponent>().Texture;
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = l_Registry != nullptr && l_Texture ? l_Registry->Find(l_Texture) : nullptr;
    const std::string l_Name = !l_Texture ? std::string("None") : l_Record != nullptr ? GetAssetName(*l_Record) : std::format("Missing {}", l_Texture);

    Label("Texture");
    const float l_ClearWidth = ImGui::GetFrameHeight();
    const float l_Width = std::max(ImGui::GetContentRegionAvail().x - l_ClearWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f);
    if (ImGui::Button(std::format("{}###Texture", l_Name).c_str(), ImVec2(l_Width, 0.0f)))
    {
        m_AssetFilter.clear();
        ImGui::OpenPopup(c_TexturePickerPopup);
    }

    if (l_Record != nullptr && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", l_Record->Path.c_str());
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_AssetPayload))
        {
            std::uint64_t l_Value = 0;
            std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));
            const Trinity::AssetRecord* l_Dropped = l_Registry != nullptr ? l_Registry->Find(Trinity::UUID(l_Value)) : nullptr;
            if (l_Dropped != nullptr && l_Dropped->Importer == Trinity::TextureAsset::c_AssetType)
            {
                SetTexture(entity, l_Dropped->ID);
            }
            else
            {
                TR_WARN("Forge: only a texture can go in a SpriteRenderer's Texture slot");
            }
        }

        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!l_Texture);
    if (ImGui::Button(std::format("{}##ClearTexture", Trinity::Icons::c_TimesCircle).c_str(), ImVec2(l_ClearWidth, 0.0f)))
    {
        SetTexture(entity, {});
    }

    ImGui::EndDisabled();

    DrawAssetPicker(c_TexturePickerPopup, Trinity::TextureAsset::c_AssetType, l_Texture, [this, entity](Trinity::UUID texture) { SetTexture(entity, texture); });
}

// The project's assets of one type by path, narrowed by what is typed
void PropertiesPanel::DrawAssetPicker(const char* popup, std::string_view assetType, Trinity::UUID current, const std::function<void(Trinity::UUID)>& pick)
{
    if (!ImGui::BeginPopup(popup))
    {
        return;
    }

    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.0f);
    ImGui::InputTextWithHint("##Filter", std::format("Search {} assets", assetType).c_str(), &m_AssetFilter);

    if (ImGui::Selectable("None"))
    {
        pick({});
    }

    std::vector<const Trinity::AssetRecord*> l_Assets;
    if (const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry())
    {
        for (const Trinity::AssetRecord* it_Record : l_Registry->GetRecords())
        {
            const auto a_Matches = [this](const std::string& path) { return m_AssetFilter.empty() || std::ranges::search(path, m_AssetFilter, [](char left, char right) { return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right)); }).begin() != path.end(); };
            if (it_Record->Importer == assetType && a_Matches(it_Record->Path))
            {
                l_Assets.push_back(it_Record);
            }
        }
    }

    std::ranges::sort(l_Assets, {}, &Trinity::AssetRecord::Path);
    for (const Trinity::AssetRecord* it_Record : l_Assets)
    {
        if (ImGui::Selectable(std::format("{}###{}", it_Record->Path, it_Record->ID).c_str(), it_Record->ID == current))
        {
            pick(it_Record->ID);
        }
    }

    if (l_Assets.empty())
    {
        ImGui::TextDisabled("%s", std::format("No {} assets in the project match", assetType).c_str());
    }

    ImGui::EndPopup();
}

// Components the engine or a module registered without an editor here, and those no loaded code knows, which are kept as they were read
void PropertiesPanel::DrawOtherComponents(Trinity::Entity entity)
{
    for (const std::string_view it_Name : Trinity::SceneSerializer::GetComponentNames())
    {
        if (std::ranges::find(c_EditedComponents, it_Name) != c_EditedComponents.end() || !Trinity::SceneSerializer::HasComponent(entity, it_Name))
        {
            continue;
        }

        if (BeginComponent(it_Name, Trinity::Icons::c_Gear, true))
        {
            ImGui::TextDisabled("No editor for this component yet");
        }
    }

    if (!entity.Has<Trinity::UnknownComponentsComponent>())
    {
        return;
    }

    for (const Trinity::UnknownComponentsComponent::Entry& it_Unknown : entity.Get<Trinity::UnknownComponentsComponent>().Entries)
    {
        if (BeginComponent(it_Unknown.Name, Trinity::Icons::c_Warning, false))
        {
            ImGui::TextDisabled("Kept as it was read, since no loaded code knows this component");
        }
    }
}

// Every registered component the entity is without, except Transform, which it always has
void PropertiesPanel::DrawAddComponent(Trinity::Entity entity)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float l_Width = ImGui::GetFontSize() * 12.0f;
    ImGui::SetCursorPosX(std::max((ImGui::GetContentRegionAvail().x - l_Width) * 0.5f, 0.0f) + ImGui::GetCursorPosX());
    if (ImGui::Button("Add Component", ImVec2(l_Width, 0.0f)))
    {
        ImGui::OpenPopup(c_AddComponentPopup);
    }

    if (!ImGui::BeginPopup(c_AddComponentPopup))
    {
        return;
    }

    bool l_Any = false;
    for (const std::string_view it_Name : Trinity::SceneSerializer::GetComponentNames())
    {
        if (it_Name == Trinity::TransformComponent::c_TypeName || Trinity::SceneSerializer::HasComponent(entity, it_Name))
        {
            continue;
        }

        l_Any = true;
        if (ImGui::MenuItem(std::string(GetComponentLabel(it_Name)).c_str()))
        {
            m_Session.GetHistory().Execute(Trinity::CreateScope<AddComponentCommand>(entity.GetUUID(), std::string(it_Name)));
        }
    }

    if (!l_Any)
    {
        ImGui::TextDisabled("The entity has every component there is");
    }

    ImGui::EndPopup();
}

// An asset picked in the Content Browser: where it is, for a texture a preview and its import settings, for a model its import settings and materials, and for a material its editor
void PropertiesPanel::DrawAsset(Trinity::UUID id)
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = l_Registry != nullptr ? l_Registry->Find(id) : nullptr;
    if (l_Record == nullptr)
    {
        m_Session.SetInspectedAsset({});

        return;
    }

    const bool l_Texture = l_Record->Importer == Trinity::TextureAsset::c_AssetType;
    const bool l_Model = l_Record->Importer == ModelImporter::c_Importer;
    const bool l_Material = l_Record->Importer == Trinity::MaterialAsset::c_AssetType;
    const char* l_Icon = l_Texture ? Trinity::Icons::c_FileImage : (l_Model ? Trinity::Icons::c_Cube : (l_Material ? Trinity::Icons::c_PaintBrush : Trinity::Icons::c_File));
    ImGui::TextUnformatted(std::format("{} {}", l_Icon, GetAssetName(*l_Record)).c_str());
    ImGui::TextDisabled("%s", l_Record->Path.c_str());
    ImGui::TextDisabled("UUID %s, %s", id.ToString().c_str(), l_Record->Importer.c_str());
    ImGui::Spacing();

    if (!l_Material)
    {
        m_Material = {};
    }

    if (l_Model)
    {
        m_Preview = {};
        DrawModelSettings(*l_Record);
        DrawModelMaterials(*l_Record);

        return;
    }

    if (l_Material)
    {
        m_Preview = {};
        DrawMaterial(*l_Record);

        return;
    }

    if (!l_Texture)
    {
        m_Preview = {};
        ImGui::TextDisabled("This kind of asset has no import settings");

        return;
    }

    if (m_Preview.GetID() != id)
    {
        m_Preview = Trinity::AssetRef<Trinity::TextureAsset>(id);
    }

    DrawTexturePreview();
    if (l_Record->Parent.IsValid())
    {
        ImGui::TextDisabled("Imported with its model, which decides how");

        return;
    }

    DrawTextureSettings(*l_Record);
}

// Fitted to the panel's width, and to a height that leaves the settings in view
void PropertiesPanel::DrawTexturePreview()
{
    const Trinity::TextureAsset* l_Texture = m_Preview.IsReady() ? m_Preview.Get() : nullptr;
    if (l_Texture == nullptr || !l_Texture->GetTexture() || l_Texture->GetShaderResourceIndex() == Trinity::RHI::c_NoBindlessIndex)
    {
        ImGui::TextDisabled("%s", m_Preview.GetState() == Trinity::AssetState::Failed ? "The texture could not be loaded" : "Loading...");
        ImGui::Spacing();

        return;
    }

    const float l_Width = static_cast<float>(l_Texture->GetWidth());
    const float l_Height = static_cast<float>(std::max(l_Texture->GetHeight(), 1u));
    const float l_Scale = std::min({ ImGui::GetContentRegionAvail().x / l_Width, ImGui::GetFontSize() * 16.0f / l_Height, 1.0f });
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(Trinity::GetImGuiTextureID(*l_Texture))), ImVec2(l_Width * l_Scale, l_Height * l_Scale));
    ImGui::TextDisabled("%ux%u, %u mip(s), %s", l_Texture->GetWidth(), l_Texture->GetHeight(), l_Texture->GetMipLevels(), std::string(Trinity::RHI::ToString(l_Texture->GetFormat())).c_str());
    ImGui::Spacing();
}

// Edited here first, and written to the .meta and reimported only on Apply, which the texture's sprites pick up once the new one has loaded
void PropertiesPanel::DrawTextureSettings(const Trinity::AssetRecord& record)
{
    const TextureImportSettings l_Saved = TextureImporter::ReadSettings(record);
    if (m_SettingsAsset != record.ID)
    {
        m_SettingsAsset = record.ID;
        m_TextureSettings = l_Saved;
    }

    if (!BeginComponent("Import Settings", Trinity::Icons::c_Gear, false))
    {
        return;
    }

    Label("sRGB");
    ImGui::BeginDisabled(m_TextureSettings.NormalMap);
    ImGui::Checkbox("##Srgb", &m_TextureSettings.Srgb);
    ImGui::EndDisabled();

    Label("Normal Map");
    ImGui::Checkbox("##NormalMap", &m_TextureSettings.NormalMap);

    Label("Generate Mips");
    ImGui::Checkbox("##GenerateMips", &m_TextureSettings.GenerateMips);

    Label("UASTC Level");
    int l_Level = static_cast<int>(m_TextureSettings.UastcLevel);
    if (ImGui::SliderInt("##UastcLevel", &l_Level, 0, static_cast<int>(TextureImporter::c_MaxUastcLevel), "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        m_TextureSettings.UastcLevel = static_cast<std::uint32_t>(l_Level);
    }

    Label("Filter");
    int l_Filter = m_TextureSettings.Filter == Trinity::RHI::Filter::Nearest ? 1 : 0;
    if (ImGui::Combo("##Filter", &l_Filter, "Linear\0Nearest\0"))
    {
        m_TextureSettings.Filter = l_Filter == 1 ? Trinity::RHI::Filter::Nearest : Trinity::RHI::Filter::Linear;
    }

    ImGui::Spacing();
    const bool l_Changed = m_TextureSettings != l_Saved;
    ImGui::BeginDisabled(!l_Changed);
    if (ImGui::Button("Apply"))
    {
        static_cast<void>(m_Session.ApplyImportSettings(record.ID, TextureImporter::MakeSettings(m_TextureSettings)));
    }

    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        m_TextureSettings = l_Saved;
    }

    ImGui::EndDisabled();

    if (m_Session.IsImporting(record.ID))
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Importing...");
    }
}

// Units and axes as the file gives them unless set here. Apply writes them to the .meta and imports the model again, which entities already created from it do not follow
void PropertiesPanel::DrawModelSettings(const Trinity::AssetRecord& record)
{
    const ModelImportSettings l_Saved = ModelImporter::ReadSettings(record);
    if (m_SettingsAsset != record.ID)
    {
        m_SettingsAsset = record.ID;
        m_ModelSettings = l_Saved;
    }

    if (!BeginComponent("Import Settings", Trinity::Icons::c_Gear, false))
    {
        return;
    }

    Label("Auto Unit Scale");
    bool l_AutoScale = !m_ModelSettings.UnitScale.has_value();
    if (ImGui::Checkbox("##AutoUnitScale", &l_AutoScale))
    {
        m_ModelSettings.UnitScale = l_AutoScale ? std::nullopt : std::optional<float>(1.0f);
    }

    if (m_ModelSettings.UnitScale)
    {
        Label("Unit Scale");
        ImGui::DragFloat("##UnitScale", &*m_ModelSettings.UnitScale, 0.001f, 0.0001f, 10000.0f, "%.4f m", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
    }

    Label("Up Axis");
    int l_Axis = static_cast<int>(m_ModelSettings.UpAxis);
    if (ImGui::Combo("##UpAxis", &l_Axis, "Auto\0Y\0Z\0"))
    {
        m_ModelSettings.UpAxis = static_cast<ModelUpAxis>(l_Axis);
    }

    ImGui::Spacing();
    const bool l_Changed = m_ModelSettings != l_Saved;
    ImGui::BeginDisabled(!l_Changed);
    if (ImGui::Button("Apply"))
    {
        static_cast<void>(m_Session.ApplyImportSettings(record.ID, ModelImporter::MakeSettings(m_ModelSettings, record)));
    }

    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        m_ModelSettings = l_Saved;
    }

    ImGui::EndDisabled();

    if (m_Session.IsImportingModels())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Importing...");
    }
}

// Each of the model's materials, which opens its editor, with how many fields its .meta overrides
void PropertiesPanel::DrawModelMaterials(const Trinity::AssetRecord& record)
{
    if (!BeginComponent("Materials", Trinity::Icons::c_PaintBrush, false))
    {
        return;
    }

    bool l_Any = false;
    for (const Trinity::SubAsset& it_SubAsset : record.SubAssets)
    {
        if (it_SubAsset.Importer != ModelImporter::c_MaterialImporter)
        {
            continue;
        }

        l_Any = true;
        const std::size_t l_Overrides = ModelImporter::ReadMaterialOverrides(record, it_SubAsset.Key).size();
        const std::string l_Label = l_Overrides == 0 ? it_SubAsset.Key : std::format("{} ({} overridden)", it_SubAsset.Key, l_Overrides);
        if (ImGui::Selectable(std::format("{} {}###{}", Trinity::Icons::c_PaintBrush, l_Label, it_SubAsset.ID).c_str()))
        {
            m_Session.SetInspectedAsset(it_SubAsset.ID);
        }
    }

    if (!l_Any)
    {
        ImGui::TextDisabled("%s", m_Session.IsImportingModels() ? "Importing..." : "The model has no materials");
    }
}

// Every factor and texture, each edit written at once and shown the same frame, and undone in one step. A model's material marks in colour the fields its .meta overrides, and right-clicking one puts back what was imported. Colours are edited as they look, in sRGB, and kept linear
void PropertiesPanel::DrawMaterial(const Trinity::AssetRecord& record)
{
    if (m_Material.GetID() != record.ID)
    {
        m_Material = Trinity::AssetRef<Trinity::MaterialAsset>(record.ID);
        m_ImportedMaterialImports = UINT64_MAX;
    }

    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Model = record.Parent.IsValid() && l_Registry != nullptr ? l_Registry->Find(record.Parent) : nullptr;
    if (l_Model != nullptr)
    {
        if (m_ImportedMaterialImports != m_Session.GetModelImportCount())
        {
            m_ImportedMaterial = m_Session.ReadImportedMaterial(record.ID);
            m_ImportedMaterialImports = m_Session.GetModelImportCount();
        }

        ImGui::TextDisabled("Imported with %s. Edits are kept in its .meta", GetAssetName(*l_Model).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(std::format("{} Show Model", Trinity::Icons::c_Cube).c_str()))
        {
            m_Session.SetInspectedAsset(l_Model->ID);

            return;
        }

        ImGui::Spacing();
    }
    else
    {
        m_ImportedMaterial.reset();
    }

    const Trinity::MaterialAsset* l_Asset = m_Material.IsReady() ? m_Material.Get() : nullptr;
    if (l_Asset == nullptr)
    {
        ImGui::TextDisabled("%s", m_Material.GetState() == Trinity::AssetState::Failed ? "The material could not be loaded" : "Loading...");

        return;
    }

    if (!BeginComponent("Material", Trinity::Icons::c_PaintBrush, false))
    {
        return;
    }

    const Trinity::MaterialData l_Current = l_Asset->GetData();
    std::vector<Trinity::MaterialField> l_ImportedFields;
    std::vector<std::string> l_Overridden;
    if (m_ImportedMaterial)
    {
        l_ImportedFields = Trinity::GetMaterialFields(*m_ImportedMaterial);
        const std::vector<Trinity::MaterialField> l_Fields = Trinity::GetMaterialFields(l_Current);
        for (std::size_t it_Field = 0; it_Field < l_Fields.size(); ++it_Field)
        {
            if (l_Fields[it_Field] != l_ImportedFields[it_Field])
            {
                l_Overridden.push_back(l_Fields[it_Field].Name);
            }
        }
    }

    // The label, in the accent colour when any of its fields is overridden, with a menu that puts them back as imported
    const auto a_Label = [&](const char* label, std::initializer_list<std::string_view> fields)
    {
        const bool l_IsOverridden = std::ranges::any_of(fields, [&l_Overridden](std::string_view field) { return std::ranges::find(l_Overridden, field) != l_Overridden.end(); });
        ImGui::AlignTextToFramePadding();
        if (l_IsOverridden)
        {
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", label);
        }
        else
        {
            ImGui::TextUnformatted(label);
        }

        const std::string l_Popup = std::format("##Revert{}", label);
        if (l_IsOverridden)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("Overridden in the model's .meta. Right-click to put back what was imported");
            }

            ImGui::OpenPopupOnItemClick(l_Popup.c_str(), ImGuiPopupFlags_MouseButtonRight);
        }

        if (ImGui::BeginPopup(l_Popup.c_str()))
        {
            if (ImGui::MenuItem(std::format("{} Revert to Imported", Trinity::Icons::c_Undo).c_str()))
            {
                std::vector<Trinity::MaterialField> l_Reverted;
                std::ranges::copy_if(l_ImportedFields, std::back_inserter(l_Reverted), [fields](const Trinity::MaterialField& field) { return std::ranges::find(fields, std::string_view(field.Name)) != fields.end(); });
                if (const Trinity::Expected<Trinity::MaterialData, std::string> l_Edited = Trinity::ApplyMaterialFields(l_Current, l_Reverted))
                {
                    static_cast<void>(m_Session.EditMaterial(record.ID, l_Current, *l_Edited, std::format("Revert {}", label)));
                }
            }

            ImGui::EndPopup();
        }

        ImGui::SameLine(ImGui::GetFontSize() * c_LabelWidthInFonts);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };

    // The widget edits a copy, and a change becomes an edit, which merges with the next while the widget stays active
    const auto a_Edit = [&](std::string_view field, const auto& widget)
    {
        Trinity::MaterialData l_Edited = l_Current;
        if (widget(l_Edited))
        {
            static_cast<void>(m_Session.EditMaterial(record.ID, l_Current, l_Edited, std::string(field)));
        }
    };

    const auto a_ToSrgb = [](glm::vec3 color) { return glm::vec3(Trinity::LinearToSrgb(color.r), Trinity::LinearToSrgb(color.g), Trinity::LinearToSrgb(color.b)); };
    const auto a_ToLinear = [](glm::vec3 color) { return glm::vec3(Trinity::SrgbToLinear(color.r), Trinity::SrgbToLinear(color.g), Trinity::SrgbToLinear(color.b)); };

    a_Label("Base Color", { "BaseColorFactor" });
    a_Edit("Base Color", [&](Trinity::MaterialData& material)
    {
        glm::vec4 l_Color(a_ToSrgb(glm::vec3(material.BaseColorFactor)), material.BaseColorFactor.a);
        const bool l_Changed = ImGui::ColorEdit4("##BaseColor", &l_Color.x, ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar);
        material.BaseColorFactor = glm::vec4(a_ToLinear(glm::vec3(l_Color)), l_Color.a);

        return l_Changed;
    });

    a_Label("Metallic", { "MetallicFactor" });
    a_Edit("Metallic", [](Trinity::MaterialData& material) { return ImGui::SliderFloat("##Metallic", &material.MetallicFactor, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    a_Label("Roughness", { "RoughnessFactor" });
    a_Edit("Roughness", [](Trinity::MaterialData& material) { return ImGui::SliderFloat("##Roughness", &material.RoughnessFactor, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    a_Label("Emissive", { "EmissiveFactor" });
    a_Edit("Emissive", [&](Trinity::MaterialData& material)
    {
        glm::vec3 l_Color = a_ToSrgb(material.EmissiveFactor);
        const bool l_Changed = ImGui::ColorEdit3("##Emissive", &l_Color.x);
        material.EmissiveFactor = a_ToLinear(l_Color);

        return l_Changed;
    });

    a_Label("Emissive Strength", { "EmissiveStrength" });
    a_Edit("Emissive Strength", [](Trinity::MaterialData& material) { return ImGui::DragFloat("##EmissiveStrength", &material.EmissiveStrength, 0.05f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    a_Label("Normal Scale", { "NormalScale" });
    a_Edit("Normal Scale", [](Trinity::MaterialData& material) { return ImGui::DragFloat("##NormalScale", &material.NormalScale, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    a_Label("Occlusion Strength", { "OcclusionStrength" });
    a_Edit("Occlusion Strength", [](Trinity::MaterialData& material) { return ImGui::SliderFloat("##OcclusionStrength", &material.OcclusionStrength, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    a_Label("Alpha Mode", { "AlphaMode" });
    a_Edit("Alpha Mode", [](Trinity::MaterialData& material)
    {
        int l_Mode = static_cast<int>(material.AlphaMode);
        const bool l_Changed = ImGui::Combo("##AlphaMode", &l_Mode, "Opaque\0Mask\0Blend\0");
        material.AlphaMode = static_cast<Trinity::MaterialAlphaMode>(l_Mode);

        return l_Changed;
    });

    a_Label("Alpha Cutoff", { "AlphaCutoff" });
    ImGui::BeginDisabled(l_Current.AlphaMode != Trinity::MaterialAlphaMode::Mask);
    a_Edit("Alpha Cutoff", [](Trinity::MaterialData& material) { return ImGui::SliderFloat("##AlphaCutoff", &material.AlphaCutoff, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });
    ImGui::EndDisabled();

    a_Label("Double Sided", { "DoubleSided" });
    a_Edit("Double Sided", [](Trinity::MaterialData& material) { return ImGui::Checkbox("##DoubleSided", &material.DoubleSided); });

    DrawMaterialTextures(record, l_Current, a_Label);

    // Nothing is being dragged or typed into, so the next change is an edit of its own
    if (!ImGui::IsAnyItemActive())
    {
        m_Session.GetHistory().EndMerge();
    }
}

// Each slot shows its texture once loaded, takes a texture dropped from elsewhere in Forge or picked from the project's, and names the UV set it is sampled with
void PropertiesPanel::DrawMaterialTextures(const Trinity::AssetRecord& record, const Trinity::MaterialData& current, const std::function<void(const char*, std::initializer_list<std::string_view>)>& label)
{
    struct Slot
    {
        const char* Label;
        std::string_view Texture;
        std::string_view TexCoord;
        Trinity::MaterialTexture Trinity::MaterialData::* Member;
    };

    constexpr std::array<Slot, 5> c_Slots{ {
        { "Base Color Map", "BaseColorTexture", "BaseColorTexCoord", &Trinity::MaterialData::BaseColorTexture },
        { "Metal/Rough Map", "MetallicRoughnessTexture", "MetallicRoughnessTexCoord", &Trinity::MaterialData::MetallicRoughnessTexture },
        { "Normal Map", "NormalTexture", "NormalTexCoord", &Trinity::MaterialData::NormalTexture },
        { "Occlusion Map", "OcclusionTexture", "OcclusionTexCoord", &Trinity::MaterialData::OcclusionTexture },
        { "Emissive Map", "EmissiveTexture", "EmissiveTexCoord", &Trinity::MaterialData::EmissiveTexture }
    } };

    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    for (const Slot& it_Slot : c_Slots)
    {
        ImGui::PushID(it_Slot.Label);
        const Trinity::MaterialTexture& l_Slot = current.*it_Slot.Member;
        const auto a_Set = [&](Trinity::MaterialTexture texture)
        {
            Trinity::MaterialData l_Edited = current;
            l_Edited.*it_Slot.Member = texture;
            static_cast<void>(m_Session.EditMaterial(record.ID, current, l_Edited, it_Slot.Label));
            m_Session.GetHistory().EndMerge();
        };

        label(it_Slot.Label, { it_Slot.Texture, it_Slot.TexCoord });

        // The thumbnail only for a texture already loaded, which the material keeps loaded
        const float l_Size = ImGui::GetFrameHeight();
        const Trinity::Asset* l_Loaded = l_Slot.Texture.IsValid() && Trinity::AssetManager::GetState(l_Slot.Texture) == Trinity::AssetState::Ready ? Trinity::AssetManager::GetAsset(l_Slot.Texture) : nullptr;
        const Trinity::TextureAsset* l_Texture = l_Loaded != nullptr && l_Loaded->GetAssetType() == Trinity::TextureAsset::c_AssetType ? static_cast<const Trinity::TextureAsset*>(l_Loaded) : nullptr;
        if (l_Texture != nullptr && l_Texture->GetTexture() && l_Texture->GetShaderResourceIndex() != Trinity::RHI::c_NoBindlessIndex)
        {
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(Trinity::GetImGuiTextureID(*l_Texture))), ImVec2(l_Size, l_Size));
        }
        else
        {
            ImGui::Dummy(ImVec2(l_Size, l_Size));
        }

        ImGui::SameLine();
        const Trinity::AssetRecord* l_Record = l_Registry != nullptr && l_Slot.Texture ? l_Registry->Find(l_Slot.Texture) : nullptr;
        const std::string l_Name = !l_Slot.Texture ? std::string("None") : l_Record != nullptr ? GetAssetName(*l_Record) : std::format("Missing {}", l_Slot.Texture);
        const float l_UVWidth = ImGui::GetFontSize() * 3.5f;
        const float l_Width = std::max(ImGui::GetContentRegionAvail().x - l_Size - l_UVWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f, 1.0f);
        const std::string l_Popup = std::format("##Pick{}", it_Slot.Texture);
        if (ImGui::Button(std::format("{}###Texture", l_Name).c_str(), ImVec2(l_Width, 0.0f)))
        {
            m_AssetFilter.clear();
            ImGui::OpenPopup(l_Popup.c_str());
        }

        if (l_Record != nullptr && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", l_Record->Path.c_str());
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_AssetPayload))
            {
                std::uint64_t l_Value = 0;
                std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));
                const Trinity::AssetRecord* l_Dropped = l_Registry != nullptr ? l_Registry->Find(Trinity::UUID(l_Value)) : nullptr;
                if (l_Dropped != nullptr && l_Dropped->Importer == Trinity::TextureAsset::c_AssetType)
                {
                    a_Set({ l_Dropped->ID, l_Slot.TexCoord });
                }
                else
                {
                    TR_WARN("Forge: only a texture can go in a material's {}", it_Slot.Label);
                }
            }

            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!l_Slot.Texture);
        if (ImGui::Button(std::format("{}##Clear", Trinity::Icons::c_TimesCircle).c_str(), ImVec2(l_Size, 0.0f)))
        {
            a_Set({});
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(l_UVWidth);
        int l_TexCoord = static_cast<int>(l_Slot.TexCoord);
        if (ImGui::Combo("##UV", &l_TexCoord, "UV 0\0UV 1\0"))
        {
            a_Set({ l_Slot.Texture, static_cast<std::uint32_t>(l_TexCoord) });
        }

        DrawAssetPicker(l_Popup.c_str(), Trinity::TextureAsset::c_AssetType, l_Slot.Texture, [&](Trinity::UUID texture) { a_Set({ texture, l_Slot.TexCoord }); });
        ImGui::PopID();
    }
}

// A section with the component's name, open by default. Right-clicking a removable one offers to remove it
bool PropertiesPanel::BeginComponent(std::string_view name, const char* icon, bool removable)
{
    const bool l_Open = ImGui::CollapsingHeader(std::format("{} {}###{}", icon, GetComponentLabel(name), name).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (removable && ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem(std::format("{} Remove Component", Trinity::Icons::c_Trash).c_str()))
        {
            m_PendingRemoval = std::string(name);
        }

        ImGui::EndPopup();
    }

    return l_Open;
}

void PropertiesPanel::SetTexture(Trinity::Entity entity, Trinity::UUID texture)
{
    Trinity::SpriteRendererComponent l_Value = entity.Get<Trinity::SpriteRendererComponent>();
    l_Value.Texture = texture;

    CommandStack& l_History = m_Session.GetHistory();
    l_History.Execute(Trinity::CreateScope<SetComponentCommand<Trinity::SpriteRendererComponent>>(entity.GetUUID(), std::move(l_Value), "Texture"));
    l_History.EndMerge();
}