#pragma once

#include "Trinity/Renderer/Framebuffer.h"

#include "glm/glm.hpp"

namespace Trinity
{
	class OpenGLFramebuffer : public Framebuffer
	{
	public:
		OpenGLFramebuffer(const FramebufferSpecifications& specs);
		virtual ~OpenGLFramebuffer();

		void Invalidate();
		
		virtual void Bind() override;
		virtual void Unbind() override;

		virtual void Resize(uint32_t width, uint32_t height) override;

		virtual uint32_t GetColorAttachmentRendererID() const override { return m_ColorAttachment; }
		virtual const FramebufferSpecifications& GetSpecification() const override { return m_Specifications; }

	private:
		uint32_t m_RendererID = 0;
		uint32_t m_ColorAttachment = 0;
		uint32_t m_DepthAttachment = 0;

		FramebufferSpecifications m_Specifications;
	};
}