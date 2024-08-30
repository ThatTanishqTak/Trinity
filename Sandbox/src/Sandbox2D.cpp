#include "Sandbox2D.h"

#include "Platform/OpenGL/OpenGLShader.h"

#include "imgui/imgui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

Sandbox2D::Sandbox2D() : Layer("Sandbox2D Layer"), m_CameraController(1280.0f / 720.0f)
{

}

void Sandbox2D::OnAttach()
{
	m_SquareVA = Trinity::VertexArray::Create();

	float verticesSquare[5 * 4] =
	{
		  -0.75f, -0.75f, 0.0f,
		   0.75f, -0.75f, 0.0f,
		   0.75f,  0.75f, 0.0f,
		  -0.75f,  0.75f, 0.0f 
	};

	Trinity::Ref<Trinity::VertexBuffer> squareVB;
	squareVB.reset(Trinity::VertexBuffer::Create(verticesSquare, sizeof(verticesSquare)));
	Trinity::BufferLayout squareLayout =
	{
		{ Trinity::ShaderDataType::Float3, std::string("a_Position") }
	};

	squareVB->SetLayout(squareLayout);
	m_SquareVA->AddVertexBuffer(squareVB);

	uint32_t indicesSquare[6] = { 0, 1, 2, 2, 3, 0 };
	Trinity::Ref<Trinity::IndexBuffer> squareIB;
	squareIB.reset(Trinity::IndexBuffer::Create(indicesSquare, sizeof(indicesSquare) / sizeof(uint32_t)));
	m_SquareVA->SetIndexBuffer(squareIB);

	m_FlatColorShader = Trinity::Shader::Create("assets/shaders/FlatColor.glsl");
}

void Sandbox2D::OnDetach()
{

}

void Sandbox2D::OnUpdate(Trinity::Timestep timestep)
{
	m_CameraController.OnUpdate(timestep);


	Trinity::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
	Trinity::RenderCommand::Clear();

	Trinity::Renderer::BeginScene(m_CameraController.GetCamera());

	std::dynamic_pointer_cast<Trinity::OpenGLShader>(m_FlatColorShader)->Bind();
	std::dynamic_pointer_cast<Trinity::OpenGLShader>(m_FlatColorShader)->UploadUniformFloat4("u_Color", m_SquareColor);

	Trinity::Renderer::Submit(m_FlatColorShader, m_SquareVA, glm::scale(glm::mat4(1.0f), glm::vec3(1.0f)));

	Trinity::Renderer::EndScene();
}

void Sandbox2D::OnEvent(Trinity::Event& e)
{
	m_CameraController.OnEvent(e);
}

void Sandbox2D::OnImGuiRender()
{
	ImGui::Begin("Color Setting");
	ImGui::ColorEdit4("SquareColor", glm::value_ptr(m_SquareColor));
	ImGui::End();
}