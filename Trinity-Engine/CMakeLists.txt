# Trinity-Engine CMakeLists.txt

file(GLOB_RECURSE TRINITY_ENGINE_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)

file(GLOB_RECURSE TRINITY_ENGINE_INTERNAL_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Internal/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Internal/*.hpp"
)

file(GLOB_RECURSE TRINITY_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

set(TRINITY_ENGINE_FILES
    ${TRINITY_ENGINE_PUBLIC_HEADERS}
    ${TRINITY_ENGINE_INTERNAL_HEADERS}
    ${TRINITY_ENGINE_SOURCES}
)

if(TRINITY_PLATFORM STREQUAL "Windows")
    set(TRINITY_PLATFORM_DIRECTORY "Windows")
else()
    set(TRINITY_PLATFORM_DIRECTORY "Generic")
endif()

foreach(directory IN ITEMS Windows Generic)
    if(NOT directory STREQUAL TRINITY_PLATFORM_DIRECTORY)
        list(FILTER TRINITY_ENGINE_FILES EXCLUDE REGEX "/Trinity/Platform/${directory}/")
    endif()
endforeach()

# Each RHI backend compiles only where it is enabled
foreach(backend IN ITEMS D3D12 Vulkan Metal)
    string(TOUPPER "${backend}" backend_option)
    if(NOT TRINITY_RHI_${backend_option})
        list(FILTER TRINITY_ENGINE_FILES EXCLUDE REGEX "/Trinity/RHI/${backend}/")
    endif()
endforeach()

# Global new and delete are compiled into the engine, and on Windows also into each executable and module that uses the shared engine
set(TRINITY_ENGINE_GLOBAL_ALLOCATIONS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Trinity/Core/GlobalAllocations.cpp")
list(REMOVE_ITEM TRINITY_ENGINE_FILES "${TRINITY_ENGINE_GLOBAL_ALLOCATIONS}")
if(TRINITY_TRACK_GLOBAL_ALLOCATIONS)
    list(APPEND TRINITY_ENGINE_FILES "${TRINITY_ENGINE_GLOBAL_ALLOCATIONS}")
endif()

set(TRINITY_ENGINE_HEADERS ${TRINITY_ENGINE_FILES})
list(FILTER TRINITY_ENGINE_HEADERS EXCLUDE REGEX "\\.cpp$")
set(TRINITY_ENGINE_COMPILED ${TRINITY_ENGINE_FILES})
list(FILTER TRINITY_ENGINE_COMPILED INCLUDE REGEX "\\.cpp$")

# CMake builds every library an executable links in any configuration, in every configuration so each engine target compiles the engine only in the configurations that use it, and a placeholder in the others, written outside bin.
set(TRINITY_ENGINE_STATIC_CONFIG "$<NOT:${TRINITY_ENGINE_SHARED_CONFIG}>")
set(TRINITY_ENGINE_PLACEHOLDER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Placeholder/$<CONFIG>")
set(TRINITY_ENGINE_PLACEHOLDER "${CMAKE_CURRENT_BINARY_DIR}/EnginePlaceholder.cpp")

# The export makes the linker write an import library for the unused DLL, which Ninja expects as an output
file(CONFIGURE OUTPUT "${TRINITY_ENGINE_PLACEHOLDER}" CONTENT "#if defined(_WIN32)\n__declspec(dllexport)\n#endif\nint TrinityEnginePlaceholder = 0;\n")

function(trinity_add_engine_library target type active_config)
    add_library(${target} ${type}
        ${TRINITY_ENGINE_HEADERS}
        "$<${active_config}:${TRINITY_ENGINE_COMPILED}>"
        "$<$<NOT:${active_config}>:${TRINITY_ENGINE_PLACEHOLDER}>"
    )

    trinity_configure_target(${target})
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_ENGINE_FILES})

    set_target_properties(${target} PROPERTIES
        LINKER_LANGUAGE CXX
        RUNTIME_OUTPUT_DIRECTORY "$<IF:${active_config},${OUTPUT_BASE},${TRINITY_ENGINE_PLACEHOLDER_DIRECTORY}>"
        LIBRARY_OUTPUT_DIRECTORY "$<IF:${active_config},${OUTPUT_BASE},${TRINITY_ENGINE_PLACEHOLDER_DIRECTORY}>"
        ARCHIVE_OUTPUT_DIRECTORY "$<IF:${active_config},${OUTPUT_BASE},${TRINITY_ENGINE_PLACEHOLDER_DIRECTORY}>"
    )

    target_include_directories(${target}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Internal"
    )
    # The placeholder still gets a header: an empty list in some configurations breaks Ninja generation with MSVC
    target_precompile_headers(${target}
        PRIVATE "$<IF:${active_config},${CMAKE_CURRENT_SOURCE_DIR}/Internal/TrinityPCH.hpp,<cstddef$<ANGLE-R>>"
    )

    target_compile_definitions(${target}
        PRIVATE
            TR_VERSION_STRING="${PROJECT_VERSION}"
            $<$<BOOL:${TRINITY_TRACK_GLOBAL_ALLOCATIONS}>:TR_TRACK_GLOBAL_ALLOCATIONS>
        PUBLIC
            GLM_FORCE_DEPTH_ZERO_TO_ONE
            GLM_FORCE_RADIANS
            GLM_ENABLE_EXPERIMENTAL
    )

    target_link_libraries(${target}
        PUBLIC
            Trinity::BuildConfig
            glm::glm
            EnTT::EnTT
        PRIVATE
            spdlog::spdlog
    )

    if(TRINITY_PLATFORM STREQUAL "Windows")
        target_link_libraries(${target} PRIVATE user32 gdi32 shell32 ole32 uuid)
        if(TRINITY_RHI_D3D12)
            target_link_libraries(${target} PRIVATE Microsoft::DirectX-Headers Microsoft::DirectX-Guids d3d12 dxgi dxguid)
        endif()
    else()
        target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS})
    endif()
endfunction()

trinity_add_engine_library(Trinity-Engine SHARED "${TRINITY_ENGINE_SHARED_CONFIG}")
target_compile_definitions(Trinity-Engine
    PUBLIC TR_ENGINE_SHARED
    PRIVATE TR_BUILD_ENGINE
)

# C4251 flags standard library members of exported classes, which is safe while every module uses the same compiler and runtime
target_compile_options(Trinity-Engine PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4251>)
set_target_properties(Trinity-Engine PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
)

# Keeps spdlog and other static dependencies private to the engine on Linux, as a DLL does on Windows
target_link_options(Trinity-Engine PRIVATE $<$<PLATFORM_ID:Linux>:LINKER:--exclude-libs,ALL>)

trinity_add_engine_library(Trinity-Engine-Static STATIC "${TRINITY_ENGINE_STATIC_CONFIG}")

# What executables and modules link: the shared engine or the static one, depending on the configuration
add_library(Trinity-Engine-Link INTERFACE)
add_library(Trinity::Engine ALIAS Trinity-Engine-Link)
target_link_libraries(Trinity-Engine-Link INTERFACE "$<IF:${TRINITY_ENGINE_SHARED_CONFIG},Trinity-Engine,Trinity-Engine-Static>")

if(WIN32 AND TRINITY_TRACK_GLOBAL_ALLOCATIONS)
    target_sources(Trinity-Engine-Link INTERFACE "$<${TRINITY_ENGINE_SHARED_CONFIG}:${TRINITY_ENGINE_GLOBAL_ALLOCATIONS}>")
endif()

# Compiled shaders go to bin/<configuration>/Trinity/Engine/shaders, which the engine mounts read-only as /engine
file(GLOB TRINITY_ENGINE_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.slang")
trinity_add_shaders(Trinity-Shaders ${TRINITY_ENGINE_SHADERS})
add_dependencies(Trinity-Engine Trinity-Shaders)
add_dependencies(Trinity-Engine-Static Trinity-Shaders)

# The D3D12 Agility SDK runtime goes to bin/<configuration>/Trinity/D3D12, beside every executable
trinity_add_agility_sdk(Trinity-AgilitySDK)
add_dependencies(Trinity-Engine Trinity-AgilitySDK)
add_dependencies(Trinity-Engine-Static Trinity-AgilitySDK)