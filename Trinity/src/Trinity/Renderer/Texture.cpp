#include "trpch.h"
#include "Texture.h"

#include "Renderer.h"
#include "Platform/OpenGL/OpenGLTexture.h"

namespace Trinity
{
	Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:     { TR_CORE_ASSERT(false, "RendererAPI::None is not currently support!"); return nullptr; }
			case RendererAPI::API::OpenGL:   { return CreateRef<OpenGLTexture2D>(width, height); }
		}

		TR_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
	Ref<Texture2D> Texture2D::Create(const std::string& path)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:     { TR_CORE_ASSERT(false, "RendererAPI::None is not currently support!"); return nullptr; }
			case RendererAPI::API::OpenGL:   { return CreateRef<OpenGLTexture2D>(path); }
		}

		TR_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}