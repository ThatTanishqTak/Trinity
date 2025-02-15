-- Trinity Dependencies

VULKAN_SDK = os.getenv("VULKAN_SDK")

IncludeDir = {}
IncludeDir["stb_image"] = "%{wks.location}/Trinity/vendor/stb_image"
IncludeDir["yaml_cpp"] = "%{wks.location}/Trinity/vendor/yaml-cpp/include"
IncludeDir["Box2D"] = "%{wks.location}/Trinity/vendor/Box2D/include"
IncludeDir["GLFW"] = "%{wks.location}/Trinity/vendor/GLFW/include"
IncludeDir["Glad"] = "%{wks.location}/Trinity/vendor/Glad/include"
IncludeDir["ImGui"] = "%{wks.location}/Trinity/vendor/ImGui"
IncludeDir["ImGuizmo"] = "%{wks.location}/Trinity/vendor/ImGuizmo"
IncludeDir["glm"] = "%{wks.location}/Trinity/vendor/glm"
IncludeDir["entt"] = "%{wks.location}/Trinity/vendor/entt/include"
IncludeDir["mono"] = "%{wks.location}/Trinity/vendor/mono/include"
IncludeDir["shaderc"] = "%{wks.location}/Trinity/vendor/VulkanSDK/1.3.296.0/Include/shaderc"
IncludeDir["SPIRV_Cross"] = "%{wks.location}/Trinity/vendor/VulkanSDK/1.3.296.0/Include/spirv_cross"
IncludeDir["VulkanSDK"] = "%{VULKAN_SDK}/Include"

LibraryDir = {}
LibraryDir["mono"] = "%{wks.location}/Trinity/vendor/mono/lib/%{cfg.buildcfg}"
LibraryDir["VulkanSDK"] = "%{VULKAN_SDK}/Lib"
LibraryDir["VulkanSDK_Debug"] = "%{wks.location}/Trinity/vendor/VulkanSDK/1.3.296.0/Lib"
LibraryDir["VulkanSDK_DebugDLL"] = "%{wks.location}/Trinity/vendor/VulkanSDK/1.3.296.0/Bin"

Library = {}
Library["mono"] = "%{LibraryDir.mono}/libmono-static-sgen.lib"

Library["Vulkan"] = "%{LibraryDir.VulkanSDK}/vulkan-1.lib"
Library["VulkanUtils"] = "%{LibraryDir.VulkanSDK}/VkLayer_utils.lib"

Library["ShaderC_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/shaderc_sharedd.lib"
Library["SPIRV_Cross_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/spirv-cross-cored.lib"
Library["SPIRV_Cross_GLSL_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/spirv-cross-glsld.lib"
Library["SPIRV_Tools_Debug"] = "%{LibraryDir.VulkanSDK_Debug}/SPIRV-Toolsd.lib"
Library["ShaderC_Release"] = "%{LibraryDir.VulkanSDK}/shaderc_shared.lib"
Library["SPIRV_Cross_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-core.lib"
Library["SPIRV_Cross_GLSL_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-glsl.lib"

-- Windows only
Library["WinSock"] = "Ws2_32.lib"
Library["Winmm"] = "Winmm.lib"
Library["Version"] = "Version.lib"
Library["Bcrypt"] = "Bcrypt.lib"