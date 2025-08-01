##--- Engine ---##

project(Trinity LANGUAGES CXX)

# Defines
add_definitions(-DGLFW_INCLUDE_VULKAN)

# Third-Party libraries
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

# Link libraries
target_link_libraries(${PROJECT_NAME} PUBLIC
    spdlog
    stb
    EnTT
    glfw
    glm
    Vulkan
)