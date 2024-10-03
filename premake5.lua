workspace "Trinity"
	architecture "x64"
	startproject "Forge"
	
	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

	flags
	{
		"MultiProcessorCompile"
	}

outputDir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["GLFW"] = "%{wks.location}/Trinity/vendor/GLFW/include"
IncludeDir["Glad"] = "%{wks.location}/Trinity/vendor/Glad/include"
IncludeDir["ImGui"] = "%{wks.location}/Trinity/vendor/imgui"
IncludeDir["glm"] = "%{wks.location}/Trinity/vendor/glm"
IncludeDir["stb_image"] = "%{wks.location}/Trinity/vendor/stb_image"
IncludeDir["entt"] = "%{wks.location}/Trinity/vendor/entt/include"
IncludeDir["yaml_cpp"] = "%{wks.location}/Trinity/vendor/yaml-cpp/include"

group "Dependencies"
	include "Trinity/vendor/GLFW"
	include "Trinity/vendor/Glad"
	include "Trinity/vendor/imgui"
	include "Trinity/vendor/yaml-cpp"
group ""

include "Trinity"
include "Sandbox"
include "Forge"