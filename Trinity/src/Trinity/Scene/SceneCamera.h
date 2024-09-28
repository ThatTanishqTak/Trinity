#pragma once

#include "Trinity/Renderer/Camera.h"

namespace Trinity
{
	class SceneCamera : public Camera
	{
	public:
		SceneCamera();
		virtual ~SceneCamera() = default;

		void SetOrthographic(float size, float nearClip, float farClip);
		void SetViewportSize(uint32_t width, uint32_t height);
		void SetOrthographicSize(float orthographicSize) { m_OrthographicSize = orthographicSize; RecalculateProjection(); }

		float GetOrthographicSize() const { return m_OrthographicSize; }

	private:
		void RecalculateProjection();

	private:
		float m_OrthographicSize = 10.0f;
		float m_OrthographicNear = -1.0f;
		float m_OrthographicFar = 1.0f;

		float m_AspectRatio = 0.0f;
	};
}