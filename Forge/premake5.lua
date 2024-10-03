project "Forge"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	staticruntime "on"

	targetdir ("%{wks.location}/bin/" .. outputDir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputDir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp"
	}

    defines
    {
        "TR_PLATFORM_WINDOWS"
    }

	includedirs
	{
		"%{wks.location}/Trinity/vendor/spdlog/include",
		"%{wks.location}/Trinity/src",
		"%{wks.location}/Trinity/vendor",
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