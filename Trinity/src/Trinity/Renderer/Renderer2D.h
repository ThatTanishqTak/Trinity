#pragma once

#include "Trinity/Renderer/OrthographicCamera.h"
#include "Trinity/Renderer/Texture.h"
#include "Trinity/Renderer/SubTexture2D.h"
#include "Trinity/Renderer/Camera.h"
#include "Trinity/Renderer/EditorCamera.h"

#include "Trinity/Scene/Components.h"

namespace Trinity
{
	class Renderer2D
	{
	public:
		static void Init();
		static void Shutdown();

		static void BeginScene(const Camera& camera, const glm::mat4& transform);
		static void BeginScene(const EditorCamera& editorCamera);
		static void BeginScene(const OrthographicCamera& camera);
		static void EndScene();
		static void Flush();

		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color);
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color);
		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, const glm::vec4& tint = glm::vec4(1.0f));
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, const glm::vec4& tint = glm::vec4(1.0f));
		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<SubTexture2D>& subTexture, const glm::vec4& tint = glm::vec4(1.0f));
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<SubTexture2D>& subTexture, const glm::vec4& tint = glm::vec4(1.0f));

		static void DrawQuad(const glm::mat4& transform, float rotation, const glm::vec4& color, int entityID = -1);
		static void DrawQuad(const glm::mat4& transform, float rotation, const Ref<Texture2D>& texture, const glm::vec4& tint = glm::vec4(1.0f), int entityID = -1);
		
		static void DrawTilledQuad(const glm::mat4& transform, float rotation, const Ref<Texture2D>& texture, float tillingFactor = 1.0f, const glm::vec4& tint = glm::vec4(1.0f));

		static void DrawTilledQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tillingFactor = 1.0f, const glm::vec4& tint = glm::vec4(1.0f));
		static void DrawTilledQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tillingFactor = 1.0f, const glm::vec4& tint = glm::vec4(1.0f));
		static void DrawTilledQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<SubTexture2D>& subTexture, float tillingFactor = 1.0f, const glm::vec4& tint = glm::vec4(1.0f));
		static void DrawTilledQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<SubTexture2D>& subTexture, float tillingFactor = 1.0f, const glm::vec4& tint = glm::vec4(1.0f));

		static void DrawSprite(const glm::mat4& transform, float rotation, SpriteRendererComponent& spriteRendererComponent, int entityID);
		
		struct Statistics
		{
			uint32_t DrawCalls = 0;
			uint32_t QuadCount = 0;

			uint32_t GetTotalVertexCount()   { return QuadCount * 4; }
			uint32_t GetTotalIndexCount()    { return QuadCount * 6; }
		};

		static void ResetStats();
		static Statistics GetStats();

	private:
		static void FlushAndReset();
	};
}