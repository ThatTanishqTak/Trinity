#include "trpch.h"
#include "Trinity/Utilities/PlatformUtils.h"
#include "Trinity/Core/Application.h"

#include <commdlg.h>

#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace Trinity
{
	float Time::GetTime()
	{
		return glfwGetTime();
	}

	std::string FileDialogs::OpenFile(const char* filter)
	{
		OPENFILENAMEA openFileName;
		CHAR szFile[260] = { 0 };
		CHAR currentDir[256] = { 0 };
		
		ZeroMemory(&openFileName, sizeof(OPENFILENAME));
		openFileName.lStructSize = sizeof(OPENFILENAME);
		openFileName.hwndOwner = glfwGetWin32Window((GLFWwindow*)Application::Get().GetWindow().GetNativeWindow());
		openFileName.lpstrFile = szFile;
		openFileName.nMaxFile = sizeof(szFile);
		
		if (GetCurrentDirectoryA(256, currentDir))
		{
			openFileName.lpstrInitialDir = currentDir;
		}
		
		openFileName.lpstrFilter = filter;
		openFileName.nFilterIndex = 1;
		openFileName.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

		if (GetOpenFileNameA(&openFileName) == TRUE)
		{
			return openFileName.lpstrFile;
		}

		return std::string();
	}

	std::string FileDialogs::SaveFile(const char* filter)
	{
		OPENFILENAMEA openFileName;
		CHAR szFile[260] = { 0 };
		CHAR currentDir[256] = { 0 };

		ZeroMemory(&openFileName, sizeof(OPENFILENAME));
		openFileName.lStructSize = sizeof(OPENFILENAME);
		openFileName.hwndOwner = glfwGetWin32Window((GLFWwindow*)Application::Get().GetWindow().GetNativeWindow());
		openFileName.lpstrFile = szFile;
		openFileName.nMaxFile = sizeof(szFile);
		openFileName.lpstrFilter = filter;
		openFileName.nFilterIndex = 1;
		openFileName.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
		openFileName.lpstrDefExt = strchr(filter, '\0') + 1; 		// Sets the default extension by extracting it from the filter

		if (GetSaveFileNameA(&openFileName) == TRUE)
		{
			return openFileName.lpstrFile;
		}

		return std::string();
	}
}