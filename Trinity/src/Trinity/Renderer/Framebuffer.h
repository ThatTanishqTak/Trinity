#pragma once

#include "Trinity/Core/Core.h"

namespace Trinity
{
	struct FramebufferSpecifications
	{
		uint32_t Width;
		uint32_t Height;

		/*FramebufferFormat Format = */
		
		uint32_t Samples = 1;

		bool SwapChainTarget = false;
	};

	class Framebuffer
	{
	public:
		virtual void Bind() = 0;
		virtual void Unbind() = 0;

		virtual void Resize(uint32_t width, uint32_t height) = 0;

		virtual uint32_t GetColorAttachmentRendererID() const = 0;
		virtual const FramebufferSpecifications& GetSpecification() const = 0;

		static Ref<Framebuffer> Create(const FramebufferSpecifications& specs);
	};
}