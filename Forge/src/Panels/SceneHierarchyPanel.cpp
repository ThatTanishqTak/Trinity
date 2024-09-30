#include "trpch.h"
#include "SceneHierarchyPanel.h"

#include "Trinity/Scene/Components.h"

#include <imgui/imgui.h>
#include <glm/gtc/type_ptr.hpp>

namespace Trinity
{
	SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& context)
	{
		SetContext(context);
	}

	void SceneHierarchyPanel::SetContext(const Ref<Scene>& context)
	{
		m_Context = context;
	}

	void SceneHierarchyPanel::OnImGuiRender()
	{
		ImGui::Begin("Scene Hierarchy");
		{
			m_Context->m_Registry.view<entt::entity>().each([this](auto entityID)
				{
					Entity entity{ entityID, m_Context.get() };
					DrawEntityNode(entity);
				});

			if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
			{
				m_SelectionContext = {};
			}

			ImGui::End();
		}

		ImGui::Begin("Properties");
		{
			if (m_SelectionContext)
			{
				DrawComponents(m_SelectionContext);
			}

			ImGui::End();
		}
	}

	void SceneHierarchyPanel::DrawEntityNode(Entity entity)
	{
		auto& tag = entity.GetComponent<TagComponent>().Tag;

		ImGuiTreeNodeFlags flags = ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
		bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.c_str());

		if (ImGui::IsItemClicked())
		{
			m_SelectionContext = entity;
		}

		if (opened)
		{
			ImGui::TreePop();
		}
	}

	void SceneHierarchyPanel::DrawComponents(Entity entity)
	{
		if (entity.HasComponent<TagComponent>())
		{
			auto& tag = entity.GetComponent<TagComponent>().Tag;

			char buffer[256];
			memset(buffer, 0, sizeof(buffer));
			strcpy_s(buffer, sizeof(buffer), tag.c_str());

			if (ImGui::InputText("Tag", buffer, sizeof(buffer)))
			{
				tag = std::string(buffer);
			}

		}

		if (entity.HasComponent<TransformComponent>())
		{
			if (ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Transform"))
			{
				auto& transform = entity.GetComponent<TransformComponent>().Transform;
				ImGui::DragFloat3("Position", glm::value_ptr(transform[3]), 0.1f);

				ImGui::TreePop();
			}
		}

		if (entity.HasComponent<CameraComponent>())
		{
			if (ImGui::TreeNodeEx((void*)typeid(CameraComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Camera"))
			{
				auto& camera = entity.GetComponent<CameraComponent>().Camera;

				ImGui::Checkbox("Primary", &entity.GetComponent<CameraComponent>().Primary);

				const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
				const char* currentProjectionTypeString = projectionTypeStrings[(int)camera.GetProjectionType()];

				if (ImGui::BeginCombo("projection", currentProjectionTypeString))
				{
					for (int i = 0; i < 2; i++)
					{
						bool isSelected = currentProjectionTypeString == projectionTypeStrings[i];
						if (ImGui::Selectable(projectionTypeStrings[i], isSelected))
						{
							currentProjectionTypeString = projectionTypeStrings[i];
							camera.SetProjectionType((SceneCamera::ProjectionType)i);
						}

						if (isSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}

					ImGui::EndCombo();
				}

				if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
				{
					float verticalFOV = glm::degrees(camera.GetPerspectiveFOV());
					if (ImGui::DragFloat("Vertical FOV", &verticalFOV))
					{
						camera.SetPerspectiveFOV(glm::radians(verticalFOV));
					}

					float perspectiveNearClip = camera.GetPerspectiveNearClip();
					if (ImGui::DragFloat("Near Clip", &perspectiveNearClip))
					{
						camera.SetPerspectiveNearClip(perspectiveNearClip);
					}

					float perspectiveFarClip = camera.GetPerspectiveFarClip();
					if (ImGui::DragFloat("Far Clip", &perspectiveFarClip))
					{
						camera.SetPerspectiveFarClip(perspectiveFarClip);
					}
				}

				if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
				{
					ImGui::Checkbox("Fixed Aspect Ratio", &entity.GetComponent<CameraComponent>().FixedAspectRatio);

					float orthoSize = camera.GetOrthographicSize();
					if (ImGui::DragFloat("Size", &orthoSize))
					{
						camera.SetOrthographicSize(orthoSize);
					}

					float orthoNearClip = camera.GetOrthographicNearClip();
					if (ImGui::DragFloat("Near Clip", &orthoNearClip))
					{
						camera.SetOrthographicNearClip(orthoNearClip);
					}

					float orthoFarClip = camera.GetOrthographicFarClip();
					if (ImGui::DragFloat("Far Clip", &orthoFarClip))
					{
						camera.SetOrthographicFarClip(orthoFarClip);
					}
				}

				ImGui::TreePop();
			}
		}

		if (entity.HasComponent<SpriteRendererComponent>())
		{

		}

		if (entity.HasComponent<NativeScriptComponent>())
		{

		}
	}
}