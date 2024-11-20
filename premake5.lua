include "./vendor/premake/premake_customization/solution_items.lua"
include "Dependencies.lua"

workspace "Trinity"
	architecture "x86_64"
	startproject "Forge"
	
	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

	solution_items
	{
		".editorconfig"
	}

	flags
	{
		"MultiProcessorCompile"
	}

outputDir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

group "Dependencies"
	include "vendor/premake"
	include "Trinity/vendor/Box2D"
	include "Trinity/vendor/GLFW"
	include "Trinity/vendor/Glad"
	include "Trinity/vendor/imgui"
	include "Trinity/vendor/yaml-cpp"
group ""


include "Trinity"
include "Forge"
--include "Sandbox"