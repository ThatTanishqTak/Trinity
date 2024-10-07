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

		virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) override;

		virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const override { return m_ColorAttachments[index]; }
		virtual const FramebufferSpecifications& GetSpecification() const override { return m_Specifications; }

	private:
		uint32_t m_RendererID = 0;

		FramebufferSpecifications m_Specifications;

		std::vector<FramebufferTextureSpecifications> m_ColorAttachmentSpecification;
		FramebufferTextureSpecifications m_DepthAttachmentSpecification = FramebufferTextureFormat::None;

		std::vector<uint32_t> m_ColorAttachments;
		uint32_t m_DepthAttachment = 0;
	};
}