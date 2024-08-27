#include "trpch.h"
#include "Shader.h"

#include "Renderer.h"
#include "Platform/OpenGL/OpenGLShader.h"

namespace Trinity
{
	Shader* Shader::Create(const std::string& filePath)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None: { TR_CORE_ASSERT(false, "RendererAPI::None is not currently support!"); return nullptr; }
		case RendererAPI::API::OpenGL: { return new OpenGLShader(filePath); }
		}

		TR_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	Shader* Shader::Create(const std::string& vertexSource, const std::string& fragmentSource)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:     { TR_CORE_ASSERT(false, "RendererAPI::None is not currently support!"); return nullptr; }
			case RendererAPI::API::OpenGL:   { return new OpenGLShader(vertexSource, fragmentSource); }
		}

		TR_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}