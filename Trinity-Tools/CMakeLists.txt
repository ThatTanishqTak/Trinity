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

# How build tools are found, shared by Forge and Hub so both find the same. No game links it
add_library(${TARGET_NAME} STATIC ${TRINITY_TOOLS_SOURCES} ${TRINITY_TOOLS_HEADERS})
add_library(Trinity::Tools ALIAS ${TARGET_NAME})

trinity_configure_target(${TARGET_NAME})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_TOOLS_HEADERS} ${TRINITY_TOOLS_SOURCES})

target_include_directories(${TARGET_NAME} PUBLIC Include)
target_link_libraries(${TARGET_NAME} PUBLIC Trinity::Engine)