#pragma once

#include "Trinity/Core/Core.h"

namespace Trinity
{
	enum class FramebufferTextureFormat
	{
		None = 0,
		
		// Color
		RGBA8,
		RED_INTEGER,

		// Depth/Stencil
		DEPTH24STENCIL8,

		// Default
		Depth = DEPTH24STENCIL8
	};

	struct FramebufferTextureSpecifications
	{
		FramebufferTextureSpecifications() = default;
		FramebufferTextureSpecifications(FramebufferTextureFormat format) : TextureFormat(format) {}

		FramebufferTextureFormat TextureFormat = FramebufferTextureFormat::None;
	};

	struct FramebufferAttachmentSpecifications
	{
		FramebufferAttachmentSpecifications() = default;
		FramebufferAttachmentSpecifications(std::initializer_list<FramebufferTextureSpecifications> attachments) : Attachments(attachments) {}

		std::vector<FramebufferTextureSpecifications> Attachments;
	};

	struct FramebufferSpecifications
	{
		uint32_t Width;
		uint32_t Height;
		uint32_t Samples = 1;
		FramebufferAttachmentSpecifications Attachments;

		bool SwapChainTarget = false;
	};

	class Framebuffer
	{
	public:
		virtual ~Framebuffer() = default;

		virtual void Bind() = 0;
		virtual void Unbind() = 0;
		virtual void Resize(uint32_t width, uint32_t height) = 0;
		virtual void ClearAttachment(uint32_t attachmentIndex, int value) = 0;

		virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) = 0;

		virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const = 0;
		virtual const FramebufferSpecifications& GetSpecification() const = 0;

		static Ref<Framebuffer> Create(const FramebufferSpecifications& specs);
	};
}