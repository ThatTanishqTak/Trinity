#include "trpch.h"

#include "RenderCommand.h"

#include "Platform/OpenGL/OpenGLRendererAPI.h" 

namespace Trinity
{
	RendererAPI* RenderCommand::s_RendererAPI = new OpenGLRendererAPI;
}