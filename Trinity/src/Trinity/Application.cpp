#include "trpch.h"
#include "Application.h"

#include "Trinity/Log.h"

#include "Trinity/Renderer/Renderer.h"

#include "Input.h"

#include "glm/glm.hpp"

namespace Trinity
{
#define BIND_EVENT_FN(x) std::bind(&Application::x, this, std::placeholders::_1)

	Application* Application::s_Instance = nullptr;

	Application::Application() : m_Camera(-1.6f, 1.6f, -0.9f, 0.9f)
	{
		TR_CORE_ASSERT(!s_Instance, "Application already exists!")
			s_Instance = this;

		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(BIND_EVENT_FN(OnEvent));

		m_ImGuiLayer = new ImGuiLayer();
		PushOverlay(m_ImGuiLayer);

		m_VertexArray.reset(VertexArray::Create());

		float vertices[3 * 7] =
		{
			//----- VERTICES -----//   //----- COLOR -----//
			  -0.5f, -0.5f, 0.0f,      1.0f, 0.0f, 0.0f, 1.0f,
			   0.5f, -0.5f, 0.0f,      0.0f, 1.0f, 0.0f, 1.0f,
			   0.0f,  0.5f, 0.0f,      0.0f, 0.0f, 1.0f, 1.0f
		};

		std::shared_ptr<VertexBuffer> vertexBuffer;
		vertexBuffer.reset(VertexBuffer::Create(vertices, sizeof(vertices)));
		BufferLayout layout =
		{
			{ ShaderDataType::Float3, std::string("a_Position") },
			{ ShaderDataType::Float4, std::string("a_Color") }
		};

		vertexBuffer->SetLayout(layout);
		m_VertexArray->AddVertexBuffer(vertexBuffer);

		uint32_t indices[3] = { 0, 1, 2 };
		std::shared_ptr<IndexBuffer> indexBuffer;
		indexBuffer.reset(IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t)));
		m_VertexArray->SetIndexBuffer(indexBuffer);

		m_SquareVA.reset(VertexArray::Create());

		float verticesSquare[3 * 4] =
		{
			//----- VERTICES -----//
			  -0.75f, -0.75f, 0.0f,
			   0.75f, -0.75f, 0.0f,
			   0.75f,  0.75f, 0.0f,
			  -0.75f,  0.75f, 0.0f
		};

		std::shared_ptr<VertexBuffer> squareVB;
		squareVB.reset(VertexBuffer::Create(verticesSquare, sizeof(verticesSquare)));

		squareVB->SetLayout
		({ 
			{ ShaderDataType::Float3, std::string("a_Position") } 
		});
		
		m_SquareVA->AddVertexBuffer(squareVB);

		uint32_t indicesSquare[6] = { 0, 1, 2, 2, 3, 0 };
		std::shared_ptr<IndexBuffer> squareIB;
		squareIB.reset(IndexBuffer::Create(indicesSquare, sizeof(indicesSquare) / sizeof(uint32_t)));
		m_SquareVA->SetIndexBuffer(squareIB);

		std::string vertexSource = 
		R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec4 a_Color;

			uniform mat4 u_ViewProjection;

			out vec3 v_Position;
			out vec4 v_Color;
						
			void main()
			{
				v_Position = a_Position;
				v_Color = a_Color;
				
				gl_Position = u_ViewProjection * vec4(a_Position, 1.0f);
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

		std::string vertexSourceSquare =
			R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;

			uniform mat4 u_ViewProjection;

			out vec3 v_Position;
			out vec4 v_Color;
						
			void main()
			{
				v_Position = a_Position;				
				gl_Position = u_ViewProjection * vec4(a_Position, 1.0f);
			}
		)";

		std::string fragmentSourceSquare =
			R"(
			#version 330 core
			
			layout(location = 0) out vec4 color;
						
			void main()
			{
				color = vec4(0.2f, 0.3f, 0.8f, 1.0f);
			}
		)";

		m_ShaderSquare.reset(new Shader(vertexSourceSquare, fragmentSourceSquare));
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
			RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
			RenderCommand::Clear();

			//m_Camera.SetPosition({ 0.0f, 0.0f, 0.0f });
			//m_Camera.SetRotation(0.0f);

			Renderer::BeginScene(m_Camera);

			Renderer::Submit(m_ShaderSquare, m_SquareVA);
			Renderer::Submit(m_Shader, m_VertexArray);

			Renderer::EndScene();

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