# Trinity-Tools CMakeLists.txt

set(TARGET_NAME Trinity-Tools)

# Collect the source files
file(GLOB_RECURSE TRINITY_TOOLS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

# Collect the public header files
file(GLOB_RECURSE TRINITY_TOOLS_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)

# How the tools a project's scripts and a game's player are built with are found, which Forge builds with and Hub checks for, so both find the same ones. It is the editor's side alone, so no game links it, and it links nothing but the engine
add_library(${TARGET_NAME} STATIC ${TRINITY_TOOLS_SOURCES} ${TRINITY_TOOLS_HEADERS})
add_library(Trinity::Tools ALIAS ${TARGET_NAME})

trinity_configure_target(${TARGET_NAME})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_TOOLS_HEADERS} ${TRINITY_TOOLS_SOURCES})

target_include_directories(${TARGET_NAME} PUBLIC Include)
target_link_libraries(${TARGET_NAME} PUBLIC Trinity::Engine)