#include <Trinity.h>
#include <Trinity/Core/EntryPoint.h>

#include "Platform/OpenGL/OpenGLShader.h"
#include "ImGui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Sandbox2D.h"

class DemoLayer : public Trinity::Layer
{
public:
	DemoLayer() : Layer("DemoLayer"),
		m_CameraController(1280.0f / 720.0f)
	{
		m_VertexArray = Trinity::VertexArray::Create();

		float vertices[3 * 7] =
		{
			  -0.5f, -0.5f, 0.0f,      
			   0.5f, -0.5f, 0.0f,      
			   0.0f,  0.5f, 0.0f,      
		};

		Trinity::Ref<Trinity::VertexBuffer> vertexBuffer;
		vertexBuffer.reset(Trinity::VertexBuffer::Create(vertices, sizeof(vertices)));
		Trinity::BufferLayout layout =
		{
			{ Trinity::ShaderDataType::Float3, std::string("a_Position") }
		};

		vertexBuffer->SetLayout(layout);
		m_VertexArray->AddVertexBuffer(vertexBuffer);

		uint32_t indices[3] = { 0, 1, 2 };
		Trinity::Ref<Trinity::IndexBuffer> indexBuffer;
		indexBuffer.reset(Trinity::IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t)));
		m_VertexArray->SetIndexBuffer(indexBuffer);

		m_SquareVA = Trinity::VertexArray::Create();

		float verticesSquare[5 * 4] =
		{
			  -0.75f, -0.75f, 0.0f, 0.0f, 0.0f,
			   0.75f, -0.75f, 0.0f, 1.0f, 0.0f,
			   0.75f,  0.75f, 0.0f, 1.0f, 1.0f,
			  -0.75f,  0.75f, 0.0f, 0.0f, 1.0f
		};

		Trinity::Ref<Trinity::VertexBuffer> squareVB;
		squareVB.reset(Trinity::VertexBuffer::Create(verticesSquare, sizeof(verticesSquare)));
		Trinity::BufferLayout squareLayout =
		{
			{ Trinity::ShaderDataType::Float3, std::string("a_Position") },
			{ Trinity::ShaderDataType::Float2, std::string("a_TexCoord") }
		};

		squareVB->SetLayout(squareLayout);
		m_SquareVA->AddVertexBuffer(squareVB);

		uint32_t indicesSquare[6] = { 0, 1, 2, 2, 3, 0 };
		Trinity::Ref<Trinity::IndexBuffer> squareIB;
		squareIB.reset(Trinity::IndexBuffer::Create(indicesSquare, sizeof(indicesSquare) / sizeof(uint32_t)));
		m_SquareVA->SetIndexBuffer(squareIB);

		std::string vertexSource =
			R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;

			uniform mat4 u_ViewProjection;
			uniform mat4 u_Transform;

			out vec3 v_Position;
						
			void main()
			{
				v_Position = a_Position;
				gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0f);
			}
		)";

		std::string fragmentSource =
			R"(
			#version 330 core
			
			layout(location = 0) out vec4 color;

			uniform vec4 v_Color;
						
			void main()
			{
				color = v_Color;
			}
		)";

		m_Shader = Trinity::Shader::Create("Triangle Shader", vertexSource, fragmentSource);

		Trinity::Shader::Create("assets/shaders/Texture.glsl");

		std::string flatColorVertexSource =
			R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;

			uniform mat4 u_ViewProjection;
			uniform mat4 u_Transform;

			out vec3 v_Position;
						
			void main()
			{
				v_Position = a_Position;				
				gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0f);
			}
		)";

		std::string flatColorFragmentSource =
			R"(
			#version 330 core
			
			layout(location = 0) out vec4 color;
			
			uniform vec4 u_Color;
						
			void main()
			{
				color = u_Color;
			}
		)";

		m_FlatColorShader = Trinity::Shader::Create("Square Shader", flatColorVertexSource, flatColorFragmentSource);

		auto textureShader = m_ShaderLibrary.Load("assets/shaders/Texture.glsl");

		m_Texture = Trinity::Texture2D::Create("assets/textures/texture.png");
		m_Blend = Trinity::Texture2D::Create("assets/textures/blend.png");

		std::dynamic_pointer_cast<Trinity::OpenGLShader>(textureShader)->Bind();
		std::dynamic_pointer_cast<Trinity::OpenGLShader>(textureShader)->UploadUniformInt("u_Texture", 0);
	}

	void OnUpdate(Trinity::Timestep timestep) override
	{
		float deltaTime = timestep;

		m_CameraController.OnUpdate(deltaTime);

		Trinity::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
		Trinity::RenderCommand::Clear();

		Trinity::Renderer::BeginScene(m_CameraController.GetCamera());

		static glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));

		std::dynamic_pointer_cast<Trinity::OpenGLShader>(m_FlatColorShader)->Bind();
		std::dynamic_pointer_cast<Trinity::OpenGLShader>(m_FlatColorShader)->UploadUniformFloat4("u_Color", m_SquareColor);
		
		for (int y = 0; y < 20; y++)
		{
			for (int x = 0; x < 20; x++)
			{
				glm::vec3 pos(x * 0.2f, y * 0.2f, 0.0f);
				glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos) * scale;

				Trinity::Renderer::Submit(m_FlatColorShader, m_SquareVA, transform);
			}
		}

		auto textureShader = m_ShaderLibrary.Get("Texture");

		m_Texture->Bind();
		Trinity::Renderer::Submit(textureShader, m_SquareVA, glm::scale(glm::mat4(1.0f), glm::vec3(0.5f)));

		m_Blend->Bind();
		Trinity::Renderer::Submit(textureShader, m_SquareVA, glm::scale(glm::mat4(1.0f), glm::vec3(0.5f)));

		Trinity::Renderer::EndScene();
	}

	void OnEvent(Trinity::Event& event) override
	{
		m_CameraController.OnEvent(event);
	}

	virtual void OnImGuiRender() override
	{

	}

private:
	Trinity::ShaderLibrary m_ShaderLibrary;

	Trinity::Ref<Trinity::Shader> m_Shader;
	Trinity::Ref<Trinity::Shader> m_FlatColorShader;

	Trinity::Ref<Trinity::VertexArray> m_VertexArray;
	Trinity::Ref<Trinity::VertexArray> m_SquareVA;

	Trinity::Ref<Trinity::Texture2D> m_Texture;
	Trinity::Ref<Trinity::Texture2D> m_Blend;

	Trinity::OrthographicCameraController m_CameraController;

	glm::vec3 m_SquarePosition;
	float m_SquareMoveSpeed;

	glm::vec4 m_SquareColor = { 0.8f, 0.2f, 0.3f, 1.0f };
};

class Sandbox : public Trinity::Application
{
public:
	Sandbox()
	{
		//PushLayer(new DemoLayer());
		PushLayer(new Sandbox2D());
	}

	~Sandbox()
	{

	}
};

Trinity::Application* Trinity::CreateApplication()
{
	return new Sandbox();
}