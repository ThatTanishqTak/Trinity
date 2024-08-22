#include "trpch.h"

#include "RendererAPI.h"
#include "Renderer.h"

namespace Trinity
{
	void Trinity::Renderer::BeginScene()
	{

	}

	void Renderer::EndScene()
	{

	}

	void Renderer::Submit(const std::shared_ptr<VertexArray>& vertexArray)
	{
		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}
}