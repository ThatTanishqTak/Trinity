# Vendor CMakeLists.txt

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_FOLDER "Vendor")

set(TRINITY_VENDOR_TARGETS "")

function(trinity_vendor_set name value)
    set(${name} "${value}" CACHE INTERNAL "")
endfunction()

function(trinity_vendor_directory name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "SUBDIRECTORY" "")
    set(source_dir "${CMAKE_CURRENT_SOURCE_DIR}/${name}")
    if(arg_SUBDIRECTORY)
        string(APPEND source_dir "/${arg_SUBDIRECTORY}")
    endif()
    if(NOT EXISTS "${source_dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "Dependency '${name}' is missing (${source_dir}).\n"
            "Run Scripts/Bootstrap.ps1 (Windows) or Scripts/Bootstrap.sh (Linux/macOS) from the repository root.")
    endif()
    add_subdirectory("${source_dir}" "${CMAKE_CURRENT_BINARY_DIR}/${name}" EXCLUDE_FROM_ALL SYSTEM)
endfunction()

function(trinity_vendor_require name file)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}/${file}")
        message(FATAL_ERROR
            "Dependency '${name}' is missing (${CMAKE_CURRENT_SOURCE_DIR}/${name}).\n"
            "Run Scripts/Bootstrap.ps1 (Windows) or Scripts/Bootstrap.sh (Linux/macOS) from the repository root.")
    endif()
endfunction()

# --- Core -------------------------------------------------------------------------------------

# spdlog - logging. Uses std::format so engine types need one formatter, not an fmt one as well.
trinity_vendor_set(SPDLOG_USE_STD_FORMAT ON)
trinity_vendor_set(SPDLOG_BUILD_EXAMPLE OFF)
trinity_vendor_set(SPDLOG_INSTALL OFF)
trinity_vendor_directory(spdlog)
list(APPEND TRINITY_VENDOR_TARGETS spdlog)

# glm - math, header-only.
trinity_vendor_set(GLM_BUILD_LIBRARY OFF)
trinity_vendor_set(GLM_BUILD_TESTS OFF)
trinity_vendor_set(GLM_BUILD_INSTALL OFF)
trinity_vendor_directory(glm)

# EnTT - ECS, header-only.
trinity_vendor_directory(entt)

# yaml-cpp - scene and project serialisation.
trinity_vendor_set(YAML_CPP_BUILD_TOOLS OFF)
trinity_vendor_set(YAML_CPP_BUILD_TESTS OFF)
trinity_vendor_set(YAML_CPP_BUILD_CONTRIB OFF)
trinity_vendor_set(YAML_CPP_INSTALL OFF)
trinity_vendor_set(YAML_CPP_FORMAT_SOURCE OFF)
trinity_vendor_set(YAML_BUILD_SHARED_LIBS OFF)
trinity_vendor_directory(yaml-cpp)
list(APPEND TRINITY_VENDOR_TARGETS yaml-cpp)

# --- Physics ----------------------------------------------------------------------------------

# Jolt - 3D physics.
trinity_vendor_set(OVERRIDE_CXX_FLAGS OFF)
trinity_vendor_set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF)
trinity_vendor_set(INTERPROCEDURAL_OPTIMIZATION OFF)
trinity_vendor_set(ENABLE_ALL_WARNINGS OFF)
trinity_vendor_set(ENABLE_INSTALL OFF)
trinity_vendor_set(CPP_RTTI_ENABLED ON)
trinity_vendor_set(CPP_EXCEPTIONS_ENABLED ON)
trinity_vendor_set(JPH_USE_DX12 OFF)
trinity_vendor_set(JPH_USE_VK OFF)
trinity_vendor_set(JPH_USE_MTL OFF)
trinity_vendor_directory(JoltPhysics SUBDIRECTORY Build)
list(APPEND TRINITY_VENDOR_TARGETS Jolt)

# Box2D v3
trinity_vendor_directory(box2d)
list(APPEND TRINITY_VENDOR_TARGETS box2d)

# --- Fonts and UI -----------------------------------------------------------------------------

# FreeType
trinity_vendor_set(FT_DISABLE_ZLIB TRUE)
trinity_vendor_set(FT_DISABLE_BZIP2 TRUE)
trinity_vendor_set(FT_DISABLE_PNG TRUE)
trinity_vendor_set(FT_DISABLE_HARFBUZZ TRUE)
trinity_vendor_set(FT_DISABLE_BROTLI TRUE)
trinity_vendor_directory(freetype)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
list(APPEND TRINITY_VENDOR_TARGETS freetype)

# Dear ImGui (docking branch)
trinity_vendor_require(imgui imgui.cpp)
add_library(imgui STATIC EXCLUDE_FROM_ALL
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/misc/freetype/imgui_freetype.cpp
)
add_library(imgui::imgui ALIAS imgui)
target_include_directories(imgui SYSTEM PUBLIC imgui)
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_features(imgui PUBLIC cxx_std_17)
target_link_libraries(imgui PRIVATE Freetype::Freetype)
list(APPEND TRINITY_VENDOR_TARGETS imgui)

# ImGuizmo - gizmos, plus its sequencer / curve / gradient / graph editors.
trinity_vendor_require(ImGuizmo src/ImGuizmo.cpp)
add_library(ImGuizmo STATIC EXCLUDE_FROM_ALL
    ImGuizmo/src/ImGuizmo.cpp
    ImGuizmo/src/ImSequencer.cpp
    ImGuizmo/src/ImCurveEdit.cpp
    ImGuizmo/src/ImGradient.cpp
    ImGuizmo/src/GraphEditor.cpp
)
add_library(ImGuizmo::ImGuizmo ALIAS ImGuizmo)
target_include_directories(ImGuizmo SYSTEM PUBLIC ImGuizmo/src)
target_link_libraries(ImGuizmo PUBLIC imgui::imgui)
list(APPEND TRINITY_VENDOR_TARGETS ImGuizmo)

# RmlUi - in-game UI. Finds FreeType through the target defined above.
trinity_vendor_set(RMLUI_SAMPLES OFF)
trinity_vendor_set(RMLUI_FONT_ENGINE "freetype")
trinity_vendor_set(CMAKE_DISABLE_FIND_PACKAGE_Freetype TRUE)   # never fall back to a system copy
trinity_vendor_directory(RmlUi)
list(APPEND TRINITY_VENDOR_TARGETS rmlui_core rmlui_debugger)

# --- Assets -----------------------------------------------------------------------------------

# stb - image decode for the importer (header-only; the implementation unit lives in the engine).
trinity_vendor_require(stb stb_image.h)
add_library(stb INTERFACE)
add_library(stb::stb ALIAS stb)
target_include_directories(stb SYSTEM INTERFACE stb)

# KTX-Software - KTX2 textures with Basis Universal. ktx_read reads and transcodes, and ktx also encodes and writes
trinity_vendor_set(KTX_GIT_VERSION_FULL v4.4.2)
trinity_vendor_set(KTX_FEATURE_TOOLS OFF)
trinity_vendor_set(KTX_FEATURE_TESTS OFF)
trinity_vendor_set(KTX_FEATURE_DOC OFF)
trinity_vendor_set(KTX_FEATURE_JNI OFF)
trinity_vendor_set(KTX_FEATURE_PY OFF)
trinity_vendor_set(KTX_FEATURE_GL_UPLOAD OFF)
trinity_vendor_set(KTX_FEATURE_VK_UPLOAD OFF)
trinity_vendor_set(KTX_FEATURE_LOADTEST_APPS OFF)
trinity_vendor_set(BASISU_SUPPORT_OPENCL OFF)
trinity_vendor_directory(KTX-Software)
list(APPEND TRINITY_VENDOR_TARGETS ktx ktx_read)

# miniaudio - audio device, mixing and decoding.
trinity_vendor_require(miniaudio miniaudio.c)
add_library(miniaudio STATIC EXCLUDE_FROM_ALL miniaudio/miniaudio.c)
add_library(miniaudio::miniaudio ALIAS miniaudio)
target_include_directories(miniaudio SYSTEM PUBLIC miniaudio)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
list(APPEND TRINITY_VENDOR_TARGETS miniaudio)

# simdjson + fastgltf - glTF 2.0
trinity_vendor_set(SIMDJSON_DEVELOPER_MODE OFF)
trinity_vendor_directory(simdjson)
if(NOT TARGET simdjson::simdjson)
    add_library(simdjson::simdjson ALIAS simdjson)
endif()
trinity_vendor_set(FASTGLTF_ENABLE_INSTALL OFF)
trinity_vendor_directory(fastgltf)
list(APPEND TRINITY_VENDOR_TARGETS simdjson fastgltf)

# assimp
trinity_vendor_set(ASSIMP_BUILD_TESTS OFF)
trinity_vendor_set(ASSIMP_BUILD_ASSIMP_TOOLS OFF)
trinity_vendor_set(ASSIMP_BUILD_SAMPLES OFF)
trinity_vendor_set(ASSIMP_BUILD_DOCS OFF)
trinity_vendor_set(ASSIMP_INSTALL OFF)
trinity_vendor_set(ASSIMP_NO_EXPORT ON)
trinity_vendor_set(ASSIMP_WARNINGS_AS_ERRORS OFF)
trinity_vendor_set(ASSIMP_INJECT_DEBUG_POSTFIX OFF)
trinity_vendor_set(ASSIMP_BUILD_USE_CCACHE OFF)
trinity_vendor_set(ASSIMP_BUILD_ZLIB ON)
trinity_vendor_set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
trinity_vendor_set(ASSIMP_BUILD_FBX_IMPORTER ON)
trinity_vendor_set(ASSIMP_BUILD_OBJ_IMPORTER ON)
trinity_vendor_set(ASSIMP_BUILD_COLLADA_IMPORTER ON)
trinity_vendor_directory(assimp)
list(APPEND TRINITY_VENDOR_TARGETS assimp)

# --- Graphics ---------------------------------------------------------------------------------

if(TRINITY_RHI_VULKAN)
    trinity_vendor_directory(Vulkan-Headers)

    # volk - function loader
    set(_volk_platform_define "")
    if(TRINITY_PLATFORM STREQUAL "Windows")
        set(_volk_platform_define VK_USE_PLATFORM_WIN32_KHR)
    elseif(TRINITY_PLATFORM STREQUAL "macOS")
        set(_volk_platform_define VK_USE_PLATFORM_METAL_EXT)
    endif()
    trinity_vendor_set(VOLK_PULL_IN_VULKAN OFF)
    trinity_vendor_set(VOLK_STATIC_DEFINES "${_volk_platform_define}")
    trinity_vendor_directory(volk)
    target_link_libraries(volk PUBLIC Vulkan::Headers)
    list(APPEND TRINITY_VENDOR_TARGETS volk)

    # VulkanMemoryAllocator
    trinity_vendor_set(VMA_ENABLE_INSTALL OFF)
    trinity_vendor_directory(VulkanMemoryAllocator)
endif()

if(TRINITY_RHI_D3D12)
    # DirectX-Headers
    trinity_vendor_set(DXHEADERS_BUILD_TEST OFF)
    trinity_vendor_set(DXHEADERS_BUILD_GOOGLE_TEST OFF)
    trinity_vendor_set(DXHEADERS_INSTALL OFF)
    trinity_vendor_directory(DirectX-Headers)

    # D3D12MemoryAllocator
    trinity_vendor_set(D3D12MA_BUILD_SAMPLE OFF)
    trinity_vendor_directory(D3D12MemoryAllocator)
    target_link_libraries(D3D12MemoryAllocator PUBLIC
        $<BUILD_INTERFACE:Microsoft::DirectX-Headers>
        $<BUILD_INTERFACE:Microsoft::DirectX-Guids>)
    target_compile_definitions(D3D12MemoryAllocator PUBLIC D3D12MA_USING_DIRECTX_HEADERS)
    list(APPEND TRINITY_VENDOR_TARGETS DirectX-Headers DirectX-Guids D3D12MemoryAllocator)
endif()

# --- Aggregate --------------------------------------------------------------------------------

add_custom_target(Trinity-Vendor COMMENT "All third-party libraries built")
add_dependencies(Trinity-Vendor ${TRINITY_VENDOR_TARGETS})