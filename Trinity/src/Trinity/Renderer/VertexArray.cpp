#include "trpch.h"
#include "VertexArray.h"

#include "Trinity/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLVertexArray.h"

namespace Trinity
{
	VertexArray* VertexArray::Create()
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:     { TR_CORE_ASSERT(false, "RendererAPI::None is not currently support!"); return nullptr; }
			case RendererAPI::API::OpenGL:   { return new OpenGLVertexArray(); }
		}
		
		TR_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}