##--- Engine ---##

project(Trinity LANGUAGES CXX)

# Defines
add_definitions(-DGLFW_INCLUDE_VULKAN)

# Third-Party libraries
# filewatch
add_library(filewatch INTERFACE)
target_include_directories(filewatch INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/filewatch>    
)

# spdlog
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/spdlog/include>
)

# stb image
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb>    
)

# entt
add_library(EnTT INTERFACE)
target_include_directories(EnTT INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/entt/src>    
)

# GLFW
add_library(glfw INTERFACE)
target_include_directories(glfw INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw/include>
)
target_link_libraries(glfw INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw/lib/glfw3.lib
)

# glm
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/glm>    
)

# nlohmann
add_library(nlohmann INTERFACE)
target_include_directories(nlohmann INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann/include>    
)

# imgui
add_library(imgui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends>
)
target_link_libraries(imgui PUBLIC
    glfw
    Vulkan
)

# assimp
add_library(assimp STATIC IMPORTED)
set_target_properties(assimp PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/assimp/lib/${ARCH}/assimp-vc143-mt.lib
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/vendor/assimp/include
)

add_library(imguifiledialog STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGuiFileDialog/ImGuiFileDialog.cpp
)
target_include_directories(imguifiledialog PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGuiFileDialog>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends>
)
target_link_libraries(imguifiledialog PUBLIC imgui)

# Vulkan
add_library(Vulkan INTERFACE)
if(DEFINED ENV{VULKAN_SDK})
    set(Vulkan_SDK $ENV{VULKAN_SDK})
    message(STATUS "Using Vulkan SDK from ${Vulkan_SDK}")

    target_include_directories(Vulkan INTERFACE
        ${Vulkan_SDK}/Include)
    target_link_libraries(Vulkan INTERFACE
        ${Vulkan_SDK}/Lib/vulkan-1.lib)
else()
    message(FATAL_ERROR "VULKAN_SDK environment variable not set")
endif()

# shaderc
set(SHADERC_POSSIBLE_LIB_NAMES shaderc_combined shaderc_shared shaderc)
find_library(SHADERC_LIB NAMES ${SHADERC_POSSIBLE_LIB_NAMES}
    HINTS ${Vulkan_SDK}/Lib
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/vendor/shaderc/lib
          /usr/lib
          /usr/local/lib
)
if(NOT SHADERC_LIB)
    message(FATAL_ERROR "shaderc library not found")
endif()

add_library(shaderc INTERFACE)
target_include_directories(shaderc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/shaderc/include>
    $<BUILD_INTERFACE:${Vulkan_SDK}/Include>
)
target_link_libraries(shaderc INTERFACE ${SHADERC_LIB})

# Engine source files
file(GLOB_RECURSE ENGINE_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

# Build .lib
add_library(${PROJECT_NAME} STATIC 
    ${ENGINE_SOURCE}
)
# Include directory
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

# Set pre compiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Trinity/trpch.h
)

# yaml-cpp
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/yaml-cpp)

# Link libraries
target_link_libraries(${PROJECT_NAME} PUBLIC
    spdlog
    stb
    EnTT
    glfw
    nlohmann
    glm
    imgui
    imguifiledialog
    assimp
    Vulkan
    shaderc
    filewatch
    yaml-cpp
)

# Enable runtime shader compilation when requested
if(TR_RUNTIME_SHADER_COMPILE)
    target_compile_definitions(${PROJECT_NAME} PUBLIC TR_RUNTIME_SHADER_COMPILE)
endif()

# Copy engine shader resources to the output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Trinity/Resources/Shaders
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/Resources/Shaders
    COMMENT "Copying engine shaders to output directory"
)