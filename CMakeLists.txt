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

# Slang shader compiler
find_program(SLANGC_EXECUTABLE slangc HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")

# DXIL for the DirectX 12 backend
option(TRINITY_SHADERS_DXIL "Compile shaders to DXIL as well as SPIR-V" ${WIN32})
function(trinity_add_shaders target)
    cmake_parse_arguments(PARSE_ARGV 1 SHADER "" "SOURCE" "ENTRIES")

    if(NOT SLANGC_EXECUTABLE)
        message(FATAL_ERROR "slangc not found. Install the Vulkan SDK (1.3.296 or newer) or set SLANGC_EXECUTABLE")
    endif()

    if(NOT SHADER_SOURCE OR NOT SHADER_ENTRIES)
        message(FATAL_ERROR "trinity_add_shaders(${target}) needs SOURCE and at least one ENTRIES <entry>:<stage>")
    endif()

    get_filename_component(SOURCE_PATH "${SHADER_SOURCE}" ABSOLUTE)
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME_WE)
    set(OUTPUT_DIRECTORY "${OUTPUT_BASE}/Shaders")

    set(FORMATS "spirv|spv|")
    if(TRINITY_SHADERS_DXIL)
        list(APPEND FORMATS "dxil|dxil|-profile,sm_6_6")
    endif()

    set(OUTPUTS)
    foreach(ENTRY IN LISTS SHADER_ENTRIES)
        string(REPLACE ":" ";" ENTRY_PARTS "${ENTRY}")
        list(LENGTH ENTRY_PARTS ENTRY_PART_COUNT)
        if(NOT ENTRY_PART_COUNT EQUAL 2)
            message(FATAL_ERROR "trinity_add_shaders(${target}): '${ENTRY}' should be <entry>:<stage>, such as VertexMain:vertex")
        endif()

        list(GET ENTRY_PARTS 0 ENTRY_NAME)
        list(GET ENTRY_PARTS 1 STAGE)

        foreach(FORMAT IN LISTS FORMATS)
            string(REPLACE "|" ";" FORMAT_PARTS "${FORMAT}")
            list(GET FORMAT_PARTS 0 SLANG_TARGET)
            list(GET FORMAT_PARTS 1 EXTENSION)
            list(GET FORMAT_PARTS 2 EXTRA_ARGUMENTS)
            string(REPLACE "," ";" EXTRA_ARGUMENTS "${EXTRA_ARGUMENTS}")

            set(OUTPUT_FILE "${OUTPUT_DIRECTORY}/${SHADER_NAME}.${ENTRY_NAME}.${EXTENSION}")

            # One slangc run per entry point and format, each with its own depfile, so editing an included file rebuilds only what uses it
            add_custom_command(
                OUTPUT "${OUTPUT_FILE}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_DIRECTORY}"
                COMMAND "${SLANGC_EXECUTABLE}" "${SOURCE_PATH}" -entry "${ENTRY_NAME}" -stage "${STAGE}" -target "${SLANG_TARGET}" ${EXTRA_ARGUMENTS} -o "${OUTPUT_FILE}" -depfile "${OUTPUT_FILE}.d"
                DEPENDS "${SOURCE_PATH}"
                DEPFILE "${OUTPUT_FILE}.d"
                COMMENT "Compiling ${SHADER_NAME}.slang ${ENTRY_NAME} (${STAGE}) to ${EXTENSION}"
                VERBATIM
                COMMAND_EXPAND_LISTS
            )

            list(APPEND OUTPUTS "${OUTPUT_FILE}")
        endforeach()
    endforeach()

    # Listed as sources so they build with the target and show up in the IDE
    set_source_files_properties("${SOURCE_PATH}" PROPERTIES HEADER_FILE_ONLY ON)
    target_sources(${target} PRIVATE "${SOURCE_PATH}" ${OUTPUTS})
    source_group("Shaders" FILES "${SOURCE_PATH}")
endfunction()

# Subdirectories, Vendor first
add_subdirectory(Vendor)
add_subdirectory(Trinity-Engine)
add_subdirectory(Trinity-Forge)

# Visual Studio startup project
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT Trinity-Forge)