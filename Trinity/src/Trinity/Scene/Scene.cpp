#include "trpch.h"
#include "Scene.h"
#include "Components.h"
#include "ScriptableEntity.h"
#include "Entity.h"

#include "Trinity/Renderer/Renderer2D.h"

#include <glm/glm.hpp>

#include <box2d/b2_world.h>
#include <box2d/b2_body.h>
#include <box2d/b2_fixture.h>
#include <box2d/b2_polygon_shape.h>

namespace Trinity
{
	static b2BodyType TrinityRigidbody2DTypeToBox2D(Rigidbody2DComponent::BodyType bodyType)
	{
		switch (bodyType)
		{
			case Rigidbody2DComponent::BodyType::Static:      { return b2_staticBody; }
			case Rigidbody2DComponent::BodyType::Dynamic:     { return b2_dynamicBody; }
			case Rigidbody2DComponent::BodyType::Kinematic:   { return b2_kinematicBody; }
		}

		TR_CORE_ASSERT(false, "Unknow type");

		return b2_staticBody;
	}

	Scene::Scene()
	{

	}

	Scene::~Scene()
	{

	}

	template<typename T>
	static void CopyComponent(entt::registry& dest, entt::registry& source, const std::unordered_map<UUID, entt::entity>& enttMap)
	{
		auto view = source.view<T>();
		for (auto e : view)
		{
			UUID uuid = source.get<IDComponent>(e).ID;
			entt::entity destinationEnttID = enttMap.at(uuid);

			auto& component = source.get<T>(e);
			dest.emplace_or_replace<T>(destinationEnttID, component);
		}
	}

	template<typename T>
	static void CopyComponentIfExists(Entity dest, Entity source)
	{
		if (source.HasComponent<T>())
		{
			dest.AddOrReplaceComponent<T>(source.GetComponent<T>());
		}
	}

	Ref<Scene> Scene::Copy(Ref<Scene> sourceScene)
	{
		Ref<Scene> destinationScene = CreateRef<Scene>();

		destinationScene->m_ViewportWidth = sourceScene->m_ViewportWidth;
		destinationScene->m_ViewportHeight = sourceScene->m_ViewportHeight;

		auto& sourceRegistry = sourceScene->m_Registry;
		auto& destinationRegistry = destinationScene->m_Registry;
		std::unordered_map<UUID, entt::entity> enttMap;
		
		auto IDView = sourceRegistry.view<IDComponent>();
		for (auto e : IDView)
		{
			UUID uuid = sourceRegistry.get<IDComponent>(e).ID;
			const auto& name = sourceRegistry.get<TagComponent>(e).Tag;
			
			Entity newEntity = destinationScene->CreateEntityWithUUID(uuid, name);
			enttMap[uuid] = (entt::entity)newEntity;
		}

		CopyComponent<TransformComponent>(destinationRegistry, sourceRegistry, enttMap);
		CopyComponent<SpriteRendererComponent>(destinationRegistry, sourceRegistry, enttMap);
		CopyComponent<CircleRendererComponent>(destinationRegistry, sourceRegistry, enttMap);
		CopyComponent<CameraComponent>(destinationRegistry, sourceRegistry, enttMap);
		CopyComponent<NativeScriptComponent>(destinationRegistry, sourceRegistry, enttMap);
		CopyComponent<Rigidbody2DComponent>(destinationRegistry, sourceRegistry, enttMap);
		CopyComponent<BoxCollider2DComponent>(destinationRegistry, sourceRegistry, enttMap);

		return destinationScene;
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<TransformComponent>();

		do
		{
			count++;
		} while (std::binary_search(entityList.begin(), entityList.end(), count));

		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? std::string("Entity_" + std::to_string(count)) : name;

		entityList.push_back(count);

		TR_CORE_INFO(count);

		return entity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		do
		{
			count--;
		} while (!std::binary_search(entityList.begin(), entityList.end(), count));

		TR_CORE_INFO(count);

		m_Registry.destroy(entity);
	}

	void Scene::OnRuntimeStart()
	{
		m_PhysicsWorld = new b2World({ 0.0f, -9.8f });

		auto view = m_Registry.view<Rigidbody2DComponent>();
		for (auto e : view)
		{
			Entity entity = { e, this };

			auto& transform = entity.GetComponent<TransformComponent>();
			auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();

			b2BodyDef bodyDef;
			bodyDef.type = TrinityRigidbody2DTypeToBox2D(rb2d.Type);
			bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
			bodyDef.angle = transform.Rotation.z;

			b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
			body->SetFixedRotation(rb2d.FixedRotation);

			rb2d.RuntimeBody = body;

			if (entity.HasComponent<BoxCollider2DComponent>())
			{
				auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();

				b2PolygonShape boxShape;
				boxShape.SetAsBox(bc2d.Size.x * transform.Scale.x, bc2d.Size.y * transform.Scale.y);

				b2FixtureDef fixtureDef;
				fixtureDef.shape = &boxShape;
				fixtureDef.density = bc2d.Density;
				fixtureDef.friction = bc2d.Friction;
				fixtureDef.restitution = bc2d.Restitution;
				fixtureDef.restitutionThreshold = bc2d.RestitutionThreshold;
			
				body->CreateFixture(&fixtureDef);
			}
		}
	}

	void Scene::OnRuntimeStop()
	{
		delete m_PhysicsWorld;
		m_PhysicsWorld = nullptr;
	}

	void Scene::OnUpdateRuntime(Timestep timestep)
	{
		// Update scripts
		{
			m_Registry.view<NativeScriptComponent>().each([=](auto entity, auto& nativeScriptComponent)
			{
				if (!nativeScriptComponent.Instance)
				{
					nativeScriptComponent.Instance = nativeScriptComponent.InstantiateScript();
					nativeScriptComponent.Instance->m_Entity = Entity{ entity, this };
					nativeScriptComponent.Instance->OnCreate();
				}

				nativeScriptComponent.Instance->OnUpdate(timestep);
			});
		}

		// Physics
		{
			const int32_t velocityIteration = 6;
			const int32_t positionIteration = 2;

			m_PhysicsWorld->Step(timestep, velocityIteration, positionIteration);

			auto view = m_Registry.view<Rigidbody2DComponent>();
			for (auto e : view)
			{
				Entity entity = { e, this };
				auto& transform = entity.GetComponent<TransformComponent>();
				auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();
			
				b2Body* body = (b2Body*)rb2d.RuntimeBody;
				
				const auto& position = body->GetPosition();
				transform.Translation.x = position.x;
				transform.Translation.y = position.y;
				transform.Rotation.z = body->GetAngle();
			}
		}

		// Render 2D
		Camera* mainCamera = nullptr;
		glm::mat4 cameraTransform;
		{
			auto view = m_Registry.view<TransformComponent, CameraComponent>();
			for (auto entity : view)
			{
				auto [transform, camera] = view.get<TransformComponent, CameraComponent>(entity);

				if (camera.Primary)
				{
					mainCamera = &camera.Camera;
					cameraTransform = transform.GetTransform();

					break;
				}
			}
		}

		if (mainCamera)
		{
			Renderer2D::BeginScene(mainCamera->GetProjection(), cameraTransform);

			// Quad
			{
				auto group = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
				for (auto entity : group)
				{
					auto [transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);

					Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
				}
			}

			// Circle
			{
				auto view = m_Registry.view<TransformComponent, CircleRendererComponent>();
				for (auto entity : view)
				{
					auto [transform, circle] = view.get<TransformComponent, CircleRendererComponent>(entity);

					Renderer2D::DrawCircle(transform.GetTransform(), circle.Color, circle.Thickness, circle.Fade, (int)entity);
				}
			}

			Renderer2D::EndScene();
		}
	}

	void Scene::OnUpdateEditor(Timestep timestep, EditorCamera& editorCamera)
	{
		Renderer2D::BeginScene(editorCamera);
		
		// Quad
		{
			auto group = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
			for (auto entity : group)
			{
				auto [transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);

				Renderer2D::DrawSprite(transform.GetTransform(), sprite, (int)entity);
			}
		}

		// Circle
		{
			auto view = m_Registry.view<TransformComponent, CircleRendererComponent>();
			for (auto entity : view)
			{
				auto [transform, circle] = view.get<TransformComponent, CircleRendererComponent>(entity);

				Renderer2D::DrawCircle(transform.GetTransform(), circle.Color, circle.Thickness, circle.Fade, (int)entity);
			}
		}

		Renderer2D::EndScene();
	}

	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		m_ViewportWidth = width;
		m_ViewportHeight = height;

		auto view = m_Registry.view<CameraComponent>();
		for (auto entity : view)
		{
			auto& cameraComponent = view.get<CameraComponent>(entity);
			if (!cameraComponent.FixedAspectRatio)
			{
				cameraComponent.Camera.SetViewportSize(width, height);
			}
		}
	}

	void Scene::DuplicateEntity(Entity entity)
	{
		std::string name = entity.GetName();
		Entity newEntity = CreateEntity(name);

		CopyComponentIfExists<TransformComponent>(newEntity, entity);
		CopyComponentIfExists<SpriteRendererComponent>(newEntity, entity);
		CopyComponentIfExists<CircleRendererComponent>(newEntity, entity);
		CopyComponentIfExists<CameraComponent>(newEntity, entity);
		CopyComponentIfExists<NativeScriptComponent>(newEntity, entity);
		CopyComponentIfExists<Rigidbody2DComponent>(newEntity, entity);
		CopyComponentIfExists<BoxCollider2DComponent>(newEntity, entity);
	}

	Entity Scene::GetPrimaryCameraEntity()
	{
		auto view = m_Registry.view<CameraComponent>();

		for (auto entity : view)
		{
			const auto& camera = view.get<CameraComponent>(entity);
			if (camera.Primary)
			{
				return Entity{ entity, this };
			}

			return {};
		}

		return {};
	}

	template<typename T>
	void Scene::OnComponentAdded(Entity entity, T& component)
	{
		static_assert(false);
	}

	template<>
	void Scene::OnComponentAdded<IDComponent>(Entity entity, IDComponent& idComponent)
	{

	}

	template<>
	void Scene::OnComponentAdded<TransformComponent>(Entity entity, TransformComponent& transformComponent)
	{

	}

	template<>
	void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& cameraComponent)
	{
		if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
		{
			cameraComponent.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
		}
	}

	template<>
	void Scene::OnComponentAdded<SpriteRendererComponent>(Entity entity, SpriteRendererComponent& spriteRendererComponent)
	{

	}

	template<>
	void Scene::OnComponentAdded<CircleRendererComponent>(Entity entity, CircleRendererComponent& circleRendererComponent)
	{

	}

	template<>
	void Scene::OnComponentAdded<TagComponent>(Entity entity, TagComponent& tagComponent)
	{

	}

	template<>
	void Scene::OnComponentAdded<NativeScriptComponent>(Entity entity, NativeScriptComponent& nativeScriptComponent)
	{

	}

	template<>
	void Scene::OnComponentAdded<Rigidbody2DComponent>(Entity entity, Rigidbody2DComponent& rigidbody2DComponent)
	{

	}

	template<>
	void Scene::OnComponentAdded<BoxCollider2DComponent>(Entity entity, BoxCollider2DComponent& boxCollider2DComponent)
	{

	}
}