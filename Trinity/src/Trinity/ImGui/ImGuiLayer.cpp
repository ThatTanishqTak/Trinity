#include "trpch.h"
#include "ImGuiLayer.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "Trinity/Core/Application.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include "ImGuizmo.h"

namespace Trinity
{
	ImGuiLayer::ImGuiLayer() : Layer("ImGUI Layer")
	{

	}

	ImGuiLayer::~ImGuiLayer()
	{

	}

	void ImGuiLayer::OnAttach()
	{
		IMGUI_CHECKVERSION();

		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoTaskBarIcons;
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoMerge;

		ImGui::StyleColorsDark();
		//ImGui::StyleColorsClassic();

		io.Fonts->AddFontFromFileTTF("assets/fonts/OpenSans/OpenSans-Bold.ttf", 18.0f);
		io.FontDefault = io.Fonts->AddFontFromFileTTF("assets/fonts/OpenSans/OpenSans-Regular.ttf", 18.0f);

		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		SetDarkThemeColors();

		Application& app = Application::Get();
		GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 410");
	}

	void ImGuiLayer::OnDetach()
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		
		ImGui::DestroyContext();
	}

	void ImGuiLayer::OnEvent(Event& e)
	{
		if (m_BlockEvents)
		{
			ImGuiIO& io = ImGui::GetIO();

			e.Handled |= e.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
			e.Handled |= e.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
		}
	}

	void ImGuiLayer::Begin()
	{
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
	
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void ImGuiLayer::End()
	{
		ImGuiIO& io = ImGui::GetIO();
		Application& app = Application::Get();

		io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup_current_context = glfwGetCurrentContext();

			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();

			glfwMakeContextCurrent(backup_current_context);
		}
	}

	void ImGuiLayer::SetDarkThemeColors()
	{
		auto& colors = ImGui::GetStyle().Colors;

		colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };
		
		colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
		colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
		colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };

		colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
	}

	void ImGuiLayer::OnImGuiRender()
	{
		//ImGui::ShowDemoWindow();
	}

	void OTHER_THEMES()
	{
		//IMP: THIS IS FOR GRUVBOX THEME
		auto& style = ImGui::GetStyle();
		style.ChildRounding = 0;
		style.GrabRounding = 0;
		style.FrameRounding = 0;
		style.PopupRounding = 0;
		style.ScrollbarRounding = 0;
		style.TabRounding = 0;
		style.WindowRounding = 0;
		style.FramePadding = { 4, 4 };

		style.WindowTitleAlign = { 0.5, 0.5 };

		ImVec4* colors = ImGui::GetStyle().Colors;
		//Updated to use IM_COL32 for more precise colors and to add table colors(1.80 feature)
		colors[ImGuiCol_Text] = ImColor{ IM_COL32(0xeb, 0xdb, 0xb2, 0xFF) };
		colors[ImGuiCol_TextDisabled] = ImColor{ IM_COL32(0x92, 0x83, 0x74, 0xFF) };
		colors[ImGuiCol_WindowBg] = ImColor{ IM_COL32(0x1d, 0x20, 0x21, 0xF0) };
		colors[ImGuiCol_ChildBg] = ImColor{ IM_COL32(0x1d, 0x20, 0x21, 0xFF) };
		colors[ImGuiCol_PopupBg] = ImColor{ IM_COL32(0x1d, 0x20, 0x21, 0xF0) };
		colors[ImGuiCol_Border] = ImColor{ IM_COL32(0x1d, 0x20, 0x21, 0xFF) };
		colors[ImGuiCol_BorderShadow] = ImColor{ 0 };
		colors[ImGuiCol_FrameBg] = ImColor{ IM_COL32(0x3c, 0x38, 0x36, 0x90) };
		colors[ImGuiCol_FrameBgHovered] = ImColor{ IM_COL32(0x50, 0x49, 0x45, 0xFF) };
		colors[ImGuiCol_FrameBgActive] = ImColor{ IM_COL32(0x66, 0x5c, 0x54, 0xA8) };
		colors[ImGuiCol_TitleBg] = ImColor{ IM_COL32(0xd6, 0x5d, 0x0e, 0xFF) };
		colors[ImGuiCol_TitleBgActive] = ImColor{ IM_COL32(0xfe, 0x80, 0x19, 0xFF) };
		colors[ImGuiCol_TitleBgCollapsed] = ImColor{ IM_COL32(0xd6, 0x5d, 0x0e, 0x9C) };
		colors[ImGuiCol_MenuBarBg] = ImColor{ IM_COL32(0x28, 0x28, 0x28, 0xF0) };
		colors[ImGuiCol_ScrollbarBg] = ImColor{ IM_COL32(0x00, 0x00, 0x00, 0x28) };
		colors[ImGuiCol_ScrollbarGrab] = ImColor{ IM_COL32(0x3c, 0x38, 0x36, 0xFF) };
		colors[ImGuiCol_ScrollbarGrabHovered] = ImColor{ IM_COL32(0x50, 0x49, 0x45, 0xFF) };
		colors[ImGuiCol_ScrollbarGrabActive] = ImColor{ IM_COL32(0x66, 0x5c, 0x54, 0xFF) };
		colors[ImGuiCol_CheckMark] = ImColor{ IM_COL32(0xd6, 0x5d, 0x0e, 0x9E) };
		colors[ImGuiCol_SliderGrab] = ImColor{ IM_COL32(0xd6, 0x5d, 0x0e, 0x70) };
		colors[ImGuiCol_SliderGrabActive] = ImColor{ IM_COL32(0xfe, 0x80, 0x19, 0xFF) };
		colors[ImGuiCol_Button] = ImColor{ IM_COL32(0xd6, 0x5d, 0x0e, 0x66) };
		colors[ImGuiCol_ButtonHovered] = ImColor{ IM_COL32(0xfe, 0x80, 0x19, 0x9E) };
		colors[ImGuiCol_ButtonActive] = ImColor{ IM_COL32(0xfe, 0x80, 0x19, 0xFF) };
		colors[ImGuiCol_Header] = ImColor{ IM_COL32(0xd6, 0x5d, 0x0e, 0.4F) };
		colors[ImGuiCol_HeaderHovered] = ImColor{ IM_COL32(0xfe, 0x80, 0x19, 0xCC) };
		colors[ImGuiCol_HeaderActive] = ImColor{ IM_COL32(0xfe, 0x80, 0x19, 0xFF) };
		colors[ImGuiCol_Separator] = ImColor{ IM_COL32(0x66, 0x5c, 0x54, 0.50f) };
		colors[ImGuiCol_SeparatorHovered] = ImColor{ IM_COL32(0x50, 0x49, 0x45, 0.78f) };
		colors[ImGuiCol_SeparatorActive] = ImColor{ IM_COL32(0x66, 0x5c, 0x54, 0xFF) };
		colors[ImGuiCol_ResizeGrip] = ImColor{ IM_COL32(0xd6, 0x5d, 0x0e, 0x40) };
		colors[ImGuiCol_ResizeGripHovered] = ImColor{ IM_COL32(0xfe, 0x80, 0x19, 0xAA) };
		colors[ImGuiCol_ResizeGripActive] = ImColor{ IM_COL32(0xfe, 0x80, 0x19, 0xF2) };
		colors[ImGuiCol_Tab] = ImColor{ IM_COL32(0xd6, 0x5d, 0x0e, 0xD8) };
		colors[ImGuiCol_TabHovered] = ImColor{ IM_COL32(0xfe, 0x80, 0x19, 0xCC) };
		colors[ImGuiCol_TabActive] = ImColor{ IM_COL32(0xfe, 0x80, 0x19, 0xFF) };
		colors[ImGuiCol_TabUnfocused] = ImColor{ IM_COL32(0x1d, 0x20, 0x21, 0.97f) };
		colors[ImGuiCol_TabUnfocusedActive] = ImColor{ IM_COL32(0x1d, 0x20, 0x21, 0xFF) };
		colors[ImGuiCol_PlotLines] = ImColor{ IM_COL32(0xd6, 0x5d, 0x0e, 0xFF) };
		colors[ImGuiCol_PlotLinesHovered] = ImColor{ IM_COL32(0xfe, 0x80, 0x19, 0xFF) };
		colors[ImGuiCol_PlotHistogram] = ImColor{ IM_COL32(0x98, 0x97, 0x1a, 0xFF) };
		colors[ImGuiCol_PlotHistogramHovered] = ImColor{ IM_COL32(0xb8, 0xbb, 0x26, 0xFF) };
		colors[ImGuiCol_TextSelectedBg] = ImColor{ IM_COL32(0x45, 0x85, 0x88, 0x59) };
		colors[ImGuiCol_DragDropTarget] = ImColor{ IM_COL32(0x98, 0x97, 0x1a, 0.90f) };
		colors[ImGuiCol_TableHeaderBg] = ImColor{ IM_COL32(0x38, 0x3c, 0x36, 0xFF) };
		colors[ImGuiCol_TableBorderStrong] = ImColor{ IM_COL32(0x28, 0x28, 0x28, 0xFF) };
		colors[ImGuiCol_TableBorderLight] = ImColor{ IM_COL32(0x38, 0x3c, 0x36, 0xFF) };
		colors[ImGuiCol_TableRowBg] = ImColor{ IM_COL32(0x1d, 0x20, 0x21, 0xFF) };
		colors[ImGuiCol_TableRowBgAlt] = ImColor{ IM_COL32(0x28, 0x28, 0x28, 0xFF) };
		colors[ImGuiCol_TextSelectedBg] = ImColor{ IM_COL32(0x45, 0x85, 0x88, 0xF0) };
		colors[ImGuiCol_NavHighlight] = ImColor{ IM_COL32(0x83, 0xa5, 0x98, 0xFF) };
		colors[ImGuiCol_NavWindowingHighlight] = ImColor{ IM_COL32(0xfb, 0xf1, 0xc7, 0xB2) };
		colors[ImGuiCol_NavWindowingDimBg] = ImColor{ IM_COL32(0x7c, 0x6f, 0x64, 0x33) };
		colors[ImGuiCol_ModalWindowDimBg] = ImColor{ IM_COL32(0x1d, 0x20, 0x21, 0x59) };
	}
}