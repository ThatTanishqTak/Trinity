#pragma once

#include "Trinity/Renderer/Framebuffer.h"

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

		virtual uint32_t GetColorAttachmentRendererID() const override { return m_ColorAttachment; }
		virtual const FramebufferSpecifications& GetSpecification() const override { return m_Specifications; }

	private:
		uint32_t m_RendererID;
		uint32_t m_ColorAttachment;
		uint32_t m_DepthAttachment;

		FramebufferSpecifications m_Specifications;
	};
}