# Trinity-Engine CMakeLists.txt

set(TARGET_NAME Trinity-Engine)

# Collect the source files
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

if(APPLE)
    file(GLOB_RECURSE OBJC_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.mm"
    )
    list(APPEND SOURCES ${OBJC_SOURCES})
endif()

# Platform backends are selected here
if(NOT WIN32)
    list(FILTER SOURCES EXCLUDE REGEX ".*/(WindowsWindow|GameInputGamepad|D3D12Renderer)\\.cpp$")
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(FILTER SOURCES EXCLUDE REGEX ".*/(WaylandWindow|X11Window)\\.cpp$")
endif()

# Collect the public header files
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)

# Collect the private header files
file(GLOB_RECURSE INTERNALS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Internal/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Internal/*.hpp"
)

# Create the static library
add_library(${TARGET_NAME} STATIC ${SOURCES} ${HEADERS} ${INTERNALS})

# IDE source layout
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Include" PREFIX "Include" FILES ${HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Internal" PREFIX "Internal" FILES ${INTERNALS})

# Include directories: Include is public (visible to Forge), Internal is engine-only
target_include_directories(${TARGET_NAME}
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Internal"
)

# C++ standard
target_compile_features(${TARGET_NAME} PUBLIC cxx_std_23)

# Warnings and platform defines
trinity_set_compile_options(${TARGET_NAME})

# Trace and debug logging compile out outside Debug builds, and asserts are Debug-only
target_compile_definitions(${TARGET_NAME} PUBLIC
    $<IF:$<CONFIG:Debug>,SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE,SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
    $<$<CONFIG:Debug>:TR_ENABLE_ASSERTS>
)

# Third-party libraries
target_link_libraries(${TARGET_NAME}
    PUBLIC spdlog
    PRIVATE volk VulkanMemoryAllocator
)

if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE GameInput D3D12MemoryAllocator d3d12 dxgi dxguid)
endif()

# Engine properties
set_target_properties(${TARGET_NAME} PROPERTIES
    # Set output directory
    ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_BASE}"

    # Set C++ standard
    CXX_EXTENSIONS OFF
)