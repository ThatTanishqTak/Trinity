# Trinity-Engine CMakeLists.txt

set(TARGET_NAME Trinity-Engine)

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

if(TRINITY_PLATFORM STREQUAL "Windows")
    list(APPEND TRINITY_ENGINE_INTERNAL_HEADERS
        "Internal/Trinity/Platform/Windows/WindowsHeaders.hpp"
        "Internal/Trinity/Platform/Windows/WindowsWindow.hpp"
    )
    list(APPEND TRINITY_ENGINE_SOURCES
        "Source/Trinity/Platform/Windows/WindowsPlatform.cpp"
        "Source/Trinity/Platform/Windows/WindowsWindow.cpp"
    )
else()
    list(APPEND TRINITY_ENGINE_SOURCES
        "Source/Trinity/Platform/Generic/GenericPlatform.cpp"
    )
endif()

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

add_library(${TARGET_NAME} STATIC ${TRINITY_ENGINE_FILES})
add_library(Trinity::Engine ALIAS ${TARGET_NAME})

trinity_configure_target(${TARGET_NAME})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_ENGINE_FILES})

target_include_directories(${TARGET_NAME}
    PUBLIC "Include"
    PRIVATE "Internal"
)
target_precompile_headers(${TARGET_NAME} 
    PRIVATE "Internal/TrinityPCH.hpp"
)

target_compile_definitions(${TARGET_NAME}
    PRIVATE
        TR_VERSION_STRING="${PROJECT_VERSION}"
        $<$<BOOL:${TRINITY_TRACK_GLOBAL_ALLOCATIONS}>:TR_TRACK_GLOBAL_ALLOCATIONS>
    PUBLIC
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_ENABLE_EXPERIMENTAL
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        Trinity::BuildConfig
        spdlog::spdlog
        glm::glm
        EnTT::EnTT
)

if(TRINITY_PLATFORM STREQUAL "Windows")
    target_link_libraries(${TARGET_NAME} PUBLIC user32 gdi32 shell32 ole32 uuid)
endif()