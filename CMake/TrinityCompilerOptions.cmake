if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(TRINITY_CXX_STANDARD 23)
else()
    set(TRINITY_CXX_STANDARD 26)
endif()

# Debug and Release use the shared engine and Distribution the static one, or the static one everywhere with TRINITY_SHARED_ENGINE off
set(TRINITY_ENGINE_SHARED_CONFIG "$<AND:$<BOOL:${TRINITY_SHARED_ENGINE}>,$<NOT:$<CONFIG:Distribution>>>")

add_library(Trinity-BuildConfig INTERFACE)
add_library(Trinity::BuildConfig ALIAS Trinity-BuildConfig)

target_compile_definitions(Trinity-BuildConfig INTERFACE
    $<$<CONFIG:Debug>:TR_DEBUG>
    $<$<CONFIG:Release>:TR_RELEASE>
    $<$<CONFIG:Distribution>:TR_DISTRIBUTION>
    $<$<CONFIG:Debug,Release>:TR_ENABLE_ASSERTS>
    $<$<AND:$<BOOL:${TRINITY_ENABLE_PROFILING}>,$<CONFIG:Debug,Release>>:TR_ENABLE_PROFILING>
    TR_PLATFORM_${TRINITY_PLATFORM_UPPER}
    $<$<BOOL:${TRINITY_RHI_D3D12}>:TR_RHI_D3D12>
    $<$<BOOL:${TRINITY_RHI_VULKAN}>:TR_RHI_VULKAN>
    $<$<BOOL:${TRINITY_RHI_METAL}>:TR_RHI_METAL>
)

if(TRINITY_PLATFORM STREQUAL "Windows" OR TRINITY_PLATFORM STREQUAL "Xbox")
    target_compile_definitions(Trinity-BuildConfig INTERFACE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

function(trinity_configure_target target)
    get_target_property(target_type ${target} TYPE)

    set_target_properties(${target} PROPERTIES
        CXX_STANDARD ${TRINITY_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        COMPILE_WARNING_AS_ERROR ${TRINITY_WARNINGS_AS_ERRORS}
    )

    if(TRINITY_ENABLE_LTO)
        set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_DISTRIBUTION ON)
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:preprocessor
            /Zc:__cplusplus
            /utf-8
            /MP
        )
        if(NOT target_type STREQUAL "STATIC_LIBRARY")
            target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
            -Wconversion -Wsign-conversion
            $<$<CONFIG:Release>:-g>
        )
    endif()
endfunction()

function(trinity_configure_executable target)
    trinity_configure_target(${target})

    if(MSVC)
        target_link_options(${target} PRIVATE "$<$<CONFIG:Distribution>:/SUBSYSTEM:WINDOWS;/ENTRY:mainCRTStartup>")
    endif()

    set_target_properties(${target} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/${target}")

    # Finds the shared engine beside the executable, as Windows does
    if(NOT WIN32)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN")
    endif()
endfunction()

# A module is a library loaded at runtime, such as a game module. It is built only where the engine is shared, so it uses that one engine, and is a placeholder written outside bin elsewhere
function(trinity_add_module target)
    set(sources "")
    foreach(source IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND sources "${source}")
    endforeach()

    set(placeholder "${CMAKE_BINARY_DIR}/TrinityModulePlaceholder.cpp")
    file(CONFIGURE OUTPUT "${placeholder}" CONTENT "int TrinityModulePlaceholder = 0;\n")

    add_library(${target} MODULE
        "$<${TRINITY_ENGINE_SHARED_CONFIG}:${sources}>"
        "$<$<NOT:${TRINITY_ENGINE_SHARED_CONFIG}>:${placeholder}>"
    )

    trinity_configure_target(${target})
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${sources})

    set_target_properties(${target} PROPERTIES
        PREFIX ""
        LINKER_LANGUAGE CXX
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "$<IF:${TRINITY_ENGINE_SHARED_CONFIG},${OUTPUT_BASE},${CMAKE_CURRENT_BINARY_DIR}/Placeholder/$<CONFIG>>"
    )

    # GCC marks some template statics as unique symbols, which stops a module from ever unloading
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)
    target_link_libraries(${target} PRIVATE Trinity::Engine)
endfunction()