#include "EditorLayer.h"

#include "imgui/imgui.h"
#include "ImGuizmo/ImGuizmo.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Trinity/Scene/SceneSerializer.h"
#include "Trinity/Utilities/PlatformUtils.h"
#include "Trinity/Math/Math.h"

namespace Trinity
{
    extern const std::filesystem::path g_AssetPath;

    EditorLayer::EditorLayer() : Layer("Forge Layer"), m_CameraController(1600.0f / 900.0f)
    {
        
    }

    void EditorLayer::OnAttach()
    {
        m_IconPlay = Texture2D::Create("Resources/Icons/PlayButton.png");
        m_IconSimulate = Texture2D::Create("Resources/Icons/SimulateButton.png");
        m_IconStop = Texture2D::Create("Resources/Icons/StopButton.png");

        FramebufferSpecifications specs;
        specs.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
        specs.Width = 1600;
        specs.Height = 900;
        m_Framebuffer = Framebuffer::Create(specs);

        m_EditorScene = CreateRef<Scene>();
        m_ActiveScene = m_EditorScene;

        auto commandLineArgs = Application::Get().GetCommandLineArgs();
        if (commandLineArgs.Count > 1)
        {
            auto sceneFilePath = commandLineArgs[1];
            SceneSerializer serializer(m_ActiveScene);
            serializer.DeserializeText(sceneFilePath);
        }

        m_EditorCamera = EditorCamera(30.0f, 1.778f, 0.1f, 1000.0f);
    }

    void EditorLayer::OnDetach()
    {

    }

    void EditorLayer::OnUpdate(Timestep timestep)
    {
        if (FramebufferSpecifications spec = m_Framebuffer->GetSpecification();
            m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f && (spec.Width != m_ViewportSize.x || spec.Height != m_ViewportSize.y))
        {
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_CameraController.OnResize(m_ViewportSize.x, m_ViewportSize.y);
            m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);

            m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        }

        // Render
        Renderer2D::ResetStats();
        m_Framebuffer->Bind();
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });

        m_Framebuffer->ClearAttachment(1, -1);

        switch (m_SceneState)
        {
            case SceneState::Edit:
            {
                // Update
                if (m_ViewportFocused)
                {
                    m_CameraController.OnUpdate(timestep);
                }

                m_EditorCamera.OnUpdate(timestep);

                m_ActiveScene->OnUpdateEditor(timestep, m_EditorCamera);
                break;
            }

            case SceneState::Simulate:
            {
                m_EditorCamera.OnUpdate(timestep);

                m_ActiveScene->OnUpdateSimulation(timestep, m_EditorCamera);
                break;
            }

            case SceneState::Play:
            {
                m_ActiveScene->OnUpdateRuntime(timestep);
                break;
            }
        }

        auto[mX, mY] = ImGui::GetMousePos();
        mX -= m_ViewportBounds[0].x;
        mY -= m_ViewportBounds[0].y;
        glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
        mY = viewportSize.y - mY;

        int mouseX = int(mX);
        int mouseY = int(mY);

        if (mouseX >= 0 && mouseY >= 0 && mouseX < (int)viewportSize.x && mouseY < (int)viewportSize.y)
        {
            int pixelData = m_Framebuffer->ReadPixel(1, mouseX, mouseY);
            m_HoveredEntity = pixelData == -1 ? Entity() : Entity((entt::entity)pixelData, m_ActiveScene.get());
        }

        OnOverlayRenderer();

        m_Framebuffer->Unbind();
    }

    void EditorLayer::OnImGuiRender()
    {
#pragma region DockSpace
        static bool dockspaceOpen = true;

        static bool opt_fullscreen = true;
        static bool opt_padding = false;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }
        else
        {
            dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
        }

        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;

        if (!opt_padding)
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
        if (!opt_padding)
            ImGui::PopStyleVar();

        if (opt_fullscreen)
            ImGui::PopStyleVar(2);
#pragma endregion

        ImGuiIO& io = ImGui::GetIO();
        ImGuiStyle& style = ImGui::GetStyle();
        float minWinSizeX = style.WindowMinSize.x;
        style.WindowMinSize.x = 370.0f;

        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        style.WindowMinSize.x = minWinSizeX;

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Scene", "Ctr+N"))
                {
                    NewScene();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Open...", "Ctr+O"))
                {
                    OpenScene();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Save", "Ctr+S"))
                {
                    SaveScene();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Save As...", "Ctr+Shift+S"))
                {
                    SaveSceneAs();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit"))
                { 
                    Application::Get().Close();
                }

                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

		m_SceneHierarchyPanel.OnImGuiRender();
        m_ContentBrowserPanel.OnImGuiRender();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
        ImGui::Begin("Viewport");
        {
            auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
            auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
            auto viewportOffset = ImGui::GetWindowPos();

            m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
            m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

            m_ViewportFocused = ImGui::IsWindowFocused();
            m_ViewportHovered = ImGui::IsWindowHovered();

            Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused && !m_ViewportHovered);

            ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
            m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

            uint64_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
            ImGui::Image(reinterpret_cast<void*>(textureID), ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0.0f, 1.0f }, ImVec2{ 1.0f, 0.0f });

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    const wchar_t* path = (const wchar_t*)payload->Data;
                    OpenScene(std::filesystem::path(g_AssetPath) / path);
                }

                ImGui::EndDragDropTarget();
            }

            Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
            if (selectedEntity && m_GizmoType != -1 && /*This is temporary->*/ m_SceneState != SceneState::Play /*<-Till I fix the camera situation*/)
            {
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y, m_ViewportBounds[1].x - m_ViewportBounds[0].x, m_ViewportBounds[1].y - m_ViewportBounds[0].y);

                // RUNTIME CAMERA
                //auto cameraEntity = m_ActiveScene->GetPrimaryCameraEntity();
                //const auto& camera = cameraEntity.GetComponent<CameraComponent>().Camera;

                //const glm::mat4& cameraProjection = camera.GetProjection();
                //glm::mat4 cameraView = glm::inverse(cameraEntity.GetComponent<TransformComponent>().GetTransform());

                // EDITOR CAMERA
                const glm::mat4& cameraProjection = m_EditorCamera.GetProjection();
                glm::mat4 cameraView = m_EditorCamera.GetViewMatrix();

                auto& transformComponent = selectedEntity.GetComponent<TransformComponent>();
                glm::mat4 transform = transformComponent.GetTransform();

                bool snap = Input::IsKeyPressed(Key::LeftControl);
                float snapValue = 0.5f;

                if (m_GizmoType == ImGuizmo::OPERATION::ROTATE)
                {
                    snapValue = 45.0f;
                }

                float snapValues[3] = { snapValue, snapValue , snapValue };

                ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                    (ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform),
                    nullptr, snap ? snapValues : nullptr);

                if (ImGuizmo::IsUsing())
                {
                    glm::vec3 translation;
                    glm::vec3 rotation;
                    glm::vec3 scale;
                    Math::DecomposeTransform(transform, translation, rotation, scale);

                    glm::vec3 deltaRotation = rotation - transformComponent.Rotation;
                    transformComponent.Translation = translation;
                    transformComponent.Rotation += deltaRotation;
                    transformComponent.Scale = scale;
                }
            }

            ImGui::End();
            ImGui::PopStyleVar();
        }

        ImGui::Begin("Stats");
        {
            std::string hoveredName = "None";
            if (m_HoveredEntity)
            {
                hoveredName = m_HoveredEntity.GetComponent<TagComponent>().Tag;
            }
            ImGui::Text("Hovered Entity: %s", hoveredName.c_str());

            std::string selectedName = "None";
            if (m_SceneHierarchyPanel.GetSelectedEntity())
            {
                m_SelectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
                selectedName = m_SelectedEntity.GetComponent<TagComponent>().Tag;
            }
            ImGui::Text("Selected Entity: %s", selectedName.c_str());
            
            auto stats = Renderer2D::GetStats();
            ImGui::Text("Renderer2D Stats:");
            ImGui::Text("Draw Calls: %d", stats.DrawCalls);
            ImGui::Text("Quads: %d", stats.QuadCount);
            ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
            ImGui::Text("Indicies: %d", stats.GetTotalIndexCount());

            ImGui::End();
        }

        ImGui::Begin("Settings");
        {
            ImGui::Checkbox("Show Colliders", &m_ShowColliders);

            ImGui::End();
        }

        UI_ToolBar();

        ImGui::End();
    }

    void EditorLayer::UI_ToolBar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        auto& colors = ImGui::GetStyle().Colors;

        const auto& buttonHoveredColor = colors[ImGuiCol_ButtonHovered];
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHoveredColor.x, buttonHoveredColor.y, buttonHoveredColor.z, 0.5f));

        const auto& buttonActiveColor = colors[ImGuiCol_ButtonActive];
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonActiveColor.x, buttonActiveColor.y, buttonActiveColor.z, 0.5f));

        ImGui::Begin("##ToolBar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            bool toolbarEnabled = (bool)m_ActiveScene;

            ImVec4 tintColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            if (!toolbarEnabled)
            {
                tintColor.w = 0.5f;
            }

            float size = ImGui::GetWindowHeight() - 4.0f;
            {
                Ref<Texture2D> icon = (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate) ? m_IconPlay : m_IconStop;
                ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 0.5f));
                if (toolbarEnabled && ImGui::ImageButton((ImTextureID)icon->GetRendererID(), ImVec2(size, size), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), 0, 
                    ImVec4(0.0f, 0.0f, 0.0f, 0.0f), tintColor))
                {
                    if (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate)
                    {
                        OnScenePlay();
                    }
                    else if (m_SceneState == SceneState::Play)
                    {
                        OnSceneStop();
                    }
                }
            }

            ImGui::SameLine();
            {
                Ref<Texture2D> icon = (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Play) ? m_IconSimulate : m_IconStop;
                if (toolbarEnabled && ImGui::ImageButton((ImTextureID)icon->GetRendererID(), ImVec2(size, size), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), 0, 
                    ImVec4(0.0f, 0.0f, 0.0f, 0.0f), tintColor))
                {
                    if (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Play)
                    {
                        OnSceneSimulate();
                    }
                    else if (m_SceneState == SceneState::Simulate)
                    {
                        OnSceneStop();
                    }
                }
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);

            ImGui::End();
        }
    }

    void EditorLayer::OnEvent(Event& e)
    {
        m_CameraController.OnEvent(e);
        m_EditorCamera.OnEvent(e);

        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<KeyPressedEvent>(TR_BIND_EVENT_FN(EditorLayer::OnKeyPressed));
        dispatcher.Dispatch<MouseButtonPressedEvent>(TR_BIND_EVENT_FN(EditorLayer::OnMouseButtonPressed));
    }

    bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
    {
        if (e.GetRepeatCount() > 0)
        {
            return false;
        }

        bool isControlPressed = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
        bool isShiftPressed = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);

        switch (e.GetKeyCode())
        {
            case Key::N:
            {
                if (isControlPressed)
                {
                    NewScene();
                }

                break;
            }

            case Key::O:
            {
                if (isControlPressed)
                {
                    OpenScene();
                }

                break;
            }

            case Key::S:
            {
                if (isControlPressed)
                {
                    SaveScene();
                }

                if (isControlPressed && isShiftPressed)
                {
                    SaveSceneAs();
                }

                break;
            }

            case Key::Q:
            {
                m_GizmoType = -1;

                break;
            }

            case Key::W:
            {
                m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;

                break;
            }

            case Key::E:
            {
                m_GizmoType = ImGuizmo::OPERATION::ROTATE;

                break;
            }

            case Key::R:
            {
                m_GizmoType = ImGuizmo::OPERATION::SCALE;

                break;
            }

            case Key::D:
            {
                if (isControlPressed)
                {
                    OnDuplicateEntity();
                }
            }

            default:
                break;
        }

        return false;
    }

    bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (e.GetMouseButton() == Mouse::MouseLeft)
        {
            if (CanSelectEntity())
            {
                m_SceneHierarchyPanel.SetSelectedEntity(m_HoveredEntity);
            }
        }

        return false;
    }

    void EditorLayer::OnOverlayRenderer()
    {
        if (m_SceneState == SceneState::Play)
        {
            Entity activeCamera = m_ActiveScene->GetPrimaryCameraEntity();
            if (!activeCamera)
            {
                return;
            }

            Renderer2D::BeginScene(activeCamera.GetComponent<CameraComponent>().Camera, activeCamera.GetComponent<TransformComponent>().GetTransform());
        }
        else
        {
            Renderer2D::BeginScene(m_EditorCamera);
        }

        if (m_ShowColliders)
        {
            // Box collider
            {
                auto view = m_ActiveScene->GetAllEntitiesWith<TransformComponent, BoxCollider2DComponent>();
                for (auto entity : view)
                {
                    if (entity == m_SelectedEntity)
                    {
                        auto [tc, bc2d] = view.get<TransformComponent, BoxCollider2DComponent>(entity);

                        glm::vec3 translation = tc.Translation + glm::vec3(bc2d.Offset, 0.001f);
                        float rotation = tc.Rotation.z;
                        glm::vec3 scale = tc.Scale * glm::vec3(bc2d.Size * 2.0f, 1.0f);

                        glm::mat4 transform =
                            glm::translate(glm::mat4(1.0f), translation)
                            * glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 0.0f, 1.0f))
                            * glm::scale(glm::mat4(1.0f), scale);

                        Renderer2D::DrawRect(transform, glm::vec4{ 0.1f, 1.0f, 0.1f, 1.0f });
                    }
                }
            }

            // Circle collider
            {
                auto view = m_ActiveScene->GetAllEntitiesWith<TransformComponent, CircleCollider2DComponent>();
                for (auto entity : view)
                {
                    if (entity == m_SelectedEntity)
                    {
                        auto [tc, cc2d] = view.get<TransformComponent, CircleCollider2DComponent>(entity);

                        glm::vec3 translation = tc.Translation + glm::vec3(cc2d.Offset, 0.001f);
                        glm::vec3 scale = tc.Scale * glm::vec3(cc2d.Radius * 2.0f);

                        glm::mat4 transform = glm::translate(glm::mat4(1.0f), translation) * glm::scale(glm::mat4(1.0f), scale);

                        Renderer2D::DrawCircle(transform, glm::vec4{ 0.1f, 1.0f, 0.1f, 1.0f }, 0.025f);
                    }
                }
            }
        }

        Renderer2D::EndScene();
    }

    void EditorLayer::NewScene()
    {
        m_ActiveScene = CreateRef<Scene>();
        m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);

        m_EditorScenePath = std::filesystem::path();
    }

    void EditorLayer::OpenScene()
    {
        std::string filePath = FileDialogs::OpenFile("Trinity Scene (*.trinity)\0*.trinity\0");
        if (!filePath.empty())
        {
            OpenScene(filePath);
        }
    }

    void EditorLayer::OpenScene(const std::filesystem::path& path)
    {
        if (m_SceneState != SceneState::Edit)
        {
            OnSceneStop();
        }

        Ref<Scene> newScene = CreateRef<Scene>();
        SceneSerializer serializer(newScene);
        if (serializer.DeserializeText(path.string()))
        {
            m_EditorScene = newScene;
            m_EditorScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_SceneHierarchyPanel.SetContext(m_EditorScene);
            
            m_ActiveScene = m_EditorScene;
            m_EditorScenePath = path;
        }
    }

    void EditorLayer::SerializeScene(Ref<Scene> scene, const std::filesystem::path& path)
    {
        SceneSerializer serializer(scene);
        serializer.SerializeText(path.string());
    }

    void EditorLayer::OnScenePlay()
    {
        if (m_SceneState == SceneState::Simulate)
        {
            OnSceneStop();
        }

        m_SceneState = SceneState::Play;

        m_ActiveScene = Scene::Copy(m_EditorScene);
        m_ActiveScene->OnRuntimeStart();

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    void EditorLayer::OnSceneStop()
    {
        if (m_SceneState == SceneState::Play)
        {
            m_ActiveScene->OnRuntimeStop();
        }

        else if (m_SceneState == SceneState::Simulate)
        {
            m_ActiveScene->OnSimulationStop();
        }

        m_SceneState = SceneState::Edit;

        m_ActiveScene = m_EditorScene;
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    void EditorLayer::OnSceneSimulate()
    {
        if (m_SceneState == SceneState::Play)
        {
            OnSceneStop();
        }

        m_SceneState = SceneState::Simulate;

        m_ActiveScene = Scene::Copy(m_EditorScene);
        m_ActiveScene->OnSimulationStart();

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    void EditorLayer::OnDuplicateEntity()
    {
        if (m_SceneState != SceneState::Edit)
        {
            return;
        }

        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (selectedEntity)
        {
            m_EditorScene->DuplicateEntity(selectedEntity);
        }
    }

    void EditorLayer::OnDeleteEntity()
    {
        if (m_SceneState != SceneState::Edit)
        {
            return;
        }

        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (selectedEntity)
        {
            m_EditorScene->DestroyEntity(selectedEntity);
        }
    }

    void EditorLayer::SaveScene()
    {
        if (!m_EditorScenePath.empty())
        {
            SerializeScene(m_ActiveScene, m_EditorScenePath);
        }

        else
        {
            SaveSceneAs();
        }
    }

    void EditorLayer::SaveSceneAs()
    {
        std::string filePath = FileDialogs::SaveFile("Trinity Scene (*.trinity)\0*.trinity\0");
        if (!filePath.empty())
        {
            SerializeScene(m_ActiveScene, filePath);
            m_EditorScenePath = filePath;
        }
    }

    bool EditorLayer::CanSelectEntity() const
    {
        return m_ViewportFocused && m_ViewportHovered && !ImGuizmo::IsOver() && !Input::IsKeyPressed(Key::LeftAlt);
    }
}