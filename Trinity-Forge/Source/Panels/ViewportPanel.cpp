#include "Panels/ViewportPanel.hpp"

#include "EditorCommands.hpp"
#include "EditorPayloads.hpp"
#include "ImGuizmoInclude.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

namespace
{
    constexpr double c_SettleSeconds = 0.1;
    constexpr double c_CameraSaveSeconds = 0.5;
    constexpr float c_StatsMargin = 8.0f;

    constexpr ImU32 c_CameraOutlineColor = IM_COL32(255, 255, 255, 200);

    // A dropped texture's sprite is this many of its pixels to a world unit
    constexpr float c_PixelsPerUnit = 100.0f;

    // The gizmo's camera looks down Z from this far above the sprites, which sit near Z = 0
    constexpr float c_GizmoDistance = 10000.0f;
    constexpr std::array<const char*, 3> c_GizmoFields{ "Position", "Rotation", "Scale" };
    constexpr std::array<std::string_view, 3> c_GizmoOperationNames{ "Translate", "Rotate", "Scale" };

    // In 3D: a model or sprite dropped where the mouse's ray misses the ground, or meets it further away than the maximum, lands this far ahead of the camera
    constexpr float c_DropDistance = 10.0f;
    constexpr float c_MaximumDropDistance = 1000.0f;
    // A light's marker is this many pixels across, and a click this near its centre picks it
    constexpr float c_LightMarkerRadius = 5.0f;
    constexpr float c_LightPickPixels = 8.0f;
    // Segments in each circle drawn for a light's reach
    constexpr int c_CircleSegments = 48;
    // A scene camera's frustum is drawn this far out in front of it
    constexpr float c_FrustumDistance = 5.0f;
    // Flying never jumps further than a frame of this long, however long the frame took
    constexpr float c_MaximumFlyStep = 0.1f;
    // Framing's glide closes this much of what is left each second, as a rate, so it is there in about a quarter of a second, and it is there once within this fraction of the distance
    constexpr float c_GlideRate = 18.0f;
    constexpr float c_GlideTolerance = 0.001f;
    // A turn is there once within this many degrees of the yaw and pitch it turns to
    constexpr float c_GlideAngleTolerance = 0.01f;

    // The orientation gizmo's cones reach this many fonts from its centre, round a cube this share of that across, with a cone's base this share of it from the centre and this share of it wide
    constexpr float c_OrientationRadiusInFonts = 2.4f;
    constexpr float c_OrientationCube = 0.2f;
    constexpr float c_OrientationConeBase = 0.42f;
    constexpr float c_OrientationConeWidth = 0.23f;
    // Unity's handle colours for X, Y and Z, its grey for the negative axes, and its yellow for what the mouse is over
    constexpr std::array<ImU32, 3> c_AxisColors{ IM_COL32(219, 62, 29, 255), IM_COL32(154, 243, 72, 255), IM_COL32(58, 122, 248, 255) };
    constexpr ImU32 c_NegativeAxisColor = IM_COL32(170, 170, 170, 255);
    constexpr ImU32 c_HoveredAxisColor = IM_COL32(246, 242, 50, 255);

    // A view down one of the world's axes, as the orientation gizmo's cone towards it and its menu turn to it: looking back along the axis from its side, at a yaw and pitch, with Top keeping -Z up the screen as Front looks down -Z. Forge's front looks down -Z, where Unity's looks down +Z, so Front and Back trade cones with Unity's
    struct SideView
    {
        const char* Name;
        int Axis;
        float Sign;
        float Yaw;
        float Pitch;
    };

    constexpr std::array<SideView, 6> c_SideViews{ SideView{ "Top", 1, 1.0f, 0.0f, -90.0f }, SideView{ "Bottom", 1, -1.0f, 0.0f, 90.0f }, SideView{ "Left", 0, -1.0f, 90.0f, 0.0f }, SideView{ "Right", 0, 1.0f, -90.0f, 0.0f }, SideView{ "Front", 2, 1.0f, 0.0f, 0.0f }, SideView{ "Back", 2, -1.0f, 180.0f, 0.0f } };

    ImVec2 ToImVec2(glm::vec2 point)
    {
        return { point.x, point.y };
    }

    // How far the point is from the segment
    float GetSegmentDistance(glm::vec2 point, glm::vec2 start, glm::vec2 end)
    {
        const glm::vec2 l_Segment = end - start;
        const float l_Length = glm::dot(l_Segment, l_Segment);
        const float l_Along = l_Length > 0.0f ? std::clamp(glm::dot(point - start, l_Segment) / l_Length, 0.0f, 1.0f) : 0.0f;

        return glm::distance(point, start + l_Segment * l_Along);
    }

    // A whole number of steps, so a snapped value is exactly k times the step
    float Snap(float value, float step)
    {
        return std::round(value / step) * step;
    }

    // The editor camera as ImGuizmo takes it, a view and a projection apart, with depth the usual way round
    glm::mat4 GetGizmoView(const EditorCamera& camera)
    {
        return glm::translate(glm::mat4(1.0f), glm::vec3(-camera.GetPosition(), -c_GizmoDistance));
    }

    glm::mat4 GetGizmoProjection(const EditorCamera& camera, glm::vec2 viewportSize)
    {
        const glm::vec2 l_HalfExtent = camera.GetHalfExtent(viewportSize);

        return glm::ortho(-l_HalfExtent.x, l_HalfExtent.x, -l_HalfExtent.y, l_HalfExtent.y, 1.0f, c_GizmoDistance * 2.0f);
    }

    // The angle of the X axis about Z. A negative X scale turns it round, but by the same amount before and after a rotation, so differences stay true
    float GetAngleZ(const glm::mat4& matrix)
    {
        return std::atan2(matrix[0][1], matrix[0][0]);
    }

    void TextLine(std::string_view text)
    {
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    std::string GetCameraPath(Trinity::UUID scene)
    {
        return std::format("{}/Editor/Views/{}.view", Trinity::Project::c_CacheMount, scene);
    }

    // The unit quad a sprite is drawn on, through its world transform, in X and Y
    std::array<glm::vec2, 4> GetSpriteCorners(const glm::mat4& world)
    {
        constexpr std::array<glm::vec2, 4> c_Local{ glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, -0.5f), glm::vec2(0.5f, 0.5f), glm::vec2(-0.5f, 0.5f) };

        std::array<glm::vec2, 4> l_Corners{};
        for (std::size_t it_Corner = 0; it_Corner < c_Local.size(); ++it_Corner)
        {
            l_Corners[it_Corner] = glm::vec2(world * glm::vec4(c_Local[it_Corner], 0.0f, 1.0f));
        }

        return l_Corners;
    }

    // Finite numbers apart by spaces, from the start of the text, which is left after the last of them
    bool ReadNumbers(std::string_view& text, std::span<float> values)
    {
        const char* l_Cursor = text.data();
        const char* l_End = text.data() + text.size();
        for (float& it_Value : values)
        {
            while (l_Cursor != l_End && (*l_Cursor == ' ' || *l_Cursor == '\n' || *l_Cursor == '\r'))
            {
                ++l_Cursor;
            }

            const std::from_chars_result l_Parsed = std::from_chars(l_Cursor, l_End, it_Value);
            if (l_Parsed.ec != std::errc() || !std::isfinite(it_Value))
            {
                return false;
            }

            l_Cursor = l_Parsed.ptr;
        }

        text = std::string_view(l_Cursor, l_End);

        return true;
    }

    // Three numbers: the camera's X and Y, and the world height it shows
    bool ParseCamera(std::string_view text, glm::vec2& position, float& height)
    {
        std::array<float, 3> l_Values{};
        if (!ReadNumbers(text, l_Values))
        {
            return false;
        }

        position = { l_Values[0], l_Values[1] };
        height = l_Values[2];

        return height > 0.0f;
    }

    // The second line, which views saved before 3D editing are without: 2D or 3D, as the scene was last edited, then the 3D camera's focus, yaw, pitch, distance and flying speed
    bool ParseCamera3D(std::string_view text, bool& mode3D, EditorCamera3D& camera)
    {
        const std::size_t l_LineEnd = text.find('\n');
        if (l_LineEnd == std::string_view::npos)
        {
            return false;
        }

        std::string_view l_Line = text.substr(l_LineEnd + 1);
        const bool l_Mode3D = l_Line.starts_with("3D");
        if (!l_Mode3D && !l_Line.starts_with("2D"))
        {
            return false;
        }

        l_Line.remove_prefix(2);
        std::array<float, 7> l_Values{};
        if (!ReadNumbers(l_Line, l_Values) || l_Values[5] <= 0.0f || l_Values[6] <= 0.0f)
        {
            return false;
        }

        mode3D = l_Mode3D;
        camera.Set(glm::vec3(l_Values[0], l_Values[1], l_Values[2]), l_Values[3], l_Values[4], l_Values[5], l_Values[6]);
        // Saved before Iso, a camera is in perspective
        camera.SetOrthographic(l_Line.starts_with(" Iso"));

        return true;
    }

    // A mesh asset once it has loaded, which its bounds need
    const Trinity::MeshAsset* GetLoadedMesh(Trinity::UUID id)
    {
        if (!id || Trinity::AssetManager::GetState(id) != Trinity::AssetState::Ready)
        {
            return nullptr;
        }

        const Trinity::Asset* l_Asset = Trinity::AssetManager::GetAsset(id);

        return l_Asset != nullptr && l_Asset->GetAssetType() == Trinity::MeshAsset::c_AssetType ? static_cast<const Trinity::MeshAsset*>(l_Asset) : nullptr;
    }

    std::array<glm::vec3, 8> GetBoxCorners(const Trinity::MeshBounds& bounds, const glm::mat4& world)
    {
        std::array<glm::vec3, 8> l_Corners{};
        for (std::size_t it_Corner = 0; it_Corner < l_Corners.size(); ++it_Corner)
        {
            const glm::vec3 l_Local((it_Corner & 1) != 0 ? bounds.Max.x : bounds.Min.x, (it_Corner & 2) != 0 ? bounds.Max.y : bounds.Min.y, (it_Corner & 4) != 0 ? bounds.Max.z : bounds.Min.z);
            l_Corners[it_Corner] = glm::vec3(world * glm::vec4(l_Local, 1.0f));
        }

        return l_Corners;
    }

    // The unit quad a sprite is drawn on, through its world transform, in 3D
    std::array<glm::vec3, 4> GetSpriteCorners3D(const glm::mat4& world)
    {
        constexpr std::array<glm::vec2, 4> c_Local{ glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, -0.5f), glm::vec2(0.5f, 0.5f), glm::vec2(-0.5f, 0.5f) };

        std::array<glm::vec3, 4> l_Corners{};
        for (std::size_t it_Corner = 0; it_Corner < c_Local.size(); ++it_Corner)
        {
            l_Corners[it_Corner] = glm::vec3(world * glm::vec4(c_Local[it_Corner], 0.0f, 1.0f));
        }

        return l_Corners;
    }

    // A world point in pixels from the image's top-left corner, or nothing when it is behind the eye
    std::optional<glm::vec2> ProjectPoint(const glm::mat4& viewProjection, const glm::vec3& point, glm::vec2 viewportSize)
    {
        const glm::vec4 l_Clip = viewProjection * glm::vec4(point, 1.0f);
        if (l_Clip.w <= EditorCamera3D::c_Near)
        {
            return std::nullopt;
        }

        return glm::vec2((l_Clip.x / l_Clip.w + 1.0f) * 0.5f * viewportSize.x, (1.0f - l_Clip.y / l_Clip.w) * 0.5f * viewportSize.y);
    }

    // As the light looks, from linear to sRGB, and opaque
    ImU32 GetLightColor(const Trinity::LightComponent& light, float alpha)
    {
        const glm::vec3 l_Color = glm::clamp(light.Color, glm::vec3(0.0f), glm::vec3(1.0f));

        return ImGui::ColorConvertFloat4ToU32(ImVec4(Trinity::LinearToSrgb(l_Color.r), Trinity::LinearToSrgb(l_Color.g), Trinity::LinearToSrgb(l_Color.b), alpha));
    }
}

// The Scene panel, which imgui.ini still knows by the Viewport's ID, so a saved layout keeps it where it was
ViewportPanel::ViewportPanel(Trinity::ImGuiLayer& imGui, EditorSession& session) : Panel("Scene", Trinity::Icons::c_Monitor, DockSlot::Centre, "Viewport"), m_ImGui(imGui), m_Session(session)
{
    SetBorderless(true);
    m_CloseListener = m_Session.AddCloseListener([this] { m_Colliders.Release(); });
}

// The session is still open here, so a camera moved in the last moments is saved. The Viewport is ImGuizmo's only user, so it lets go of ImGuizmo's memory too
ViewportPanel::~ViewportPanel()
{
    if (m_CameraDirty)
    {
        SaveCamera();
    }

    ReleaseImGuizmo();
    m_Session.RemoveCloseListener(m_CloseListener);
}

// The Viewport's own lines in imgui.ini: the stats overlay, the gizmo's operation and axes, the snap toggle, the light heatmap and the physics drawing. The snap steps, which the preferences keep now, are read from an imgui.ini written before them into preferences not saved yet
bool ViewportPanel::ReadSetting(std::string_view key, std::string_view value)
{
    const auto a_ReadStep = [this, value](glm::length_t axis)
    {
        float l_Value = 0.0f;
        const std::from_chars_result l_Parsed = std::from_chars(value.data(), value.data() + value.size(), l_Value);
        if (!m_Session.HasPreferencesFile() && l_Parsed.ec == std::errc() && std::isfinite(l_Value))
        {
            EditorPreferences l_Preferences = m_Session.GetPreferences();
            l_Preferences.SnapSteps[axis] = std::clamp(l_Value, EditorPreferences::c_MinimumSnap, EditorPreferences::c_MaximumSnap);
            m_Session.SetPreferences(l_Preferences);
        }
    };

    if (key == "ViewportStats")
    {
        m_ShowStats = value != "0";
    }
    else if (key == "GizmoOperation")
    {
        const auto a_Found = std::ranges::find(c_GizmoOperationNames, value);
        if (a_Found != c_GizmoOperationNames.end())
        {
            m_GizmoOperation = static_cast<GizmoOperation>(a_Found - c_GizmoOperationNames.begin());
        }
    }
    else if (key == "GizmoSpace")
    {
        m_GizmoLocal = value != "World";
    }
    else if (key == "Snap")
    {
        m_Snap = value != "0";
    }
    else if (key == "SnapTranslate")
    {
        a_ReadStep(0);
    }
    else if (key == "SnapRotate")
    {
        a_ReadStep(1);
    }
    else if (key == "SnapScale")
    {
        a_ReadStep(2);
    }
    else if (key == "ViewCamera")
    {
        // Written while this panel could show the scene's camera, which the Game panel now does, so it is read and dropped
    }
    else if (key == "LightHeatmap")
    {
        m_LightHeatmap = value != "0";
    }
    else if (key == "PhysicsDebug")
    {
        m_PhysicsDebug = value != "0";
    }
    else
    {
        return false;
    }

    return true;
}

void ViewportPanel::WriteSettings(ImGuiTextBuffer& buffer) const
{
    const std::string l_Lines = std::format("ViewportStats={}\nGizmoOperation={}\nGizmoSpace={}\nSnap={}\nLightHeatmap={}\nPhysicsDebug={}\n",
        m_ShowStats ? 1 : 0, c_GizmoOperationNames[static_cast<std::size_t>(m_GizmoOperation)], m_GizmoLocal ? "Local" : "World", m_Snap ? 1 : 0, m_LightHeatmap ? 1 : 0, m_PhysicsDebug ? 1 : 0);
    buffer.append(l_Lines.c_str(), l_Lines.c_str() + l_Lines.size());
}

// Saved in imgui.ini with the panels
void ViewportPanel::SetShowingStats(bool show)
{
    if (show != m_ShowStats)
    {
        m_ShowStats = show;
        ImGui::MarkIniSettingsDirty();
    }
}

// The scene's meshes, submitted to the main view before the frame graph is built, through the editor camera, which looks down -Z in 2D and is a perspective or an orthographic camera in 3D, with the 3D grid drawn over the meshes and hidden by them. Every entity is drawn into ID targets too, for a click to pick from and the selection's outline, and the frame's debug lines over the image, among them the selection's colliders, or with Physics on every collider and joint, and where a playing copy's bodies touch, which the copy is asked to keep only then. After the transform pass
void ViewportPanel::PrepareScene()
{
    const glm::vec2 l_ViewportSize = GetViewportSize();
    Trinity::Scene& l_Scene = m_Session.GetScene();
    if (l_ViewportSize.x <= 0.0f || l_ViewportSize.y <= 0.0f)
    {
        return;
    }

    if (Trinity::SceneRuntime* l_Runtime = m_Session.GetRuntime())
    {
        l_Runtime->SetGatherContacts(m_PhysicsDebug);
        if (m_PhysicsDebug && l_Runtime->GetPhysics() != nullptr)
        {
            ColliderDrawing::DrawContacts(l_Runtime->GetPhysics()->GetContactPoints());
        }

        if (m_PhysicsDebug && l_Runtime->GetPhysics2D() != nullptr)
        {
            ColliderDrawing::DrawContacts(l_Runtime->GetPhysics2D()->GetContactPoints());
        }
    }

    if (m_PhysicsDebug)
    {
        m_Colliders.Draw(l_Scene, {});
    }
    else if (const Trinity::Entity l_Selected = l_Scene.FindEntityByUUID(m_Session.GetSelection()))
    {
        m_Colliders.Draw(l_Scene, l_Selected);
    }
    else
    {
        m_Colliders.Release();
    }

    Trinity::RenderView l_View;
    if (m_Mode3D)
    {
        l_View = Trinity::RenderView::FromMatrices(m_Camera3D.GetView(), m_Camera3D.GetProjection(l_ViewportSize), m_Camera3D.IsOrthographic());
    }
    else
    {
        // The editor camera's projection holds its place, so its view is the world's own axes
        l_View = Trinity::RenderView::FromMatrices(glm::mat4(1.0f), m_Camera.GetViewProjection(l_ViewportSize), true);
        l_View.Position = glm::vec3(m_Camera.GetPosition(), 0.0f);
    }

    Trinity::SceneOptions l_Options;
    l_Options.LightHeatmap = m_LightHeatmap;
    l_Options.SampleCount = Trinity::Renderer3D::c_SampleCount;
    l_Options.EntityIDs = true;
    l_Options.PickPixel = std::exchange(m_PickPixel, std::nullopt);
    l_Options.Outlined = GetOutlined();
    l_Options.DebugLines = true;
    if (m_Mode3D)
    {
        l_Options.Overlay = [this](Trinity::RHI::CommandList& commands, const Trinity::RenderView& view, Trinity::RHI::Format colorFormat, std::uint32_t sampleCount) { m_Grid3D.Draw(commands, view, colorFormat, sampleCount); };
    }
    Trinity::Application::Get().GetRenderer().SubmitScene(l_Scene, l_View, l_Options);
}

// Into the main view's scene target, which the Scene panel shows, over the scene's meshes: through the 2D editor camera the grid, then the scene's sprites, and through the 3D editor camera the sprites alone, since the 3D grid was drawn with the meshes
void ViewportPanel::RenderScene(Trinity::RHI::CommandList& commands)
{
    const glm::vec2 l_ViewportSize = GetViewportSize();
    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    if (m_Mode3D)
    {
        Trinity::Application::Get().GetRenderer().GetRenderer2D().DrawScene(commands, m_Session.GetScene(), m_Camera3D.GetProjection(l_ViewportSize) * m_Camera3D.GetView(), l_Renderer.GetSceneFormat(), l_Renderer.GetSceneWidth(), l_Renderer.GetSceneHeight());

        return;
    }

    m_Grid.Draw(commands, m_Camera, l_ViewportSize);
    Trinity::Application::Get().GetRenderer().GetRenderer2D().DrawScene(commands, m_Session.GetScene(), m_Camera.GetViewProjection(l_ViewportSize), l_Renderer.GetSceneFormat(), l_Renderer.GetSceneWidth(), l_Renderer.GetSceneHeight());
}

// The tone mapped image, the display target, is drawn at the scene target's own size, which only catches up with the panel once its size settles, so it is cropped or bordered meanwhile but never stretched. Overlays follow the image, so they line up with what it shows
void ViewportPanel::OnImGuiRender()
{
    FollowScene();
    ApplyPick();
    m_Session.SetSpawnPoint(m_Mode3D ? m_Camera3D.GetFocus() : glm::vec3(m_Camera.GetPosition(), 0.0f), m_Mode3D);

    const ImVec2 l_Available = ImGui::GetContentRegionAvail();
    const std::uint32_t l_Width = static_cast<std::uint32_t>(std::max(std::floor(l_Available.x), 1.0f));
    const std::uint32_t l_Height = static_cast<std::uint32_t>(std::max(std::floor(l_Available.y), 1.0f));
    FollowPanelSize(l_Width, l_Height);

    Trinity::Application& l_Application = Trinity::Application::Get();
    const Trinity::RHI::TextureHandle l_Target = l_Application.GetRenderer().GetDisplayTarget();
    const std::uint32_t l_Index = l_Target ? l_Application.GetDevice().GetShaderResourceIndex(l_Target) : Trinity::RHI::c_NoBindlessIndex;
    const glm::vec2 l_ViewportSize = GetViewportSize();
    const ImVec2 l_ImageMin = ImGui::GetCursorScreenPos();
    if (l_Index != Trinity::RHI::c_NoBindlessIndex)
    {
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(l_Index)), ImVec2(l_ViewportSize.x, l_ViewportSize.y));
        AcceptAssetDrop(glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize);
    }

    // The scene has input over the whole panel. Picking, panning and zooming are only for the image itself, not the toolbar over it
    const bool l_Hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const bool l_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    m_ImGui.SetSceneInput(l_Hovered, l_Focused);

    // Picking, the gizmo, the camera and the outlines all work in the editor camera's view, 2D or 3D
    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    l_DrawList.PushClipRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y), true);
    if (m_Mode3D)
    {
        DrawOverlays3D(l_DrawList, glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize);
    }
    else
    {
        DrawOverlays(l_DrawList, glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize);
    }

    DrawGizmo(glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize);
    DrawOrientation(glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize);
    l_DrawList.PopClipRect();

    if (m_Mode3D)
    {
        HandleInput3D(glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize, ImGui::IsWindowHovered(), l_Focused);
    }
    else
    {
        HandleInput(glm::vec2(l_ImageMin.x, l_ImageMin.y), l_ViewportSize, ImGui::IsWindowHovered(), l_Focused);
    }

    if (const std::optional<bool> l_Request = std::exchange(m_FrameRequest, std::nullopt))
    {
        Frame(l_ViewportSize, *l_Request);
    }

    UpdateGlide(l_ViewportSize);

    if (m_ShowStats)
    {
        DrawStats(l_ViewportSize);
    }

    DrawToolbar();

    // While following, a line at the image's foot says what, and how to stop
    if (const Trinity::Entity l_Followed = m_Followed ? m_Session.GetScene().FindEntityByUUID(m_Followed) : Trinity::Entity())
    {
        const std::string l_Text = std::format("{} Following {}. Shift+F stops", Trinity::Icons::c_Eye, GetEntityLabel(l_Followed));
        const ImVec2 l_TextSize = ImGui::CalcTextSize(l_Text.c_str());
        const ImVec2 l_Padding = ImGui::GetStyle().WindowPadding;
        const ImVec2 l_Min(l_ImageMin.x + c_StatsMargin, l_ImageMin.y + l_ViewportSize.y - c_StatsMargin - l_TextSize.y - l_Padding.y * 2.0f);
        const ImVec2 l_Max(l_Min.x + l_TextSize.x + l_Padding.x * 2.0f, l_Min.y + l_TextSize.y + l_Padding.y * 2.0f);
        ImDrawList& l_Foot = *ImGui::GetWindowDrawList();
        l_Foot.AddRectFilled(l_Min, l_Max, IM_COL32(0, 0, 0, 140), ImGui::GetStyle().ChildRounding);
        l_Foot.AddText(ImVec2(l_Min.x + l_Padding.x, l_Min.y + l_Padding.y), ImGui::GetColorU32(ImGuiCol_Text), l_Text.c_str());
    }

    if (m_CameraDirty && ImGui::GetTime() - m_CameraChangeTime >= c_CameraSaveSeconds)
    {
        SaveCamera();
    }
}

// A size is taken once it has held for c_SettleSeconds, so dragging a splitter rebuilds the target once rather than every frame, and the sizes a panel passes through as the layout is built are skipped
void ViewportPanel::FollowPanelSize(std::uint32_t width, std::uint32_t height)
{
    const double l_Now = ImGui::GetTime();
    if (width != m_PanelWidth || height != m_PanelHeight)
    {
        m_PanelWidth = width;
        m_PanelHeight = height;
        m_PanelSizeTime = l_Now;
    }

    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const bool l_Changed = width != l_Renderer.GetSceneWidth() || height != l_Renderer.GetSceneHeight();
    if (l_Changed && l_Now - m_PanelSizeTime >= c_SettleSeconds)
    {
        l_Renderer.SetSceneSize(width, height);
    }
}

// Each saved scene keeps its cameras, and whether it was last edited in 2D or 3D. A scene saved for the first time keeps the view it had while untitled, one without a saved camera starts at the origin, and one saved before 3D editing opens in 2D
void ViewportPanel::FollowScene()
{
    const Trinity::UUID l_SceneID = m_Session.GetSceneID();
    if (l_SceneID == m_SceneID)
    {
        return;
    }

    if (m_CameraDirty)
    {
        SaveCamera();
    }

    const bool l_WasUntitled = !m_SceneID;
    m_SceneID = l_SceneID;
    m_CameraDirty = false;
    m_AutoOrthographic = false;
    StopGlide();
    m_PickPixel.reset();
    m_PicksPending = 0;
    if (!m_SceneID)
    {
        m_Camera.Reset();
        m_Camera3D.Reset(m_Session.GetPreferences().CameraSpeed);

        return;
    }

    const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(GetCameraPath(m_SceneID));
    glm::vec2 l_Position{ 0.0f };
    float l_Height = EditorCamera::c_DefaultHeight;
    if (l_Text && ParseCamera(*l_Text, l_Position, l_Height))
    {
        m_Camera.Set(l_Position, l_Height);
        if (!ParseCamera3D(*l_Text, m_Mode3D, m_Camera3D))
        {
            m_Mode3D = false;
            m_Camera3D.Reset(m_Session.GetPreferences().CameraSpeed);
        }
    }
    else if (!l_WasUntitled)
    {
        m_Camera.Reset();
        m_Camera3D.Reset(m_Session.GetPreferences().CameraSpeed);
    }
}

// Per user and per project, so it lives in Cache beside the cooked assets. An untitled scene's camera is not kept
void ViewportPanel::SaveCamera()
{
    m_CameraDirty = false;
    if (!m_SceneID || !m_Session.HasProject())
    {
        return;
    }

    const glm::vec2 l_Position = m_Camera.GetPosition();
    const glm::vec3 l_Focus = m_Camera3D.GetFocus();
    const std::string l_Text = std::format("{} {} {}\n{} {} {} {} {} {} {} {} {}\n", l_Position.x, l_Position.y, m_Camera.GetHeight(), m_Mode3D ? "3D" : "2D", l_Focus.x, l_Focus.y, l_Focus.z, m_Camera3D.GetYaw(), m_Camera3D.GetPitch(), m_Camera3D.GetDistance(), m_Camera3D.GetSpeed(), m_Camera3D.IsOrthographic() ? "Iso" : "Persp");
    const Trinity::Expected<void, Trinity::FileError> l_Written = Trinity::FileSystem::WriteText(GetCameraPath(m_SceneID), l_Text);
    if (!l_Written)
    {
        TR_WARN("Scene view: the camera of scene {} could not be saved: {}", m_SceneID, Trinity::ToString(l_Written.GetError()));
    }
}

void ViewportPanel::MarkCameraChanged()
{
    m_CameraDirty = true;
    m_CameraChangeTime = ImGui::GetTime();
}

// Middle drag pans, the wheel zooms towards the cursor, F frames the selection and Shift+F follows it, and a left click picks the top-most sprite or clears the selection. A pan begun in the Viewport carries on outside it
void ViewportPanel::HandleInput(glm::vec2 imageMin, glm::vec2 viewportSize, bool hovered, bool focused)
{
    const ImGuiIO& l_IO = ImGui::GetIO();
    const glm::vec2 l_Mouse = glm::vec2(l_IO.MousePos.x, l_IO.MousePos.y) - imageMin;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        m_Panning = true;
    }

    if (m_Panning && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        m_Panning = false;
    }

    // Panning takes the camera off what it follows. Zooming only ends a glide, so a followed entity stays in the middle at the new zoom
    if (m_Panning && (l_IO.MouseDelta.x != 0.0f || l_IO.MouseDelta.y != 0.0f))
    {
        StopGlide();
        m_Camera.Pan(glm::vec2(l_IO.MouseDelta.x, l_IO.MouseDelta.y), viewportSize);
        MarkCameraChanged();
    }

    if (hovered && l_IO.MouseWheel != 0.0f)
    {
        m_Glide.reset();
        m_Camera.ZoomAt(l_Mouse, viewportSize, l_IO.MouseWheel);
        MarkCameraChanged();
    }

    // W, E and R pick the gizmo's operation and X its axes, as long as no drag is under way
    if (focused && !l_IO.WantTextInput && !m_GizmoDragging)
    {
        if (ImGui::IsKeyChordPressed(ImGuiKey_F) || ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_F))
        {
            Frame(viewportSize, l_IO.KeyShift);
        }

        HandleGizmoKeys();
    }

    // A click on the gizmo is the gizmo's, not a pick
    if (hovered && !m_GizmoHovered && !m_GizmoDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        RequestPick(l_Mouse, viewportSize);
    }
}

// The right mouse button looks around, and while it is held W, A, S and D fly, Q and E go down and up, Shift flies faster and the wheel changes the speed. Alt with the left button orbits the focus, a middle drag pans, the wheel moves towards the focus, F frames the selection and a left click picks. A drag begun in the Viewport carries on outside it, and the gizmo's keys wait while flying, since W and E are flying keys then
void ViewportPanel::HandleInput3D(glm::vec2 imageMin, glm::vec2 viewportSize, bool hovered, bool focused)
{
    const ImGuiIO& l_IO = ImGui::GetIO();
    const glm::vec2 l_Mouse = glm::vec2(l_IO.MousePos.x, l_IO.MousePos.y) - imageMin;
    const glm::vec2 l_Delta(l_IO.MouseDelta.x, l_IO.MouseDelta.y);
    bool l_Changed = false;

    // The orientation gizmo's clicks are its own
    if (hovered && !m_OrientationHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        m_Flying = true;
    }

    if (hovered && !m_OrientationHovered && l_IO.KeyAlt && !m_GizmoDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        m_Orbiting = true;
    }

    if (hovered && !m_OrientationHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        m_Panning = true;
    }

    m_Flying = m_Flying && ImGui::IsMouseDown(ImGuiMouseButton_Right);
    m_Orbiting = m_Orbiting && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    m_Panning = m_Panning && ImGui::IsMouseDown(ImGuiMouseButton_Middle);

    // Looking, panning and flying take the camera off what it follows. Orbiting keeps the focus, so it goes on, but not a turn on its way, and moving in only ends a glide, keeping the distance it is moved to. Looking and orbiting turn an Iso side view back to perspective, as in Unity
    if ((l_Delta.x != 0.0f || l_Delta.y != 0.0f) && (m_Flying || m_Orbiting))
    {
        if (m_Glide)
        {
            m_Glide->Angles.reset();
        }

        if (std::exchange(m_AutoOrthographic, false))
        {
            m_Camera3D.SetOrthographic(false);
        }
    }

    if (l_Delta.x != 0.0f || l_Delta.y != 0.0f)
    {
        if (m_Flying)
        {
            StopGlide();
            m_Camera3D.Look(l_Delta);
            l_Changed = true;
        }
        else if (m_Orbiting)
        {
            m_Camera3D.Orbit(l_Delta);
            l_Changed = true;
        }
        else if (m_Panning)
        {
            StopGlide();
            m_Camera3D.Pan(l_Delta, viewportSize);
            l_Changed = true;
        }
    }

    if (m_Flying)
    {
        const auto a_Axis = [](ImGuiKey negative, ImGuiKey positive) { return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f); };
        const glm::vec3 l_Direction(a_Axis(ImGuiKey_A, ImGuiKey_D), a_Axis(ImGuiKey_Q, ImGuiKey_E), a_Axis(ImGuiKey_S, ImGuiKey_W));
        if (!l_IO.WantTextInput && l_Direction != glm::vec3(0.0f))
        {
            StopGlide();
            m_Camera3D.Fly(l_Direction, std::min(l_IO.DeltaTime, c_MaximumFlyStep), l_IO.KeyShift);
            l_Changed = true;
        }

        if (l_IO.MouseWheel != 0.0f)
        {
            m_Camera3D.ChangeSpeed(l_IO.MouseWheel);
            l_Changed = true;
        }
    }
    else if (hovered && l_IO.MouseWheel != 0.0f)
    {
        m_Glide.reset();
        m_Camera3D.Dolly(l_IO.MouseWheel);
        l_Changed = true;
    }

    if (l_Changed)
    {
        MarkCameraChanged();
    }

    if (focused && !l_IO.WantTextInput && !m_GizmoDragging && !m_Flying)
    {
        if (ImGui::IsKeyChordPressed(ImGuiKey_F) || ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_F))
        {
            Frame(viewportSize, l_IO.KeyShift);
        }

        HandleGizmoKeys();
    }

    // A click on the gizmo is the gizmo's, and one with Alt held begins an orbit, so neither is a pick. A light's marker is picked at once, and anything else through the entity IDs
    if (hovered && !l_IO.KeyAlt && !m_GizmoHovered && !m_GizmoDragging && !m_OrientationHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (const Trinity::Entity l_Light = PickLight(l_Mouse, viewportSize))
        {
            m_Session.SetSelection(l_Light.GetUUID());
        }
        else
        {
            RequestPick(l_Mouse, viewportSize);
        }
    }
}

// W, E and R pick the gizmo's operation, X its axes, and Ctrl+\ turns snapping on and off, as in Unity
void ViewportPanel::HandleGizmoKeys()
{
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Backslash))
    {
        m_Snap = !m_Snap;
        ImGui::MarkIniSettingsDirty();
    }

    constexpr std::array<ImGuiKey, 3> c_OperationKeys{ ImGuiKey_W, ImGuiKey_E, ImGuiKey_R };
    for (std::size_t it_Operation = 0; it_Operation < c_OperationKeys.size(); ++it_Operation)
    {
        if (ImGui::IsKeyPressed(c_OperationKeys[it_Operation], false))
        {
            m_GizmoOperation = static_cast<GizmoOperation>(it_Operation);
            ImGui::MarkIniSettingsDirty();
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_X, false))
    {
        m_GizmoLocal = !m_GizmoLocal;
        ImGui::MarkIniSettingsDirty();
    }
}

// The light whose marker is nearest the mouse, within a few pixels. A light has no surface to draw an ID on, so it is picked here rather than through the entity IDs
Trinity::Entity ViewportPanel::PickLight(glm::vec2 mouse, glm::vec2 viewportSize)
{
    Trinity::Scene& l_Scene = m_Session.GetScene();
    Trinity::SceneRegistry& l_Registry = l_Scene.GetRegistry();
    const glm::mat4 l_ViewProjection = m_Camera3D.GetProjection(viewportSize) * m_Camera3D.GetView();

    entt::entity l_Hit = entt::null;
    float l_Closest = c_LightPickPixels;
    for (const auto [it_Entity, it_Light, it_World] : l_Registry.view<Trinity::LightComponent, Trinity::WorldTransformComponent>().each())
    {
        const std::optional<glm::vec2> l_Pixel = ProjectPoint(l_ViewProjection, glm::vec3(it_World.Matrix[3]), viewportSize);
        if (l_Pixel && glm::distance(*l_Pixel, mouse) < l_Closest)
        {
            l_Closest = glm::distance(*l_Pixel, mouse);
            l_Hit = it_Entity;
        }
    }

    return l_Hit != entt::null ? Trinity::Entity(l_Hit, &l_Scene) : Trinity::Entity();
}

// The pixel under the mouse, read from the entity IDs the next frame draws. The answer comes back a frame or two later, and selects whatever was drawn there, a sprite over a mesh as they are seen
void ViewportPanel::RequestPick(glm::vec2 mouse, glm::vec2 viewportSize)
{
    if (mouse.x < 0.0f || mouse.y < 0.0f || mouse.x >= viewportSize.x || mouse.y >= viewportSize.y)
    {
        return;
    }

    m_PickPixel = glm::uvec2(mouse);
    ++m_PicksPending;
}

// Each pick that comes back selects its entity, or nothing where nothing was drawn or the entity has gone since, in the order the clicks were made. One no longer awaited, since the scene changed, is dropped
void ViewportPanel::ApplyPick()
{
    const std::optional<Trinity::PickResult> l_Result = Trinity::Application::Get().GetRenderer().TakePickResult();
    if (!l_Result || m_PicksPending == 0)
    {
        return;
    }

    --m_PicksPending;
    Trinity::Scene& l_Scene = m_Session.GetScene();
    const bool l_Valid = l_Result->Entity != entt::null && l_Scene.GetRegistry().valid(l_Result->Entity);
    m_Session.SetSelection(l_Valid ? Trinity::Entity(l_Result->Entity, &l_Scene).GetUUID() : Trinity::UUID());
}

// The selected entity and everything under it, so selecting a model's root outlines the whole model, and what the Hierarchy's search matches, as Unity's Scene view shows what its search finds
std::vector<std::uint32_t> ViewportPanel::GetOutlined()
{
    std::vector<std::uint32_t> l_Outlined;
    Trinity::Scene& l_Scene = m_Session.GetScene();
    if (const std::unordered_set<Trinity::UUID>* l_Matches = m_Highlighted ? m_Highlighted() : nullptr)
    {
        for (const Trinity::UUID it_Match : *l_Matches)
        {
            if (const Trinity::Entity l_Match = l_Scene.FindEntityByUUID(it_Match))
            {
                l_Outlined.push_back(Trinity::ToPickID(l_Match.GetHandle()));
            }
        }
    }

    const Trinity::Entity l_Selected = l_Scene.FindEntityByUUID(m_Session.GetSelection());
    if (!l_Selected)
    {
        return l_Outlined;
    }

    std::vector<Trinity::Entity> l_Pending{ l_Selected };
    while (!l_Pending.empty())
    {
        const Trinity::Entity l_Entity = l_Pending.back();
        l_Pending.pop_back();
        l_Outlined.push_back(Trinity::ToPickID(l_Entity.GetHandle()));
        for (Trinity::Entity it_Child = l_Entity.GetFirstChild(); it_Child; it_Child = it_Child.GetNextSibling())
        {
            l_Pending.push_back(it_Child);
        }
    }

    return l_Outlined;
}

// Where F puts the 2D camera: around the sprites of the selected entity and everything under it, at the entity's own position with the zoom kept when it has none, or with nothing selected around every sprite in the scene. An empty scene frames the origin
EditorCamera ViewportPanel::GetFramed(glm::vec2 viewportSize)
{
    Trinity::Scene& l_Scene = m_Session.GetScene();
    Trinity::SceneRegistry& l_Registry = l_Scene.GetRegistry();
    glm::vec2 l_Minimum(std::numeric_limits<float>::max());
    glm::vec2 l_Maximum(std::numeric_limits<float>::lowest());
    const auto a_Include = [&](entt::entity entity)
    {
        for (const glm::vec2& it_Corner : GetSpriteCorners(l_Registry.get<Trinity::WorldTransformComponent>(entity).Matrix))
        {
            l_Minimum = glm::min(l_Minimum, it_Corner);
            l_Maximum = glm::max(l_Maximum, it_Corner);
        }
    };

    EditorCamera l_Camera = m_Camera;
    const Trinity::Entity l_Selected = l_Scene.FindEntityByUUID(m_Session.GetSelection());
    if (l_Selected)
    {
        std::vector<Trinity::Entity> l_Pending{ l_Selected };
        while (!l_Pending.empty())
        {
            const Trinity::Entity l_Entity = l_Pending.back();
            l_Pending.pop_back();
            if (l_Entity.Has<Trinity::SpriteRendererComponent>())
            {
                a_Include(l_Entity.GetHandle());
            }

            for (Trinity::Entity it_Child = l_Entity.GetFirstChild(); it_Child; it_Child = it_Child.GetNextSibling())
            {
                l_Pending.push_back(it_Child);
            }
        }

        if (l_Minimum.x > l_Maximum.x)
        {
            l_Camera.Set(glm::vec2(l_Selected.Get<Trinity::WorldTransformComponent>().Matrix[3]), m_Camera.GetHeight());

            return l_Camera;
        }
    }
    else
    {
        for (const entt::entity it_Entity : l_Registry.view<Trinity::SpriteRendererComponent>())
        {
            a_Include(it_Entity);
        }
    }

    if (l_Minimum.x > l_Maximum.x)
    {
        l_Camera.Reset();
    }
    else
    {
        l_Camera.Frame(l_Minimum, l_Maximum, viewportSize);
    }

    return l_Camera;
}

// Where F puts the 3D camera: around the selected entity and everything under it, or with nothing selected the scene's meshes and sprites: meshes by their bounds, sprites by their quads, and anything else by where it is. A scene with none of them frames its entities, and an empty one the origin. The camera keeps the way it faces
EditorCamera3D ViewportPanel::GetFramed3D(glm::vec2 viewportSize)
{
    Trinity::Scene& l_Scene = m_Session.GetScene();
    glm::vec3 l_Minimum(std::numeric_limits<float>::max());
    glm::vec3 l_Maximum(std::numeric_limits<float>::lowest());
    const auto a_Include = [&](Trinity::Entity entity, bool position)
    {
        const glm::mat4& l_World = entity.Get<Trinity::WorldTransformComponent>().Matrix;
        const auto a_Add = [&](const glm::vec3& point)
        {
            l_Minimum = glm::min(l_Minimum, point);
            l_Maximum = glm::max(l_Maximum, point);
        };

        bool l_Shaped = false;
        const Trinity::MeshAsset* l_Mesh = entity.Has<Trinity::MeshRendererComponent>() ? GetLoadedMesh(entity.Get<Trinity::MeshRendererComponent>().Mesh) : nullptr;
        if (l_Mesh != nullptr)
        {
            for (const glm::vec3& it_Corner : GetBoxCorners(l_Mesh->GetBounds(), l_World))
            {
                a_Add(it_Corner);
            }

            l_Shaped = true;
        }

        if (entity.Has<Trinity::SpriteRendererComponent>())
        {
            for (const glm::vec3& it_Corner : GetSpriteCorners3D(l_World))
            {
                a_Add(it_Corner);
            }

            l_Shaped = true;
        }

        if (!l_Shaped && position)
        {
            a_Add(glm::vec3(l_World[3]));
        }
    };

    const Trinity::Entity l_Selected = l_Scene.FindEntityByUUID(m_Session.GetSelection());
    if (l_Selected)
    {
        std::vector<Trinity::Entity> l_Pending{ l_Selected };
        while (!l_Pending.empty())
        {
            const Trinity::Entity l_Entity = l_Pending.back();
            l_Pending.pop_back();
            a_Include(l_Entity, true);
            for (Trinity::Entity it_Child = l_Entity.GetFirstChild(); it_Child; it_Child = it_Child.GetNextSibling())
            {
                l_Pending.push_back(it_Child);
            }
        }
    }
    else
    {
        for (const bool it_Positions : { false, true })
        {
            if (l_Minimum.x <= l_Maximum.x)
            {
                break;
            }

            for (Trinity::Entity it_Entity = l_Scene.GetFirstRoot(); it_Entity; it_Entity = l_Scene.GetNextInHierarchyOrder(it_Entity))
            {
                a_Include(it_Entity, it_Positions);
            }
        }
    }

    EditorCamera3D l_Camera = m_Camera3D;
    if (l_Minimum.x > l_Maximum.x)
    {
        l_Camera.Reset();
    }
    else
    {
        l_Camera.Frame((l_Minimum + l_Maximum) * 0.5f, glm::length(l_Maximum - l_Minimum) * 0.5f, viewportSize);
    }

    return l_Camera;
}

ViewportPanel::Glide ViewportPanel::GetFramedGlide(glm::vec2 viewportSize)
{
    if (m_Mode3D)
    {
        const EditorCamera3D l_Framed = GetFramed3D(viewportSize);

        return { .Focus = l_Framed.GetFocus(), .Distance = l_Framed.GetDistance(), .Mode3D = true, .Angles = std::nullopt };
    }

    const EditorCamera l_Framed = GetFramed(viewportSize);

    return { .Focus = glm::vec3(l_Framed.GetPosition(), 0.0f), .Distance = l_Framed.GetHeight(), .Mode3D = false, .Angles = std::nullopt };
}

// F glides to the selection, or with nothing selected to the whole scene, as Unity's Frame Selected does. Shift+F glides there too and then follows the selection as it moves, as Unity's Lock View to Selected does, until it is pressed again
void ViewportPanel::Frame(glm::vec2 viewportSize, bool follow)
{
    const Trinity::UUID l_Selection = m_Session.GetSelection();
    if (follow && m_Followed && m_Followed == l_Selection)
    {
        m_Followed = {};

        return;
    }

    m_Followed = follow && m_Session.GetScene().FindEntityByUUID(l_Selection) ? l_Selection : Trinity::UUID();
    m_Glide = GetFramedGlide(viewportSize);
}

// Each frame closes the same share of what is left by the time it took, so the glide slows as it arrives and takes as long at any frame rate. Distances go by their ratio, so a long way in or out moves as evenly as a short one, and a turn takes the short way round. While following, the focus is the entity's again each frame, at the distance the first glide was going to, or once it is over, the camera's own. The camera holds still while the gizmo drags, so what it drags stays under the mouse
void ViewportPanel::UpdateGlide(glm::vec2 viewportSize)
{
    if (m_GizmoDragging)
    {
        return;
    }

    if (m_Followed && (m_Session.GetSelection() != m_Followed || !m_Session.GetScene().FindEntityByUUID(m_Followed)))
    {
        m_Followed = {};
    }

    if (m_Followed)
    {
        const bool l_Gliding = m_Glide && m_Glide->Mode3D == m_Mode3D;
        const float l_Distance = l_Gliding ? m_Glide->Distance : (m_Mode3D ? m_Camera3D.GetDistance() : m_Camera.GetHeight());
        m_Glide = Glide{ .Focus = GetFramedGlide(viewportSize).Focus, .Distance = l_Distance, .Mode3D = m_Mode3D, .Angles = l_Gliding ? m_Glide->Angles : std::nullopt };
    }

    if (!m_Glide || m_Glide->Mode3D != m_Mode3D)
    {
        m_Glide.reset();

        return;
    }

    const glm::vec3 l_From = m_Mode3D ? m_Camera3D.GetFocus() : glm::vec3(m_Camera.GetPosition(), 0.0f);
    const float l_FromDistance = std::max(m_Mode3D ? m_Camera3D.GetDistance() : m_Camera.GetHeight(), FLT_MIN);
    const float l_ToDistance = std::max(m_Glide->Distance, FLT_MIN);
    const float l_Blend = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * c_GlideRate);
    glm::vec3 l_Focus = glm::mix(l_From, m_Glide->Focus, l_Blend);
    float l_Distance = std::exp(glm::mix(std::log(l_FromDistance), std::log(l_ToDistance), l_Blend));
    const bool l_Placed = glm::distance(l_Focus, m_Glide->Focus) <= l_ToDistance * c_GlideTolerance && std::abs(std::log(l_Distance / l_ToDistance)) <= c_GlideTolerance;
    if (l_Placed)
    {
        l_Focus = m_Glide->Focus;
        l_Distance = m_Glide->Distance;
    }

    const glm::vec2 l_FromAngles(m_Camera3D.GetYaw(), m_Camera3D.GetPitch());
    glm::vec2 l_Angles = l_FromAngles;
    bool l_Turned = true;
    if (m_Glide->Angles)
    {
        l_Angles = glm::vec2(l_FromAngles.x + std::remainder(m_Glide->Angles->x - l_FromAngles.x, 360.0f) * l_Blend, glm::mix(l_FromAngles.y, m_Glide->Angles->y, l_Blend));
        l_Turned = std::abs(std::remainder(m_Glide->Angles->x - l_Angles.x, 360.0f)) <= c_GlideAngleTolerance && std::abs(m_Glide->Angles->y - l_Angles.y) <= c_GlideAngleTolerance;
        if (l_Turned)
        {
            l_Angles = *m_Glide->Angles;
        }
    }

    const bool l_Arrived = l_Placed && l_Turned;
    if (l_Focus != l_From || l_Distance != l_FromDistance || l_Angles != l_FromAngles)
    {
        if (m_Mode3D)
        {
            m_Camera3D.Set(l_Focus, l_Angles.x, l_Angles.y, l_Distance, m_Camera3D.GetSpeed());
        }
        else
        {
            m_Camera.Set(glm::vec2(l_Focus), l_Distance);
        }

        MarkCameraChanged();
    }

    if (l_Arrived && !m_Followed)
    {
        m_Glide.reset();
    }
}

// The camera moved by hand: framing's glide ends, and so does following
void ViewportPanel::StopGlide()
{
    m_Glide.reset();
    m_Followed = {};
}

// A glide about the focus to the yaw and pitch, keeping the focus and distance any glide on its way has, so a frame or a follow goes on. Iso turned to on the way is automatic, as a side view's is, and goes back to perspective once the view is turned by hand
void ViewportPanel::TurnView(float yaw, float pitch, bool orthographic, bool automatic)
{
    const bool l_Gliding = m_Glide && m_Glide->Mode3D;
    m_Glide = Glide{ .Focus = l_Gliding ? m_Glide->Focus : m_Camera3D.GetFocus(), .Distance = l_Gliding ? m_Glide->Distance : m_Camera3D.GetDistance(), .Mode3D = true, .Angles = glm::vec2(yaw, pitch) };
    if (orthographic != m_Camera3D.IsOrthographic())
    {
        m_Camera3D.SetOrthographic(orthographic);
        m_AutoOrthographic = orthographic && automatic;
    }
    else if (!orthographic)
    {
        m_AutoOrthographic = false;
    }

    MarkCameraChanged();
}

// Ctrl flips the toggle for as long as it is held
bool ViewportPanel::IsSnapping() const
{
    return m_Snap != ImGui::GetIO().KeyCtrl;
}

// Where a dropped asset lands: under the mouse on Z = 0 in 2D, and in 3D where the mouse's ray meets the ground, Y = 0, or a little way ahead of the camera when it meets the ground nowhere near
glm::vec3 ViewportPanel::GetDropPoint(glm::vec2 imageMin, glm::vec2 viewportSize) const
{
    const ImVec2 l_Mouse = ImGui::GetMousePos();
    const glm::vec2 l_Pixel = glm::vec2(l_Mouse.x, l_Mouse.y) - imageMin;
    if (!m_Mode3D)
    {
        return glm::vec3(m_Camera.ScreenToWorld(l_Pixel, viewportSize), 0.0f);
    }

    const glm::vec3 l_Origin = m_Camera3D.GetRayOrigin(l_Pixel, viewportSize);
    const glm::vec3 l_Direction = m_Camera3D.GetRayDirection(l_Pixel, viewportSize);
    const float l_Distance = std::abs(l_Direction.y) > 1e-6f ? -l_Origin.y / l_Direction.y : -1.0f;

    return l_Origin + l_Direction * (l_Distance > 0.0f && l_Distance <= c_MaximumDropDistance ? l_Distance : c_DropDistance);
}

// Dropped from the Content Browser, a texture becomes a sprite where it lands, at its pixel size, and a model becomes its hierarchy under one root there, each in one command. Nothing is taken while a copy plays
void ViewportPanel::AcceptAssetDrop(glm::vec2 imageMin, glm::vec2 viewportSize)
{
    if (m_Session.IsPlaying() || !ImGui::BeginDragDropTarget())
    {
        return;
    }

    if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_AssetPayload))
    {
        std::uint64_t l_Value = 0;
        std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));
        const Trinity::UUID l_ID(l_Value);
        const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
        const Trinity::AssetRecord* l_Record = l_Registry != nullptr ? l_Registry->Find(l_ID) : nullptr;
        const glm::vec3 l_Point = GetDropPoint(imageMin, viewportSize);
        if (l_Record != nullptr && l_Record->Importer == ModelImporter::c_Importer)
        {
            static_cast<void>(m_Session.CreateModel(l_ID, {}, {}, l_Point));
        }
        else if (l_Record == nullptr || l_Record->Importer != Trinity::TextureAsset::c_AssetType)
        {
            TR_WARN("Forge: only a texture or a model dropped into the Viewport makes entities");
        }
        else
        {
            // From the texture when it is loaded, and otherwise from the source file's header
            glm::vec2 l_Pixels{ c_PixelsPerUnit };
            const Trinity::Asset* l_Asset = Trinity::AssetManager::GetState(l_ID) == Trinity::AssetState::Ready ? Trinity::AssetManager::GetAsset(l_ID) : nullptr;
            if (l_Asset != nullptr && l_Asset->GetAssetType() == Trinity::TextureAsset::c_AssetType)
            {
                const Trinity::TextureAsset& l_Texture = static_cast<const Trinity::TextureAsset&>(*l_Asset);
                l_Pixels = { static_cast<float>(l_Texture.GetWidth()), static_cast<float>(l_Texture.GetHeight()) };
            }
            else if (const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_Source = Trinity::FileSystem::ReadFile(l_Record->Path))
            {
                int l_Width = 0;
                int l_Height = 0;
                int l_Channels = 0;
                if (stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(l_Source->data()), static_cast<int>(l_Source->size()), &l_Width, &l_Height, &l_Channels) != 0)
                {
                    l_Pixels = { static_cast<float>(l_Width), static_cast<float>(l_Height) };
                }
            }

            const std::string l_Name = std::filesystem::path(l_Record->Path).stem().string();
            m_Session.GetHistory().Execute(Trinity::CreateScope<CreateSpriteCommand>(l_Name, l_ID, l_Point, l_Pixels / c_PixelsPerUnit));
        }
    }

    ImGui::EndDragDropTarget();
}

// The primary camera's bounds, at the Viewport's aspect ratio as a game drawn here would show them. Corners snap to pixel centres so one-pixel lines stay sharp. The selection is outlined from the entity IDs
void ViewportPanel::DrawOverlays(ImDrawList& drawList, glm::vec2 imageMin, glm::vec2 viewportSize)
{
    Trinity::Scene& l_Scene = m_Session.GetScene();
    Trinity::SceneRegistry& l_Registry = l_Scene.GetRegistry();
    const auto a_ToScreen = [&](glm::vec2 world)
    {
        const glm::vec2 l_Pixel = imageMin + m_Camera.WorldToScreen(world, viewportSize);

        return ImVec2(std::floor(l_Pixel.x) + 0.5f, std::floor(l_Pixel.y) + 0.5f);
    };

    const auto a_DrawQuad = [&](const std::array<glm::vec2, 4>& corners, ImU32 color, float thickness)
    {
        const std::array<ImVec2, 4> l_Points{ a_ToScreen(corners[0]), a_ToScreen(corners[1]), a_ToScreen(corners[2]), a_ToScreen(corners[3]) };
        drawList.AddPolyline(l_Points.data(), static_cast<int>(l_Points.size()), color, thickness, ImDrawFlags_Closed);
    };

    for (Trinity::Entity it_Entity = l_Scene.GetFirstRoot(); it_Entity; it_Entity = l_Scene.GetNextInHierarchyOrder(it_Entity))
    {
        const Trinity::CameraComponent* l_Camera = l_Registry.try_get<Trinity::CameraComponent>(it_Entity.GetHandle());
        if (l_Camera == nullptr || !l_Camera->Primary)
        {
            continue;
        }

        const float l_HalfHeight = l_Camera->OrthographicSize * 0.5f;
        const float l_HalfWidth = l_HalfHeight * (viewportSize.y > 0.0f ? viewportSize.x / viewportSize.y : 1.0f);
        const glm::mat4 l_Bounds = l_Registry.get<Trinity::WorldTransformComponent>(it_Entity.GetHandle()).Matrix * glm::scale(glm::mat4(1.0f), glm::vec3(l_HalfWidth * 2.0f, l_HalfHeight * 2.0f, 1.0f));
        a_DrawQuad(GetSpriteCorners(l_Bounds), c_CameraOutlineColor, 1.0f);

        break;
    }
}

// Through the 3D camera: the primary camera's frustum a little way out, a marker on every light, since nothing else shows where one is, and for a selected light its reach: a point light's range as three circles, a spot light's cones and the way a directional light shines. Lines are cut where they pass behind the eye. The selection itself is outlined from the entity IDs
void ViewportPanel::DrawOverlays3D(ImDrawList& drawList, glm::vec2 imageMin, glm::vec2 viewportSize)
{
    Trinity::Scene& l_Scene = m_Session.GetScene();
    Trinity::SceneRegistry& l_Registry = l_Scene.GetRegistry();
    const glm::mat4 l_View = m_Camera3D.GetView();
    const glm::mat4 l_Projection = m_Camera3D.GetProjection(viewportSize);
    const auto a_ToScreen = [&](const glm::vec3& view)
    {
        const glm::vec4 l_Clip = l_Projection * glm::vec4(view, 1.0f);

        return ImVec2(imageMin.x + (l_Clip.x / l_Clip.w + 1.0f) * 0.5f * viewportSize.x, imageMin.y + (1.0f - l_Clip.y / l_Clip.w) * 0.5f * viewportSize.y);
    };

    // Each end in view space, where the eye looks down -Z, and the part nearer than the near plane cut off
    const auto a_Line = [&](const glm::vec3& from, const glm::vec3& to, ImU32 color, float thickness)
    {
        glm::vec3 l_From(l_View * glm::vec4(from, 1.0f));
        glm::vec3 l_To(l_View * glm::vec4(to, 1.0f));
        const float l_Plane = -EditorCamera3D::c_Near;
        if (l_From.z > l_Plane && l_To.z > l_Plane)
        {
            return;
        }

        if (l_From.z > l_Plane)
        {
            l_From = glm::mix(l_From, l_To, (l_From.z - l_Plane) / (l_From.z - l_To.z));
        }
        else if (l_To.z > l_Plane)
        {
            l_To = glm::mix(l_To, l_From, (l_To.z - l_Plane) / (l_To.z - l_From.z));
        }

        drawList.AddLine(a_ToScreen(l_From), a_ToScreen(l_To), color, thickness);
    };

    const auto a_Circle = [&](const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB, float radius, ImU32 color)
    {
        glm::vec3 l_Previous = center + axisA * radius;
        for (int it_Segment = 1; it_Segment <= c_CircleSegments; ++it_Segment)
        {
            const float l_Angle = glm::two_pi<float>() * static_cast<float>(it_Segment) / static_cast<float>(c_CircleSegments);
            const glm::vec3 l_Point = center + (axisA * std::cos(l_Angle) + axisB * std::sin(l_Angle)) * radius;
            a_Line(l_Previous, l_Point, color, 1.0f);
            l_Previous = l_Point;
        }
    };

    for (Trinity::Entity it_Entity = l_Scene.GetFirstRoot(); it_Entity; it_Entity = l_Scene.GetNextInHierarchyOrder(it_Entity))
    {
        const Trinity::CameraComponent* l_Camera = l_Registry.try_get<Trinity::CameraComponent>(it_Entity.GetHandle());
        if (l_Camera == nullptr || !l_Camera->Primary)
        {
            continue;
        }

        // Reversed depth: 1 is the near plane, and the far end is the frustum distance for a perspective camera and the far plane for an orthographic one
        const float l_AspectRatio = viewportSize.y > 0.0f ? viewportSize.x / viewportSize.y : 1.0f;
        const glm::mat4 l_Inverse = glm::inverse(Trinity::RenderView::FromCamera(*l_Camera, l_Registry.get<Trinity::WorldTransformComponent>(it_Entity.GetHandle()).Matrix, l_AspectRatio).ViewProjection);
        const float l_FarDepth = l_Camera->Projection == Trinity::CameraProjection::Perspective ? l_Camera->PerspectiveNear / c_FrustumDistance : 0.0f;
        std::array<glm::vec3, 8> l_Corners{};
        for (std::size_t it_Corner = 0; it_Corner < l_Corners.size(); ++it_Corner)
        {
            const glm::vec4 l_Point = l_Inverse * glm::vec4((it_Corner & 1) != 0 ? 1.0f : -1.0f, (it_Corner & 2) != 0 ? 1.0f : -1.0f, (it_Corner & 4) != 0 ? l_FarDepth : 1.0f, 1.0f);
            l_Corners[it_Corner] = glm::vec3(l_Point) / l_Point.w;
        }

        for (const auto [it_From, it_To] : { std::pair{ 0, 1 },{ 1, 3 },{ 3, 2 },{ 2, 0 },{ 4, 5 },{ 5, 7 },{ 7, 6 },{ 6, 4 },{ 0, 4 },{ 1, 5 },{ 2, 6 },{ 3, 7 } })
        {
            a_Line(l_Corners[static_cast<std::size_t>(it_From)], l_Corners[static_cast<std::size_t>(it_To)], c_CameraOutlineColor, 1.0f);
        }

        break;
    }

    const glm::mat4 l_ViewProjection = l_Projection * l_View;
    for (const auto [it_Entity, it_Light, it_World] : l_Registry.view<Trinity::LightComponent, Trinity::WorldTransformComponent>().each())
    {
        if (const std::optional<glm::vec2> l_Pixel = ProjectPoint(l_ViewProjection, glm::vec3(it_World.Matrix[3]), viewportSize))
        {
            drawList.AddCircleFilled(ImVec2(imageMin.x + l_Pixel->x, imageMin.y + l_Pixel->y), c_LightMarkerRadius, GetLightColor(it_Light, 0.9f));
            drawList.AddCircle(ImVec2(imageMin.x + l_Pixel->x, imageMin.y + l_Pixel->y), c_LightMarkerRadius, IM_COL32(0, 0, 0, 200));
        }
    }

    const Trinity::Entity l_Selected = l_Scene.FindEntityByUUID(m_Session.GetSelection());
    if (!l_Selected)
    {
        return;
    }

    const glm::mat4& l_World = l_Selected.Get<Trinity::WorldTransformComponent>().Matrix;
    if (l_Selected.Has<Trinity::LightComponent>())
    {
        const Trinity::LightComponent& l_Light = l_Selected.Get<Trinity::LightComponent>();
        const ImU32 l_Color = GetLightColor(l_Light, 1.0f);
        const glm::vec3 l_Position(l_World[3]);
        const float l_Length = glm::length(glm::vec3(l_World[2]));
        const glm::vec3 l_Forward = l_Length > 1e-12f ? -glm::vec3(l_World[2]) / l_Length : glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 l_Side = glm::normalize(glm::cross(l_Forward, std::abs(l_Forward.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f)));
        const glm::vec3 l_Up = glm::cross(l_Side, l_Forward);
        switch (l_Light.Type)
        {
            case Trinity::LightType::Point:
            {
                const float l_Range = l_Light.GetRange();
                a_Circle(l_Position, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), l_Range, l_Color);
                a_Circle(l_Position, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), l_Range, l_Color);
                a_Circle(l_Position, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), l_Range, l_Color);
                break;
            }
            case Trinity::LightType::Spot:
            {
                // Each cone ends where it meets the sphere of the light's range, which is a circle round the axis
                const float l_Range = l_Light.GetRange();
                for (const float it_Angle : { l_Light.OuterConeAngle, l_Light.InnerConeAngle })
                {
                    const float l_Radians = glm::radians(std::clamp(it_Angle, 0.0f, 90.0f));
                    const glm::vec3 l_Center = l_Position + l_Forward * (l_Range * std::cos(l_Radians));
                    const float l_Radius = l_Range * std::sin(l_Radians);
                    const ImU32 l_ConeColor = it_Angle == l_Light.OuterConeAngle ? l_Color : GetLightColor(l_Light, 0.45f);
                    a_Circle(l_Center, l_Side, l_Up, l_Radius, l_ConeColor);
                    for (const glm::vec3& it_Edge : { l_Side, -l_Side, l_Up, -l_Up })
                    {
                        a_Line(l_Position, l_Center + it_Edge * l_Radius, l_ConeColor, 1.0f);
                    }
                }

                a_Line(l_Position, l_Position + l_Forward * l_Range, l_Color, 1.0f);
                break;
            }
            case Trinity::LightType::Directional:
            {
                // A ring with rays along the light, sized by its distance so it reads the same at any zoom
                const float l_Size = std::max(glm::distance(l_Position, m_Camera3D.GetPosition()) * 0.15f, 0.01f);
                a_Circle(l_Position, l_Side, l_Up, l_Size * 0.25f, l_Color);
                for (int it_Ray = 0; it_Ray < 8; ++it_Ray)
                {
                    const float l_Angle = glm::two_pi<float>() * static_cast<float>(it_Ray) / 8.0f;
                    const glm::vec3 l_Start = l_Position + (l_Side * std::cos(l_Angle) + l_Up * std::sin(l_Angle)) * (l_Size * 0.25f);
                    a_Line(l_Start, l_Start + l_Forward * l_Size, l_Color, 1.0f);
                }

                a_Line(l_Position, l_Position + l_Forward * (l_Size * 1.5f), l_Color, 2.0f);
                break;
            }
        }
    }
}

// Over the image's top-left corner. The overlay takes no input, so the scene under it keeps the mouse
void ViewportPanel::DrawStats(glm::vec2 viewportSize) const
{
    const ImGuiIO& l_IO = ImGui::GetIO();
    const Trinity::RHI::DeviceInfo& l_Device = Trinity::Application::Get().GetDevice().GetInfo();
    Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();
    const Trinity::Renderer2D::Statistics& l_Sprites = l_Renderer.GetRenderer2D().GetStatistics();
    const Trinity::Entity l_Selected = m_Session.GetScene().FindEntityByUUID(m_Session.GetSelection());
    const glm::vec2 l_Position = m_Camera.GetPosition();

    const ImVec2 l_Start = ImGui::GetCursorStartPos();
    ImGui::SetCursorPos(ImVec2(l_Start.x + c_StatsMargin, l_Start.y + c_StatsMargin));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    const ImGuiChildFlags l_ChildFlags = ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
    const ImGuiWindowFlags l_WindowFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginChild("##Stats", ImVec2(0.0f, 0.0f), l_ChildFlags, l_WindowFlags))
    {
        TextLine(std::format("{:.0f} fps, {:.2f} ms", l_IO.Framerate, l_IO.Framerate > 0.0f ? 1000.0f / l_IO.Framerate : 0.0f));
        TextLine(std::format("{} on {}", Trinity::ToString(l_Device.API), l_Device.AdapterName.empty() ? "no adapter" : l_Device.AdapterName));
        TextLine(std::format("Scene {}x{}, {} sprite(s) in {} draw call(s)", l_Renderer.GetSceneWidth(), l_Renderer.GetSceneHeight(), l_Sprites.Sprites, l_Sprites.DrawCalls));
        // The counts are the main view's own, since the Game panel's view is collected and drawn in the same frame. The cluster statistics are the first view's, which is the main view
        const Trinity::Renderer3D::Statistics& l_Meshes = l_Renderer.GetRenderer3D().GetStatistics();
        const Trinity::SceneDrawList& l_Draws = l_Renderer.GetSceneDraws();
        TextLine(std::format("{} of {} submesh(es) drawn, {} of them blended, {} mesh(es) loading", l_Draws.Submeshes - l_Draws.Culled, l_Draws.Submeshes, l_Draws.Transparent.size(), l_Draws.Pending));
        if (l_Draws.DefaultSun)
        {
            TextLine("Lit by the default sun");
        }
        else
        {
            // The cluster counts are read back a couple of frames late
            const std::string l_Overfull = l_Meshes.OverfullClusters > 0 ? std::format(", {} cluster(s) over the cap of {}", l_Meshes.OverfullClusters, Trinity::ClusterGrid::c_MaxLights) : std::string();
            const std::string l_Dropped = l_Draws.DroppedLights > 0 ? std::format(", {} past the limit unlit", l_Draws.DroppedLights) : std::string();
            TextLine(std::format("{} directional and {} point or spot light(s), up to {} in a cluster{}{}", l_Draws.DirectionalCount, l_Meshes.Lights, l_Meshes.MostLightsInCluster, l_Overfull, l_Dropped));
        }

        const std::uint32_t l_ShadowMaps = (l_Draws.SunShadow != Trinity::ShadowAtlas::c_NoShadow ? Trinity::ShadowAtlas::c_Cascades : 0) + l_Draws.SpotShadows;
        if (l_ShadowMaps > 0 && !l_Draws.ShadowDraws.empty())
        {
            TextLine(std::format("{} shadow map(s), {} caster draw(s)", l_ShadowMaps, l_Draws.ShadowDraws.size()));
        }

        // Averaged over 30 frames by the graph, so the lines change twice a second at 60 fps
        const Trinity::FrameGraph& l_Graph = l_Renderer.GetFrameGraph();
        if (!l_Graph.GetPassTimes().empty())
        {
            TextLine(std::format("GPU {:.2f} ms", l_Graph.GetGpuMilliseconds()));
            for (const Trinity::FrameGraph::PassTime& it_Time : l_Graph.GetPassTimes())
            {
                TextLine(std::format("  {} {:.2f} ms", it_Time.Name, it_Time.Milliseconds));
            }
        }
        if (m_Mode3D)
        {
            const glm::vec3 l_Eye = m_Camera3D.GetPosition();
            TextLine(std::format("Camera at ({:.2f}, {:.2f}, {:.2f}), yaw {:.0f}\xC2\xB0, pitch {:.0f}\xC2\xB0, flying at {:.3g} m/s, grid every {:g}", l_Eye.x, l_Eye.y, l_Eye.z, m_Camera3D.GetYaw(), m_Camera3D.GetPitch(), m_Camera3D.GetSpeed(), EditorGrid3D::GetSpacing(l_Eye)));
        }
        else
        {
            TextLine(std::format("Camera at ({:.2f}, {:.2f}), {:.3g} units high, grid every {:g}", l_Position.x, l_Position.y, m_Camera.GetHeight(), EditorGrid::GetSpacing(m_Camera, viewportSize)));
        }

        // While a copy plays: the last frame's fixed steps and their time, and each world's bodies as it counts them
        if (Trinity::SceneRuntime* l_Runtime = m_Session.GetRuntime())
        {
            const Trinity::SceneRuntime::Statistics l_Physics = l_Runtime->GetStatistics();
            TextLine(std::format("Physics: {} step(s) in {:.2f} ms{}", l_Physics.Steps, l_Physics.StepMilliseconds, m_Session.IsPaused() ? ", paused" : ""));
            if (l_Runtime->GetPhysics() != nullptr)
            {
                TextLine(std::format("  3D: {} bodies, {} awake, {} asleep", l_Physics.Bodies3D.Bodies, l_Physics.Bodies3D.Awake, l_Physics.Bodies3D.Sleeping));
            }

            if (l_Runtime->GetPhysics2D() != nullptr)
            {
                TextLine(std::format("  2D: {} bodies, {} awake, {} asleep", l_Physics.Bodies2D.Bodies, l_Physics.Bodies2D.Awake, l_Physics.Bodies2D.Sleeping));
            }

            // The last frame's time in each language's scripts, and what each has running, as the runtime counts them
            const Trinity::ScriptRuntime::Statistics l_Scripts = l_Runtime->GetScripts().GetStatistics();
            TextLine(std::format("Scripts: {:.2f} ms in C++, {:.2f} ms in C#", l_Scripts.Cpp.Milliseconds, l_Scripts.CSharp.Milliseconds));
            for (const auto& [it_Name, it_Language] : { std::pair<const char*, const Trinity::ScriptRuntime::LanguageStatistics*>("C++", &l_Scripts.Cpp), std::pair<const char*, const Trinity::ScriptRuntime::LanguageStatistics*>("C#", &l_Scripts.CSharp) })
            {
                TextLine(std::format("  {}: {} instance(s), {} timer(s), {} coroutine(s)", it_Name, it_Language->Instances, it_Language->Timers, it_Language->Coroutines));
            }

            if (l_Scripts.Unknown > 0 || l_Scripts.Faulted > 0)
            {
                TextLine(std::format("  {} without loaded code, {} stopped by a failed call", l_Scripts.Unknown, l_Scripts.Faulted));
            }
        }

        // The garbage collector's heap, which no memory tag counts, whether or not a scene plays
        if (Trinity::ManagedScripts::IsAvailable())
        {
            const Trinity::HeapStatistics l_Heap = Trinity::ManagedScripts::GetHeapStatistics();
            TextLine(std::format("Managed heap: {:.2f} MB, {} collection(s)", static_cast<double>(l_Heap.Bytes) / (1024.0 * 1024.0), l_Heap.Collections));
        }

        TextLine(l_Selected ? std::format("Selected: {}", std::string_view(l_Selected.Get<Trinity::TagComponent>().Tag)) : std::string("Nothing selected"));
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// On the selected entity, editing its transform relative to its parent, so its children follow. ImGuizmo drags a matrix of its own, and the entity takes from it the part the operation changes. The drag is one command, and holds ImGui's active item while it lasts, so nothing else closes the command or takes the mouse
void ViewportPanel::DrawGizmo(glm::vec2 imageMin, glm::vec2 viewportSize)
{
    ImGuizmo::BeginFrame();
    m_GizmoHovered = false;
    m_GizmoID = ImGui::GetID("##Gizmo");

    Trinity::Scene& l_Scene = m_Session.GetScene();
    const Trinity::Entity l_Entity = l_Scene.FindEntityByUUID(m_Session.GetSelection());
    if (m_GizmoDragging && (!l_Entity || l_Entity.GetUUID() != m_GizmoEntity || m_Session.IsPlaying()))
    {
        EndGizmoDrag();
    }

    // A playing copy cannot be edited, so it shows no gizmo
    if (!l_Entity || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f || m_Session.IsPlaying())
    {
        return;
    }

    const Trinity::Entity l_Parent = l_Entity.GetParent();
    const glm::mat4 l_ParentWorld = l_Parent ? l_Scene.ComputeWorldMatrix(l_Parent) : glm::mat4(1.0f);
    if (!m_GizmoDragging)
    {
        m_GizmoMatrix = l_ParentWorld * l_Entity.Get<Trinity::TransformComponent>().GetMatrix();
    }

    ImGuizmo::OPERATION l_Operation = ImGuizmo::TRANSLATE;
    bool l_Changed = false;
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(imageMin.x, imageMin.y, viewportSize.x, viewportSize.y);
    if (m_Mode3D)
    {
        // A full 3D gizmo, which snaps as it drags while snapping is on, or off with Ctrl held: along the axis dragged, by the angle turned, and by the ratio scaled. It lets go of the mouse while the camera is being moved, or Alt is held to start orbiting, or the mouse is over the orientation gizmo
        const std::array<ImGuizmo::OPERATION, 3> l_Operations{ ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE };
        l_Operation = l_Operations[static_cast<std::size_t>(m_GizmoOperation)];
        const float l_Step = m_Session.GetPreferences().SnapSteps[static_cast<glm::length_t>(m_GizmoOperation)];
        const std::array<float, 3> l_Snap{ l_Step, l_Step, l_Step };
        const glm::mat4 l_View = m_Camera3D.GetView();
        const glm::mat4 l_Projection = m_Camera3D.GetGizmoProjection(viewportSize);

        ImGuizmo::SetOrthographic(m_Camera3D.IsOrthographic());
        ImGuizmo::AllowAxisFlip(true);
        ImGuizmo::Enable(m_GizmoDragging || !(ImGui::GetIO().KeyAlt || m_Orbiting || m_Flying || m_Panning || m_OrientationHovered));
        l_Changed = ImGuizmo::Manipulate(glm::value_ptr(l_View), glm::value_ptr(l_Projection), l_Operation, m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(m_GizmoMatrix), nullptr, IsSnapping() ? l_Snap.data() : nullptr);
    }
    else
    {
        // A 2D gizmo: moving in X and Y, turning about Z, and scaling X and Y, unsnapped, since the snapping is done on the entity's values
        const std::array<ImGuizmo::OPERATION, 3> l_Operations{ ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y, ImGuizmo::ROTATE_Z, ImGuizmo::SCALE_X | ImGuizmo::SCALE_Y };
        l_Operation = l_Operations[static_cast<std::size_t>(m_GizmoOperation)];
        const glm::mat4 l_View = GetGizmoView(m_Camera);
        const glm::mat4 l_Projection = GetGizmoProjection(m_Camera, viewportSize);

        ImGuizmo::SetOrthographic(true);
        ImGuizmo::AllowAxisFlip(false);
        ImGuizmo::Enable(true);
        l_Changed = ImGuizmo::Manipulate(glm::value_ptr(l_View), glm::value_ptr(l_Projection), l_Operation, m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(m_GizmoMatrix));
    }

    m_GizmoHovered = ImGuizmo::IsOver(l_Operation);

    if (ImGuizmo::IsUsing() && !m_GizmoDragging)
    {
        m_GizmoDragging = true;
        m_GizmoEntity = l_Entity.GetUUID();
        m_GizmoStart = l_Entity.Get<Trinity::TransformComponent>();
        m_Session.GetHistory().EndMerge();
    }

    if (!m_GizmoDragging)
    {
        return;
    }

    if (!ImGuizmo::IsUsing())
    {
        EndGizmoDrag();

        return;
    }

    if (ImGui::GetActiveID() != m_GizmoID)
    {
        ImGui::SetActiveID(m_GizmoID, ImGui::GetCurrentWindow());
    }

    ImGui::KeepAliveID(m_GizmoID);

    if (l_Changed)
    {
        Trinity::TransformComponent l_Value = m_Mode3D ? ApplyGizmo3D(l_ParentWorld) : ApplyGizmo(l_ParentWorld, IsSnapping());
        m_Session.GetHistory().Execute(Trinity::CreateScope<SetComponentCommand<Trinity::TransformComponent>>(m_GizmoEntity, std::move(l_Value), c_GizmoFields[static_cast<std::size_t>(m_GizmoOperation)]));
    }
}

void ViewportPanel::EndGizmoDrag()
{
    m_GizmoDragging = false;
    m_GizmoEntity = {};
    m_Session.GetHistory().EndMerge();
    if (ImGui::GetActiveID() == m_GizmoID)
    {
        ImGui::ClearActiveID();
    }
}

// Unity's Scene Gizmo, in 3D under the toolbar at the image's top-right corner: the world's axes as the camera sees them, a cone along each in its colour, and in grey along the negative ones, round a cube, with the view's name below, or Persp or Iso. A cone turns the view to look back along its axis, in Iso while the view was in perspective, the cube and the name switch between perspective and Iso, and a right-click lists the views. What is in front is drawn last, and found first under the mouse after the cube
void ViewportPanel::DrawOrientation(glm::vec2 imageMin, glm::vec2 viewportSize)
{
    m_OrientationHovered = false;
    if (!m_Mode3D)
    {
        return;
    }

    // Each side view's cone: its axis as the camera sees it, with Y down the screen, and how far it points at the eye
    struct Cone
    {
        std::size_t View = 0;
        glm::vec2 Screen{ 0.0f };
        float Depth = 0.0f;
    };

    const float l_Radius = ImGui::GetFontSize() * c_OrientationRadiusInFonts;
    const float l_Width = l_Radius * c_OrientationConeWidth;
    const float l_CubeHalf = l_Radius * c_OrientationCube;
    const glm::vec2 l_Center(imageMin.x + viewportSize.x - c_StatsMargin - l_Radius * 1.2f, imageMin.y + c_StatsMargin * 2.0f + m_ToolbarHeight + l_Radius * 1.2f);
    const glm::mat3 l_Rotation(m_Camera3D.GetView());
    std::array<Cone, c_SideViews.size()> l_Cones{};
    std::optional<std::size_t> l_Aligned;
    for (std::size_t it_View = 0; it_View < c_SideViews.size(); ++it_View)
    {
        const SideView& l_View = c_SideViews[it_View];
        glm::vec3 l_Axis(0.0f);
        l_Axis[l_View.Axis] = l_View.Sign;
        const glm::vec3 l_Seen = l_Rotation * l_Axis;
        l_Cones[it_View] = { .View = it_View, .Screen = glm::vec2(l_Seen.x, -l_Seen.y), .Depth = l_Seen.z };
        if (std::abs(std::remainder(m_Camera3D.GetYaw() - l_View.Yaw, 360.0f)) <= c_GlideAngleTolerance && std::abs(m_Camera3D.GetPitch() - l_View.Pitch) <= c_GlideAngleTolerance)
        {
            l_Aligned = it_View;
        }
    }

    std::ranges::sort(l_Cones, {}, &Cone::Depth);
    const char* l_Name = l_Aligned ? c_SideViews[*l_Aligned].Name : (m_Camera3D.IsOrthographic() ? "Iso" : "Persp");
    const ImVec2 l_NameSize = ImGui::CalcTextSize(l_Name);
    const glm::vec2 l_NameMin(l_Center.x - l_NameSize.x * 0.5f, l_Center.y + l_Radius * 1.15f);

    const glm::vec2 l_Mouse(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
    const auto a_OnCone = [&](const Cone& cone) { return GetSegmentDistance(l_Mouse, l_Center + cone.Screen * (l_Radius * c_OrientationConeBase), l_Center + cone.Screen * l_Radius) <= l_Width; };
    const auto a_OnCube = [&] { return glm::distance(l_Mouse, l_Center) <= l_CubeHalf * 1.5f; };
    std::optional<std::size_t> l_HoveredCone;
    bool l_HoveredCube = false;
    bool l_HoveredName = false;
    // The cube is the centre's, even where a cone towards the eye lies over it, so it switches the projection in a side view too
    if (ImGui::IsWindowHovered())
    {
        l_HoveredCube = a_OnCube();
        for (auto it_Cone = l_Cones.rbegin(); it_Cone != l_Cones.rend() && !l_HoveredCube && !l_HoveredCone; ++it_Cone)
        {
            if (a_OnCone(*it_Cone))
            {
                l_HoveredCone = it_Cone->View;
            }
        }

        l_HoveredName = l_Mouse.x >= l_NameMin.x && l_Mouse.x <= l_NameMin.x + l_NameSize.x && l_Mouse.y >= l_NameMin.y && l_Mouse.y <= l_NameMin.y + l_NameSize.y;
        m_OrientationHovered = glm::distance(l_Mouse, l_Center) <= l_Radius * 1.2f || l_HoveredName;
    }

    const auto a_SwitchProjection = [this]
    {
        m_Camera3D.SetOrthographic(!m_Camera3D.IsOrthographic());
        m_AutoOrthographic = false;
        MarkCameraChanged();
    };

    if (m_OrientationHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt)
    {
        if (l_HoveredCone)
        {
            TurnView(c_SideViews[*l_HoveredCone].Yaw, c_SideViews[*l_HoveredCone].Pitch, true, true);
        }
        else if (l_HoveredCube || l_HoveredName)
        {
            a_SwitchProjection();
        }
    }

    if (m_OrientationHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("##ViewMenu");
    }

    ImDrawList& l_DrawList = *ImGui::GetWindowDrawList();
    const auto a_Shade = [](ImU32 color, float light)
    {
        const ImVec4 l_Color = ImGui::ColorConvertU32ToFloat4(color);

        return ImGui::GetColorU32(ImVec4(l_Color.x * light, l_Color.y * light, l_Color.z * light, l_Color.w));
    };

    // The base faces the eye when the cone points away from it, so it goes over the side, and under it otherwise
    const auto a_DrawCone = [&](const Cone& cone)
    {
        const SideView& l_View = c_SideViews[cone.View];
        const ImU32 l_Color = l_HoveredCone == cone.View ? c_HoveredAxisColor : (l_View.Sign > 0.0f ? c_AxisColors[static_cast<std::size_t>(l_View.Axis)] : c_NegativeAxisColor);
        const float l_Length = glm::length(cone.Screen);
        const glm::vec2 l_Side = l_Length > 1e-4f ? glm::vec2(-cone.Screen.y, cone.Screen.x) / l_Length * l_Width : glm::vec2(l_Width, 0.0f);
        const glm::vec2 l_Tip = l_Center + cone.Screen * l_Radius;
        const glm::vec2 l_Base = l_Center + cone.Screen * (l_Radius * c_OrientationConeBase);
        const ImVec2 l_BaseRadius(std::max(l_Width * std::abs(cone.Depth), 0.5f), l_Width);
        const float l_Angle = std::atan2(cone.Screen.y, cone.Screen.x);
        if (cone.Depth >= 0.0f)
        {
            l_DrawList.AddEllipseFilled(ToImVec2(l_Base), l_BaseRadius, a_Shade(l_Color, 0.7f), l_Angle);
        }

        l_DrawList.AddTriangleFilled(ToImVec2(l_Tip), ToImVec2(l_Base + l_Side), ToImVec2(l_Base - l_Side), l_Color);
        if (cone.Depth < 0.0f)
        {
            l_DrawList.AddEllipseFilled(ToImVec2(l_Base), l_BaseRadius, a_Shade(l_Color, 0.7f), l_Angle);
        }

        if (l_View.Sign > 0.0f && l_Length > 0.2f)
        {
            constexpr std::array<const char*, 3> c_Labels{ "x", "y", "z" };
            const char* l_Label = c_Labels[static_cast<std::size_t>(l_View.Axis)];
            const ImVec2 l_LabelSize = ImGui::CalcTextSize(l_Label);
            const glm::vec2 l_LabelCenter = l_Center + cone.Screen / l_Length * (l_Radius * l_Length + ImGui::GetFontSize() * 0.6f);
            l_DrawList.AddText(ImVec2(l_LabelCenter.x - l_LabelSize.x * 0.5f, l_LabelCenter.y - l_LabelSize.y * 0.5f), l_Color, l_Label);
        }
    };

    // The faces turned to the eye, lighter the more squarely they face it
    const auto a_DrawCube = [&]
    {
        for (const SideView& it_Face : c_SideViews)
        {
            glm::vec3 l_Normal(0.0f);
            l_Normal[it_Face.Axis] = it_Face.Sign;
            const float l_Facing = (l_Rotation * l_Normal).z;
            if (l_Facing <= 0.0f)
            {
                continue;
            }

            glm::vec3 l_U(0.0f);
            glm::vec3 l_V(0.0f);
            l_U[(it_Face.Axis + 1) % 3] = 1.0f;
            l_V[(it_Face.Axis + 2) % 3] = 1.0f;
            constexpr std::array<glm::vec2, 4> c_Signs{ glm::vec2(1.0f, 1.0f), glm::vec2(-1.0f, 1.0f), glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f) };
            std::array<ImVec2, 4> l_Corners{};
            for (std::size_t it_Corner = 0; it_Corner < c_Signs.size(); ++it_Corner)
            {
                const glm::vec3 l_Seen = l_Rotation * ((l_Normal + l_U * c_Signs[it_Corner].x + l_V * c_Signs[it_Corner].y) * l_CubeHalf);
                l_Corners[it_Corner] = ToImVec2(l_Center + glm::vec2(l_Seen.x, -l_Seen.y));
            }

            const ImU32 l_Face = a_Shade(l_HoveredCube ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255), 0.55f + 0.45f * l_Facing);
            l_DrawList.AddQuadFilled(l_Corners[0], l_Corners[1], l_Corners[2], l_Corners[3], l_Face);
            l_DrawList.AddQuad(l_Corners[0], l_Corners[1], l_Corners[2], l_Corners[3], IM_COL32(40, 40, 40, 160));
        }
    };

    bool l_CubeDrawn = false;
    for (const Cone& it_Cone : l_Cones)
    {
        if (it_Cone.Depth >= 0.0f && !std::exchange(l_CubeDrawn, true))
        {
            a_DrawCube();
        }

        a_DrawCone(it_Cone);
    }

    if (!l_CubeDrawn)
    {
        a_DrawCube();
    }

    const ImVec2 l_Padding = ImGui::GetStyle().FramePadding;
    l_DrawList.AddRectFilled(ImVec2(l_NameMin.x - l_Padding.x, l_NameMin.y - l_Padding.y * 0.5f), ImVec2(l_NameMin.x + l_NameSize.x + l_Padding.x, l_NameMin.y + l_NameSize.y + l_Padding.y * 0.5f), IM_COL32(0, 0, 0, 140), ImGui::GetStyle().FrameRounding);
    l_DrawList.AddText(ToImVec2(l_NameMin), l_HoveredName || l_HoveredCube ? c_HoveredAxisColor : ImGui::GetColorU32(ImGuiCol_Text), l_Name);

    if (ImGui::BeginPopup("##ViewMenu"))
    {
        if (ImGui::MenuItem("Free", nullptr, !l_Aligned))
        {
            TurnView(EditorCamera3D::c_DefaultYaw, EditorCamera3D::c_DefaultPitch, false, false);
        }

        ImGui::Separator();
        for (std::size_t it_View = 0; it_View < c_SideViews.size(); ++it_View)
        {
            if (ImGui::MenuItem(c_SideViews[it_View].Name, nullptr, l_Aligned == it_View))
            {
                TurnView(c_SideViews[it_View].Yaw, c_SideViews[it_View].Pitch, true, true);
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Perspective", nullptr, !m_Camera3D.IsOrthographic()))
        {
            a_SwitchProjection();
        }

        ImGui::EndPopup();
    }
}

// The change the drag has made so far, in the parent's space, applied to the transform the drag began with. Only the part the operation changes is taken, so the rest keeps its values exactly. Snapping puts the result on the grid of the step, not the change
Trinity::TransformComponent ViewportPanel::ApplyGizmo(const glm::mat4& parentWorld, bool snap) const
{
    const glm::mat4 l_Local = glm::inverse(parentWorld) * m_GizmoMatrix;
    const glm::mat4 l_Start = m_GizmoStart.GetMatrix();
    const glm::vec3 l_Steps = m_Session.GetPreferences().SnapSteps;

    Trinity::TransformComponent l_Value = m_GizmoStart;
    switch (m_GizmoOperation)
    {
        case GizmoOperation::Translate:
        {
            l_Value.Position.x = snap ? Snap(l_Local[3].x, l_Steps.x) : l_Local[3].x;
            l_Value.Position.y = snap ? Snap(l_Local[3].y, l_Steps.x) : l_Local[3].y;
            break;
        }
        case GizmoOperation::Rotate:
        {
            l_Value.Rotation = glm::angleAxis(GetAngleZ(l_Local) - GetAngleZ(l_Start), glm::vec3(0.0f, 0.0f, 1.0f)) * m_GizmoStart.Rotation;
            if (snap)
            {
                glm::vec3 l_Euler = glm::eulerAngles(l_Value.Rotation);
                l_Euler.z = glm::radians(Snap(glm::degrees(l_Euler.z), l_Steps.y));
                l_Value.Rotation = glm::quat(l_Euler);
            }

            break;
        }
        case GizmoOperation::Scale:
        {
            // As a ratio of each axis' length, which keeps a flip. Snapping never lands on zero, which would flatten the sprite for good
            for (glm::length_t it_Axis = 0; it_Axis < 2; ++it_Axis)
            {
                const float l_Before = glm::length(glm::vec3(l_Start[it_Axis]));
                if (l_Before > 1e-12f)
                {
                    l_Value.Scale[it_Axis] = m_GizmoStart.Scale[it_Axis] * glm::length(glm::vec3(l_Local[it_Axis])) / l_Before;
                }

                if (snap)
                {
                    const float l_Snapped = Snap(l_Value.Scale[it_Axis], l_Steps.z);
                    l_Value.Scale[it_Axis] = l_Snapped != 0.0f ? l_Snapped : std::copysign(l_Steps.z, m_GizmoStart.Scale[it_Axis]);
                }
            }

            break;
        }
    }

    return l_Value;
}

// The change the drag has made so far, in the parent's space, applied to the transform the drag began with, and only the part the operation changes, as in 2D. A turn is the rotation from the start to now, which leaves the scale, flips and all, as it was, and a scale is each axis' length as a ratio of its length at the start, which keeps a flip
Trinity::TransformComponent ViewportPanel::ApplyGizmo3D(const glm::mat4& parentWorld) const
{
    const glm::mat4 l_Local = glm::inverse(parentWorld) * m_GizmoMatrix;
    const glm::mat4 l_Start = m_GizmoStart.GetMatrix();

    Trinity::TransformComponent l_Value = m_GizmoStart;
    switch (m_GizmoOperation)
    {
        case GizmoOperation::Translate:
        {
            l_Value.Position = glm::vec3(l_Local[3]);
            break;
        }
        case GizmoOperation::Rotate:
        {
            Trinity::TransformComponent l_Before;
            l_Before.SetMatrix(l_Start);
            Trinity::TransformComponent l_Now;
            l_Now.SetMatrix(l_Local);
            l_Value.Rotation = glm::normalize(l_Now.Rotation * glm::inverse(l_Before.Rotation) * m_GizmoStart.Rotation);
            break;
        }
        case GizmoOperation::Scale:
        {
            for (glm::length_t it_Axis = 0; it_Axis < 3; ++it_Axis)
            {
                const float l_Before = glm::length(glm::vec3(l_Start[it_Axis]));
                if (l_Before > 1e-12f)
                {
                    l_Value.Scale[it_Axis] = m_GizmoStart.Scale[it_Axis] * glm::length(glm::vec3(l_Local[it_Axis])) / l_Before;
                }
            }

            break;
        }
    }

    return l_Value;
}

// Over the image's top-right corner: the operation, the axes, and the steps Ctrl snaps to. Placed by its width last frame, since it sizes itself to what it holds
void ViewportPanel::DrawToolbar()
{
    const ImVec2 l_Start = ImGui::GetCursorStartPos();
    ImGui::SetCursorPos(ImVec2(std::max(l_Start.x + c_StatsMargin, ImGui::GetWindowWidth() - m_ToolbarWidth - c_StatsMargin), l_Start.y + c_StatsMargin));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));

    const ImGuiChildFlags l_ChildFlags = ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (ImGui::BeginChild("##Toolbar", ImVec2(0.0f, 0.0f), l_ChildFlags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        if (ImGui::Button(std::format("{}###ViewMode", m_Mode3D ? "3D" : "2D").c_str()))
        {
            m_Mode3D = !m_Mode3D;
            m_Flying = false;
            m_Orbiting = false;
            StopGlide();
            MarkCameraChanged();
        }

        ImGui::SetItemTooltip("The editor's camera: looking down -Z at the sprites in 2D, or in perspective in 3D, where the right mouse button looks and with W, A, S, D, Q and E flies, Alt with the left button orbits, the middle button pans and the wheel moves in. Each scene keeps its own");
        ImGui::SameLine();

        const bool l_Heatmap = m_LightHeatmap;
        if (l_Heatmap)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }

        if (ImGui::Button(std::format("{}###LightHeatmap", Trinity::Icons::c_PaintBrush).c_str()))
        {
            m_LightHeatmap = !m_LightHeatmap;
            ImGui::MarkIniSettingsDirty();
        }

        if (l_Heatmap)
        {
            ImGui::PopStyleColor();
        }

        ImGui::SetItemTooltip("Light heatmap: how many point and spot lights each cluster holds, from blue for none to red at the cap of %u and magenta past it", Trinity::ClusterGrid::c_MaxLights);
        ImGui::SameLine();

        const bool l_PhysicsDebug = m_PhysicsDebug;
        if (l_PhysicsDebug)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }

        if (ImGui::Button(std::format("{}###PhysicsDebug", Trinity::Icons::c_CubeOutline).c_str()))
        {
            m_PhysicsDebug = !m_PhysicsDebug;
            ImGui::MarkIniSettingsDirty();
        }

        if (l_PhysicsDebug)
        {
            ImGui::PopStyleColor();
        }

        ImGui::SetItemTooltip("Physics: every collider and joint in the scene, not only the selection's, and while it plays, where bodies touch, each with its normal");
        ImGui::SameLine();
        ImGui::SameLine();

        constexpr std::array<const char*, 3> c_Icons{ Trinity::Icons::c_Arrows, Trinity::Icons::c_RotateRight, Trinity::Icons::c_Expand };
        constexpr std::array<const char*, 3> c_Tips{ "Move (W)", "Rotate about Z (E)", "Scale (R)" };
        for (std::size_t it_Operation = 0; it_Operation < c_Icons.size(); ++it_Operation)
        {
            const bool l_Current = static_cast<std::size_t>(m_GizmoOperation) == it_Operation;
            if (l_Current)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }

            if (ImGui::Button(std::format("{}##Operation{}", c_Icons[it_Operation], it_Operation).c_str()))
            {
                m_GizmoOperation = static_cast<GizmoOperation>(it_Operation);
                ImGui::MarkIniSettingsDirty();
            }

            if (l_Current)
            {
                ImGui::PopStyleColor();
            }

            ImGui::SetItemTooltip("%s", m_Mode3D && it_Operation == 1 ? "Rotate (E)" : c_Tips[it_Operation]);
            ImGui::SameLine();
        }

        if (ImGui::Button(std::format("{} {}###Space", m_GizmoLocal ? Trinity::Icons::c_Cube : Trinity::Icons::c_Globe, m_GizmoLocal ? "Local" : "World").c_str()))
        {
            m_GizmoLocal = !m_GizmoLocal;
            ImGui::MarkIniSettingsDirty();
        }

        ImGui::SetItemTooltip("The gizmo's axes: the entity's own, or the world's (X)");

        // The preferences' steps, which Edit > Preferences shows too
        EditorPreferences l_Preferences = m_Session.GetPreferences();
        const auto a_Step = [this, &l_Preferences](const char* id, glm::length_t axis, const char* format, const char* tip)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.5f);
            if (ImGui::DragFloat(id, &l_Preferences.SnapSteps[axis], 0.01f, EditorPreferences::c_MinimumSnap, EditorPreferences::c_MaximumSnap, format, ImGuiSliderFlags_AlwaysClamp))
            {
                m_Session.SetPreferences(l_Preferences);
            }

            ImGui::SetItemTooltip("%s", tip);
        };

        ImGui::SameLine();
        const bool l_Snap = m_Snap;
        if (l_Snap)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }

        if (ImGui::Button(std::format("{}###Snap", Trinity::Icons::c_Magnet).c_str()))
        {
            m_Snap = !m_Snap;
            ImGui::MarkIniSettingsDirty();
        }

        if (l_Snap)
        {
            ImGui::PopStyleColor();
        }

        ImGui::SetItemTooltip("Snap (Ctrl+\\): while it is on, every drag goes by these steps. Ctrl held while dragging does the opposite, snapping while it is off and moving freely while it is on");
        a_Step("##SnapTranslate", 0, "%g", "Move snap, in units");
        a_Step("##SnapRotate", 1, "%g\xC2\xB0", "Rotate snap, in degrees");
        a_Step("##SnapScale", 2, "%g", "Scale snap");

        m_ToolbarWidth = ImGui::GetWindowWidth();
        m_ToolbarHeight = ImGui::GetWindowHeight();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// The scene target's size, which the image is drawn at and the camera maps through
glm::vec2 ViewportPanel::GetViewportSize() const
{
    const Trinity::Renderer& l_Renderer = Trinity::Application::Get().GetRenderer();

    return { static_cast<float>(l_Renderer.GetSceneWidth()), static_cast<float>(l_Renderer.GetSceneHeight()) };
}