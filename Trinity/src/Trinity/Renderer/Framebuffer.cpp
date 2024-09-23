#include "trpch.h"
#include "Framebuffer.h"

#include "Platform/OpenGL/OpenGLFramebuffer.h"

#include "Trinity/Renderer/Renderer.h"

namespace Trinity
{
	Ref<Framebuffer> Framebuffer::Create(const FramebufferSpecifications& specs)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:     { TR_CORE_ASSERT(false, "RendererAPI::None is not currently support!"); return nullptr; }
			case RendererAPI::API::OpenGL:   { return CreateRef<OpenGLFramebuffer>(specs); }
		}

		TR_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}