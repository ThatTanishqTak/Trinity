project "Sandbox"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputDir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputDir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp"
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

	filter "configurations:Debug"
		defines "TR_DEBUG"
		runtime "Debug"
		symbols "on"

		postbuildcommands
		{
			"{COPYFILE} \"%{LibraryDir.VulkanSDK_DebugDLL}/shaderc_sharedd.dll\" \"%{cfg.targetdir}\"",
			"{COPYFILE} \"../Sandbox/imgui.ini\" \"%{cfg.targetdir}\"",
			
			"{MKDIR} \"%{cfg.targetdir}/assets\"",
			"{COPYDIR} ../Sandbox/assets/ %{cfg.targetdir}/assets"
		}

	filter "configurations:Release"
		defines "TR_RELEASE"
		runtime "Release"
		optimize "on"

		postbuildcommands
		{
			"{COPYFILE} \"%{LibraryDir.VulkanSDK_DebugDLL}/shaderc_sharedd.dll\" \"%{cfg.targetdir}\"",
			"{COPYFILE} \"../Sandbox/imgui.ini\" \"%{cfg.targetdir}\"",
			
			"{MKDIR} \"%{cfg.targetdir}/assets\"",
			"{COPYDIR} ../Sandbox/assets/ %{cfg.targetdir}/assets"
		}

	filter "configurations:Dist"
		defines "TR_DIST"
		runtime "Release"
		optimize "on"
		kind "WindowedApp"
		entrypoint "mainCRTStartup"

		postbuildcommands
		{
			"{COPYFILE} \"%{LibraryDir.VulkanSDK_DebugDLL}/shaderc_sharedd.dll\" \"%{cfg.targetdir}\"",
			"{COPYFILE} \"../Sandbox/imgui.ini\" \"%{cfg.targetdir}\"",
			
			"{MKDIR} \"%{cfg.targetdir}/assets\"",
			"{COPYDIR} ../Sandbox/assets/ %{cfg.targetdir}/assets"
		}