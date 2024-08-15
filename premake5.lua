workspace "Trinity"
	architecture "x64"
	startproject "Sandbox"
	
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

include "Trinity/vendor/GLFW"
include "Trinity/vendor/Glad"
include "Trinity/vendor/imgui"

project "Trinity"
	location "Trinity"
	kind "SharedLib"
	language "C++"
	staticruntime "off"

	targetdir ("bin/" .. outputDir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputDir .. "/%{prj.name}")

	pchheader "trpch.h"
	pchsource "Trinity/src/trpch.cpp"

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}

	includedirs
	{
		"%{prj.name}/src",
		"%{prj.name}/vendor/spdlog/include",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.Glad}",
		"%{IncludeDir.ImGui}"
	}

	links
	{
		"GLFW",
		"Glad",
		"ImGui",
		"opengl32.lib",
		"dwmapi.lib"
	}

	filter "system:windows"
		cppdialect "C++17"
		systemversion "latest"
		runtime "Debug"

		defines 
		{
			"TR_PLATFORM_WINDOWS",
			"TR_BUILD_DLL",
			"GLFW_INCLUDE_NONE"
		}

		postbuildcommands
		{
			"{MKDIR} %[bin/" .. outputDir .. "/Sandbox]",
			"{COPYFILE} %[%{!cfg.buildtarget.abspath}] %[bin/" .. outputDir .. "/Sandbox]"
		}

	filter "configurations:Debug"
		defines "TR_DEBUG"
		runtime "Debug"
		symbols "On"

	filter "configurations:Release"
		defines "TR_RELEASE"
		runtime "Release"
		optimize "On"

	filter "configurations:Dist"
		defines "TR_DIST"
		runtime "Release"
		optimize "On"

project "Sandbox"
	location "Sandbox"
	kind "ConsoleApp"
	language "C++"
	staticruntime "off"

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
		"Trinity/src"
	}

	links
	{
		"Trinity"
	}

	filter "system:windows"
		cppdialect "C++17"
		systemversion "latest"
		runtime "Debug"

		defines 
		{
			"TR_PLATFORM_WINDOWS"
		}

	filter "configurations:Debug"
		defines "TR_DEBUG"
		runtime "Debug"
		symbols "On"

	filter "configurations:Release"
		defines "TR_RELEASE"
		runtime "Release"
		optimize "On"

	filter "configurations:Dist"
		defines "TR_DIST"
		runtime "Release"
		optimize "On"