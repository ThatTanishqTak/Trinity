# Root CMakeLists.txt

cmake_minimum_required(VERSION 3.30...4.2)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use a preset: cmake --preset <name>")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

# Must run before project(): the configuration list is latched by the first project() call.
include(TrinityConfigurations)
trinity_declare_configurations()

project(Trinity VERSION 0.1.0 DESCRIPTION "Trinity game engine, Forge editor, Hub launcher and Sandbox" LANGUAGES C CXX)

trinity_setup_configuration_flags()

# Options come before the includes
option(TRINITY_WARNINGS_AS_ERRORS "Treat warnings in Trinity targets as errors" OFF)
option(TRINITY_ENABLE_LTO "Link-time optimisation for Trinity targets in Distribution" OFF)
option(TRINITY_TRACK_GLOBAL_ALLOCATIONS "Count every global new and delete under the Untagged memory tag" ON)
option(TRINITY_ENABLE_PROFILING "Compile profiling scopes into Debug and Release builds" ON)
option(TRINITY_SHARED_ENGINE "Build the engine as a shared library in Debug and Release. OFF links it statically everywhere" ON)

include(TrinityPlatform)
include(TrinityCompilerOptions)
include(TrinityShaders)
include(TrinityAgilitySDK)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Convert CMake architecture name to more readable architecture name
if(DEFINED CMAKE_VS_PLATFORM_NAME)
    string(TOLOWER "${CMAKE_VS_PLATFORM_NAME}" ARCH_NAME)
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ARCH_NAME)
endif()

if(ARCH_NAME MATCHES "amd64|x86_64|x64")
    set(ARCH_NAME "x86_64")
elseif(ARCH_NAME MATCHES "i[3-6]86|win32|x86")
    set(ARCH_NAME "x86_32")
elseif(ARCH_NAME MATCHES "aarch64|arm64")
    set(ARCH_NAME "arm64")
endif()

# Output directory
set(OUTPUT_BASE "${PROJECT_SOURCE_DIR}/bin/$<CONFIG>-${CMAKE_SYSTEM_NAME}-${ARCH_NAME}/${PROJECT_NAME}")

# One output tree for every target so executables find each other and their runtime files.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_BASE}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_BASE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_BASE}")

add_subdirectory(Vendor)
add_subdirectory(Trinity-Engine)
add_subdirectory(Trinity-Forge)
add_subdirectory(Trinity-Hub)
add_subdirectory(Trinity-Sandbox)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Trinity-Sandbox)

message(STATUS "Trinity ${PROJECT_VERSION}: platform=${TRINITY_PLATFORM} d3d12=${TRINITY_RHI_D3D12} vulkan=${TRINITY_RHI_VULKAN} metal=${TRINITY_RHI_METAL}")