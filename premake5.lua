workspace "Trinity"
	architecture "x64"
	startproject "Forge"
	
	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

outputDir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["GLFW"] = "Trinity/vendor/GLFW/include"
IncludeDir["Glad"] = "Trinity/vendor/Glad/include"
IncludeDir["ImGui"] = "Trinity/vendor/imgui"
IncludeDir["glm"] = "Trinity/vendor/glm"
IncludeDir["stb_image"] = "Trinity/vendor/stb_image"
IncludeDir["entt"] = "Trinity/vendor/entt/include"

group "Dependencies"
	include "Trinity/vendor/GLFW"
	include "Trinity/vendor/Glad"
	include "Trinity/vendor/imgui"
group ""

project "Trinity"
	location "Trinity"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	staticruntime "on"

	targetdir ("bin/" .. outputDir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputDir .. "/%{prj.name}")

	pchheader "trpch.h"
	pchsource "Trinity/src/trpch.cpp"

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp",
		"%{prj.name}/vendor/stb_image/**.h",
		"%{prj.name}/vendor/stb_image/**.cpp",
		"%{prj.name}/vendor/glm/glm/**.hpp",
		"%{prj.name}/vendor/glm/glm/**.inl",
	}

	includedirs
	{
		"%{prj.name}/src",
		"%{prj.name}/vendor/spdlog/include",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.Glad}",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.stb_image}",
		"%{IncludeDir.entt}"
	}
	defines
	{
		"_CRT_SECURE_NO_WARNINGS"
	}

	links
	{
		"GLFW",
		"Glad",
		"ImGui",
		"opengl32.lib"
	}

	filter "system:windows"
		systemversion "latest"
		runtime "Debug"

		defines 
		{
			"TR_PLATFORM_WINDOWS",
			"TR_BUILD_DLL",
			"GLFW_INCLUDE_NONE"
		}

	filter "configurations:Debug"
		defines "TR_DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines "TR_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		defines "TR_DIST"
		runtime "Release"
		optimize "on"

project "Sandbox"
	location "Sandbox"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	staticruntime "on"

	targetdir ("bin/" .. outputDir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputDir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}

	includedirs
	{
		"$(SolutionDir)Trinity/vendor/spdlog/include",
		"Trinity/src",
		"Trinity/vendor",
		"%{IncludeDir.glm}",
		"%{IncludeDir.entt}"
	}

	links
	{
		"Trinity"
	}

	filter "system:windows"
		systemversion "latest"
		runtime "Debug"

		defines 
		{
			"TR_PLATFORM_WINDOWS"
		}

	filter "configurations:Debug"
		defines "TR_DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines "TR_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		defines "TR_DIST"
		runtime "Release"
		optimize "on"

project "Forge"
	location "Forge"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	staticruntime "on"

	targetdir ("bin/" .. outputDir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputDir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}

	includedirs
	{
		"$(SolutionDir)Trinity/vendor/spdlog/include",
		"Trinity/src",
		"Trinity/vendor",
		"%{IncludeDir.glm}",
		"%{IncludeDir.entt}"
	}

	links
	{
		"Trinity"
	}

	filter "system:windows"
		systemversion "latest"
		runtime "Debug"

		defines 
		{
			"TR_PLATFORM_WINDOWS"
		}

	filter "configurations:Debug"
		defines "TR_DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines "TR_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		defines "TR_DIST"
		runtime "Release"
		optimize "on"