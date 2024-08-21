#include "trpch.h"
#include "Application.h"

#include "Trinity/Log.h"

#include "glad/glad.h"

#include "Input.h"

#include "glm/glm.hpp"

namespace Trinity
{
#define BIND_EVENT_FN(x) std::bind(&Application::x, this, std::placeholders::_1)

	Application* Application::s_Instance = nullptr;

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

	Application::Application()
	{
		TR_CORE_ASSERT(!s_Instance, "Application already exists!")
		s_Instance = this;

		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(BIND_EVENT_FN(OnEvent));

		m_ImGuiLayer = new ImGuiLayer();
		PushOverlay(m_ImGuiLayer);

		glGenVertexArrays(1, &m_VertexArray);
		glBindVertexArray(m_VertexArray);

		float vertices[3 * 7] =
		{
		   //----- VERTICES -----//   //----- COLOR -----//
		     -0.5f, -0.5f, 0.0f,      1.0f, 0.0f, 0.0f, 1.0f,
		 	  0.5f, -0.5f, 0.0f,      0.0f, 1.0f, 0.0f, 1.0f,
			  0.0f,  0.5f, 0.0f,      0.0f, 0.0f, 1.0f, 1.0f
		};

		m_VertexBuffer.reset(VertexBuffer::Create(vertices, sizeof(vertices)));

		{
			BufferLayout layout =
			{
				{ ShaderDataType::Float3, std::string("a_Position") },
				{ ShaderDataType::Float4, std::string("a_Color") }
			};

			m_VertexBuffer->SetLayout(layout);
		}

		uint32_t index = 0;
		const auto& layout = m_VertexBuffer->GetLayout();
		for (const auto& element : layout)
		{
			glEnableVertexAttribArray(index);
			glVertexAttribPointer(index, element.GetComponentCount(), ShaderDataTypeToOpenGLBaseType(element.Type), 
				                   element.Normalized ? GL_TRUE : GL_FALSE, layout.GetStride(), (const void*)element.Offset);

			index++;
		}

		uint32_t indices[3] = { 0, 1, 2 };
		m_IndexBuffer.reset(IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t)));

		std::string vertexSource = 
		R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec4 a_Color;

			out vec3 v_Position;
			out vec4 v_Color;
						
			void main()
			{
				v_Position = a_Position;
				v_Color = a_Color;
				
				gl_Position = vec4(a_Position, 1.0f);
			}
		)";

		std::string fragmentSource = 
		R"(
			#version 330 core
			
			layout(location = 0) out vec4 color;

			in vec4 v_Color;
						
			void main()
			{
				color = v_Color;
			}
		)";

		m_Shader.reset(new Shader(vertexSource, fragmentSource));
	}

	Application::~Application()
	{

	}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::PushOverlay(Layer* layer)
	{
		m_LayerStack.PushOverlay(layer);
		layer->OnAttach();
	}

	void Application::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(OnWindowClose));

		for (auto it = m_LayerStack.end(); it != m_LayerStack.begin();)
		{
			(*--it)->OnEvent(event);
			if (event.Handled)
			{
				break;
			}
		}
	}

	void Application::Run()
	{
		while (m_Running)
		{
			glClearColor(0.1f, 0.1f, 0.1f, 1);
			glClear(GL_COLOR_BUFFER_BIT);

			m_Shader->Bind();

			glBindVertexArray(m_VertexArray);
			glDrawElements(GL_TRIANGLES, m_IndexBuffer->GetCount(), GL_UNSIGNED_INT, nullptr);

			for (Layer* layer : m_LayerStack)
			{
				layer->OnUpdate();
			}

			m_ImGuiLayer->Begin();

			for (Layer* layer : m_LayerStack)
			{
				layer->OnImGuiRender();
			}

			m_ImGuiLayer->End();

			m_Window->OnUpdate();
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& closeEvent)
	{
		m_Running = false;

		return true;
	}
}