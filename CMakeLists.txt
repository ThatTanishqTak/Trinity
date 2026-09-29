# Root CMakeLists.txt

cmake_minimum_required(VERSION 3.30)
project(Trinity LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

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

# Warnings and platform defines shared by every Trinity target
function(trinity_set_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(WIN32)
        target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    endif()
endfunction()

# Copies files players need at runtime next to the executable
function(trinity_copy_runtime_files target)
    if(WIN32 AND GAMEINPUT_REDIST)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GAMEINPUT_REDIST}" "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copying GameInputRedist.msi"
            VERBATIM
        )
    endif()
endfunction()

# Subdirectories, Vendor first
add_subdirectory(Vendor)
add_subdirectory(Trinity-Engine)
add_subdirectory(Trinity-Forge)

# Visual Studio startup project
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT Trinity-Forge)