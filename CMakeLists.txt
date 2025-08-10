##--- Workspace ---##

cmake_minimum_required(VERSION  3.20)
project(TrinityEngine LANGUAGES CXX)

# Set workspace-wide standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add compiler options
if(MSVC)
	add_compile_options(/MP)
	add_compile_options(/utf-8)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(ARCH "x64")
else()
	set(ARCH "x86")
endif()

# Create custom output directory
set(OUTPUT_DIR ${CMAKE_SYSTEM_NAME}-${CMAKE_BUILD_TYPE}-${ARCH})

# Set custom output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${OUTPUT_DIR}/${PROJECT_NAME})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${OUTPUT_DIR}/${PROJECT_NAME})

# Add engine and editor subdirectory
add_subdirectory(Trinity)
add_subdirectory(TrinityForge)
add_subdirectory(TrinityCentre)