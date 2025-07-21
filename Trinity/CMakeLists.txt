project(Trinity LANGUAGES CXX)

# Defines
add_definitions(-DGLFW_INCLUDE_VULKAN)

# Third-Party libraries

# Vulkan
find_package(Vulkan REQUIRED)

# GLFW
set(GLFW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw")
set(GLFW_INCLUDE_DIR "${GLFW_ROOT}/include")
set(GLFW_LIBRARY_DIR "${GLFW_ROOT}/lib")

# Engine source files
file(GLOB_RECURSE ENGINE_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

# Build static library
add_library(${PROJECT_NAME} STATIC
    ${ENGINE_SOURCE}
)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    PRIVATE ${GLFW_INCLUDE_DIR}
)

# Precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Trinity/trpch.h
)

# Link libraries
if (WIN32)
    set(GLFW_LIB "${GLFW_LIBRARY_DIR}/glfw3.lib")
else()
    set(GLFW_LIB "${GLFW_LIBRARY_DIR}/libglfw3.a")
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC
    Vulkan::Vulkan
    ${GLFW_LIB}
)