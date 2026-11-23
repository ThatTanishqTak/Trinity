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

# Global new and delete go into the engine, and on Windows into each user of the shared engine
set(TRINITY_ENGINE_GLOBAL_ALLOCATIONS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Trinity/Core/GlobalAllocations.cpp")
list(REMOVE_ITEM TRINITY_ENGINE_FILES "${TRINITY_ENGINE_GLOBAL_ALLOCATIONS}")
if(TRINITY_TRACK_GLOBAL_ALLOCATIONS)
    list(APPEND TRINITY_ENGINE_FILES "${TRINITY_ENGINE_GLOBAL_ALLOCATIONS}")
endif()

set(TRINITY_ENGINE_HEADERS ${TRINITY_ENGINE_FILES})
list(FILTER TRINITY_ENGINE_HEADERS EXCLUDE REGEX "\\.cpp$")
set(TRINITY_ENGINE_COMPILED ${TRINITY_ENGINE_FILES})
list(FILTER TRINITY_ENGINE_COMPILED INCLUDE REGEX "\\.cpp$")

# Dear ImGui is compiled into the engine, so Forge and every game module share one context
set(TRINITY_IMGUI_DIRECTORY "${CMAKE_SOURCE_DIR}/Vendor/imgui")
set(TRINITY_IMGUI_SOURCES
    "${TRINITY_IMGUI_DIRECTORY}/imgui.cpp"
    "${TRINITY_IMGUI_DIRECTORY}/imgui_demo.cpp"
    "${TRINITY_IMGUI_DIRECTORY}/imgui_draw.cpp"
    "${TRINITY_IMGUI_DIRECTORY}/imgui_tables.cpp"
    "${TRINITY_IMGUI_DIRECTORY}/imgui_widgets.cpp"
    "${TRINITY_IMGUI_DIRECTORY}/misc/cpp/imgui_stdlib.cpp"
    "${TRINITY_IMGUI_DIRECTORY}/misc/freetype/imgui_freetype.cpp"
)
set_source_files_properties(${TRINITY_IMGUI_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# FreeType's types meet ImGui's int and float there, so only that file drops conversion warnings
set_source_files_properties("${TRINITY_IMGUI_DIRECTORY}/misc/freetype/imgui_freetype.cpp" PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/wd4244;/wd4267,-Wno-conversion;-Wno-sign-conversion>")
list(APPEND TRINITY_ENGINE_COMPILED ${TRINITY_IMGUI_SOURCES})

# Physics sources build for Jolt's instructions, without the PCH, so Main's CPU check runs first
file(GLOB TRINITY_ENGINE_PHYSICS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Trinity/Physics/*.cpp")
set_source_files_properties(${TRINITY_ENGINE_PHYSICS_SOURCES} PROPERTIES COMPILE_OPTIONS "${TRINITY_PHYSICS_COMPILE_OPTIONS}" SKIP_PRECOMPILE_HEADERS ON)

# CMake builds each linked library in every configuration, so an unused engine is a placeholder
set(TRINITY_ENGINE_STATIC_CONFIG "$<NOT:${TRINITY_ENGINE_SHARED_CONFIG}>")
set(TRINITY_ENGINE_PLACEHOLDER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Placeholder/$<CONFIG>")
set(TRINITY_ENGINE_PLACEHOLDER "${CMAKE_CURRENT_BINARY_DIR}/EnginePlaceholder.cpp")

# The export makes the linker write an import library for the unused DLL, which Ninja expects
file(CONFIGURE OUTPUT "${TRINITY_ENGINE_PLACEHOLDER}" CONTENT "#if defined(_WIN32)\n__declspec(dllexport)\n#endif\nint TrinityEnginePlaceholder = 0;\n")

function(trinity_add_engine_library target type active_config)
    add_library(${target} ${type}
        ${TRINITY_ENGINE_HEADERS}
        "$<${active_config}:${TRINITY_ENGINE_COMPILED}>"
        "$<$<NOT:${active_config}>:${TRINITY_ENGINE_PLACEHOLDER}>"
    )

    trinity_configure_target(${target})
    # The scene serializer's templates make more sections in Debug than MSVC's default format holds
    target_compile_options(${target} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/bigobj>")
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_ENGINE_FILES})
    source_group("Vendor/imgui" FILES ${TRINITY_IMGUI_SOURCES})

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
    target_include_directories(${target} SYSTEM PUBLIC "${TRINITY_IMGUI_DIRECTORY}")
    # The placeholder still gets a header: an empty list in a configuration breaks Ninja with MSVC
    target_precompile_headers(${target}
        PRIVATE "$<IF:${active_config},${CMAKE_CURRENT_SOURCE_DIR}/Internal/TrinityPCH.hpp,<cstddef$<ANGLE-R>>"
    )

    # The version and commit are in TrinityBuildInfo.hpp, which only Version.cpp includes
    trinity_use_version_info(${target})

    target_compile_definitions(${target}
        PRIVATE
            $<$<BOOL:${TRINITY_TRACK_GLOBAL_ALLOCATIONS}>:TR_TRACK_GLOBAL_ALLOCATIONS>
            TR_PHYSICS_AVX2=$<BOOL:${TRINITY_PHYSICS_AVX2}>
        PUBLIC
            GLM_FORCE_DEPTH_ZERO_TO_ONE
            GLM_FORCE_RADIANS
            GLM_ENABLE_EXPERIMENTAL
            "IMGUI_USER_CONFIG=\"Trinity/UI/ImGuiConfig.hpp\""
    )

    target_link_libraries(${target}
        PUBLIC
            Trinity::BuildConfig
            glm::glm
            EnTT::EnTT
        PRIVATE
            spdlog::spdlog
            Freetype::Freetype
            rmlui_core
            yaml-cpp::yaml-cpp
            Jolt
            box2d
            miniaudio
            zstd::headers
    )

    # The shared engine links ktx_read. The static one's users link ktx_read or libktx, never both
    if(type STREQUAL "STATIC")
        target_link_libraries(${target} PRIVATE "$<COMPILE_ONLY:ktx_read>")
    else()
        target_link_libraries(${target} PRIVATE ktx_read)
    endif()

    if(TRINITY_PLATFORM STREQUAL "Windows")
        target_link_libraries(${target} PRIVATE user32 gdi32 shell32 shcore ole32 uuid winhttp)
        trinity_link_gameinput(${target})
        if(TRINITY_RHI_D3D12)
            target_link_libraries(${target} PRIVATE Microsoft::DirectX-Headers Microsoft::DirectX-Guids D3D12MemoryAllocator d3d12 dxgi dxguid)
        endif()
    else()
        target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS})
    endif()

    if(TRINITY_RHI_VULKAN)
        target_link_libraries(${target} PRIVATE volk::volk GPUOpen::VulkanMemoryAllocator)
    endif()

    trinity_link_dotnet_host(${target})
endfunction()

trinity_add_engine_library(Trinity-Engine SHARED "${TRINITY_ENGINE_SHARED_CONFIG}")
target_compile_definitions(Trinity-Engine
    PUBLIC TR_ENGINE_SHARED
    PRIVATE TR_BUILD_ENGINE
)

# C4251 flags std members of exported classes, safe while modules share the compiler and runtime
target_compile_options(Trinity-Engine PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4251>)
set_target_properties(Trinity-Engine PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
)

# Keeps spdlog and other static dependencies private to the engine on Linux, as a DLL does
target_link_options(Trinity-Engine PRIVATE $<$<PLATFORM_ID:Linux>:LINKER:--exclude-libs,ALL>)

trinity_add_engine_library(Trinity-Engine-Static STATIC "${TRINITY_ENGINE_STATIC_CONFIG}")

# RmlUi's debugger is never linked in Distribution, so the game package never names it
target_link_libraries(Trinity-Engine PRIVATE "$<$<NOT:$<CONFIG:Distribution>>:rmlui_debugger>")
if(NOT TRINITY_SHARED_ENGINE)
    target_link_libraries(Trinity-Engine-Static PRIVATE "$<$<NOT:$<CONFIG:Distribution>>:rmlui_debugger>")
endif()

# What executables and modules link: the shared or static engine, and with the static its libktx
add_library(Trinity-Engine-Link INTERFACE)
add_library(Trinity::Engine ALIAS Trinity-Engine-Link)
target_link_libraries(Trinity-Engine-Link INTERFACE "$<IF:${TRINITY_ENGINE_SHARED_CONFIG},Trinity-Engine,Trinity-Engine-Static>" "$<$<NOT:${TRINITY_ENGINE_SHARED_CONFIG}>:$<IF:$<BOOL:$<TARGET_PROPERTY:TRINITY_TEXTURE_ENCODER>>,ktx,ktx_read>>")

if(WIN32 AND TRINITY_TRACK_GLOBAL_ALLOCATIONS)
    target_sources(Trinity-Engine-Link INTERFACE "$<${TRINITY_ENGINE_SHARED_CONFIG}:${TRINITY_ENGINE_GLOBAL_ALLOCATIONS}>")
endif()

# Compiled shaders go to bin/<configuration>/Trinity/Engine/shaders, mounted as /engine
file(GLOB TRINITY_ENGINE_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.slang")
trinity_add_shaders(Trinity-Shaders ${TRINITY_ENGINE_SHADERS})
# The RHI's tests, which only the Sandbox draws, so Forge's export leaves them out of a game
set_property(GLOBAL PROPERTY TRINITY_TEST_SHADERS ComputeTest LayeredTest QuadField RasterTest TexturedQuad Triangle)
add_dependencies(Trinity-Engine Trinity-Shaders)
add_dependencies(Trinity-Engine-Static Trinity-Shaders)

# Fonts and their licenses go to bin/<configuration>/Trinity/Engine/fonts, at /engine/fonts
file(GLOB TRINITY_ENGINE_FONTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Fonts/*")
trinity_add_assets(Trinity-Fonts Engine/fonts ${TRINITY_ENGINE_FONTS})
add_dependencies(Trinity-Engine Trinity-Fonts)
add_dependencies(Trinity-Engine-Static Trinity-Fonts)

# The D3D12 Agility SDK runtime goes to bin/<configuration>/Trinity/D3D12, beside every executable
trinity_add_agility_sdk(Trinity-AgilitySDK)
add_dependencies(Trinity-Engine Trinity-AgilitySDK)
add_dependencies(Trinity-Engine-Static Trinity-AgilitySDK)

# GameInput's redistributable goes to Redist in Release and Distribution, for exports to copy
trinity_add_gameinput_redist(Trinity-GameInputRedist)
add_dependencies(Trinity-Engine Trinity-GameInputRedist)
add_dependencies(Trinity-Engine-Static Trinity-GameInputRedist)

# nethost.dll beside every executable on Windows, by which the engine finds the .NET runtime
trinity_add_dotnet_host_runtime(Trinity-NetHost)
add_dependencies(Trinity-Engine Trinity-NetHost)
add_dependencies(Trinity-Engine-Static Trinity-NetHost)

# Microsoft's C++ runtime beside the Release build, from where a Development export copies it
trinity_add_msvc_runtime(Trinity-MsvcRuntime)
add_dependencies(Trinity-Engine Trinity-MsvcRuntime)

# TrinityConfig.cmake and TrinityToolchain.cmake beside the shared engine, for game modules
set(TRINITY_PACKAGE_MSVC_RUNTIME "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
trinity_write_engine_package(Trinity-Engine)