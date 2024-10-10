include "Dependencies.lua"

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

group "Dependencies"
	include "Trinity/vendor/GLFW"
	include "Trinity/vendor/Glad"
	include "Trinity/vendor/imgui"
	include "Trinity/vendor/yaml-cpp"
group ""

group "Engine"
	include "Trinity"
group ""

group "Tool"
	include "Forge"
group ""

group "Misc"
	include "Sandbox"
group ""