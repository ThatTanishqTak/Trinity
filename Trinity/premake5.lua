project "Trinity"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	staticruntime "on"

	targetdir ("bin/" .. outputDir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputDir .. "/%{prj.name}")

	pchheader "trpch.h"
	pchsource "src/trpch.cpp"

	files
	{
		"src/**.h",
		"src/**.h",
		"src/**.cpp",
		"vendor/stb_image/**.h",
		"vendor/stb_image/**.cpp",
		"vendor/glm/glm/**.hpp",
		"vendor/glm/glm/**.inl",
		"vendor/ImGuizmo/ImGuizmo.h",
		"vendor/ImGuizmo/ImGuizmo.cpp"
	}

	defines
	{
        "TR_PLATFORM_WINDOWS",
		"_CRT_SECURE_NO_WARNINGS",
        "GLFW_INCLUDE_NONE"
	}

	includedirs
	{
		"src",
		"vendor/spdlog/include",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.Glad}",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.stb_image}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.yaml_cpp}",
		"%{IncludeDir.ImGuizmo}"
	}

	links
	{
		"GLFW",
		"Glad",
		"ImGui",
		"yaml-cpp",
		"opengl32.lib"
	}

	filter "files:vendor/ImGuizmo/**.cpp"
	flags { "NoPCH" }

	filter "system:windows"
		systemversion "latest"

		defines 
		{
            "_CRT_SECURE_NO_WARNINGS",
		    "GLFW_INCLUDE_NONE",
            "YAML_CPP_STATIC_DEFINE"
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