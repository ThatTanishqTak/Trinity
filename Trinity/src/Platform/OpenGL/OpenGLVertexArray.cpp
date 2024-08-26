#include "trpch.h"
#include "OpenGLVertexArray.h"

#include <glad/glad.h>

namespace Trinity
{
	static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type)
	{
		switch (type)
		{
			case Trinity::ShaderDataType::Float:
			{
				return GL_FLOAT;
			}
			case Trinity::ShaderDataType::Float2:
			{
				return GL_FLOAT;
			}
			case Trinity::ShaderDataType::Float3:
			{
				return GL_FLOAT;
			}
			case Trinity::ShaderDataType::Float4:
			{
				return GL_FLOAT;
			}
			case Trinity::ShaderDataType::Mat3:
			{
				return GL_FLOAT;
			}
			case Trinity::ShaderDataType::Mat4:
			{
				return GL_FLOAT;
			}
			case Trinity::ShaderDataType::Int:
			{
				return GL_INT;
			}
			case Trinity::ShaderDataType::Int2:
			{
				return GL_INT;
			}
			case Trinity::ShaderDataType::Int3:
			{
				return GL_INT;
			}
			case Trinity::ShaderDataType::Int4:
			{
				return GL_INT;
			}
			case Trinity::ShaderDataType::Bool:
			{
				return GL_BOOL;
			}

			TR_CORE_ASSERT(false, "ShaderDataType not found!");

			return 0;
		}
	}

	OpenGLVertexArray::OpenGLVertexArray()
	{
		glCreateVertexArrays(1, &m_RendererID);
	}

	OpenGLVertexArray::~OpenGLVertexArray()
	{
		glDeleteVertexArrays(1, &m_RendererID);
	}

	void OpenGLVertexArray::Bind() const
	{
		glBindVertexArray(m_RendererID);
	}

	void OpenGLVertexArray::Unbind() const
	{
		glBindVertexArray(0);
	}

	void OpenGLVertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)
	{
		TR_CORE_ASSERT(vertexBuffer->GetLayout().GetElements().size(), "Vertex buffer has no layout!");

		glBindVertexArray(m_RendererID);
		vertexBuffer->Bind();

		uint32_t index = 0;
		const auto& layout = vertexBuffer->GetLayout();
		for (const auto& element : layout)
		{
			glEnableVertexAttribArray(index);
			glVertexAttribPointer(index, element.GetComponentCount(), ShaderDataTypeToOpenGLBaseType(element.Type),
				element.Normalized ? GL_TRUE : GL_FALSE, layout.GetStride(), (const void*)element.Offset);

			index++;
		}

		m_VertexBuffers.push_back(vertexBuffer);
	}

	void OpenGLVertexArray::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)
	{
		glBindVertexArray(m_RendererID);
		indexBuffer->Bind();

		m_IndexBuffer = indexBuffer;
	}
}