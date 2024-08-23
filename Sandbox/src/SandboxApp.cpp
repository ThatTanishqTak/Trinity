#include <Trinity.h>

#include "ImGui/imgui.h"

class DemoLayer : public Trinity::Layer
{
public:
	DemoLayer() : Layer("DemoLayer"), m_Camera(-1.6f, 1.6f, -0.9f, 0.9f), m_CameraPosition(0.0f, 0.0f, 0.0f), m_CameraMoveSpeed(0.05f), m_CameraRotation(0.0f), m_CameraRotationSpeed(0.05f)
	{
		m_VertexArray.reset(Trinity::VertexArray::Create());

		float vertices[3 * 7] =
		{
			//----- VERTICES -----//   //----- COLOR -----//
			  -0.5f, -0.5f, 0.0f,      1.0f, 0.0f, 0.0f, 1.0f,
			   0.5f, -0.5f, 0.0f,      0.0f, 1.0f, 0.0f, 1.0f,
			   0.0f,  0.5f, 0.0f,      0.0f, 0.0f, 1.0f, 1.0f
		};

		std::shared_ptr<Trinity::VertexBuffer> vertexBuffer;
		vertexBuffer.reset(Trinity::VertexBuffer::Create(vertices, sizeof(vertices)));
		Trinity::BufferLayout layout =
		{
			{ Trinity::ShaderDataType::Float3, std::string("a_Position") },
			{ Trinity::ShaderDataType::Float4, std::string("a_Color") }
		};

		vertexBuffer->SetLayout(layout);
		m_VertexArray->AddVertexBuffer(vertexBuffer);

		uint32_t indices[3] = { 0, 1, 2 };
		std::shared_ptr<Trinity::IndexBuffer> indexBuffer;
		indexBuffer.reset(Trinity::IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t)));
		m_VertexArray->SetIndexBuffer(indexBuffer);

		m_SquareVA.reset(Trinity::VertexArray::Create());

		float verticesSquare[3 * 4] =
		{
			//----- VERTICES -----//
			  -0.75f, -0.75f, 0.0f,
			   0.75f, -0.75f, 0.0f,
			   0.75f,  0.75f, 0.0f,
			  -0.75f,  0.75f, 0.0f
		};

		std::shared_ptr<Trinity::VertexBuffer> squareVB;
		squareVB.reset(Trinity::VertexBuffer::Create(verticesSquare, sizeof(verticesSquare)));

		squareVB->SetLayout
		({
			{ Trinity::ShaderDataType::Float3, std::string("a_Position") }
			});

		m_SquareVA->AddVertexBuffer(squareVB);

		uint32_t indicesSquare[6] = { 0, 1, 2, 2, 3, 0 };
		std::shared_ptr<Trinity::IndexBuffer> squareIB;
		squareIB.reset(Trinity::IndexBuffer::Create(indicesSquare, sizeof(indicesSquare) / sizeof(uint32_t)));
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

		m_Shader.reset(new Trinity::Shader(vertexSource, fragmentSource));

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

		m_ShaderSquare.reset(new Trinity::Shader(vertexSourceSquare, fragmentSourceSquare));
	}

	void OnUpdate() override
	{
		if (Trinity::Input::IsKeyPressed(TR_KEY_UP)) { m_CameraPosition.y += m_CameraMoveSpeed; }
		else if (Trinity::Input::IsKeyPressed(TR_KEY_DOWN)) { m_CameraPosition.y -= m_CameraMoveSpeed; }
		if (Trinity::Input::IsKeyPressed(TR_KEY_RIGHT)) { m_CameraPosition.x += m_CameraMoveSpeed; }
		else if (Trinity::Input::IsKeyPressed(TR_KEY_LEFT)) { m_CameraPosition.x -= m_CameraMoveSpeed; }

		if (Trinity::Input::IsKeyPressed(TR_KEY_D)) { m_CameraRotation -= m_CameraRotationSpeed; }
		else if (Trinity::Input::IsKeyPressed(TR_KEY_A)) { m_CameraRotation += m_CameraRotationSpeed; }

		Trinity::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
		Trinity::RenderCommand::Clear();

		m_Camera.SetPosition(m_CameraPosition);
		m_Camera.SetRotation(m_CameraRotation);

		Trinity::Renderer::BeginScene(m_Camera);

		Trinity::Renderer::Submit(m_ShaderSquare, m_SquareVA);
		Trinity::Renderer::Submit(m_Shader, m_VertexArray);

		Trinity::Renderer::EndScene();
	}

	void OnEvent(Trinity::Event& event) override
	{

	}

	virtual void OnImGuiRender() override
	{

	}

private:
	std::shared_ptr<Trinity::Shader> m_Shader;
	std::shared_ptr<Trinity::Shader> m_ShaderSquare;

	std::shared_ptr<Trinity::VertexArray> m_VertexArray;
	std::shared_ptr<Trinity::VertexArray> m_SquareVA;

	Trinity::OrthographicCamera m_Camera;
	glm::vec3 m_CameraPosition;
	float m_CameraMoveSpeed;

	float m_CameraRotation;
	float m_CameraRotationSpeed;
};

class Sandbox : public Trinity::Application
{
public:
	Sandbox()
	{
		PushLayer(new DemoLayer);
	}

	~Sandbox()
	{

	}
};

Trinity::Application* Trinity::CreateApplication()
{
	return new Sandbox();
}