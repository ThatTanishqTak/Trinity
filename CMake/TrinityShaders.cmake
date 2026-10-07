set(TRINITY_VULKAN_SDK_VERSION "1.4.363.0")

find_program(TRINITY_SLANGC slangc HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")
find_program(TRINITY_SPIRV_VAL spirv-val HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")

function(trinity_read_vulkan_header_version header out)
    set(version "")
    if(EXISTS "${header}")
        file(STRINGS "${header}" line REGEX "^#define VK_HEADER_VERSION [0-9]+")
        string(REGEX REPLACE "^#define VK_HEADER_VERSION ([0-9]+).*$" "\\1" version "${line}")
    endif()
    set(${out} "${version}" PARENT_SCOPE)
endfunction()

# The SDK's headers must match the pinned Vulkan-Headers submodule, so slangc, the validation layer and the engine agree on one Vulkan version
if(DEFINED ENV{VULKAN_SDK})
    trinity_read_vulkan_header_version("$ENV{VULKAN_SDK}/include/vulkan/vulkan_core.h" TRINITY_SDK_HEADER_VERSION)
    trinity_read_vulkan_header_version("${CMAKE_SOURCE_DIR}/Vendor/Vulkan-Headers/include/vulkan/vulkan_core.h" TRINITY_PINNED_HEADER_VERSION)
    if(NOT TRINITY_SDK_HEADER_VERSION STREQUAL TRINITY_PINNED_HEADER_VERSION)
        message(WARNING "The Vulkan SDK at $ENV{VULKAN_SDK} has headers 1.4.${TRINITY_SDK_HEADER_VERSION}, but Trinity pins ${TRINITY_VULKAN_SDK_VERSION}. Install the Vulkan SDK ${TRINITY_VULKAN_SDK_VERSION} and point VULKAN_SDK at it.")
    endif()
endif()

if(NOT TRINITY_SLANGC)
    message(WARNING "slangc was not found in $VULKAN_SDK or on PATH, so shaders are not built. Install the Vulkan SDK ${TRINITY_VULKAN_SDK_VERSION} and set VULKAN_SDK, or set TRINITY_SLANGC.")
endif()

function(trinity_add_shaders target)
    set(sources "")
    set(outputs "")
    set(directory "${OUTPUT_BASE}/Engine/shaders")
    set(depfile_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders/$<CONFIG>")

    foreach(source IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        cmake_path(GET source STEM name)
        list(APPEND sources "${source}")

        if(NOT TRINITY_SLANGC)
            continue()
        endif()

        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
        file(READ "${source}" remaining)

        while(TRUE)
            string(REGEX MATCH "\\[shader\\(\"([a-z]+)\"\\)\\](.*)$" match "${remaining}")
            if(NOT match)
                break()
            endif()
            set(stage "${CMAKE_MATCH_1}")
            set(remaining "${CMAKE_MATCH_2}")

            # Skips any further attributes, such as [numthreads(8, 8, 1)], then takes the name before the parameter list
            string(REGEX REPLACE "^([ \t\r\n]*\\[[^]]*\\])*" "" declaration "${remaining}")
            string(REGEX MATCH "^[ \t\r\n]*[A-Za-z_][A-Za-z0-9_<>, ]*[ \t\r\n]+([A-Za-z_][A-Za-z0-9_]*)[ \t\r\n]*\\(" declaration "${declaration}")
            if(NOT declaration)
                message(FATAL_ERROR "${source}: cannot find the function after [shader(\"${stage}\")]")
            endif()
            set(entry "${CMAKE_MATCH_1}")

            if(TRINITY_RHI_VULKAN)
                set(output "${directory}/${name}.${entry}.spv")
                set(validate "")
                if(TRINITY_SPIRV_VAL)
                    set(validate COMMAND "${TRINITY_SPIRV_VAL}" --target-env vulkan1.3 "${output}")
                endif()

                # Storage images are declared with an unknown format, which Vulkan 1.3 allows, so one shader writes any format the view has. Otherwise Slang guesses rgba32f from a float4 and the view's format must match it
                add_custom_command(OUTPUT "${output}"
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${directory}" "${depfile_directory}"
                    COMMAND "${TRINITY_SLANGC}" "${source}" -target spirv -profile spirv_1_6 -stage ${stage} -entry ${entry} -fvk-use-entrypoint-name -default-image-format-unknown -o "${output}" -depfile "${depfile_directory}/${name}.${entry}.spv.d"
                    ${validate}
                    DEPENDS "${source}" "${TRINITY_SLANGC}"
                    DEPFILE "${depfile_directory}/${name}.${entry}.spv.d"
                    COMMENT "Compiling ${name}.${entry} to SPIR-V"
                    VERBATIM
                )
                list(APPEND outputs "${output}")
            endif()

            if(TRINITY_RHI_D3D12)
                set(output "${directory}/${name}.${entry}.dxil")
                add_custom_command(OUTPUT "${output}"
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${directory}" "${depfile_directory}"
                    COMMAND "${TRINITY_SLANGC}" "${source}" -target dxil -profile sm_6_6 -stage ${stage} -entry ${entry} -o "${output}" -depfile "${depfile_directory}/${name}.${entry}.dxil.d"
                    DEPENDS "${source}" "${TRINITY_SLANGC}"
                    DEPFILE "${depfile_directory}/${name}.${entry}.dxil.d"
                    COMMENT "Compiling ${name}.${entry} to DXIL"
                    VERBATIM
                )
                list(APPEND outputs "${output}")
            endif()
        endwhile()
    endforeach()

    add_custom_target(${target} ALL DEPENDS ${outputs} SOURCES ${sources})
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${sources})
endfunction()