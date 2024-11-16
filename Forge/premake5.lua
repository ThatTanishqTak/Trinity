project "Forge"
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
			"{COPYDIR} ../Forge %{cfg.targetdir}",
			removefiles
			{
				"{DELETEDIR} \"%{cfg.targetdir}/src\"",
				"{DELETEFILE} \"%{cfg.targetdir}/premake5.lua\"",
				"{DELETEFILE} \"%{cfg.targetdir}/Forge.vcxproj.user\"",
				"{DELETEFILE} \"%{cfg.targetdir}/Forge.vcxproj.filters\"",
				"{DELETEFILE} \"%{cfg.targetdir}/Forge.vcxproj\""
			}
		}

	filter "configurations:Release"
		defines "TR_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		defines "TR_DIST"
		runtime "Release"
		optimize "on"