project "Forge"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputDir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputDir .. "/%{prj.name}")

	defines
	{
		"_CRT_SECURE_NO_WARNINGS"
	}

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
		"%{IncludeDir.entt}",
		"%{IncludeDir.ImGuizmo}"
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
			"{COPYFILE} \"../Forge/imgui.ini\" \"%{cfg.targetdir}\"",
			
			"{MKDIR} \"%{cfg.targetdir}/Resources\"",
			"{MKDIR} \"%{cfg.targetdir}/assets\"",
			"{MKDIR} \"%{cfg.targetdir}/mono\"",

			"{COPYDIR} ../Forge/Resources/ %{cfg.targetdir}/Resources",
			"{COPYDIR} ../Forge/assets/ %{cfg.targetdir}/assets",
			"{COPYDIR} ../Forge/mono %{cfg.targetdir}/mono"
		}

	filter "configurations:Release"
		defines "TR_RELEASE"
		runtime "Release"
		optimize "on"

		postbuildcommands
		{
			"{COPYFILE} \"%{LibraryDir.VulkanSDK_DebugDLL}/shaderc_sharedd.dll\" \"%{cfg.targetdir}\"",
			"{COPYFILE} \"../Forge/imgui.ini\" \"%{cfg.targetdir}\"",
			
			"{MKDIR} \"%{cfg.targetdir}/Resources\"",
			"{MKDIR} \"%{cfg.targetdir}/assets\"",
			"{MKDIR} \"%{cfg.targetdir}/mono\"",

			"{COPYDIR} ../Forge/Resources/ %{cfg.targetdir}/Resources",
			"{COPYDIR} ../Forge/assets/ %{cfg.targetdir}/assets",
			"{COPYDIR} ../Forge/mono %{cfg.targetdir}/mono"
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
			"{COPYFILE} \"../Forge/imgui.ini\" \"%{cfg.targetdir}\"",
			
			"{MKDIR} \"%{cfg.targetdir}/Resources\"",
			"{MKDIR} \"%{cfg.targetdir}/assets\"",
			"{MKDIR} \"%{cfg.targetdir}/mono\"",

			"{COPYDIR} ../Forge/Resources/ %{cfg.targetdir}/Resources",
			"{COPYDIR} ../Forge/assets/ %{cfg.targetdir}/assets",
			"{COPYDIR} ../Forge/mono %{cfg.targetdir}/mono"
		}